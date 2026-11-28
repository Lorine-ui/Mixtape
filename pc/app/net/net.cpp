#include "net.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <random>
#include <stdexcept>
#include <thread>

#include "rank.hpp"
#include "text.hpp"

namespace mx {

namespace {

const char* kUA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
const char* kAppUA = "Mixtape/2.0 (local music player)";

using Headers = std::vector<std::pair<std::string, std::string>>;

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error(msg); }

std::string pageOrigin(const std::string& url) {
    std::size_t p = url.find("://");
    if (p == std::string::npos) return url;
    std::size_t e = url.find('/', p + 3);
    return e == std::string::npos ? url : url.substr(0, e);
}

Json parseJsonBody(const std::string& body) {
    Json j;
    std::string err;
    if (!Json::tryParse(body, j, &err)) fail("Сервис вернул непонятный ответ");
    return j;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

}  // namespace

// ---------- помощники ----------

std::string resolveUrl(const std::string& base, const std::string& rel) {
    if (startsWith(rel, "http://") || startsWith(rel, "https://")) return rel;
    std::size_t p = base.find("://");
    std::string scheme = p == std::string::npos ? "https:" : base.substr(0, p + 1);
    if (startsWith(rel, "//")) return scheme + rel;
    if (startsWith(rel, "/")) return pageOrigin(base) + rel;
    std::string b = base.substr(0, base.find_first_of("?#"));
    std::size_t slash = b.rfind('/');
    std::size_t minPos = p == std::string::npos ? 0 : p + 3;
    if (slash == std::string::npos || slash < minPos) return b + "/" + rel;
    return b.substr(0, slash + 1) + rel;
}

std::vector<std::string> parseM3u8(const std::string& text, const std::string& baseUrl) {
    std::vector<std::string> out;
    for (auto& raw : split(text, '\n')) {
        std::string line = trim(raw);
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (startsWith(line, "#EXT-X-MAP:")) {
                std::size_t a = line.find("URI=\"");
                if (a != std::string::npos) {
                    a += 5;
                    std::size_t b = line.find('"', a);
                    if (b != std::string::npos) out.insert(out.begin(), resolveUrl(baseUrl, line.substr(a, b - a)));
                }
            }
            continue;
        }
        out.push_back(resolveUrl(baseUrl, line));
    }
    return out;
}

std::string extForMime(const std::string& mimeIn, const std::string& url) {
    std::string m = lowerAscii(mimeIn.substr(0, mimeIn.find(';')));
    if (m == "audio/mpeg" || m == "audio/mp3") return "mp3";
    if (m == "audio/mp4" || m == "audio/x-m4a" || m == "audio/m4a") return "m4a";
    if (m == "audio/aac") return "aac";
    if (m == "audio/ogg" || m == "application/ogg") return "ogg";
    if (m == "audio/flac" || m == "audio/x-flac") return "flac";
    if (m == "audio/wav" || m == "audio/x-wav" || m == "audio/wave") return "wav";
    if (m == "audio/webm") return "webm";
    std::string path = url.substr(0, url.find_first_of("?#"));
    std::size_t dot = path.rfind('.');
    if (dot != std::string::npos && path.size() - dot <= 5 && path.find('/', dot) == std::string::npos) {
        std::string e = lowerAscii(path.substr(dot + 1));
        if (e == "mp3" || e == "m4a" || e == "ogg" || e == "flac" || e == "wav" || e == "aac" || e == "opus") return e;
    }
    return "mp3";
}

// ---------- общее ----------

std::string Net::get(const std::string& url, const Headers& h, int timeoutMs) {
    HttpOptions o;
    o.headers = h;
    o.timeoutMs = timeoutMs;
    HttpResponse r = http_.request(url, o);
    if (!r.ok()) fail("Сайт ответил ошибкой " + std::to_string(r.status));
    return r.body;
}

Json Net::getJson(const std::string& url, int timeoutMs) {
    return parseJsonBody(get(url, {{"User-Agent", kAppUA}}, timeoutMs));
}

// ---------- Audius ----------

void Net::audiusRehost() {
    Json l = getJson("https://api.audius.co");
    std::vector<std::string> hosts;
    for (const auto& h : l["data"].items()) if (h.isString()) hosts.push_back(h.str());
    if (hosts.empty()) fail("Audius сейчас недоступен");
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lk(mu_);
    audiusHost_ = hosts[rng() % hosts.size()];
}

std::string Net::audiusHost() {
    std::lock_guard<std::mutex> lk(mu_);
    return audiusHost_;
}

