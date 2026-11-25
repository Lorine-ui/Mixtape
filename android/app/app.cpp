#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>

#include "text.hpp"

namespace fs = std::filesystem;

namespace mx {

namespace {

std::mt19937& rng() {
    static std::mt19937 g{std::random_device{}()};
    return g;
}
int randInt(int n) { return n <= 0 ? 0 : static_cast<int>(rng()() % static_cast<unsigned>(n)); }

fs::path P(const std::string& s) { return fs::u8path(s); }

// Стабильный хеш для имён файлов офлайн-копий (std::hash от сборки к сборке может отличаться).
std::string fnv(const std::string& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", h);
    return buf;
}

// Формат определяем по первым байтам: надёжнее, чем доверять заголовку ответа.
std::string sniffExt(const std::string& b, const std::string& fallback) {
    if (b.size() > 12) {
        if (b.compare(0, 4, "fLaC") == 0) return "flac";
        if (b.compare(0, 4, "OggS") == 0) return "ogg";
        if (b.compare(0, 4, "RIFF") == 0) return "wav";
        if (b.compare(4, 4, "ftyp") == 0) return "m4a";
        if (b.compare(0, 3, "ID3") == 0) return "mp3";
        if (static_cast<unsigned char>(b[0]) == 0xFF && (static_cast<unsigned char>(b[1]) & 0xE0) == 0xE0)
            return (static_cast<unsigned char>(b[1]) & 0x06) == 0 ? "aac" : "mp3";
    }
    return fallback.empty() ? "mp3" : fallback;
}

const char* kGenres[] = {"lofi", "synthwave", "ambient", "indie rock", "jazz", "house", "techno", "hip hop", "drum and bass",
                         "chillout", "trap", "metal", "folk", "classical piano", "dubstep", "electro swing", "vaporwave",
                         "funk", "reggae", "shoegaze", "post rock", "acoustic", "orchestral", "city pop", "phonk",
                         "jungle", "trance", "disco", "blues", "k-pop", "j-pop", "русский рок", "русский рэп", "bossa nova"};

}  // namespace

App::App(std::string dir, Platform pf) : dataDir(std::move(dir)), platform(std::move(pf)) {}

App::~App() { stop_ = true; wcv_.notify_all(); for (auto& w : workers_) w.detach(); }

double App::now() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count(); }

std::string App::audioFilePath(const std::string& name) const { return (P(dataDir) / "files" / P(name)).u8string(); }
std::string App::offlinePath(const Track& t, const std::string&) const {
    return (P(dataDir) / "offline" / (fnv(t.baseId()) + ".bin")).u8string();
}

// ---------- запуск ----------

std::shared_ptr<Library> App::snapshot(bool full) const {
    auto s = std::make_shared<Library>();
    s->settings = lib.settings;
    if (full) s->tracks = lib.tracks;
    return s;
}

bool App::init(std::string* err) {
    std::error_code ec;
    fs::create_directories(P(dataDir) / "offline", ec);
    fs::create_directories(P(dataDir) / "files", ec);

    std::string lp = (P(dataDir) / "library.json").u8string(), e;
    if (fs::exists(P(lp), ec) && !lib.load(lp, &e)) {
        fs::copy_file(P(lp), P(lp + ".broken"), fs::copy_options::overwrite_existing, ec);
        toast("Библиотека повреждена, копия сохранена как library.json.broken");
    }
    std::string cache;
    Json cj;
    if (readFile((P(dataDir) / "lyrics_cache.json").u8string(), cache) && Json::tryParse(cache, cj))
        for (std::size_t i = 0; i < cj.keys().size(); ++i) lyCache_[cj.keys()[i]] = lyricsFromJson(cj.items()[i]);

    volume = static_cast<float>(lib.getNum("vol", 0.8));
    player.setVolume(volume);
    std::string aerr;
    audioOk = player.start(&aerr);
    if (!audioOk) toast(aerr);

    try {
        http_ = makeHttp();
        net_ = std::make_unique<Net>(*http_);
    } catch (const std::exception& ex) {
        if (err) *err = ex.what();
        return false;
    }
    if (platform.isOnline) online = platform.isOnline();
    std::string st;
    Json sj;
    if (readFile((P(dataDir) / "stats.json").u8string(), st) && Json::tryParse(st, sj)) stats.fromJson(sj);
    cloudLoadState();
    for (int i = 0; i < 6; ++i) workers_.emplace_back([this] { workerLoop(); });
    return true;
}

void App::saveNow() {
    lib.set("vol", Json(static_cast<double>(volume)));
    std::string e;
    if (lib.save((P(dataDir) / "library.json").u8string(), &e)) dirty_ = false;
    writeFileAtomic((P(dataDir) / "stats.json").u8string(), stats.toJson().dump());
    statsDirty_ = false;
    if (lyCacheDirty_) saveLyricsCache();
}

void App::shutdown() {
    if (acct.loggedIn()) cloudSaveState();
    writeFileAtomic((P(dataDir) / "stats.json").u8string(), stats.toJson().dump());
    std::string e;
    lib.set("vol", Json(static_cast<double>(volume)));
    lib.save((P(dataDir) / "library.json").u8string(), &e);
    saveLyricsCache();
    stop_ = true;
    cancelLoad_->store(true);
    player.pause();
}

void App::saveLyricsCache() {
    while (lyCache_.size() > 150) lyCache_.erase(lyCache_.begin());
    Json j = Json::object();
    for (auto& kv : lyCache_) j.set(kv.first, toJson(kv.second));
    writeFileAtomic((P(dataDir) / "lyrics_cache.json").u8string(), j.dump());
    lyCacheDirty_ = false;
}

// ---------- потоки ----------

void App::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(wmu_);
            wcv_.wait(lk, [this] { return stop_.load() || !work_.empty(); });
            if (stop_.load()) return;
            job = std::move(work_.front());
            work_.pop_front();
        }
        try { job(); } catch (...) {}
    }
}

void App::async(std::function<void()> fn) {
    { std::lock_guard<std::mutex> lk(wmu_); work_.push_back(std::move(fn)); }
    wcv_.notify_one();
}

