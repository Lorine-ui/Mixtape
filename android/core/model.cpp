#include "model.hpp"

#include <chrono>
#include <random>

namespace mx {

const char* sourceLabel(const std::string& s) {
    if (s == "local") return "Файл";
    if (s == "audius") return "Audius";
    if (s == "archive") return "Archive";
    if (s == "soundcloud") return "SoundCloud";
    if (s == "youtube") return "YouTube Music";
    if (s == "jamendo") return "Jamendo";
    return "";
}

bool isValidSource(const std::string& s) {
    return s == "local" || s == "audius" || s == "archive" || s == "soundcloud" || s == "youtube";
}

std::string newId() {
    static std::mt19937_64 rng{std::random_device{}() ^ static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())};
    static const char* a = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s;
    for (int i = 0; i < 12; ++i) s += a[rng() % 36];
    return s;
}

Track Track::make(const std::string& source, const std::string& ref) {
    Track t;
    t.source = source;
    t.ref = ref;
    t.id = source + ":" + ref;
    return t;
}

static void putStr(Json& o, const char* k, const std::string& v) { if (!v.empty()) o.set(k, v); }

Json toJson(const Track& t) {
    Json o = Json::object();
    o.set("id", t.id);
    o.set("source", t.source);
    o.set("ref", t.ref);
    o.set("title", t.title);
    putStr(o, "artist", t.artist);
    putStr(o, "cover", t.cover);
    putStr(o, "url", t.url);
    if (t.dur > 0) o.set("dur", t.dur);
    if (t.set) o.set("set", true);
    if (t.clip) {
        o.set("clip", true);
        o.set("mode", t.mode.empty() ? "keep" : t.mode);
        o.set("start", t.start);
        o.set("end", t.end);
        putStr(o, "parent", t.parent);
    }
    putStr(o, "ptitle", t.ptitle);
    putStr(o, "partist", t.partist);
    o.set("added", t.added);
    if (t.off) { o.set("off", true); o.set("offSize", t.offSize); }
    if (!t.cuts.empty()) {
        Json a = Json::array();
        for (const auto& c : t.cuts) { Json p = Json::array(); p.push(c.s); p.push(c.e); a.push(std::move(p)); }
        o.set("cuts", std::move(a));
    }
    return o;
}

Track trackFromJson(const Json& j) {
    Track t;
    t.id = j["id"].asStr();
    t.source = j["source"].asStr();
    t.ref = j["ref"].asStr();
    if (t.id.empty() && !t.source.empty()) t.id = t.source + ":" + t.ref;
    t.title = j["title"].asStr();
    t.artist = j["artist"].asStr();
    t.cover = j["cover"].asStr();
    t.url = j["url"].asStr();
    t.dur = j["dur"].asNum();
    t.set = j["set"].asBool();
    t.clip = j["clip"].asBool();
    t.mode = j["mode"].asStr();
    t.start = j["start"].asNum();
    t.end = j["end"].asNum();
    t.parent = j["parent"].asStr();
    t.ptitle = j["ptitle"].asStr();
    t.partist = j["partist"].asStr();
    t.added = static_cast<long long>(j["added"].asNum());
    t.off = j["off"].asBool();
    t.offSize = static_cast<long long>(j["offSize"].asNum());
    const Json& cs = j["cuts"];
    for (const auto& c : cs.items())
        if (c.isArray() && c.size() >= 2) t.cuts.push_back({c.at(0).asNum(), c.at(1).asNum()});
    return t;
}

Json toJson(const Playlist& p) {
    Json o = Json::object();
    o.set("id", p.id);
    o.set("name", p.name);
    Json a = Json::array();
    for (const auto& i : p.ids) a.push(i);
    o.set("ids", std::move(a));
    return o;
}

Playlist playlistFromJson(const Json& j) {
    Playlist p;
    p.id = j["id"].asStr();
    p.name = j["name"].asStr();
    for (const auto& i : j["ids"].items()) if (i.isString()) p.ids.push_back(i.str());
    return p;
}

Json toJson(const Lyrics& l) {
    Json o = Json::object();
    putStr(o, "name", l.name);
    putStr(o, "artist", l.artist);
    putStr(o, "src", l.src);
    if (l.instrumental) o.set("instrumental", true);
    if (!l.lines.empty()) {
        Json a = Json::array();
        for (const auto& x : l.lines) { Json e = Json::object(); e.set("t", x.t); e.set("text", x.text); a.push(std::move(e)); }
        o.set("lines", std::move(a));
    }
    putStr(o, "plain", l.plain);
    return o;
}

Lyrics lyricsFromJson(const Json& j) {
    Lyrics l;
    l.name = j["name"].asStr();
    l.artist = j["artist"].asStr();
    l.src = j["src"].asStr();
    l.instrumental = j["instrumental"].asBool();
    for (const auto& x : j["lines"].items()) l.lines.push_back({x["t"].asNum(), x["text"].asStr()});
    l.plain = j["plain"].asStr();
    return l;
}

}  // namespace mx