Json Net::audiusJson(const std::string& path) {
    auto url = [&](const std::string& host) {
        return host + "/v1" + path + (path.find('?') != std::string::npos ? "&" : "?") + "app_name=Mixtape";
    };
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            return getJson(url(audiusHost()));
        } catch (const std::exception&) {
            if (attempt == 1) break;
            try { audiusRehost(); } catch (const std::exception&) { break; }
        }
    }
    fail("Audius сейчас недоступен");
}

std::vector<Track> Net::audiusSearch(const std::string& q, int n) {
    return parseAudiusTracks(audiusJson("/tracks/search?query=" + urlEncode(q) + "&limit=" + std::to_string(n)));
}

std::vector<Track> Net::audiusTrending() { return parseAudiusTracks(audiusJson("/tracks/trending?limit=20")); }

// ---------- SoundCloud ----------

std::string Net::scClient(bool force) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!force && !scCid_.empty() && nowMs() - scCidAt_ < 6LL * 3600 * 1000) return scCid_;
    }
    std::vector<std::string> urls = scScriptUrls(get("https://soundcloud.com/", {{"User-Agent", kUA}}));
    std::reverse(urls.begin(), urls.end());
    for (std::size_t i = 0; i < urls.size() && i < 8; ++i) {
        try {
            std::string id = scClientId(get(urls[i], {{"User-Agent", kUA}}));
            if (!id.empty()) {
                std::lock_guard<std::mutex> lk(mu_);
                scCid_ = id;
                scCidAt_ = nowMs();
                return id;
            }
        } catch (const std::exception&) { /* пробуем следующий файл */ }
    }
    fail("SoundCloud изменил сайт: не удалось подключиться к поиску");
}

HttpResponse Net::scRaw(const std::string& pathAndQuery) {
    for (int i = 0; i < 2; ++i) {
        std::string cid = scClient(i > 0);
        HttpOptions o;
        o.headers = {{"User-Agent", kUA}, {"Accept", "application/json"}};
        o.timeoutMs = 12000;
        std::string url = "https://api-v2.soundcloud.com" + pathAndQuery +
                          (pathAndQuery.find('?') != std::string::npos ? "&" : "?") + "client_id=" + cid;
        HttpResponse r = http_.request(url, o);
        if ((r.status == 401 || r.status == 403) && i == 0) {
            std::lock_guard<std::mutex> lk(mu_);
            scCid_.clear();
            continue;
        }
        return r;
    }
    fail("SoundCloud не принял запрос");
}

std::vector<Track> Net::scSearch(const std::string& q, int n) {
    std::string e = urlEncode(q);
    HttpResponse r = scRaw("/search/tracks?q=" + e + "&limit=20&offset=0");
    if (r.status == 404 || r.status == 400) r = scRaw("/search?q=" + e + "&limit=20");  // в ответе бывают плейлисты: parseScTracks их отсекает
    if (!r.ok()) fail("SoundCloud ответил ошибкой " + std::to_string(r.status));
    auto out = parseScTracks(parseJsonBody(r.body));
    if (static_cast<int>(out.size()) > n) out.resize(static_cast<std::size_t>(n));
    return out;
}

std::vector<Track> Net::scRelated(const std::string& permalink, int n) {
    Json t = scResolveJson(permalink);
    long long id = static_cast<long long>(t["id"].asNum());
    if (id <= 0) return {};
    HttpResponse r = scRaw("/tracks/" + std::to_string(id) + "/related?limit=" + std::to_string(std::max(n, 10)));
    if (!r.ok()) fail("SoundCloud ответил ошибкой " + std::to_string(r.status));
    auto out = parseScTracks(parseJsonBody(r.body));
    if (static_cast<int>(out.size()) > n) out.resize(static_cast<std::size_t>(n));
    return out;
}

Json Net::scResolveJson(const std::string& url) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = scResolved_.find(url);
        if (it != scResolved_.end() && nowMs() - it->second.first < 10LL * 60 * 1000) return it->second.second;
    }
    HttpResponse r = scRaw("/resolve?url=" + urlEncode(url));
    if (r.status == 404) fail("Эта ссылка SoundCloud недоступна");
    if (!r.ok()) fail("SoundCloud ответил ошибкой " + std::to_string(r.status));
    Json j = parseJsonBody(r.body);
    std::lock_guard<std::mutex> lk(mu_);
    if (scResolved_.size() > 200) scResolved_.clear();
    scResolved_[url] = {nowMs(), j};
    return j;
}