void App::post(std::function<void()> fn, double delaySec) {
    std::lock_guard<std::mutex> lk(qmu_);
    uiQueue_.push_back({now() + delaySec, std::move(fn)});
}

void App::fetchUrl(const std::string& url, std::function<void(std::string)> cb) {
    async([this, url, cb] {
        std::string body;
        try {
            HttpOptions o;
            o.headers = {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0 Safari/537.36"}};
            o.timeoutMs = 12000;
            o.maxBytes = 20u << 20;
            HttpResponse r = http_->request(url, o);
            if (r.ok()) body = std::move(r.body);
        } catch (const std::exception&) {}
        post([cb, body] { cb(body); });
    });
}

void App::loadTrending() {
    if (trendingLoading || !online || !trending.empty()) return;
    trendingLoading = true;
    async([this] {
        std::vector<Track> t;
        try { t = net_->audiusTrending(); } catch (const std::exception&) {}
        post([this, t] {
            trendingLoading = false;
            trending = t;
            for (auto& x : trending) if (!lib.has(x.id)) cache_[x.id] = x;
        });
    });
}

void App::toast(const std::string& msg) {
    toastText = msg;
    toastUntil = now() + 3.8;
}

void App::frame() {
    // задачи из рабочих потоков
    std::vector<std::function<void()>> due;
    {
        std::lock_guard<std::mutex> lk(qmu_);
        double t = now();
        for (auto it = uiQueue_.begin(); it != uiQueue_.end();) {
            if (it->at <= t) { due.push_back(std::move(it->fn)); it = uiQueue_.erase(it); }
            else ++it;
        }
    }
    for (auto& f : due) f();

    double t = now();
    if (t - lastOnlineCheck_ > 3) {
        lastOnlineCheck_ = t;
        if (platform.isOnline) online = platform.isOnline();
    }
    if (player.ended() && cur && !loading) {
        if (!ended_handled_) { ended_handled_ = true; onEnded(); }
    } else if (!player.ended()) {
        ended_handled_ = false;
    }
    if (sleepAt > 0 && t >= sleepAt) {
        sleepAt = 0;
        player.pause();
        toast("Таймер сна: музыка остановлена");
    }
    if (dirty_ && t - lastChange_ > 1.2) {
        dirty_ = false;
        std::string e;
        if (!lib.save((P(dataDir) / "library.json").u8string(), &e)) toast("Не удалось сохранить данные: " + e);
    }
    if (lyCacheDirty_) saveLyricsCache();
    maybePrefetch();
    // уведомление «что играет» (телефон): только при изменении
    if (platform.nowPlaying) {
        bool playing = cur && (isPlaying() || loading);
        std::string id = cur ? cur->id : std::string();
        if (playing != npPlaying_ || id != npId_) {
            npPlaying_ = playing;
            npId_ = id;
            if (cur) platform.nowPlaying(playing, cur->title, cur->artist);
        }
    }

    // статистика: пока песня звучит, копим время (с выключенным экраном тоже — frame() вызывается и там)
    double dtl = lastTick_ > 0 ? t - lastTick_ : 0;
    lastTick_ = t;
    if (cur && !loading && isPlaying() && !previewing_ && dtl > 0) {
        stats.add(*cur, dtl, localDay(), sessionSec_);
        statsDirty_ = true;
    }
    if (statsDirty_ && t - statsSavedAt_ > 30) saveStats();
    cloudTick();
}

void App::saveStats() {
    statsSavedAt_ = now();
    statsDirty_ = false;
    std::string data = stats.toJson().dump(), path = (P(dataDir) / "stats.json").u8string();
    async([path, data] { writeFileAtomic(path, data); });
}

void App::resetStats() {
    stats = ListenStats();
    statsDirty_ = true;
    saveStats();
}

std::vector<const Track*> App::topTracks(std::size_t n) {
    std::vector<const Track*> out;
    for (const TrackStat* s : stats.topTracks(n)) {
        if (const Track* t = lib.find(s->track.id)) { out.push_back(t); continue; }
        auto it = cache_.find(s->track.id);
        if (it == cache_.end()) it = cache_.emplace(s->track.id, s->track).first;
        out.push_back(&it->second);
    }
    return out;
}

// ---------- навигация и поиск ----------

void App::openView(ViewKind v, const std::string& playlistId) {
    view = v;
    viewPlaylist = playlistId;
}

std::vector<const Track*> App::currentList() const {
    std::vector<const Track*> out;
    switch (view) {
        case ViewKind::Search:
            for (auto& t : results) out.push_back(&t);
            break;
        case ViewKind::Library: return lib.list([](const Track& t) { return !t.clip; });
        case ViewKind::Clips: return lib.list([](const Track& t) { return t.clip; });
        case ViewKind::Offline: return lib.list([](const Track& t) { return t.off && !t.clip; });
        case ViewKind::ForYou:
            for (auto& t : recs) out.push_back(&t);
            break;
        case ViewKind::Playlist:
            for (auto& p : lib.playlists)
                if (p.id == viewPlaylist)
                    for (auto& id : p.ids) if (const Track* t = lib.find(id)) out.push_back(t);
            break;
        default: break;
    }
    return out;
}

const Track* App::getTrack(const std::string& id) const {
    if (const Track* t = lib.find(id)) return t;
    auto it = cache_.find(id);
    return it == cache_.end() ? nullptr : &it->second;
}

void App::search(const std::string& textIn) {
    std::string q = trim(textIn);
    view = ViewKind::Search;
    searchQuery = q;
    results.clear();
    linkTracks.clear();
    sourceStatus.clear();
    int token = ++searchToken_;
    if (q.empty()) { searching = false; return; }

    bool isYtVideo = !ytVideoId(q).empty() && q.find("youtu") != std::string::npos && ytListId(q).empty();
    if (isYtVideo) {
        searching = true;
        std::string id = ytVideoId(q);
        async([this, id, token] {
            Track t = net_->ytFromLink(id);
            post([this, t, token] {
                if (token != searchToken_) return;
                searching = false;
                cache_[t.id] = t;
                results = {t};
            });
        });
        return;
    }
    if (isScLink(q)) {
        searching = true;
        async([this, q, token] {
            try {
                LinkResult r = net_->scFromLink(q);
                post([this, r, token] {
                    if (token != searchToken_) return;
                    searching = false;
                    for (auto& t : r.tracks) cache_[t.id] = t;
                    results = r.tracks;
                    if (r.isPlaylist) { linkTracks = r.tracks; linkName = r.name; }
                });
            } catch (const std::exception& e) {
                std::string m = e.what();
                post([this, m, token] {
                    if (token != searchToken_) return;
                    searching = false;
                    toast(m);
                });
            }
        });
        return;
    }
    if (parseSpotifyLink(q).ok() || !ytListId(q).empty()) {
        searching = false;
        importPrefill = q;
        openImportModal = true;
        return;
    }
    searching = true;
    auto snap = snapshot(true);
    async([this, q, token, snap] {
        SearchOutcome out;
        try { out = net_->searchAll(q, *snap); } catch (const std::exception& e) { out.status.push_back({"Поиск", "", 0, true, e.what()}); }
        post([this, out, token] {
            if (token != searchToken_) return;
            searching = false;
            sourceStatus = out.status;
            results = out.tracks;
            for (auto& t : results) if (!lib.has(t.id)) cache_[t.id] = t;
        });
    });
}

// ---------- воспроизведение ----------

bool App::offlineAvailable(const Track& t) const {
    if (t.source == "local") return true;
    const Track* b = lib.find(t.baseId());
    std::error_code ec;
    return b && b->off && fs::exists(P(offlinePath(*b, "")), ec);
}

bool App::playableNow(const std::string& id) const {
    const Track* t = getTrack(id);
    return t && (online || offlineAvailable(*t));
}

void App::playFrom(const std::vector<std::string>& ids, const std::string& id) {
    queue.set(ids, id);
    if (const Track* t = getTrack(id)) play(*t);
}

void App::refreshSegment() {
    segment = cur ? lib.segmentFor(*cur) : Segment{};
    player.setSegment(segment);
}

void App::play(const Track& t) {
    if (!online && !offlineAvailable(t)) {
        toast("Нет интернета: «" + t.title + "» не скачана для офлайн");
        int i = nextPlayable(queue, 1, [this](const std::string& id) { return playableNow(id); });
        if (i >= 0) {
            queue.index = i;
            if (const Track* n = getTrack(queue.ids[static_cast<std::size_t>(i)])) { if (n->id != t.id) play(*n); }
        }
        return;
    }
    if (!lib.has(t.id)) cache_[t.id] = t;
    cur = t;
    sessionSec_ = 0;
    previewing_ = false;
    loading = true;
    loadProgress = 0;
    playError.clear();
    ly.data = Lyrics();
    ly.message.clear();
    peaks.clear();
    peaksFor_.clear();
    player.pause();
    segment = lib.segmentFor(t);
    int token = ++loadToken_;
    cancelLoad_->store(true);
    cancelLoad_ = std::make_shared<std::atomic<bool>>(false);
    startPlayback(t, token);
    if (ly.open) loadLyrics(false);
}

void App::startPlayback(const Track& t, int token) {
    // всё, что читает библиотеку, решаем здесь, в потоке интерфейса
    enum Kind { Offline, Local, Ready, Net } kind = Net;
    std::string path;
    std::optional<AudioBlob> ready;
    if (t.source == "local") { kind = Local; path = audioFilePath(t.ref); }
    else if (offlineAvailable(t)) { kind = Offline; path = offlinePath(*lib.find(t.baseId()), ""); }
    else if (prefetched_ && prefetched_->id == t.baseId()) { kind = Ready; ready = std::move(prefetched_->blob); prefetched_.reset(); }
    Segment seg = segment;
    auto cancel = cancelLoad_;
    auto snap = snapshot(false);
    std::string base = t.baseId();

    async([this, t, token, kind, path, ready, seg, cancel, snap, base]() mutable {
        std::string err;
        try {
            AudioBlob b;
            if (kind == Offline || kind == Local) {
                if (!readFile(path, b.bytes)) throw std::runtime_error("Файл не найден: добавьте его заново");
                b.ext = path.substr(path.rfind('.') + 1);
            } else if (kind == Ready) {
                b = std::move(*ready);
            } else {
                try {
                    b = net_->fetchAudio(t, [this, token](double p) { if (token == loadToken_) loadProgress = p; }, cancel.get());
                } catch (const std::exception&) {
                    if (cancel->load()) return;
                    if (t.source != "youtube") throw;
                    // YouTube не отдал звук: ищем ту же песню в других источниках
                    auto alt = net_->findAlternative(t, *snap);
                    if (!alt) throw;
                    Track a = *alt;
                    post([this, a, token, t] {
                        if (token != loadToken_) return;
                        toast("YouTube не отдал звук, играю версию из «" + std::string(sourceLabel(a.source)) + "»");
                        cache_[a.id] = a;
                        if (queue.index >= 0 && queue.index < static_cast<int>(queue.ids.size()) && queue.ids[static_cast<std::size_t>(queue.index)] == t.id)
                            queue.ids[static_cast<std::size_t>(queue.index)] = a.id;
                        Track merged = a;
                        merged.ptitle = t.lyricTitle();
                        merged.partist = t.lyricArtist();
                        play(merged);
                    });
                    return;
                }
            }
            b.ext = sniffExt(b.bytes, b.ext);
            std::string lerr;
            bool ok;
            {
                std::lock_guard<std::mutex> lk(loadMu_);
                if (token != loadToken_) return;  // пока качали, выбрали другую песню
                ok = player.load(std::move(b.bytes), b.ext, seg, seg.startPos(), &lerr);
            }
            if (!ok) err = lerr;
        } catch (const std::exception& e) {
            err = e.what();
        }
        if (cancel->load() && err.empty()) return;
        post([this, t, token, err] { finishLoad(t, token, err); });
    });
}

void App::finishLoad(const Track& t, int token, const std::string& err) {
    if (token != loadToken_) return;
    loading = false;
    if (!err.empty()) {
        playError = err;
        toast(err);
        if (++fails_ < 4 && queue.ids.size() > 1) post([this, token] { if (token == loadToken_) next(true); }, 0.9);
        return;
    }
    fails_ = 0;
    loadedBase_ = t.baseId();
    double d = player.duration();
    if (Track* lt = lib.find(t.id)) { if (lt->dur < 1 && d > 1) { lt->dur = d; touch(); } }
    if (cur && cur->dur < 1) cur->dur = d;
    player.setRate(rate);
    player.setVolume(volume);
    player.play();
}

void App::togglePause() {
    if (!cur || loading) return;
    if (player.playing()) player.pause();
    else {
        if (player.ended()) player.seek(segment.startPos());
        player.play();
    }
}

void App::onEnded() {
    if (previewing_) { endPreview(); player.pause(); player.seek(segment.startPos()); return; }
    if (sleepAtEnd) { sleepAtEnd = false; player.pause(); toast("Таймер сна: музыка остановлена"); return; }
    if (queue.loop == Loop::One) { player.seek(segment.startPos()); player.play(); return; }
    next(true);
}

void App::next(bool automatic) {
    auto playable = [this](const std::string& id) { return playableNow(id); };
    StepResult r = step(queue, 1, automatic, playable, [](int n) { return randInt(n); });
    switch (r.kind) {
        case StepResult::Play:
            if (const Track* t = getTrack(queue.ids[static_cast<std::size_t>(r.index)])) play(*t);
            break;
        case StepResult::Stop: player.pause(); break;
        case StepResult::NeedRandom: randomSong(); break;
        case StepResult::NoPlayable: toast("Нет интернета, а скачанных песен в очереди нет"); player.pause(); break;
        default: break;
    }
}

void App::prev() {
    if (!cur) return;
    if (position() > 3) { seek(0); return; }
    auto playable = [this](const std::string& id) { return playableNow(id); };
    StepResult r = step(queue, -1, false, playable, [](int n) { return randInt(n); });
    if (r.kind == StepResult::Play)
        if (const Track* t = getTrack(queue.ids[static_cast<std::size_t>(r.index)])) play(*t);
}

double App::position() const {
    double p = player.position() - segment.visibleBase();
    return p < 0 ? 0 : p;
}
double App::visibleLength() const { return segment.visibleLength(player.duration()); }

void App::seek(double sec) {
    if (!cur || loading) return;
    double len = visibleLength();
    sec = std::max(0.0, std::min(sec, len > 0.1 ? len - 0.05 : sec));
    player.seek(segment.visibleBase() + sec);
}

void App::setVolume(float v) {
    volume = std::max(0.0f, std::min(1.0f, v));
    player.setVolume(volume);
    touch();
}
void App::setRate(double r) {
    rate = std::max(0.5, std::min(2.0, r));
    player.setRate(rate);
}
void App::toggleShuffle() { queue.shuffle = !queue.shuffle; }
void App::cycleLoop() { queue.loop = mx::cycleLoop(queue.loop); }

void App::media(Media m) {
    switch (m) {
        case Media::PlayPause: togglePause(); break;
        case Media::Next: next(false); break;
        case Media::Prev: prev(); break;
        case Media::Stop: player.pause(); break;
    }
}

// Следующая песня качается заранее, чтобы переход был без паузы.
void App::maybePrefetch() {
    if (!cur || loading || !online || !player.playing() || !prefetching_.empty()) return;
    double d = player.duration(), p = player.position();
    if (d < 40 || d - p > 25 || queue.shuffle || queue.randomChain) return;
    int i = queue.index + 1;
    if (i < 0 || i >= static_cast<int>(queue.ids.size())) return;
    const Track* n = getTrack(queue.ids[static_cast<std::size_t>(i)]);
    if (!n || n->source == "local" || n->source == "youtube" || offlineAvailable(*n)) return;
    if (prefetched_ && prefetched_->id == n->baseId()) return;
    Track nt = *n;
    prefetching_ = nt.baseId();
    int token = loadToken_;
    async([this, nt, token] {
        try {
            AudioBlob b = net_->fetchAudio(nt);
            post([this, nt, token, b] {
                prefetching_.clear();
                if (token == loadToken_) prefetched_ = Prefetch{nt.baseId(), b};
            });
        } catch (const std::exception&) {
            post([this] { prefetching_ = "-"; });  // не получилось: больше не пробуем для этой песни
        }
    });
}

// «Случайная песня»: если есть своя музыка — из подборки похожего, иначе из случайного жанра.
void App::randomSong() {
    if (!online) { toast("Для случайной песни нужен интернет"); return; }
    if (tasteRandom()) return;
    randomGenre();
}

bool App::tasteRandom() {
    if (!lib.getBool("tasteRandom", true)) return false;
    if (lib.list([](const Track& t) { return !t.clip; }).size() < 3 && stats.trackCount() < 3) return false;
    std::vector<const Track*> fresh;
    for (auto& t : recs) if (!seenRandom_.count(t.id) && !lib.has(t.id)) fresh.push_back(&t);
    if (fresh.empty()) {  // подборки ещё нет или вся прослушана — подбираем заново и потом играем
        if (recsLoading) { pendingTaste_ = true; return true; }
        pendingTaste_ = true;
        loadRecs(true);
        return recsLoading;  // не удалось даже начать (нет семян) — пусть играет случайный жанр
    }
    Track t = *fresh[static_cast<std::size_t>(randInt(static_cast<int>(fresh.size())))];
    seenRandom_.insert(t.id);
    cache_[t.id] = t;
    queue.ids = {t.id};
    queue.index = 0;
    queue.randomChain = true;
    toast("Похоже на вашу музыку: " + t.title + (t.artist.empty() ? "" : " — " + t.artist));
    play(t);
    return true;
}

// Подбор похожего: берём песни, которые слушаете чаще всего, и случайные из «Моей музыки».
// Для песен SoundCloud — «похожие треки», которые показывает сам SoundCloud; для остальных — поиск по исполнителю.
void App::loadRecs(bool force) {
    if (recsLoading || (!force && !recs.empty()) || !online) return;
    std::vector<Track> seeds;
    std::set<std::string> used;
    auto take = [&](const Track& t) {
        if (seeds.size() >= 6 || used.count(t.id) || t.source == "local") return;
        used.insert(t.id);
        seeds.push_back(t);
    };
    std::vector<Track> top;
    for (const TrackStat* s : stats.topTracks(10)) top.push_back(s->track);
    std::vector<Track> mine;
    for (const Track* t : lib.list([](const Track& x) { return !x.clip; })) mine.push_back(*t);
    std::mt19937 rng(std::random_device{}());
    std::shuffle(top.begin(), top.end(), rng);
    std::shuffle(mine.begin(), mine.end(), rng);
    for (std::size_t i = 0; i < top.size() && seeds.size() < 3; ++i) take(top[i]);
    for (auto& t : mine) take(t);
    if (seeds.empty()) {
        if (pendingTaste_) { pendingTaste_ = false; }
        return;
    }
    std::set<std::string> known = seenRandom_;
    for (auto& kv : lib.tracks) known.insert(kv.first);
    bool sc = lib.srcOn("soundcloud"), au = lib.srcOn("audius");
    recsLoading = true;
    async([this, seeds, known, sc, au] {
        std::vector<Track> got;
        std::vector<std::string> basis;
        for (const Track& s : seeds) {
            std::vector<Track> part;
            if (s.source == "soundcloud" && sc) {
                try { part = net_->scRelated(s.ref, 15); } catch (const std::exception&) {}
            }
            if (part.size() < 5 && !trim(s.artist).empty()) {
                if (sc) { try { for (auto& x : net_->scSearch(s.artist, 8)) part.push_back(x); } catch (const std::exception&) {} }
                if (au) { try { for (auto& x : net_->audiusSearch(s.artist, 8)) part.push_back(x); } catch (const std::exception&) {} }
            }
            if (!part.empty() && !trim(s.artist).empty()) basis.push_back(trim(s.artist));
            got.insert(got.end(), part.begin(), part.end());
        }
        // уже знакомое, совсем короткое и повторы (та же песня с другого аккаунта) — убираем
        std::set<std::string> ids, names;
        std::vector<Track> out;
        for (auto& t : got) {
            if (known.count(t.id) || ids.count(t.id) || (t.dur > 0 && t.dur < 45)) continue;
            std::string nk = lower(trim(t.artist)) + "|" + lower(trim(t.title));
            if (names.count(nk)) continue;
            ids.insert(t.id);
            names.insert(nk);
            out.push_back(t);
        }
        std::mt19937 r2(std::random_device{}());
        std::shuffle(out.begin(), out.end(), r2);
        if (out.size() > 60) out.resize(60);
        post([this, out, basis] {
            recsLoading = false;
            bool wanted = pendingTaste_;
            pendingTaste_ = false;
            if (out.empty()) {
                if (wanted) randomGenre();
                else toast("Не получилось подобрать похожие песни. Попробуйте позже");
                return;
            }
            recs = out;
            for (auto& t : recs) cache_[t.id] = t;
            std::vector<std::string> uniq;
            for (auto& b : basis) {
                bool dup = false;
                for (auto& u : uniq) dup = dup || lower(u) == lower(b);
                if (!dup && uniq.size() < 3) uniq.push_back(b);
            }
            recsBasis = uniq.empty() ? std::string() : "Похоже на: " + join(uniq, ", ");
            if (wanted) randomSong();
        });
    });
}

void App::randomGenre() {
    static std::atomic<bool> busy{false};
    if (busy.exchange(true)) return;
    std::set<std::string> known;
    for (auto& kv : lib.tracks) known.insert(kv.first);
    std::set<std::string> seen = seenRandom_;
    bool sc = lib.srcOn("soundcloud");
    async([this, known, seen, sc] {
        auto fresh = [&](std::vector<Track> l) {
            std::vector<Track> out;
            for (auto& t : l) if (!known.count(t.id) && !seen.count(t.id) && (t.dur == 0 || t.dur > 45)) out.push_back(t);
            return out;
        };
        const int n = static_cast<int>(sizeof(kGenres) / sizeof(kGenres[0]));
        std::vector<Track> pick;
        for (int k = 0; k < 3 && pick.empty(); ++k) {
            try { pick = fresh(net_->audiusSearch(kGenres[randInt(n)], 50)); } catch (const std::exception&) {}
        }
        if (pick.empty()) { try { pick = fresh(net_->audiusTrending()); } catch (const std::exception&) {} }
        if (pick.empty() && sc) { try { pick = fresh(net_->scSearch(kGenres[randInt(n)], 15)); } catch (const std::exception&) {} }
        post([this, pick] {
            busy = false;
            if (pick.empty()) { toast("Не получилось найти новую песню. Попробуйте ещё раз"); return; }
            Track t = pick[static_cast<std::size_t>(randInt(static_cast<int>(pick.size())))];
            seenRandom_.insert(t.id);
            cache_[t.id] = t;
            queue.ids = {t.id};
            queue.index = 0;
            queue.randomChain = true;
            toast("Случайная песня: " + t.title + (t.artist.empty() ? "" : " — " + t.artist));
            play(t);
        });
    });
}

// ---------- библиотека ----------

void App::toggleFavorite(const Track& t) {
    if (lib.has(t.id)) {
        if (!t.clip) {
            std::string id = t.id;
            auto gone = lib.remove(id);
            for (auto& g : gone) { std::error_code ec; if (const Track* x = getTrack(g)) fs::remove(P(offlinePath(*x, "")), ec); }
        }
    } else {
        Track& a = lib.add(t);
        if (lib.getBool("autoOff", false) && a.canOffline()) saveOffline(a, true);
    }
    touch();
}

std::string App::newPlaylist(const std::string& name) {
    std::string n = trim(name);
    if (n.empty()) n = "Новый плейлист";
    Playlist& p = lib.createPlaylist(n);
    touch();
    return p.id;
}

void App::addToPlaylist(const std::string& playlistId, const Track& t) {
    Playlist* p = lib.playlist(playlistId);
    if (!p) return;
    Track& a = lib.add(t);
    if (std::find(p->ids.begin(), p->ids.end(), a.id) == p->ids.end()) p->ids.push_back(a.id);
    touch();
    toast("Добавлено в «" + p->name + "»");
}

void App::removeFromPlaylist(const std::string& playlistId, const std::string& trackId) {
    Playlist* p = lib.playlist(playlistId);
    if (!p) return;
    p->ids.erase(std::remove(p->ids.begin(), p->ids.end(), trackId), p->ids.end());
    touch();
}

void App::addFiles() {
    if (platform.pickAsync) { platform.pickAsync("audio", [this](std::vector<std::string> p) { addPaths(p); }); return; }
    if (!platform.pickAudioFiles) return;
    addPaths(platform.pickAudioFiles());
}

void App::addPaths(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    std::string dir = (P(dataDir) / "files").u8string();
    async([this, paths, dir] {
        std::vector<Track> added;
        for (auto& src : paths) {
            try {
                fs::path sp = P(src);
                std::string name = newId() + sp.extension().u8string();
                std::error_code ec;
                fs::copy_file(sp, P(dir) / P(name), fs::copy_options::overwrite_existing, ec);
                if (ec) continue;
                Track t = Track::make("local", name);
                t.title = sp.stem().u8string();
                t.artist = "Файл на устройстве";
                added.push_back(t);
            } catch (const std::exception&) {}
        }
        post([this, added] {
            for (auto& t : added) lib.add(t);
            touch();
            toast(added.empty() ? "Не удалось добавить файлы" : "Добавлено файлов: " + std::to_string(added.size()));
        });
    });
}

// ---------- офлайн ----------

void App::saveOffline(const Track& t, bool quiet) {
    if (!t.canOffline() || downloading.count(t.id)) return;
    Track& lt = lib.add(t);
    if (lt.off) return;
    Track copy = lt;
    std::string path = offlinePath(copy, "");
    downloading[copy.id] = 0;
    async([this, copy, path, quiet] {
        std::string err;
        long long size = 0;
        try {
            AudioBlob b = net_->fetchAudio(copy, [this, id = copy.id](double p) {
                post([this, id, p] { if (downloading.count(id)) downloading[id] = p; });
            });
            if (b.bytes.size() < 20000) throw std::runtime_error("Получен пустой файл");
            if (!writeFileAtomic(path, b.bytes, &err)) throw std::runtime_error(err);
            size = static_cast<long long>(b.bytes.size());
        } catch (const std::exception& e) {
            err = e.what();
        }
        post([this, copy, err, size, quiet] {
            downloading.erase(copy.id);
            if (!err.empty()) { if (!quiet) toast("Не удалось скачать «" + copy.title + "»: " + err); return; }
            if (Track* lt = lib.find(copy.id)) { lt->off = true; lt->offSize = size; touch(); }
            if (!quiet) toast("Скачано: «" + copy.title + "». Играет без интернета");
        });
    });
}

void App::dropOffline(const Track& t) {
    Track* lt = lib.find(t.baseId());
    if (!lt) return;
    std::error_code ec;
    fs::remove(P(offlinePath(*lt, "")), ec);
    lt->off = false;
    lt->offSize = 0;
    touch();
}

void App::dropAllOffline() {
    std::vector<Track> offs;
    for (const Track* t : lib.list([](const Track& x) { return x.off; })) offs.push_back(*t);
    for (const Track& t : offs) dropOffline(t);
    toast(offs.empty() ? "Скачанных песен нет" : "Офлайн-копии удалены: " + std::to_string(offs.size()));
}

long long App::offlineBytes() const {
    long long sum = 0;
    for (const Track* t : lib.list([](const Track& x) { return x.off; })) sum += t->offSize;
    return sum;
}

namespace {
// Имя файла из «Исполнитель - Название» без символов, запрещённых в Windows.
std::string safeFileName(const Track& t) {
    std::string n = t.artist.empty() ? t.title : t.artist + " - " + t.title;
    std::string out;
    for (char c : n) out += (std::string("<>:\"/\\|?*").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) ? '_' : c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.size() > 150) out.resize(150);
    return out.empty() ? "song" : out;
}
}  // namespace

