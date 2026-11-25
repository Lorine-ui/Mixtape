#include "library.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace mx {

// ---------- файлы ----------
bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data, std::string* err) {
    try {
        fs::path p = fs::u8path(path);
        if (p.has_parent_path()) fs::create_directories(p.parent_path());
        fs::path tmp = p;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) throw std::runtime_error("не удалось создать временный файл");
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            f.flush();
            if (!f) throw std::runtime_error("не удалось записать файл (нет места?)");
        }
        if (fs::exists(p)) {
            fs::path bak = p;
            bak += ".bak";
            std::error_code ec;
            fs::copy_file(p, bak, fs::copy_options::overwrite_existing, ec);  // копия не критична
        }
        fs::rename(tmp, p);  // на Windows перезаписывает существующий файл
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string lyricKey(const Track& t) { return t.lyricTitle() + "|" + t.lyricArtist(); }

// ---------- треки ----------
Track* Library::find(const std::string& id) {
    auto it = tracks.find(id);
    return it == tracks.end() ? nullptr : &it->second;
}
const Track* Library::find(const std::string& id) const {
    auto it = tracks.find(id);
    return it == tracks.end() ? nullptr : &it->second;
}

Track& Library::add(const Track& t) {
    auto it = tracks.find(t.id);
    if (it != tracks.end()) return it->second;
    Track c = t;
    c.added = nowMs();
    return tracks.emplace(c.id, std::move(c)).first->second;
}

std::vector<std::string> Library::remove(const std::string& id) {
    std::vector<std::string> gone;
    if (!has(id)) return gone;
    gone.push_back(id);
    for (const auto& [k, t] : tracks) if (t.parent == id) gone.push_back(k);
    for (const auto& k : gone) {
        tracks.erase(k);
        for (auto& p : playlists) p.ids.erase(std::remove(p.ids.begin(), p.ids.end(), k), p.ids.end());
    }
    return gone;
}

std::vector<const Track*> Library::list(const std::function<bool(const Track&)>& pred) const {
    std::vector<const Track*> out;
    for (const auto& [k, t] : tracks) if (!pred || pred(t)) out.push_back(&t);
    std::sort(out.begin(), out.end(), [](const Track* a, const Track* b) { return a->added != b->added ? a->added > b->added : a->id < b->id; });
    return out;
}

// ---------- вырезы ----------
static double round2(double v) { return static_cast<double>(static_cast<long long>(v * 100 + (v < 0 ? -0.5 : 0.5))) / 100.0; }

void Library::addCut(const Track& cur, Cut c) {
    Track* t = cur.clip ? find(cur.id) : &add(cur);
    if (!t) return;
    t->cuts.push_back({round2(c.s), round2(c.e)});
    t->cuts = mergeCuts(t->cuts);
}

std::vector<CutRef> Library::cutList(const Track& cur) const {
    std::vector<CutRef> out;
    const Track* base = find(cur.baseId());
    const Track* own = cur.clip ? find(cur.id) : nullptr;
    if (base) for (std::size_t i = 0; i < base->cuts.size(); ++i) out.push_back({base->cuts[i], false, static_cast<int>(i)});
    if (own) for (std::size_t i = 0; i < own->cuts.size(); ++i) out.push_back({own->cuts[i], true, static_cast<int>(i)});
    return out;
}

bool Library::removeCut(const Track& cur, const CutRef& ref) {
    Track* t = ref.own ? find(cur.id) : find(cur.baseId());
    if (!t || ref.index < 0 || ref.index >= static_cast<int>(t->cuts.size())) return false;
    t->cuts.erase(t->cuts.begin() + ref.index);
    return true;
}

Segment Library::segmentFor(const Track& t) const {
    const Track* base = find(t.baseId());
    const Track* own = t.clip ? find(t.id) : nullptr;
    static const std::vector<Cut> none;
    return buildSegment(t, base ? base->cuts : none, own ? own->cuts : none);
}

int Library::cutCount(const Track& t) const {
    const Track* x = find(t.id);
    return x ? static_cast<int>(x->cuts.size()) : 0;
}

// ---------- плейлисты ----------
Playlist* Library::playlist(const std::string& id) {
    for (auto& p : playlists) if (p.id == id) return &p;
    return nullptr;
}

Playlist& Library::createPlaylist(const std::string& name) {
    Playlist p;
    p.id = newId();
    p.name = name;
    playlists.push_back(std::move(p));
    return playlists.back();
}

bool Library::removePlaylist(const std::string& id) {
    auto it = std::remove_if(playlists.begin(), playlists.end(), [&](const Playlist& p) { return p.id == id; });
    bool any = it != playlists.end();
    playlists.erase(it, playlists.end());
    return any;
}

// ---------- настройки ----------
bool Library::getBool(const std::string& k, bool d) const { return settings[k].isBool() ? settings[k].asBool() : d; }
double Library::getNum(const std::string& k, double d) const { return settings[k].isNumber() ? settings[k].asNum() : d; }
std::string Library::getStr(const std::string& k, const std::string& d) const { return settings[k].isString() ? settings[k].str() : d; }