LinkResult Net::scFromLink(const std::string& url) {
    Json j = scResolveJson(url);
    LinkResult res;
    std::string kind = j["kind"].asStr();
    if (kind == "playlist" || kind == "system-playlist") {
        res.isPlaylist = true;
        res.name = j["title"].asStr("Плейлист SoundCloud");
        // в плейлисте часть треков приходит заглушками (только id): догружаем пачками
        Json full = Json::array();
        std::vector<std::string> need;
        for (const auto& t : j["tracks"].items()) {
            if (t["title"].isString()) continue;
            need.push_back(std::to_string(static_cast<long long>(t["id"].asNum())));
        }
        std::map<std::string, Json> byId;
        for (std::size_t i = 0; i < need.size(); i += 30) {
            std::vector<std::string> part(need.begin() + static_cast<long>(i),
                                          need.begin() + static_cast<long>(std::min(need.size(), i + 30)));
            HttpResponse r = scRaw("/tracks?ids=" + join(part, ","));
            if (!r.ok()) continue;
            Json got = parseJsonBody(r.body);  // не временный объект: иначе цикл читает уже освобождённую память
            for (const auto& t : got.items()) byId[std::to_string(static_cast<long long>(t["id"].asNum()))] = t;
        }
        for (const auto& t : j["tracks"].items()) {
            if (t["title"].isString()) { full.push(t); continue; }
            auto it = byId.find(std::to_string(static_cast<long long>(t["id"].asNum())));
            if (it != byId.end()) full.push(it->second);
        }
        res.tracks = parseScTracks(full);
        if (res.tracks.empty()) fail("В этом плейлисте нет треков, которые можно воспроизвести");
    } else {
        Json arr = Json::array();
        arr.push(j);
        res.tracks = parseScTracks(arr);
        if (res.tracks.empty()) fail("Этот трек SoundCloud нельзя воспроизвести (закрыт или только отрывок)");
        res.name = res.tracks[0].title;
    }
    return res;
}

std::string Net::scStreamUrl(const ScTranscoding& tc) {
    for (int i = 0; i < 2; ++i) {
        std::string url = tc.url + (tc.url.find('?') != std::string::npos ? "&" : "?") + "client_id=" + scClient(i > 0);
        if (!tc.auth.empty()) url += "&track_authorization=" + urlEncode(tc.auth);
        HttpOptions o;
        o.headers = {{"User-Agent", kUA}, {"Accept", "application/json"}};
        o.timeoutMs = 12000;
        HttpResponse r = http_.request(url, o);
        if ((r.status == 401 || r.status == 403) && i == 0) continue;  // ключ устарел: берём свежий и повторяем
        if (!r.ok()) fail("SoundCloud не отдал поток (код " + std::to_string(r.status) + ")");
        std::string out = parseJsonBody(r.body)["url"].asStr();
        if (out.empty()) fail("SoundCloud не отдал адрес потока");
        return out;
    }
    fail("SoundCloud не принял запрос");
}

// SoundCloud часто отвечает 404 на один вариант потока (например, progressive), хотя другой (HLS) работает.
// Поэтому перебираем все подходящие варианты; если не вышло ни один — один раз берём свежие данные трека
// (разрешение на проигрывание track_authorization со временем истекает).
AudioBlob Net::scFetch(const std::string& permalink, const ProgressFn& progress, const std::atomic<bool>* cancel) {
    std::string lastErr;
    for (int round = 0; round < 2; ++round) {
        if (round == 1) {
            std::lock_guard<std::mutex> lk(mu_);
            scResolved_.erase(permalink);
        }
        auto list = scTranscodings(scResolveJson(permalink));
        if (list.empty()) fail("SoundCloud не даёт слушать этот трек вне сайта");
        for (const auto& tc : list) {
            if (cancel && cancel->load()) fail("Загрузка отменена");
            try {
                std::string url = scStreamUrl(tc);
                return tc.progressive ? download(url, "", progress, cancel) : downloadHls(url, progress, cancel);
            } catch (const std::exception& e) {
                lastErr = e.what();
            }
        }
    }
    fail(lastErr.empty() ? std::string("SoundCloud не отдал этот трек") : lastErr);
}

// ---------- Internet Archive ----------

std::vector<Track> Net::iaSearch(const std::string& text) {
    std::string q;
    for (char c : text) q += (std::string("\"()\\:[]{}").find(c) != std::string::npos) ? ' ' : c;
    q = trim(q);
    if (q.empty()) return {};
    Json j = getJson("https://archive.org/advancedsearch.php?q=" + urlEncode(q + " AND mediatype:audio") +
                     "&fl[]=identifier&fl[]=title&fl[]=creator&rows=8&page=1&output=json&sort[]=downloads+desc");
    const auto& docs = j["response"]["docs"].items();
    std::vector<std::future<Json>> metas;
    for (const auto& d : docs)
        metas.push_back(std::async(std::launch::async, [this, id = d["identifier"].asStr()] {
            return getJson("https://archive.org/metadata/" + urlEncode(id));
        }));
    std::vector<Track> out;
    for (std::size_t i = 0; i < metas.size(); ++i) {
        try {
            auto part = parseIaItem(docs[i], metas[i].get());
            out.insert(out.end(), part.begin(), part.end());
        } catch (const std::exception&) {}
    }
    if (out.size() > 20) out.resize(20);
    return out;
}

