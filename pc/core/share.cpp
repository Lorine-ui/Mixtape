#include "share.hpp"

#include <algorithm>
#include <cctype>

#include "cloudsync.hpp"  // base64Encode / base64Decode
#include "json.hpp"
#include "text.hpp"

namespace mx {

namespace {

const char kBase[] = "https://github.com/Lorine-ui/Mixtape#mx-";

std::string clip(const std::string& s, std::size_t n) { return clipChars(s, n); }

// Из чужого JSON берём только то, что играется из сети, и подрезаем длину.
bool cleanTrack(Track& t) {
    if (t.id.empty() || t.clip || t.source == "local" || !isValidSource(t.source) || t.title.empty()) return false;
    t.title = clip(t.title, 200);
    t.artist = clip(t.artist, 200);
    if (t.cover.size() > 500) t.cover.clear();
    if (t.url.size() > 500) t.url.clear();
    t.off = false;
    t.offSize = 0;
    t.cuts.clear();
    t.added = 0;
    return true;
}

}  // namespace

std::string base64UrlEncode(const std::string& bytes) {
    std::string s = base64Encode(bytes);
    for (char& c : s) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
    while (!s.empty() && s.back() == '=') s.pop_back();
    return s;
}

bool base64UrlDecode(const std::string& text, std::string& out) {
    std::string s = text;
    for (char& c : s) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
    while (s.size() % 4) s += '=';
    return base64Decode(s, out);
}

std::string makeFriendLink(const ShareFriend& f) {
    Json j = Json::object();
    j.set("k", Json("f"));
    j.set("i", Json(f.id));
    j.set("n", Json(clip(f.name, 24)));
    j.set("s", Json(clip(f.status, 60)));
    j.set("c", Json(f.color));
    j.set("f", Json(f.frame));
    return kBase + base64UrlEncode(j.dump());
}

std::string makeSongLink(const ShareSong& s) {
    Json j = Json::object();
    j.set("k", Json("s"));
    j.set("i", Json(s.fromId));
    j.set("n", Json(clip(s.fromName, 24)));
    if (!s.note.empty()) j.set("m", Json(clip(s.note, 120)));
    j.set("t", toJson(s.track));
    return kBase + base64UrlEncode(j.dump());
}

ShareParsed parseShareText(const std::string& text) {
    ShareParsed out;
    const std::string tag = "#mx-";
    std::size_t at = text.find(tag);
    if (at == std::string::npos) return out;
    std::size_t b = at + tag.size(), e = b;
    while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '-' || text[e] == '_')) ++e;
    std::string raw, body = text.substr(b, e - b);
    Json j;
    if (body.empty() || !base64UrlDecode(body, raw) || !Json::tryParse(raw, j) || !j.isObject()) return out;
    const std::string kind = j["k"].asStr();
    if (kind == "f") {
        out.person.id = j["i"].asStr();
        out.person.name = clip(trim(j["n"].asStr()), 24);
        out.person.status = clip(j["s"].asStr(), 60);
        out.person.color = std::clamp(static_cast<int>(j["c"].asNum(0)), 0, 7);
        out.person.frame = std::max(0, static_cast<int>(j["f"].asNum(1)));
        if (out.person.id.empty() || out.person.name.empty()) return out;
        out.kind = ShareKind::Friend;
    } else if (kind == "s") {
        out.song.fromId = j["i"].asStr();
        out.song.fromName = clip(trim(j["n"].asStr()), 24);
        out.song.note = clip(j["m"].asStr(), 120);
        out.song.track = trackFromJson(j["t"]);
        if (!cleanTrack(out.song.track)) return out;
        out.kind = ShareKind::Song;
    }
    return out;
}

}  // namespace mx