void App::exportTrack(const Track& tIn) {
    if ((!platform.pickSaveAudio && !platform.publishFile) || tIn.clip || exporting.count(tIn.id)) return;
    Track t = tIn;
    // откуда брать звук: свой файл, офлайн-копия или сеть
    std::string srcPath;
    if (t.source == "local") srcPath = audioFilePath(t.ref);
    else if (offlineAvailable(t)) srcPath = offlinePath(*lib.find(t.baseId()), "");
    // расширение угадываем заранее (для окна сохранения), а после загрузки уточняем по содержимому файла
    std::string guess = "mp3";
    if (t.source == "local") { auto d = t.ref.rfind('.'); if (d != std::string::npos) guess = t.ref.substr(d + 1); }
    else if (t.source == "youtube") guess = "m4a";
    std::string path;
    if (platform.publishFile) {  // телефон: пишем во временную папку, затем система кладёт файл в «Музыка/Mixtape»
        std::error_code ec;
        fs::create_directories(P(dataDir) / "export", ec);
        path = (P(dataDir) / "export" / P(safeFileName(t) + "." + guess)).u8string();
    } else {
        path = platform.pickSaveAudio(safeFileName(t) + "." + guess);
    }
    if (path.empty()) return;
    exporting.insert(t.id);
    toast("Сохраняю «" + t.title + "»…");
    async([this, t, srcPath, path] {
        std::string err, finalPath = path;
        try {
            std::string bytes;
            if (!srcPath.empty()) {
                if (!readFile(srcPath, bytes)) throw std::runtime_error("файл песни не найден");
            } else {
                bytes = net_->fetchAudio(t).bytes;
            }
            if (bytes.size() < 20000) throw std::runtime_error("получен пустой файл");
            std::string ext = sniffExt(bytes, "");
            auto dot = finalPath.rfind('.');
            auto slash = finalPath.find_last_of("\\/");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) finalPath = finalPath.substr(0, dot);
            finalPath += "." + ext;
            if (!writeFileAtomic(finalPath, bytes, &err)) throw std::runtime_error(err);
            if (platform.publishFile) {
                std::string where = platform.publishFile(finalPath, "audio");
                if (where.empty()) throw std::runtime_error("не удалось положить файл в «Музыку»");
                finalPath = where;
            }
        } catch (const std::exception& e) {
            err = e.what();
        }
        post([this, t, err, finalPath] {
            exporting.erase(t.id);
            if (!err.empty()) toast("Не удалось сохранить «" + t.title + "»: " + err);
            else toast("Сохранено: " + finalPath);
        });
    });
}