// ---------- YouTube / Spotify ----------

// Поиск идёт по YouTube Music (только песни, с названием, исполнителем и длительностью). Если он не ответил,
// берём обычный поиск YouTube: так источник не остаётся пустым.
std::vector<Track> Net::ytSearch(const std::string& q, int n) {
    std::string text = q.substr(0, 200);
    if (text.empty()) return {};
    std::vector<Track> out;
    try {
        Json client = Json::object();
        client.set("clientName", Json("WEB_REMIX"));
        client.set("clientVersion", Json("1.20250224.01.00"));
        client.set("hl", Json("en"));
        client.set("gl", Json("US"));
        Json ctx = Json::object();
        ctx.set("client", client);
        Json body = Json::object();
        body.set("context", ctx);
        body.set("query", Json(text));
        body.set("params", Json("EgWKAQIIAWoKEAkQBRAKEAMQBA=="));  // фильтр «Песни»
        HttpOptions o;
        o.method = "POST";
        o.body = body.dump();
        o.headers = {{"Content-Type", "application/json"},
                     {"User-Agent", kUA},
                     {"Origin", "https://music.youtube.com"},
                     {"Referer", "https://music.youtube.com/"}};
        o.timeoutMs = 15000;
        HttpResponse r = http_.request("https://music.youtube.com/youtubei/v1/search?prettyPrint=false", o);
        if (r.ok()) out = parseYtmSearch(r.body);
    } catch (const std::exception&) {
        out.clear();
    }
    if (out.empty()) {
        std::string html = get("https://www.youtube.com/results?search_query=" + urlEncode(text) + "&sp=EgIQAQ%3D%3D",
                               {{"User-Agent", kUA}, {"Accept-Language", "en-US,en;q=0.9"}, {"Cookie", "CONSENT=YES+1; SOCS=CAI"}});
        out = parseYtSearch(html);
    }
    if (static_cast<int>(out.size()) > n) out.resize(static_cast<std::size_t>(n));
    return out;
}

std::string Net::ytmPost(const std::string& endpoint, Json body) {
    Json client = Json::object();
    client.set("clientName", Json("WEB_REMIX"));
    client.set("clientVersion", Json("1.20250224.01.00"));
    client.set("hl", Json("en"));  // разбор опирается на английские названия разделов
    client.set("gl", Json("US"));
    Json ctx = Json::object();
    ctx.set("client", client);
    body.set("context", ctx);
    HttpOptions o;
    o.method = "POST";
    o.body = body.dump();
    o.headers = {{"Content-Type", "application/json"},
                 {"User-Agent", kUA},
                 {"Origin", "https://music.youtube.com"},
                 {"Referer", "https://music.youtube.com/"}};
    o.timeoutMs = 15000;
    HttpResponse r = http_.request("https://music.youtube.com/youtubei/v1/" + endpoint + "?prettyPrint=false", o);
    if (!r.ok()) fail("YouTube Music не ответил (" + std::to_string(r.status) + ")");
    return r.body;
}

YtmArtistPage Net::ytArtist(const std::string& nameIn, bool exactOnly) {
    const std::string name = trim(nameIn.substr(0, 200));
    if (name.empty()) fail("Нет имени исполнителя");
    Json q = Json::object();
    q.set("query", Json(name));
    q.set("params", Json("EgWKAQIgAWoKEAkQBRAKEAMQBA=="));  // фильтр «Исполнители»
    YtmArtistRef ref = parseYtmArtistSearch(ytmPost("search", q), name, exactOnly);
    if (ref.id.empty()) fail("Исполнителя «" + name + "» нет на YouTube Music");
    Json b = Json::object();
    b.set("browseId", Json(ref.id));
    YtmArtistPage page = parseYtmArtist(ytmPost("browse", b));
    if (page.name.empty()) page.name = ref.name;
    if (!ref.cover.empty()) page.cover = ref.cover;  // в результатах поиска фото квадратное, на странице — широкая шапка
    if (page.cover.empty()) page.cover = ref.cover;
    // на странице исполнителя несколько песен и по десять альбомов: полные списки лежат по «Все …»
    try {
        if (!page.songsId.empty()) {
            Json s = Json::object();
            s.set("browseId", Json(page.songsId));
            auto all = parseYtmTracks(ytmPost("browse", s), page.name, page.cover);
            if (all.size() > page.songs.size()) page.songs = std::move(all);
        }
    } catch (const std::exception&) {}
    auto more = [&](const std::string& id, const std::string& params, std::vector<YtmAlbum>& dst) {
        if (id.empty()) return;
        try {
            Json s = Json::object();
            s.set("browseId", Json(id));
            if (!params.empty()) s.set("params", Json(params));
            auto all = parseYtmAlbums(ytmPost("browse", s));
            if (all.size() > dst.size()) dst = std::move(all);
        } catch (const std::exception&) {}
    };
    more(page.albumsId, page.albumsParams, page.albums);
    more(page.singlesId, page.singlesParams, page.singles);
    return page;
}

