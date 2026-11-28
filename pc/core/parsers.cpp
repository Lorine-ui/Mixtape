#include "parsers.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <stdexcept>

#include "text.hpp"

namespace mx {

// ===================================================================
// общие помощники
// ===================================================================
static std::string txt(const Json& x) {
    if (x.isString()) return x.str();
    if (!x.isObject()) return {};
    if (x["simpleText"].isString()) return x["simpleText"].str();
    std::string o;
    for (const auto& r : x["runs"].items()) o += r["text"].asStr();
    return o;
}

static std::string collapseWs(const std::string& s) {
    std::string o;
    bool sp = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { sp = true; continue; }
        if (sp && !o.empty()) o += ' ';
        sp = false;
        o += c;
    }
    return o;
}

// Вырезает из s первый {...}, начиная с позиции from, учитывая строки JSON.
static std::string balancedBraces(const std::string& s, std::size_t from) {
    std::size_t i = s.find('{', from);
    if (i == std::string::npos) return {};
    int depth = 0;
    bool inStr = false;
    for (std::size_t k = i; k < s.size(); ++k) {
        char c = s[k];
        if (inStr) {
            if (c == '\\') ++k;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return s.substr(i, k - i + 1);
    }
    return {};
}

// ===================================================================
// YouTube
// ===================================================================
static Json ytData(const std::string& html) {
    std::size_t pos = 0;
    while ((pos = html.find("ytInitialData", pos)) != std::string::npos) {
        std::size_t eq = html.find('=', pos);
        if (eq != std::string::npos && eq - pos < 24) {
            std::string raw = balancedBraces(html, eq);
            Json j;
            if (!raw.empty() && Json::tryParse(raw, j)) return j;
        }
        pos += 13;
    }
    throw std::runtime_error("YouTube изменил страницу, не удалось прочитать");
}

static std::string stripTopic(std::string a) {
    static const std::string suf = " - Topic";
    if (a.size() >= suf.size()) {
        std::string tail = a.substr(a.size() - suf.size());
        for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string want = " - topic";
        if (tail == want) a.erase(a.size() - suf.size());
    }
    return a;
}

static ListItem ytVideo(const Json& v) {
    ListItem o;
    o.vid = v["videoId"].asStr();
    o.title = txt(v["title"]);
    const Json* by = &v["ownerText"];
    if (by->isNull()) by = &v["shortBylineText"];
    if (by->isNull()) by = &v["longBylineText"];
    o.artist = stripTopic(txt(*by));
    const auto& th = v["thumbnail"]["thumbnails"].items();
    if (!th.empty()) o.cover = th.back()["url"].asStr();
    return o;
}

std::vector<Track> parseYtSearch(const std::string& html) {
    Json d = ytData(html);
    std::vector<const Json*> found;
    walk(d, "videoRenderer", found);
    std::vector<Track> out;
    for (const Json* v : found) {
        if (!v->isObject() || !v->has("videoId")) continue;
        ListItem it = ytVideo(*v);
        Track t = Track::make("youtube", it.vid);
        t.title = it.title; t.artist = it.artist; t.cover = it.cover;
        out.push_back(std::move(t));
    }
    return out;
}

// YouTube Music: «часы:минуты:секунды» -> секунды (0, если это не время)
static double clockToSec(const std::string& s) {
    double total = 0;
    int cur = 0;
    bool any = false;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') { cur = cur * 10 + (ch - '0'); any = true; }
        else if (ch == ':') { total = total * 60 + cur; cur = 0; }
        else return 0;
    }
    return any ? total * 60 + cur : 0;
}

std::vector<Track> parseYtmSearch(const std::string& body) {
    Json d;
    if (!Json::tryParse(body, d) || !d.isObject() || !d.has("contents"))
        throw std::runtime_error("YouTube Music изменил ответ, не удалось прочитать");
    std::vector<const Json*> found;
    walk(d, "musicResponsiveListItemRenderer", found);
    std::vector<Track> out;
    for (const Json* v : found) {
        if (!v->isObject()) continue;
        std::string vid = (*v)["playlistItemData"]["videoId"].asStr();
        if (vid.empty())
            vid = (*v)["overlay"]["musicItemThumbnailOverlayRenderer"]["content"]["musicPlayButtonRenderer"]["playNavigationEndpoint"]["watchEndpoint"]["videoId"].asStr();
        const auto& cols = (*v)["flexColumns"].items();
        if (vid.empty() || cols.empty()) continue;
        std::string title = txt(cols[0]["musicResponsiveListItemFlexColumnRenderer"]["text"]);
        if (title.empty()) continue;
        std::string artist;
        double dur = 0;
        if (cols.size() > 1) {
            // вторая строка: «Исполнитель • Альбом • 3:51»; у нескольких исполнителей между именами « & » или «, »
            const auto& runs = cols[1]["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"].items();
            bool inArtist = true;
            std::string last;
            for (std::size_t i = 0; i < runs.size(); ++i) {
                std::string s = runs[i]["text"].asStr();
                if (s == " \xE2\x80\xA2 ") {  // « • »
                    inArtist = false;
                    continue;
                }
                if (i == 0 && (s == "Song" || s == "Video") && i + 1 < runs.size() && runs[i + 1]["text"].asStr() == " \xE2\x80\xA2 ") continue;
                if (inArtist) artist += s;
                else last = s;
            }
            dur = clockToSec(last);
        }
        Track t = Track::make("youtube", vid);
        t.title = title;
        t.artist = stripTopic(artist);
        t.dur = dur;
        const auto& th = (*v)["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"].items();
        if (!th.empty()) t.cover = th.back()["url"].asStr();
        out.push_back(std::move(t));
    }
    return out;
}