void App::offlineAll(const std::vector<const Track*>& list) {
    int n = 0;
    for (const Track* t : list)
        if (t->canOffline() && !(lib.find(t->id) && lib.find(t->id)->off)) { saveOffline(*t, true); ++n; }
    toast(n ? "Скачиваю песен: " + std::to_string(n) : "Всё уже скачано");
}

// ---------- фрагменты ----------

void App::loadPeaks() {
    if (!cur || loading || peaksLoading || peaksFor_ == cur->baseId()) return;
    std::string bytes, ext;
    if (!player.copyBytes(bytes, ext)) return;
    peaksLoading = true;
    peaksFor_ = cur->baseId();
    std::string forId = peaksFor_;
    async([this, bytes = std::move(bytes), ext, forId] {
        auto p = Player::peaks(bytes, ext, 600);
        post([this, p, forId] {
            peaksLoading = false;
            if (cur && cur->baseId() == forId) peaks = p;
        });
    });
}

void App::previewSegment(bool keepMode, double s, double e) {
    if (!cur) return;
    Segment p = lib.segmentFor(*cur);
    p.hasLimit = false;
    double from = s;
    if (keepMode) {
        p.hasLimit = true;
        p.preview = true;
        p.limS = s;
        p.limE = e;
    } else {
        p.cuts.push_back({s, e});
        p.cuts = mergeCuts(p.cuts);
        from = std::max(0.0, s - 2.5);
    }
    previewing_ = true;
    player.setSegment(p);
    player.seek(from);
    player.play();
}