YtmAlbumPage Net::ytAlbum(const std::string& idIn) {
    std::string id;
    for (char c : idIn) if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') id += c;
    if (id.empty()) fail("Нет id альбома");
    Json b = Json::object();
    b.set("browseId", Json(id));
    return parseYtmAlbum(ytmPost("browse", b));
}

Track Net::ytFromLink(const std::string& id) {
    Track t = Track::make("youtube", id);
    try {
        Json j = getJson("https://www.youtube.com/oembed?format=json&url=" + urlEncode("https://youtu.be/" + id));
        t.title = j["title"].asStr("Трек YouTube");
        std::string a = j["author_name"].asStr();
        if (endsWith(a, " - Topic")) a.resize(a.size() - 8);
        t.artist = a;
        t.cover = j["thumbnail_url"].asStr();
    } catch (const std::exception&) {
        t.title = "Трек YouTube";
    }
    return t;
}

PlaylistInfo Net::ytPlaylist(const std::string& listId) {
    std::string id;
    for (char c : listId) if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') id += c;
    if (id.empty()) fail("Нет id плейлиста");
    return parseYtPlaylist(get("https://www.youtube.com/playlist?list=" + id,
                               {{"User-Agent", kUA}, {"Accept-Language", "en-US,en;q=0.9"}, {"Cookie", "CONSENT=YES+1; SOCS=CAI"}}));
}

PlaylistInfo Net::spPlaylist(const std::string& kind, const std::string& idIn) {
    std::string id;
    for (char c : idIn) if (std::isalnum(static_cast<unsigned char>(c))) id += c;
    if (id.empty()) fail("Нет id плейлиста");
    return parseSpEmbed(get("https://open.spotify.com/embed/" + std::string(kind == "album" ? "album" : "playlist") + "/" + id,
                            {{"User-Agent", kUA}, {"Accept-Language", "en-US,en;q=0.9"}}));
}

// Прямая ссылка на звук через внутренний API мобильных клиентов YouTube. Способ неофициальный и ломается:
// сервер перестаёт принимать старые версии клиентов, поэтому их надо время от времени обновлять. Если один
// клиент не сработал, пробуем следующий; если не сработал ни один, приложение ищет ту же песню в других
// источниках (findAlternative). Клиенты самого YouTube Music без входа в аккаунт звук не отдают.
std::string Net::ytDirectUrl(const std::string& videoId, std::size_t& length, std::string& userAgent) {
    struct Cl {
        const char* name;
        const char* ver;
        const char* id;
        const char* ua;
        bool ios;
    };
    static const Cl clients[] = {
        {"ANDROID", "20.10.38", "3", "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip", false},
        {"IOS", "20.10.4", "5", "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)", true},
    };
    std::string lastErr = "YouTube не отдал прямую ссылку на звук";
    for (const Cl& c : clients) {
        Json client = Json::object();
        client.set("clientName", Json(c.name));
        client.set("clientVersion", Json(c.ver));
        if (c.ios) {
            client.set("deviceMake", Json("Apple"));
            client.set("deviceModel", Json("iPhone16,2"));
            client.set("osName", Json("iPhone"));
            client.set("osVersion", Json("18.3.2.22D82"));
        } else {
            client.set("androidSdkVersion", Json(30));
            client.set("osName", Json("Android"));
            client.set("osVersion", Json("11"));
        }
        client.set("hl", Json("en"));
        client.set("gl", Json("US"));
        Json ctx = Json::object();
        ctx.set("client", client);
        Json body = Json::object();
        body.set("videoId", Json(videoId));
        body.set("context", ctx);
        body.set("contentCheckOk", Json(true));
        body.set("racyCheckOk", Json(true));

        HttpOptions o;
        o.method = "POST";
        o.body = body.dump();
        o.headers = {{"Content-Type", "application/json"},
                     {"User-Agent", c.ua},
                     {"X-YouTube-Client-Name", c.id},
                     {"X-YouTube-Client-Version", c.ver}};
        o.timeoutMs = 15000;
        HttpResponse r = http_.request("https://www.youtube.com/youtubei/v1/player?prettyPrint=false", o);
        if (!r.ok()) { lastErr = "YouTube ответил ошибкой " + std::to_string(r.status); continue; }
        Json j;
        try { j = parseJsonBody(r.body); } catch (const std::exception&) { continue; }
        std::string st = j["playabilityStatus"]["status"].asStr();
        if (!st.empty() && st != "OK") { lastErr = "YouTube не разрешил воспроизведение: " + j["playabilityStatus"]["reason"].asStr(st); continue; }

        const Json* best = nullptr;
        for (const auto& f : j["streamingData"]["adaptiveFormats"].items()) {
            if (!startsWith(f["mimeType"].asStr(), "audio/mp4") || !f["url"].isString()) continue;
            double br = f["bitrate"].asNum();
            if (!best) { best = &f; continue; }
            double bb = (*best)["bitrate"].asNum();
            bool fOk = br <= 160000, bOk = bb <= 160000;
            if ((fOk && !bOk) || (fOk == bOk && ((fOk && br > bb) || (!fOk && br < bb)))) best = &f;
        }
        if (!best) { lastErr = "YouTube не отдал прямую ссылку на звук"; continue; }
        std::string cl = (*best)["contentLength"].asStr();
        length = cl.empty() ? 0 : static_cast<std::size_t>(std::stoull(cl));
        userAgent = c.ua;
        return (*best)["url"].asStr();
    }
    fail(lastErr);
}