bool Library::srcOn(const std::string& source) const {
    const Json& s = settings["src"][source];
    return !(s.isBool() && !s.asBool());
}

void Library::setSrc(const std::string& source, bool on) {
    Json cur = settings["src"].isObject() ? settings["src"] : Json::object();
    cur.set(source, on);
    settings.set("src", std::move(cur));
}

// ---------- хранение ----------
Json Library::toJson() const {
    Json o = Json::object();
    o.set("app", "mixtape");
    o.set("v", 2);
    Json tr = Json::object();
    for (const auto& [k, t] : tracks) tr.add(k, mx::toJson(t));
    o.set("tracks", std::move(tr));
    Json pl = Json::array();
    for (const auto& p : playlists) pl.push(mx::toJson(p));
    o.set("playlists", std::move(pl));
    o.set("settings", settings);
    Json lo = Json::object();
    for (const auto& [k, v] : lyOff) lo.add(k, v);
    o.set("lyOff", std::move(lo));
    Json ly = Json::object();
    for (const auto& [k, v] : lyrics) ly.add(k, mx::toJson(v));
    o.set("lyrics", std::move(ly));
    return o;
}

bool Library::fromJson(const Json& j, std::string* err) {
    if (!j.isObject() || j["app"].asStr() != "mixtape") {
        if (err) *err = "Это не файл данных Mixtape";
        return false;
    }
    tracks.clear(); playlists.clear(); lyOff.clear(); lyrics.clear();
    settings = j["settings"].isObject() ? j["settings"] : Json::object();
    const Json& tr = j["tracks"];
    for (std::size_t i = 0; i < tr.size(); ++i) {
        Track t = trackFromJson(tr.items()[i]);
        if (t.id.empty()) t.id = tr.keys()[i];
        if (!t.id.empty()) tracks[t.id] = std::move(t);
    }
    for (const auto& p : j["playlists"].items()) playlists.push_back(playlistFromJson(p));
    const Json& lo = j["lyOff"];
    for (std::size_t i = 0; i < lo.size(); ++i) lyOff[lo.keys()[i]] = lo.items()[i].asNum();
    const Json& ly = j["lyrics"];
    for (std::size_t i = 0; i < ly.size(); ++i) lyrics[ly.keys()[i]] = lyricsFromJson(ly.items()[i]);
    return true;
}

bool Library::load(const std::string& path, std::string* err) {
    std::string text;
    if (!readFile(path, text)) { if (err) *err = "файл не найден"; return false; }
    Json j;
    std::string perr;
    if (!Json::tryParse(text, j, &perr)) {
        // основной файл повреждён: пробуем последнюю копию
        std::string bak;
        if (readFile(path + ".bak", bak) && Json::tryParse(bak, j, &perr)) return fromJson(j, err);
        if (err) *err = perr;
        return false;
    }
    return fromJson(j, err);
}

bool Library::save(const std::string& path, std::string* err) const { return writeFileAtomic(path, toJson().dump(), err); }

Library::MergeStats Library::mergeBackup(const Json& j) {
    MergeStats st;
    if (!j.isObject() || j["app"].asStr() != "mixtape") return st;
    st.ok = true;
    const Json& tr = j["tracks"];
    for (std::size_t i = 0; i < tr.size(); ++i) {
        Track t = trackFromJson(tr.items()[i]);
        if (t.id.empty()) t.id = tr.keys()[i];
        if (t.id.empty() || tracks.count(t.id) || t.source == "local") continue;
        t.off = false;
        t.offSize = 0;
        if (!isValidSource(t.source)) continue;
        tracks[t.id] = std::move(t);
        ++st.tracks;
    }
    for (const auto& p : j["playlists"].items()) {
        Playlist pl = playlistFromJson(p);
        bool dup = false;
        for (const auto& x : playlists) if (x.id == pl.id) dup = true;
        if (dup || pl.id.empty()) continue;
        // ссылки на пропущенные (локальные) треки убираем
        pl.ids.erase(std::remove_if(pl.ids.begin(), pl.ids.end(), [&](const std::string& id) { return !has(id); }), pl.ids.end());
        playlists.push_back(std::move(pl));
        ++st.playlists;
    }
    const Json& ly = j["lyrics"];
    for (std::size_t i = 0; i < ly.size(); ++i) { lyrics[ly.keys()[i]] = lyricsFromJson(ly.items()[i]); ++st.lyrics; }
    const Json& lo = j["lyOff"];
    for (std::size_t i = 0; i < lo.size(); ++i) lyOff[lo.keys()[i]] = lo.items()[i].asNum();
    const Json& ss = j["settings"];
    for (std::size_t i = 0; i < ss.size(); ++i) if (!settings.has(ss.keys()[i])) settings.set(ss.keys()[i], ss.items()[i]);
    return st;
}

}  // namespace mx