void App::endPreview() {
    previewing_ = false;
    refreshSegment();
}

void App::saveClip(bool keepMode, double s, double e) {
    if (!cur) return;
    if (e - s < 0.5) { toast("Слишком короткий кусок"); return; }
    if (keepMode) {
        Track base = *cur;
        std::string pt = cur->lyricTitle(), pa = cur->lyricArtist();
        Track clip = base;
        clip.id = newId();
        clip.clip = true;
        clip.mode = "keep";
        clip.start = s;
        clip.end = e;
        clip.parent = cur->baseId();
        clip.ptitle = pt;
        clip.partist = pa;
        clip.cuts.clear();
        clip.off = false;
        clip.offSize = 0;
        if (!cur->clip) lib.add(base);
        lib.add(clip);
        toast("Фрагмент сохранён: «Фрагменты» в меню слева");
    } else {
        lib.addCut(*cur, {s, e});
        toast("Кусок вырезан навсегда. Вернуть можно в окне фрагмента");
    }
    touch();
    endPreview();
}

void App::removeCut(const CutRef& ref) {
    if (!cur) return;
    lib.removeCut(*cur, ref);
    touch();
    refreshSegment();
}

// ---------- тексты ----------

void App::openLyrics(bool open) {
    ly.open = open;
    if (open) loadLyrics(false);
}