// ---------- звук ----------

AudioBlob Net::download(const std::string& url, const std::string& hint, const ProgressFn& progress,
                        const std::atomic<bool>* cancel, const Headers& headers) {
    HttpOptions o;
    o.headers = headers;
    if (o.headers.empty()) o.headers = {{"User-Agent", kUA}};
    o.timeoutMs = 30000;
    o.maxBytes = 400u << 20;
    o.cancel = cancel;
    if (progress) o.progress = [progress](std::size_t got, std::size_t total) { if (total) progress(static_cast<double>(got) / static_cast<double>(total)); };
    HttpResponse r = http_.request(url, o);
    std::string type = lowerAscii(r.contentType);
    if (!r.ok() || type.find("text/html") != std::string::npos || type.find("json") != std::string::npos)
        fail("Источник не отдал файл (код " + std::to_string(r.status) + ")");
    if (r.body.size() < 2000) fail("Получен пустой файл");
    AudioBlob b;
    b.mime = r.contentType;
    b.ext = extForMime(r.contentType, url.empty() ? hint : url);
    b.bytes = std::move(r.body);
    return b;
}

AudioBlob Net::downloadHls(const std::string& m3u8Url, const ProgressFn& progress, const std::atomic<bool>* cancel) {
    std::string text = get(m3u8Url, {{"User-Agent", kUA}});
    std::string base = m3u8Url;
    if (text.find("#EXT-X-STREAM-INF") != std::string::npos) {  // главный список: берём первый вариант
        auto variants = parseM3u8(text, base);
        if (variants.empty()) fail("Пустой список HLS");
        base = variants.front();
        text = get(base, {{"User-Agent", kUA}});
    }
    auto segs = parseM3u8(text, base);
    if (segs.empty()) fail("В потоке SoundCloud нет сегментов");
    AudioBlob out;
    out.ext = text.find("#EXT-X-MAP") != std::string::npos ? "m4a" : "mp3";
    if (base.find("opus") != std::string::npos || base.find("ogg") != std::string::npos) out.ext = "ogg";
    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (cancel && cancel->load()) fail("Загрузка отменена");
        HttpOptions o;
        o.headers = {{"User-Agent", kUA}};
        o.timeoutMs = 20000;
        o.cancel = cancel;
        HttpResponse r = http_.request(segs[i], o);
        if (!r.ok()) fail("Сегмент потока недоступен (код " + std::to_string(r.status) + ")");
        out.bytes += r.body;
        if (progress) progress(static_cast<double>(i + 1) / static_cast<double>(segs.size()));
    }
    out.mime = out.ext == "mp3" ? "audio/mpeg" : out.ext == "m4a" ? "audio/mp4" : "audio/ogg";
    return out;
}

std::string Net::lastYtError() {
    std::lock_guard<std::mutex> lk(mu_);
    return ytErr_;
}