// ---------- YouTube Music: страница исполнителя и альбомы ----------

std::string firstArtist(const std::string& names) {
    const std::string low = lower(names);
    std::size_t cut = names.size();
    for (const char* d : {", ", " & ", " feat.", " feat ", " ft.", " ft ", " featuring ", " \xC3\x97 ", " x ", ";", " and ", " \xD0\xB8 "}) {
        std::size_t p = low.find(d);
        if (p != std::string::npos && p > 0 && p < cut) cut = p;
    }
    return trim(names.substr(0, cut));
}

static std::string lastThumb(const Json& thumbs) {
    const auto& th = thumbs["thumbnails"].items();
    return th.empty() ? std::string() : th.back()["url"].asStr();
}

// Строки-песни в ответе browse: у альбома и списка «все песни» исполнитель/обложка могут быть пустыми — берём общие.
static std::vector<Track> ytmRows(const Json& root, const std::string& defArtist, const std::string& defCover) {
    std::vector<const Json*> found;
    walk(root, "musicResponsiveListItemRenderer", found);
    std::vector<Track> out;
    std::set<std::string> seen;
    for (const Json* v : found) {
        if (!v->isObject()) continue;
        std::string vid = (*v)["playlistItemData"]["videoId"].asStr();
        if (vid.empty())
            vid = (*v)["overlay"]["musicItemThumbnailOverlayRenderer"]["content"]["musicPlayButtonRenderer"]["playNavigationEndpoint"]["watchEndpoint"]["videoId"].asStr();
        const auto& cols = (*v)["flexColumns"].items();
        if (vid.empty() || cols.empty() || !seen.insert(vid).second) continue;
        std::string title = txt(cols[0]["musicResponsiveListItemFlexColumnRenderer"]["text"]);
        if (title.empty()) continue;
        std::string artist;
        if (cols.size() > 1) artist = trim(txt(cols[1]["musicResponsiveListItemFlexColumnRenderer"]["text"]));
        double dur = 0;
        for (const auto& fc : (*v)["fixedColumns"].items()) dur = clockToSec(trim(txt(fc["musicResponsiveListItemFixedColumnRenderer"]["text"])));
        Track t = Track::make("youtube", vid);
        t.title = title;
        t.artist = stripTopic(artist.empty() ? defArtist : artist);
        t.dur = dur;
        t.cover = lastThumb((*v)["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]);
        if (t.cover.empty()) t.cover = defCover;
        out.push_back(std::move(t));
    }
    return out;
}

// Адрес картинки YouTube Music вида «…=w120-h120-p-l90-rj» -> тот же, но нужного размера (мелкую картинку на плитке не растянуть).
static std::string thumbSized(const std::string& u, int size) {
    const std::size_t a = u.rfind("=w");
    if (a == std::string::npos) return u;
    std::size_t i = a + 2, j = i;
    while (j < u.size() && std::isdigit(static_cast<unsigned char>(u[j]))) ++j;
    if (j == i || u.compare(j, 2, "-h") != 0) return u;
    std::size_t k = j + 2, e = k;
    while (e < u.size() && std::isdigit(static_cast<unsigned char>(u[e]))) ++e;
    if (e == k) return u;
    return u.substr(0, a) + "=w" + std::to_string(size) + "-h" + std::to_string(size) + u.substr(e);
}

static YtmAlbum ytmAlbumCard(const Json& it) {
    YtmAlbum a;
    a.id = it["navigationEndpoint"]["browseEndpoint"]["browseId"].asStr();
    a.title = txt(it["title"]);
    a.sub = txt(it["subtitle"]);
    a.cover = lastThumb(it["thumbnailRenderer"]["musicThumbnailRenderer"]["thumbnail"]);
    return a;
}

YtmArtistRef parseYtmArtistSearch(const std::string& body, const std::string& want, bool exactOnly) {
    Json d;
    if (!Json::tryParse(body, d) || !d.isObject() || !d.has("contents"))
        throw std::runtime_error("YouTube Music изменил ответ, не удалось прочитать");
    std::vector<const Json*> found;
    walk(d, "musicResponsiveListItemRenderer", found);
    YtmArtistRef best;
    const std::string w = lower(trim(want));
    for (const Json* v : found) {
        std::string id = (*v)["navigationEndpoint"]["browseEndpoint"]["browseId"].asStr();
        const auto& cols = (*v)["flexColumns"].items();
        if (id.compare(0, 2, "UC") != 0 || cols.empty()) continue;
        YtmArtistRef r;
        r.id = id;
        r.name = txt(cols[0]["musicResponsiveListItemFlexColumnRenderer"]["text"]);
        r.cover = thumbSized(lastThumb((*v)["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]), 400);
        if (best.id.empty() && !exactOnly) best = r;
        if (lower(trim(r.name)) == w) return r;
    }
    return best;
}

YtmArtistPage parseYtmArtist(const std::string& body) {
    Json d;
    if (!Json::tryParse(body, d) || !d.isObject() || !d.has("contents"))
        throw std::runtime_error("YouTube Music изменил ответ, не удалось прочитать");
    YtmArtistPage p;
    if (const Json* h = walkFirst(d, "musicImmersiveHeaderRenderer")) {
        p.name = txt((*h)["title"]);
        p.cover = lastThumb((*h)["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]);
    }
    if (const Json* sh = walkFirst(d, "musicShelfRenderer")) {
        const auto& be = (*sh)["bottomEndpoint"]["browseEndpoint"];
        p.songsId = be["browseId"].asStr();
        p.songs = ytmRows(*sh, p.name, p.cover);
    }
    std::vector<const Json*> car;
    walk(d, "musicCarouselShelfRenderer", car);
    for (const Json* c : car) {
        const Json& hd = (*c)["header"]["musicCarouselShelfBasicHeaderRenderer"];
        const std::string title = txt(hd["title"]);
        const bool alb = title == "Albums", sin = title == "Singles & EPs";
        if (!alb && !sin) continue;
        auto& dst = alb ? p.albums : p.singles;
        for (const auto& it : (*c)["contents"].items()) {
            const Json& two = it["musicTwoRowItemRenderer"];
            if (!two.isObject()) continue;
            YtmAlbum a = ytmAlbumCard(two);
            if (a.id.compare(0, 4, "MPRE") == 0 && !a.title.empty()) dst.push_back(std::move(a));
        }
        const auto& more = hd["moreContentButton"]["buttonRenderer"]["navigationEndpoint"]["browseEndpoint"];
        (alb ? p.albumsId : p.singlesId) = more["browseId"].asStr();
        (alb ? p.albumsParams : p.singlesParams) = more["params"].asStr();
    }
    if (p.name.empty() && p.songs.empty() && p.albums.empty() && p.singles.empty())
        throw std::runtime_error("Не удалось прочитать страницу исполнителя");
    return p;
}

std::vector<YtmAlbum> parseYtmAlbums(const std::string& body) {
    Json d;
    if (!Json::tryParse(body, d) || !d.isObject()) throw std::runtime_error("YouTube Music изменил ответ, не удалось прочитать");
    std::vector<const Json*> found;
    walk(d, "musicTwoRowItemRenderer", found);
    std::vector<YtmAlbum> out;
    for (const Json* v : found) {
        YtmAlbum a = ytmAlbumCard(*v);
        if (a.id.compare(0, 4, "MPRE") == 0 && !a.title.empty()) out.push_back(std::move(a));
    }
    return out;
}

std::vector<Track> parseYtmTracks(const std::string& body, const std::string& artist, const std::string& cover) {
    Json d;
    if (!Json::tryParse(body, d) || !d.isObject()) throw std::runtime_error("YouTube Music изменил ответ, не удалось прочитать");
    return ytmRows(d, artist, cover);
}

YtmAlbumPage parseYtmAlbum(const std::string& body) {
    Json d;
    if (!Json::tryParse(body, d) || !d.isObject() || !d.has("contents"))
        throw std::runtime_error("YouTube Music изменил ответ, не удалось прочитать");
    YtmAlbumPage a;
    if (const Json* h = walkFirst(d, "musicResponsiveHeaderRenderer")) {
        a.title = txt((*h)["title"]);
        a.artist = stripTopic(txt((*h)["straplineTextOne"]));
        a.cover = lastThumb((*h)["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]);
        const std::string sub = txt((*h)["subtitle"]);
        for (std::size_t i = 0; i + 4 <= sub.size(); ++i)
            if (std::isdigit(static_cast<unsigned char>(sub[i])) && std::isdigit(static_cast<unsigned char>(sub[i + 1])) &&
                std::isdigit(static_cast<unsigned char>(sub[i + 2])) && std::isdigit(static_cast<unsigned char>(sub[i + 3]))) {
                a.year = sub.substr(i, 4);
                break;
            }
    }
    a.tracks = ytmRows(d, a.artist, a.cover);
    if (a.title.empty() && a.tracks.empty()) throw std::runtime_error("Не удалось прочитать альбом");
    return a;
}

PlaylistInfo parseYtPlaylist(const std::string& html) {
    Json d = ytData(html);
    PlaylistInfo pl;
    std::vector<const Json*> found;
    walk(d, "playlistVideoRenderer", found);
    for (const Json* v : found)
        if (v->isObject() && v->has("videoId")) pl.items.push_back(ytVideo(*v));
    const Json* head = walkFirst(d, "playlistHeaderRenderer");
    if (head) pl.name = txt((*head)["title"]);
    if (pl.name.empty()) pl.name = d["metadata"]["playlistMetadataRenderer"]["title"].asStr();
    if (pl.name.empty()) pl.name = "Плейлист YouTube";
    return pl;
}

PlaylistInfo parseSpEmbed(const std::string& html) {
    const std::string open = "<script id=\"__NEXT_DATA__\" type=\"application/json\">";
    std::size_t a = html.find(open);
    std::size_t b = a == std::string::npos ? a : html.find("</script>", a);
    if (a == std::string::npos || b == std::string::npos) throw std::runtime_error("Spotify изменил страницу, не удалось прочитать");
    a += open.size();
    Json d = Json::parse(html.substr(a, b - a));
    PlaylistInfo pl;
    const Json* list = walkFirst(d, "trackList");
    const Json* ent = walkFirst(d, "entity");
    if (list)
        for (const auto& t : list->items()) {
            ListItem it;
            it.title = t["title"].asStr();
            it.artist = replaceAll(t["subtitle"].asStr(), "\xC2\xA0", " ");
            if (!it.title.empty()) pl.items.push_back(std::move(it));
        }
    if (ent) { pl.name = (*ent)["name"].asStr(); if (pl.name.empty()) pl.name = (*ent)["title"].asStr(); }
    if (pl.name.empty()) pl.name = "Плейлист Spotify";
    return pl;
}

// ===================================================================
// Audius
// ===================================================================
std::vector<Track> parseAudiusTracks(const Json& root) {
    std::vector<Track> out;
    for (const auto& t : root["data"].items()) {
        if (t["is_streamable"].isBool() && !t["is_streamable"].asBool()) continue;
        std::string id = t["id"].asStr();
        if (id.empty() && t["id"].isNumber()) id = std::to_string(static_cast<long long>(t["id"].asNum()));
        if (id.empty()) continue;
        Track o = Track::make("audius", id);
        o.title = t["title"].asStr();
        o.artist = t["user"]["name"].asStr();
        const Json& a = t["artwork"];
        o.cover = a["480x480"].asStr();
        if (o.cover.empty()) o.cover = a["150x150"].asStr();
        if (o.cover.empty()) o.cover = a["1000x1000"].asStr();
        o.dur = t["duration"].asNum();
        out.push_back(std::move(o));
    }
    return out;
}

// ===================================================================
// SoundCloud
// ===================================================================
std::vector<std::string> scScriptUrls(const std::string& html) {
    static const std::string prefix = "https://a-v2.sndcdn.com/assets/";
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = html.find("<script", pos)) != std::string::npos) {
        std::size_t end = html.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = html.substr(pos, end - pos);
        std::size_t s = tag.find("src=\"");
        if (s != std::string::npos) {
            s += 5;
            std::size_t q = tag.find('"', s);
            if (q != std::string::npos) {
                std::string u = tag.substr(s, q - s);
                if (startsWith(u, prefix) && endsWith(u, ".js")) out.push_back(u);
            }
        }
        pos = end;
    }
    return out;
}

std::string scClientId(const std::string& js) {
    static const std::string key = "client_id";
    auto isAl = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
    auto take32 = [&](std::size_t i) -> std::string {
        if (i + 32 > js.size()) return {};
        for (std::size_t k = 0; k < 32; ++k) if (!isAl(js[i + k])) return {};
        return js.substr(i, 32);
    };
    std::size_t pos = 0;
    while ((pos = js.find(key, pos)) != std::string::npos) {
        std::size_t i = pos + key.size();
        pos = i;
        // client_id:"xxxxxxxx...32"  или client_id = "..."
        std::size_t j = i;
        while (j < js.size() && (js[j] == ' ' || js[j] == '\t')) ++j;
        if (j < js.size() && (js[j] == ':' || js[j] == '=')) {
            ++j;
            while (j < js.size() && (js[j] == ' ' || js[j] == '\t')) ++j;
            if (j < js.size() && js[j] == '"') {
                std::string id = take32(j + 1);
                if (!id.empty() && j + 33 < js.size() && js[j + 33] == '"') return id;
            }
        }
        // ?client_id=xxxxxxxx...32  или &client_id=...
        std::size_t start = pos - key.size();
        if (start > 0 && (js[start - 1] == '?' || js[start - 1] == '&') && i < js.size() && js[i] == '=') {
            std::string id = take32(i + 1);
            if (!id.empty()) return id;
        }
    }
    return {};
}

std::vector<Track> parseScTracks(const Json& j) {
    const Json* arr = &j;
    if (j.isObject() && j["collection"].isArray()) arr = &j["collection"];
    std::vector<Track> out;
    if (!arr->isArray()) return out;
    for (const auto& t : arr->items()) {
        if (!t.isObject()) continue;
        std::string link = t["permalink_url"].asStr(), title = t["title"].asStr();
        if (link.empty() || title.empty()) continue;
        if (t["kind"].isString() && t["kind"].str() != "track") continue;
        std::string pol = t["policy"].asStr(), acc = t["access"].asStr();
        if (pol == "BLOCK" || pol == "SNIP" || acc == "blocked" || acc == "preview") continue;
        std::string art = t["artwork_url"].asStr();
        if (art.empty()) art = t["user"]["avatar_url"].asStr();
        Track o = Track::make("soundcloud", link);
        o.title = title;
        o.artist = t["user"]["username"].asStr();
        if (o.artist.empty()) o.artist = t["user"]["full_name"].asStr();
        o.cover = replaceAll(art, "-large.", "-t300x300.");
        double d = t["full_duration"].asNum();
        if (d <= 0) d = t["duration"].asNum();
        o.dur = d / 1000.0;
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<ScTranscoding> scTranscodings(const Json& track) {
    struct Scored { int score; ScTranscoding tc; };
    std::vector<Scored> all;
    const std::string auth = track["track_authorization"].asStr();
    for (const auto& t : track["media"]["transcodings"].items()) {
        std::string url = t["url"].asStr();
        if (url.empty()) continue;
        std::string proto = t["format"]["protocol"].asStr(), mime = lower(t["format"]["mime_type"].asStr());
        if (proto != "progressive" && proto != "hls") continue;                // ctr/cbc-encrypted-hls — это DRM
        if (icontains(mime, "opus") || icontains(mime, "ogg")) continue;       // opus плеер не декодирует
        bool prog = proto == "progressive";
        bool mp3 = icontains(mime, "mpeg") || icontains(t["preset"].asStr(), "mp3");
        int score = (mp3 ? 2 : 0) + (prog ? 1 : 0);
        ScTranscoding tc;
        tc.url = url;
        tc.auth = auth;
        tc.mime = mime;
        tc.progressive = prog;
        all.push_back({score, tc});
    }
    std::stable_sort(all.begin(), all.end(), [](const Scored& x, const Scored& y) { return x.score > y.score; });
    std::vector<ScTranscoding> out;
    for (auto& s2 : all) out.push_back(s2.tc);
    return out;
}

// ===================================================================
// Internet Archive
// ===================================================================
double lenSec(const Json& v) {
    if (v.isNumber()) return v.asNum();
    if (!v.isString() || v.str().empty()) return 0;
    const std::string& s = v.str();
    if (s.find(':') != std::string::npos) {
        double acc = 0;
        for (const auto& p : split(s, ':')) acc = acc * 60 + std::atof(p.c_str());
        return acc;
    }
    return std::atof(s.c_str());
}

static std::string one(const Json& x) {
    if (x.isArray()) return x.size() ? x.at(0).asStr() : std::string();
    return x.asStr();
}

std::vector<Track> parseIaItem(const Json& doc, const Json& meta) {
    std::string id = doc["identifier"].asStr();
    const auto& files = meta["files"].items();
    auto by = [&](const std::string& needle) {
        std::vector<const Json*> r;
        for (const auto& f : files) if (icontains(f["format"].asStr(), needle)) r.push_back(&f);
        return r;
    };
    std::vector<const Json*> fs = by("mp3");
    if (fs.empty()) fs = by("ogg vorbis");
    std::vector<const Json*> good;
    for (const Json* f : fs) {
        const Json& len = (*f)["length"];
        if (len.isNull() || (len.isString() && len.str().empty()) || lenSec(len) > 20) good.push_back(f);
    }
    std::set<std::string> seen;
    std::vector<Track> out;
    for (const Json* f : good) {
        std::string name = (*f)["name"].asStr();
        if (name.empty() || !seen.insert(name).second) continue;
        std::string base = name;
        std::size_t sl = base.rfind('/');
        if (sl != std::string::npos) base = base.substr(sl + 1);
        std::size_t dot = base.rfind('.');
        if (dot != std::string::npos && dot > 0) base.erase(dot);
        Track t = Track::make("archive", id + "/" + name);
        t.title = one((*f)["title"]);
        if (t.title.empty() && good.size() == 1) t.title = one(doc["title"]);
        if (t.title.empty()) t.title = base;
        t.artist = one((*f)["creator"]);
        if (t.artist.empty()) t.artist = one((*f)["artist"]);
        if (t.artist.empty()) t.artist = one(doc["creator"]);
        t.cover = "https://archive.org/services/img/" + id;
        t.dur = lenSec((*f)["length"]);
        std::string path;
        for (const auto& seg : split(name, '/')) { if (!path.empty()) path += '/'; path += urlEncode(seg); }
        t.url = "https://archive.org/download/" + id + "/" + path;
        out.push_back(std::move(t));
        if (out.size() >= 3) break;
    }
    return out;
}

// ===================================================================
// ссылки
// ===================================================================
static bool idCh(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

std::string ytVideoId(const std::string& text) {
    for (const char* m : {"youtu.be/", "?v=", "&v=", "youtube.com/shorts/"}) {
        std::size_t p = text.find(m);
        if (p == std::string::npos) continue;
        p += std::string(m).size();
        if (p + 11 > text.size()) continue;
        bool ok = true;
        for (std::size_t k = 0; k < 11; ++k) if (!idCh(text[p + k])) { ok = false; break; }
        if (ok) return text.substr(p, 11);
    }
    return {};
}

std::string ytListId(const std::string& text) {
    for (const char* m : {"?list=", "&list="}) {
        std::size_t p = text.find(m);
        if (p == std::string::npos) continue;
        p += std::string(m).size();
        std::size_t e = p;
        while (e < text.size() && idCh(text[e])) ++e;
        if (e > p) return text.substr(p, e - p);
    }
    return {};
}

bool isScLink(const std::string& raw) {
    std::string s = trim(raw);
    if (s.find(' ') != std::string::npos) return false;
    std::string rest;
    if (startsWith(s, "https://")) rest = s.substr(8);
    else if (startsWith(s, "http://")) rest = s.substr(7);
    else return false;
    for (const char* p : {"www.", "on.", "m."}) if (startsWith(rest, p)) { rest = rest.substr(std::string(p).size()); break; }
    return startsWith(rest, "soundcloud.com/") && rest.size() > 15;
}

SpotifyLink parseSpotifyLink(const std::string& text) {
    SpotifyLink r;
    auto idAt = [&](std::size_t p) {
        std::size_t e = p;
        while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_')) ++e;
        return text.substr(p, e - p);
    };
    std::size_t p = text.find("spotify.com/");
    if (p != std::string::npos) {
        p += 12;
        if (startsWith(text.substr(p), "intl-")) {
            std::size_t sl = text.find('/', p);
            if (sl != std::string::npos) p = sl + 1;
        }
        for (const char* k : {"playlist", "album"}) {
            std::string pre = std::string(k) + "/";
            if (text.compare(p, pre.size(), pre) == 0) { r.kind = k; r.id = idAt(p + pre.size()); return r; }
        }
    }
    for (const char* k : {"playlist", "album"}) {
        std::string pre = std::string("spotify:") + k + ":";
        p = text.find(pre);
        if (p != std::string::npos) { r.kind = k; r.id = idAt(p + pre.size()); return r; }
    }
    return r;
}

// ===================================================================
// тексты песен
// ===================================================================
std::string cleanTitle(const std::string& s) {
    static const char* junk[] = {"official", "video", "lyric", "audio", "visualizer", "visualiser", "hd", "4k", "remaster", "clip", "live", "mv"};
    std::string o;
    for (std::size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == '(' || c == '[') {
            std::size_t j = s.find(c == '(' ? ')' : ']', i + 1);
            if (j != std::string::npos) {
                std::string in = s.substr(i + 1, j - i - 1);
                bool bad = false;
                for (const char* k : junk) if (icontains(in, k)) { bad = true; break; }
                o += bad ? std::string(" ") : s.substr(i, j - i + 1);
                i = j + 1;
                continue;
            }
        }
        o += c;
        ++i;
    }
    static const std::regex words(R"(\b(official|lyrics?|music video|audio)\b)", std::regex::icase);
    o = std::regex_replace(o, words, " ");
    std::size_t cut = o.find('|');
    std::size_t b = o.find("\xE2\x80\xA2");
    if (b != std::string::npos && (cut == std::string::npos || b < cut)) cut = b;
    if (cut != std::string::npos) o.erase(cut);
    return trim(collapseWs(o));
}

// Срезает «feat. …» / «ft. …» (в начале слова; «Left Behind» остаётся целым).
static std::string cutFeat(const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && s[i - 1] != ' ' && s[i - 1] != '(' && s[i - 1] != '[') continue;
        std::size_t len = 0;
        if (s.size() >= i + 4 && icontains(s.substr(i, 4), "feat") && lower(s.substr(i, 4)) == "feat") len = 4;
        else if (s.size() >= i + 2 && lower(s.substr(i, 2)) == "ft") len = 2;
        if (!len) continue;
        std::size_t j = i + len;
        if (j < s.size() && s[j] == '.') ++j;
        if (j < s.size() && (s[j] == ' ' || s[j] == '\t')) {
            std::size_t k = i;
            while (k > 0 && (s[k - 1] == ' ' || s[k - 1] == '(' || s[k - 1] == '[')) --k;
            return trim(s.substr(0, k));
        }
    }
    return s;
}

Query cleanQuery(const std::string& title, const std::string& artist) {
    std::string t = cleanTitle(title);
    std::string a = trim(artist);
    // « - Topic» и «VEVO» в конце, без учёта регистра
    {
        std::string la = lower(a);
        if (endsWith(la, " - topic")) a = trim(a.substr(0, a.size() - 8));
        la = lower(a);
        if (endsWith(la, "vevo")) a = trim(a.substr(0, a.size() - 4));
    }
    if (a == "Файл на устройстве") a.clear();
    auto m = splitDash(t);
    if (m.size() > 1) {
        std::string n0 = nrm(m[0]), na = nrm(a);
        if (a.empty() || contains(n0, na) || contains(na, n0)) {
            if (a.empty()) a = m[0];
            m.erase(m.begin());
            t = join(m, " - ");
        }
    }
    t = cutFeat(t);
    std::size_t c = a.find_first_of(",&");
    if (c != std::string::npos) a.erase(c);
    return {trim(t), trim(a)};
}

std::vector<Query> lyricVariants(const std::string& title, const std::string& artist) {
    Query base = cleanQuery(title, artist);
    std::vector<Query> out;
    std::set<std::string> seen;
    auto add = [&](std::string t, std::string a) {
        t = trim(t); a = trim(a);
        std::string k = nrm(t) + "|" + nrm(a);
        if (!t.empty() && seen.insert(k).second) out.push_back({t, a});
    };
    add(base.title, base.artist);

    std::string nv = replaceAll(replaceAll(base.title, "\xE2\x80\x93", "-"), "\xE2\x80\x94", "-");
    static const std::regex ver(R"(\s*-\s*(\d{4}\s*)?(remaster(ed)?|radio edit|single version|album version|live|mono|stereo|version|edit|mix)\b.*$)", std::regex::icase);
    static const std::regex br(R"(\s*[(\[][^)\]]*(remaster|version|edit|mix|live)[^)\]]*[)\]])", std::regex::icase);
    nv = std::regex_replace(nv, ver, "");
    nv = std::regex_replace(nv, br, "");
    add(trim(nv), base.artist);

    auto m = splitDash(title);
    if (m.size() > 1) {
        std::string first = m[0];
        m.erase(m.begin());
        add(cleanTitle(join(m, " - ")), first);
    }
    add(base.title, "");
    if (out.size() > 5) out.resize(5);
    return out;
}

std::vector<LyricLine> parseLrc(const std::string& lrc) {
    std::vector<LyricLine> out;
    std::size_t pos = 0;
    while (pos <= lrc.size()) {
        std::size_t nl = lrc.find('\n', pos);
        std::string line = lrc.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = nl == std::string::npos ? lrc.size() + 1 : nl + 1;

        std::vector<double> stamps;
        std::string text;
        for (std::size_t i = 0; i < line.size();) {
            if (line[i] != '[') { text += line[i++]; continue; }
            std::size_t close = line.find(']', i);
            if (close == std::string::npos) { text += line.substr(i); break; }
            std::string in = line.substr(i + 1, close - i - 1);
            // мм:сс[.xx|:xx]
            std::size_t c1 = in.find(':');
            bool ok = c1 != std::string::npos && c1 >= 1 && c1 <= 3;
            double v = 0;
            if (ok) {
                for (std::size_t k = 0; k < c1; ++k) if (!std::isdigit(static_cast<unsigned char>(in[k]))) ok = false;
                std::string rest = in.substr(c1 + 1);
                if (rest.size() < 2 || !std::isdigit(static_cast<unsigned char>(rest[0])) || !std::isdigit(static_cast<unsigned char>(rest[1]))) ok = false;
                if (ok) {
                    v = std::atoi(in.substr(0, c1).c_str()) * 60 + std::atoi(rest.substr(0, 2).c_str());
                    if (rest.size() > 2) {
                        if ((rest[2] != '.' && rest[2] != ':') || rest.size() < 4 || rest.size() > 6) ok = false;
                        else {
                            std::string fr = rest.substr(3);
                            for (char ch : fr) if (!std::isdigit(static_cast<unsigned char>(ch))) ok = false;
                            if (ok) { double f = std::atof(fr.c_str()); for (std::size_t k = 0; k < fr.size(); ++k) f /= 10; v += f; }
                        }
                    }
                }
            }
            if (ok) stamps.push_back(v);
            i = close + 1;  // любые [..] выкидываются из текста
        }
        if (stamps.empty()) continue;
        text = trim(text);
        for (double t : stamps) out.push_back({t, text});
    }
    std::stable_sort(out.begin(), out.end(), [](const LyricLine& a, const LyricLine& b) { return a.t < b.t; });
    return out;
}

const Json* pickBestLyrics(const Json& arr, const Query& c, double dur) {
    const Json* best = nullptr;
    int bs = 0;
    for (const auto& r : arr.items()) {
        if (!r.isObject()) continue;
        bool has = !r["syncedLyrics"].asStr().empty() || !r["plainLyrics"].asStr().empty() || r["instrumental"].asBool();
        if (!has) continue;
        int sc = 0;
        if (!r["syncedLyrics"].asStr().empty()) sc += 3;
        std::string tn = nrm(r["trackName"].asStr()), tq = nrm(c.title);
        if (!tn.empty() && tn == tq) sc += 5;
        else if (!tn.empty() && !tq.empty() && (contains(tn, tq) || contains(tq, tn))) sc += 3;
        else continue;
        std::string an = nrm(r["artistName"].asStr()), aq = nrm(c.artist);
        if (!aq.empty() && !an.empty() && (contains(an, aq) || contains(aq, an))) sc += 3;
        double rd = r["duration"].asNum();
        if (dur > 0 && rd > 0) {
            double d = std::abs(rd - dur);
            if (d <= 3) sc += 3; else if (d <= 10) sc += 1;
        }
        if (sc > bs) { bs = sc; best = &r; }
    }
    return best;
}

Lyrics shapeLyrics(const Json& j) {
    Lyrics l;
    l.name = j["trackName"].asStr();
    l.artist = j["artistName"].asStr();
    l.src = "LRCLIB";
    l.instrumental = j["instrumental"].asBool();
    std::string synced = j["syncedLyrics"].asStr();
    if (!synced.empty()) l.lines = parseLrc(synced);
    l.plain = j["plainLyrics"].asStr();
    return l;
}

std::string lyricsOvhText(const Json& root) {
    std::string t = root["lyrics"].asStr();
    static const std::string head = "paroles de la chanson";
    if (startsWith(lower(t), head)) {
        std::size_t nl = t.find('\n');
        t = nl == std::string::npos ? std::string() : t.substr(nl + 1);
    }
    t = trim(t);
    return t.size() > 40 ? t : std::string();
}

// ===================================================================
// безопасность скачивания
// ===================================================================
bool isAllowedDownloadUrl(const std::string& raw) {
    if (raw.size() > 2048) return false;
    std::string u = raw;
    if (lower(u.substr(0, 8)) != "https://") return false;
    std::string rest = u.substr(8);
    std::size_t endAuth = rest.find_first_of("/?#");
    std::string auth = rest.substr(0, endAuth);
    std::string pathq = endAuth == std::string::npos ? std::string() : rest.substr(endAuth);
    if (auth.find('@') != std::string::npos || auth.empty() || auth[0] == '[') return false;
    std::string host = auth;
    std::size_t colon = auth.find(':');
    if (colon != std::string::npos) {
        host = auth.substr(0, colon);
        std::string port = auth.substr(colon + 1);
        if (!port.empty() && port != "443") return false;
    }
    host = lower(host);
    if (host.empty() || host.find('.') == std::string::npos) return false;  // «localhost», «intranet», 0x7f000001
    bool numeric = true;
    for (char c : host) if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.')) numeric = false;
    if (numeric) return false;
    for (const auto& label : split(host, '.')) if (startsWith(label, "0x")) return false;
    if (endsWith(host, ".local") || endsWith(host, ".internal") || endsWith(host, ".localhost")) return false;

    std::size_t q = pathq.find_first_of("?#");
    std::string path = pathq.substr(0, q);
    // /v1/tracks/<id>/stream[/]
    if (startsWith(path, "/v1/tracks/")) {
        std::string r = path.substr(11);
        std::size_t sl = r.find('/');
        if (sl != std::string::npos && sl > 0) {
            std::string id = r.substr(0, sl), tail = r.substr(sl);
            bool alnum = std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
            if (alnum && (tail == "/stream" || tail == "/stream/")) return true;
        }
    }
    if ((host == "archive.org" || endsWith(host, ".archive.org")) && startsWith(path, "/download/")) return true;
    return false;
}

// ===================================================================
// импорт списков
// ===================================================================
std::vector<std::string> csvRow(const std::string& line) {
    std::vector<std::string> out;
    std::string c;
    bool q = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (q) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { c += '"'; ++i; }
                else q = false;
            } else c += ch;
        } else if (ch == '"') q = true;
        else if (ch == ',') { out.push_back(c); c.clear(); }
        else c += ch;
    }
    out.push_back(c);
    return out;
}

ListItem splitArtistTitle(const std::string& line) {
    ListItem it;
    auto m = splitDash(line);
    if (m.size() > 1) {
        it.artist = m[0];
        m.erase(m.begin());
        it.title = join(m, " - ");
    } else it.title = trim(line);
    return it;
}

std::vector<ListItem> parseList(const std::string& text) {
    std::vector<std::string> lines;
    for (auto& l : split(replaceAll(text, "\r", ""), '\n')) {
        std::string t = trim(l);
        if (!t.empty()) lines.push_back(t);
    }
    std::vector<ListItem> out;
    if (lines.empty()) return out;
    if (icontains(lines[0], "track name")) {
        auto h = csvRow(lines[0]);
        int ti = -1, ai = -1;
        for (std::size_t i = 0; i < h.size(); ++i) {
            std::string x = lower(h[i]);
            if (ti < 0 && startsWith(x, "track name")) ti = static_cast<int>(i);
            if (ai < 0 && startsWith(x, "artist name")) ai = static_cast<int>(i);
        }
        if (ti < 0) return out;
        for (std::size_t k = 1; k < lines.size(); ++k) {
            auto r = csvRow(lines[k]);
            ListItem it;
            if (static_cast<std::size_t>(ti) < r.size()) it.title = trim(r[static_cast<std::size_t>(ti)]);
            if (ai >= 0 && static_cast<std::size_t>(ai) < r.size()) it.artist = trim(replaceAll(r[static_cast<std::size_t>(ai)], ";", ", "));
            if (!it.title.empty()) out.push_back(std::move(it));
        }
        return out;
    }
    for (const auto& l : lines) out.push_back(splitArtistTitle(l));
    return out;
}

}  // namespace mx