void App::loadLyrics(bool force) {
    if (!cur) return;
    std::string key = lyricKey(*cur);
    ly.key = key;
    auto off = lib.lyOff.find(key);
    ly.offset = off == lib.lyOff.end() ? 0 : off->second;
    ly.message.clear();
    auto own = lib.lyrics.find(key);
    if (own != lib.lyrics.end()) { ly.data = own->second; ly.loading = false; return; }
    auto c = lyCache_.find(key);
    if (!force && c != lyCache_.end()) { ly.data = c->second; ly.loading = false; if (ly.data.empty()) ly.message = "Текст не найден"; return; }
    ly.loading = true;
    ly.data = Lyrics();
    int token = ++lyToken_;
    std::string title = cur->lyricTitle(), artist = cur->lyricArtist();
    double dur = cur->dur > 0 ? cur->dur : player.duration();
    async([this, key, title, artist, dur, token] {
        Lyrics l;
        std::string err;
        try { l = net_->findLyrics(title, artist, dur); } catch (const std::exception& e) { err = e.what(); }
        post([this, key, l, err, token] {
            if (token != lyToken_ || ly.key != key) return;
            ly.loading = false;
            if (!err.empty()) { ly.message = "Не удалось загрузить: " + err; return; }
            ly.data = l;
            if (l.empty()) ly.message = "Текст не найден. Можно вставить свой";
            else { lyCache_[key] = l; lyCacheDirty_ = true; }
        });
    });
}