AudioBlob Net::fetchAudio(const Track& t, const ProgressFn& progress, const std::atomic<bool>* cancel) {
    if (t.source == "audius") {
        for (int attempt = 0; attempt < 2; ++attempt) {
            try {
                return download(audiusHost() + "/v1/tracks/" + t.ref + "/stream?app_name=Mixtape", "", progress, cancel);
            } catch (const std::exception& e) {
                if (attempt == 1 || (cancel && cancel->load())) throw;
                try { audiusRehost(); } catch (const std::exception&) { throw std::runtime_error(e.what()); }
            }
        }
    }
    if (t.source == "archive") {
        if (!isAllowedDownloadUrl(t.url)) fail("Этот адрес нельзя скачать");
        return download(t.url, t.url, progress, cancel);
    }
    if (t.source == "soundcloud") {
        return scFetch(t.ref, progress, cancel);
    }
    if (t.source == "youtube") {
        if (ytExternal) {  // yt-dlp, если он есть: только так YouTube отдаёт песню целиком
            { std::lock_guard<std::mutex> lk(mu_); ytErr_.clear(); }
            try {
                return ytExternal(t.ref, progress, cancel);
            } catch (const std::exception& e) {
                { std::lock_guard<std::mutex> lk(mu_); ytErr_ = e.what(); }
                if (cancel && cancel->load()) throw;
            }
        }
        std::size_t len = 0;
        std::string ua;
        std::string url = ytDirectUrl(t.ref, len, ua);
        if (len == 0) return download(url, "x.m4a", progress, cancel, {{"User-Agent", ua}});
        AudioBlob b;
        b.mime = "audio/mp4";
        b.ext = "m4a";
        const std::size_t chunk = 9u << 20;
        for (std::size_t pos = 0; pos < len; pos += chunk) {
            if (cancel && cancel->load()) fail("Загрузка отменена");
            std::size_t end = std::min(len, pos + chunk) - 1;
            AudioBlob part = download(url + "&range=" + std::to_string(pos) + "-" + std::to_string(end), "x.m4a", {}, cancel,
                                      {{"User-Agent", ua}});
            b.bytes += part.bytes;
            if (progress) progress(static_cast<double>(end + 1) / static_cast<double>(len));
        }
        return b;
    }
    fail("Этот источник нельзя скачать");
}

std::optional<Track> Net::findAlternative(const Track& t, const Library& lib) {
    ListItem it;
    it.title = t.lyricTitle();
    it.artist = t.lyricArtist();
    Library probe;  // только настройки источников; YouTube здесь не нужен
    probe.settings = lib.settings;
    probe.setSrc("youtube", false);
    return matchTrack(it, probe);
}

// ---------- тексты ----------

namespace {
// 404 -> пустой Json; 503 один раз повторяем.
Json lrc(IHttp& http, const std::string& url) {
    for (int i = 0; i < 2; ++i) {
        HttpOptions o;
        o.headers = {{"User-Agent", kAppUA}};
        o.timeoutMs = 10000;
        HttpResponse r = http.request(url, o);
        if (r.status == 404) return Json();
        if (r.status == 503 && i == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1100)); continue; }
        if (!r.ok()) fail("Сервис текстов ответил ошибкой " + std::to_string(r.status));
        return parseJsonBody(r.body);
    }
    return Json();
}
}  // namespace

Lyrics Net::findLyrics(const std::string& title, const std::string& artist, double dur) {
    auto vs = lyricVariants(title.substr(0, 200), artist.substr(0, 200));
    if (vs.empty()) return {};
    bool answered = false;
    std::string lastErr;
    for (const auto& c : vs) {
        try {
            if (!c.artist.empty()) {
                Json j = lrc(http_, "https://lrclib.net/api/get?artist_name=" + urlEncode(c.artist) + "&track_name=" + urlEncode(c.title) +
                                        (dur > 0 ? "&duration=" + std::to_string(static_cast<long long>(dur + 0.5)) : ""));
                answered = true;
                if (j.isObject() && (!j["syncedLyrics"].isNull() || !j["plainLyrics"].isNull() || j["instrumental"].asBool()))
                    return shapeLyrics(j);
            }
            Json a1 = lrc(http_, "https://lrclib.net/api/search?" +
                                     (c.artist.empty() ? "q=" + urlEncode(c.title)
                                                       : "track_name=" + urlEncode(c.title) + "&artist_name=" + urlEncode(c.artist)));
            answered = true;
            if (const Json* best = pickBestLyrics(a1, c, dur)) return shapeLyrics(*best);
            if (!c.artist.empty()) {
                Json a2 = lrc(http_, "https://lrclib.net/api/search?q=" + urlEncode(c.artist + " " + c.title));
                if (const Json* best = pickBestLyrics(a2, c, dur)) return shapeLyrics(*best);
            }
        } catch (const std::exception& e) { lastErr = e.what(); }
    }
    for (const auto& c : vs) {
        if (c.artist.empty()) continue;
        try {
            HttpOptions o;
            o.headers = {{"User-Agent", kAppUA}};
            o.timeoutMs = 8000;
            HttpResponse r = http_.request("https://api.lyrics.ovh/v1/" + urlEncode(c.artist) + "/" + urlEncode(c.title), o);
            if (r.status == 404) { answered = true; continue; }
            if (!r.ok()) fail("lyrics.ovh ответил ошибкой " + std::to_string(r.status));
            answered = true;
            std::string txt = lyricsOvhText(parseJsonBody(r.body));
            if (!txt.empty()) {
                Lyrics l;
                l.name = c.title;
                l.artist = c.artist;
                l.src = "lyrics.ovh";
                l.plain = txt;
                return l;
            }
        } catch (const std::exception& e) { lastErr = e.what(); }
    }
    if (!answered && !lastErr.empty()) fail(lastErr);
    return {};
}