void App::setOwnLyrics(const std::string& text) {
    if (!cur) return;
    std::string key = lyricKey(*cur);
    if (trim(text).empty()) { lib.lyrics.erase(key); loadLyrics(true); touch(); return; }
    Lyrics l;
    l.src = "свой текст";
    l.name = cur->title;
    l.artist = cur->artist;
    l.lines = parseLrc(text);
    if (l.lines.empty()) l.plain = text;
    lib.lyrics[key] = l;
    ly.data = l;
    ly.message.clear();
    ly.loading = false;
    touch();
}

void App::shiftLyrics(double d) {
    if (ly.key.empty()) return;
    ly.offset = std::round((ly.offset + d) * 10) / 10;
    lib.lyOff[ly.key] = ly.offset;
    touch();
}

int App::lyricLineAt(double t) const {
    const auto& L = ly.data.lines;
    if (L.empty()) return -1;
    double tt = t + ly.offset;
    int lo = 0, hi = static_cast<int>(L.size()) - 1, ans = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (L[static_cast<std::size_t>(mid)].t <= tt) { ans = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return ans;
}

// ---------- перенос плейлистов ----------

void App::startImportLink(const std::string& linkIn) {
    if (imp.running) return;
    std::string link = trim(linkIn);
    if (!online) { imp = ImportState(); imp.done = true; imp.message = "Нет интернета: перенос невозможен."; return; }
    imp = ImportState();
    imp.running = true;
    imp.message = "Читаю плейлист…";
    async([this, link] {
        try {
            PlaylistInfo info;
            SpotifyLink sl = parseSpotifyLink(link);
            std::string lid = ytListId(link);
            if (sl.ok()) info = net_->spPlaylist(sl.kind, sl.id);
            else if (!lid.empty()) {
                info = net_->ytPlaylist(lid);
                for (auto& it : info.items) {
                    auto p = splitDash(it.title);
                    if (p.size() > 1) {
                        it.artist = p[0];
                        std::vector<std::string> rest(p.begin() + 1, p.end());
                        it.title = join(rest, " - ");
                    }
                }
            } else if (isScLink(link)) {
                LinkResult r = net_->scFromLink(link);
                post([this, r] {
                    imp = ImportState();
                    Playlist& pl = lib.createPlaylist(r.name);
                    for (auto& t : r.tracks) { lib.add(t); pl.ids.push_back(t.id); }
                    touch();
                    imp.done = true;
                    imp.total = imp.found = static_cast<int>(r.tracks.size());
                    imp.message = "Готово: плейлист «" + r.name + "», песен: " + std::to_string(r.tracks.size());
                });
                return;
            } else {
                throw std::runtime_error("Не получилось распознать ссылку. Нужна ссылка на плейлист Spotify, YouTube или SoundCloud");
            }
            if (info.items.empty()) throw std::runtime_error("В плейлисте не нашлось песен (возможно, он закрыт)");
            post([this, info] { importRun(info.name, info.items); });
        } catch (const std::exception& e) {
            std::string m = e.what();
            post([this, m] { imp = ImportState(); imp.done = true; imp.message = m; });
        }
    });
}

void App::startImportList(const std::string& name, const std::string& text) {
    if (imp.running) return;
    auto items = parseList(text);
    if (items.empty()) { imp = ImportState(); imp.done = true; imp.message = "Список пуст"; return; }
    importRun(trim(name).empty() ? "Импортированный список" : trim(name), items);
}

void App::importRun(std::string name, std::vector<ListItem> items) {
    if (!online) { imp = ImportState(); imp.done = true; imp.message = "Нет интернета: перенос невозможен."; return; }
    imp = ImportState();
    imp.running = true;
    imp.total = static_cast<int>(items.size());
    imp.name = name;
    imp.message = "Ищу песни…";
    auto snap = snapshot(false);
    async([this, name, items, snap] {
        std::vector<std::optional<Track>> found(items.size());
        std::atomic<std::size_t> idx{0};
        std::atomic<int> done{0}, ok{0};
        auto worker = [&] {
            for (;;) {
                std::size_t k = idx++;
                if (k >= items.size()) return;
                try { found[k] = net_->matchTrack(items[k], *snap); } catch (const std::exception&) {}
                if (found[k]) ++ok;
                int d = ++done, o = ok.load();
                post([this, d, o] { imp.finished = d; imp.found = o; imp.message = "Ищу: " + std::to_string(d) + " из " + std::to_string(imp.total) + ", найдено " + std::to_string(o); });
            }
        };
        std::vector<std::thread> th;
        for (int i = 0; i < 3; ++i) th.emplace_back(worker);
        for (auto& t : th) t.join();
        post([this, name, items, found] {
            std::vector<Track> okT;
            std::vector<std::string> miss;
            std::map<std::string, int> by;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (found[i]) { okT.push_back(*found[i]); ++by[sourceLabel(found[i]->source)]; }
                else miss.push_back((items[i].artist.empty() ? "" : items[i].artist + " — ") + items[i].title);
            }
            imp.running = false;
            imp.done = true;
            imp.missing = miss;
            if (!okT.empty()) {
                Playlist& pl = lib.createPlaylist(name);
                for (auto& t : okT) { Track& a = lib.add(t); if (std::find(pl.ids.begin(), pl.ids.end(), a.id) == pl.ids.end()) pl.ids.push_back(a.id); }
                touch();
            }
            std::string m = "Готово: найдено " + std::to_string(okT.size()) + " из " + std::to_string(items.size());
            if (!okT.empty()) {
                m += ". Плейлист «" + name + "» создан.\nОткуда: ";
                bool first = true;
                for (auto& kv : by) { m += (first ? "" : ", ") + kv.first + " " + std::to_string(kv.second); first = false; }
            }
            imp.message = m;
        });
    });
}