// ---------- поиск по всем источникам и перенос ----------

SearchOutcome Net::searchAll(const std::string& q, const Library& lib) {
    struct Job {
        const char* name;
        const char* source;
        std::future<std::vector<Track>> fut;
    };
    std::vector<Job> jobs;
    auto add = [&](const char* name, const char* source, std::function<std::vector<Track>()> fn) {
        if (lib.srcOn(source)) jobs.push_back({name, source, std::async(std::launch::async, std::move(fn))});
    };
    add("Audius", "audius", [&] { return audiusSearch(q, 20); });
    add("SoundCloud", "soundcloud", [&] { return scSearch(q, 15); });
    add("Internet Archive", "archive", [&] { return iaSearch(q); });
    add("YouTube Music", "youtube", [&] { return ytSearch(q, 10); });

    SearchOutcome out;
    std::vector<SearchHit> hits;
    for (auto& j : jobs) {
        SourceStatus st;
        st.name = j.name;
        st.source = j.source;
        try {
            auto tracks = j.fut.get();
            st.count = static_cast<int>(tracks.size());
            int k = 0;
            for (auto& t : tracks) {
                SearchHit h;
                h.score = relScore(t, q);
                h.rank = k++;
                h.track = std::move(t);
                hits.push_back(std::move(h));
            }
        } catch (const std::exception& e) {
            st.error = true;
            st.message = e.what();
        }
        out.status.push_back(st);
    }
    std::vector<Track> local;
    std::unordered_map<std::string, bool> inLib;
    for (const auto& kv : lib.tracks) {
        inLib[kv.first] = true;
        if (!kv.second.clip && relScore(kv.second, q) >= 9) local.push_back(kv.second);
    }
    out.tracks = mergeResults(std::move(hits), local, inLib);
    return out;
}

std::optional<Track> Net::matchTrack(const ListItem& item, const Library& lib) {
    std::string a0 = item.artist.substr(0, item.artist.find(','));
    std::string q = trim(a0 + " " + item.title);
    std::vector<Track> cand;
    bool any = false;
    auto collect = [&](const char* source, const std::function<std::vector<Track>()>& fn) {
        if (!lib.srcOn(source)) return;
        any = true;
        try { auto r = fn(); cand.insert(cand.end(), r.begin(), r.end()); } catch (const std::exception&) {}
    };
    collect("audius", [&] { return audiusSearch(q, 8); });
    collect("soundcloud", [&] { return scSearch(q, 8); });
    if (!any) collect("archive", [&] { return iaSearch(q); });

    const Track* best = nullptr;
    double bs = 0;
    for (const auto& x : cand) {
        double s = matchScore(x, item.title, item.artist) + (x.source == "audius" ? 0.2 : 0);
        if (s > bs) { bs = s; best = &x; }
    }
    if (best && bs >= 3.9) {
        Track t = *best;
        t.ptitle = item.title;
        t.partist = a0;
        return t;
    }
    if (lib.srcOn("youtube")) {
        if (!item.vid.empty()) {
            Track t = Track::make("youtube", item.vid);
            t.title = item.title;
            t.artist = item.artist;
            t.cover = item.cover;
            return t;
        }
        try {
            auto r = ytSearch(q, 6);
            const Track* yb = nullptr;
            double ys = 0;
            for (const auto& x : r) {
                double s = matchScore(x, item.title, item.artist);
                if (s > ys) { ys = s; yb = &x; }
            }
            if (!yb && !r.empty()) yb = &r[0];
            if (yb) {
                Track t = *yb;
                t.title = item.title;
                if (!item.artist.empty()) t.artist = item.artist;
                return t;
            }
        } catch (const std::exception&) {}
    }
    return std::nullopt;
}

}  // namespace mx