// ---------- данные ----------

void App::saveBackup() {
    if (platform.publishFile) {  // телефон: в «Загрузки/Mixtape»
        std::string tmp = (P(dataDir) / "mixtape-backup.json").u8string(), data = lib.toJson().dump(2);
        async([this, tmp, data] {
            std::string err, where;
            if (writeFileAtomic(tmp, data, &err)) where = platform.publishFile(tmp, "backup");
            post([this, where, err] { toast(where.empty() ? "Не удалось сохранить копию " + err : "Копия сохранена: " + where); });
        });
        return;
    }
    if (!platform.pickSavePath) return;
    std::string path = platform.pickSavePath("mixtape-backup.json");
    if (path.empty()) return;
    std::string err;
    if (writeFileAtomic(path, lib.toJson().dump(2), &err)) toast("Копия сохранена");
    else toast("Не удалось сохранить копию: " + err);
}

void App::loadBackup() {
    if (platform.pickAsync) {
        platform.pickAsync("backup", [this](std::vector<std::string> p) { if (!p.empty()) loadBackupFrom(p[0]); });
        return;
    }
    if (!platform.pickOpenPath) return;
    loadBackupFrom(platform.pickOpenPath());
}

void App::loadBackupFrom(const std::string& path) {
    if (path.empty()) return;
    std::string text, err;
    Json j;
    if (!readFile(path, text) || !Json::tryParse(text, j, &err)) { toast("Не удалось прочитать файл копии"); return; }
    auto ms = lib.mergeBackup(j);
    if (!ms.ok) { toast("Это не резервная копия Mixtape"); return; }
    touch();
    toast("Добавлено: песен " + std::to_string(ms.tracks) + ", плейлистов " + std::to_string(ms.playlists) + ", текстов " + std::to_string(ms.lyrics));
}

void App::tetoEgg(bool& firstTime) {
    firstTime = !tetoOn();
    lib.set("tetoOn", Json(true));
    lib.set("theme", Json("teto"));
    touch();
}

}  // namespace mx
