#include "cloudsync.hpp"

#include <algorithm>
#include <cstring>
#include <set>

#include "text.hpp"

namespace mx {

bool isDeviceSetting(const std::string& key) {
    static const std::set<std::string> kDevice = {"fullscreen", "uiScale", "textSize", "vol"};
    return kDevice.count(key) != 0;
}

Json cloudPayload(const Library& lib) {
    Library c = lib;
    for (auto it = c.tracks.begin(); it != c.tracks.end();) {
        if (it->second.source == "local") { it = c.tracks.erase(it); continue; }  // файл есть только на этом устройстве
        it->second.off = false;
        it->second.offSize = 0;
        ++it;
    }
    for (auto& p : c.playlists)
        p.ids.erase(std::remove_if(p.ids.begin(), p.ids.end(), [&](const std::string& id) { return !c.has(id); }), p.ids.end());
    Json s = Json::object();
    for (std::size_t i = 0; i < lib.settings.keys().size(); ++i)
        if (!isDeviceSetting(lib.settings.keys()[i])) s.set(lib.settings.keys()[i], lib.settings.items()[i]);
    c.settings = s;
    return c.toJson();
}

bool applyCloud(Library& lib, const Json& cloud, bool merge) {
    Library c;
    if (!c.fromJson(cloud)) return false;

    if (merge) {
        // объединение: недостающие песни, плейлисты (и песни в них), тексты; свои настройки не трогаем
        for (auto& kv : c.tracks) {
            if (lib.has(kv.first) || kv.second.source == "local") continue;
            Track t = kv.second;
            t.off = false;
            t.offSize = 0;
            lib.tracks[t.id] = t;
        }
        for (auto& cp : c.playlists) {
            Playlist* lp = lib.playlist(cp.id);
            if (!lp) {
                Playlist p = cp;
                p.ids.erase(std::remove_if(p.ids.begin(), p.ids.end(), [&](const std::string& id) { return !lib.has(id); }), p.ids.end());
                lib.playlists.push_back(p);
                continue;
            }
            for (auto& id : cp.ids)
                if (lib.has(id) && std::find(lp->ids.begin(), lp->ids.end(), id) == lp->ids.end()) lp->ids.push_back(id);
        }
        for (auto& kv : c.lyrics) if (!lib.lyrics.count(kv.first)) lib.lyrics[kv.first] = kv.second;
        for (auto& kv : c.lyOff) if (!lib.lyOff.count(kv.first)) lib.lyOff[kv.first] = kv.second;
        return true;
    }

    // облако главнее
    std::map<std::string, Track> tracks;
    for (auto& kv : c.tracks) {
        if (kv.second.source == "local") continue;
        Track t = kv.second;
        if (const Track* old = lib.find(t.id)) { t.off = old->off; t.offSize = old->offSize; }  // офлайн-копия на этом устройстве
        else { t.off = false; t.offSize = 0; }
        tracks[t.id] = t;
    }
    for (auto& kv : lib.tracks)
        if (kv.second.source == "local" || (kv.second.clip && kv.second.parent.rfind("local:", 0) == 0)) tracks[kv.first] = kv.second;
    // свои файлы этого устройства остаются и в своих плейлистах
    std::vector<Playlist> playlists = c.playlists;
    for (auto& p : playlists) {
        const Playlist* old = nullptr;
        for (auto& lp : lib.playlists) if (lp.id == p.id) old = &lp;
        if (!old) continue;
        for (auto& id : old->ids) {
            auto it = tracks.find(id);
            if (it != tracks.end() && it->second.source == "local" && std::find(p.ids.begin(), p.ids.end(), id) == p.ids.end())
                p.ids.push_back(id);
        }
    }
    Json settings = c.settings.isObject() ? c.settings : Json::object();
    for (std::size_t i = 0; i < lib.settings.keys().size(); ++i)
        if (isDeviceSetting(lib.settings.keys()[i])) settings.set(lib.settings.keys()[i], lib.settings.items()[i]);
    lib.tracks = std::move(tracks);
    lib.playlists = std::move(playlists);
    lib.lyrics = c.lyrics;
    lib.lyOff = c.lyOff;
    lib.settings = settings;
    return true;
}

// ---------- профиль ----------

Json Profile::toJson() const {
    Json j = Json::object();
    j.set("name", Json(name));
    j.set("status", Json(status));
    j.set("color", Json(static_cast<double>(color)));
    j.set("frame", Json(static_cast<double>(frame)));
    j.set("avatar", Json(avatar));
    j.set("updated", Json(static_cast<double>(updated)));
    return j;
}

Profile Profile::fromJson(const Json& j) {
    Profile p;
    if (!j.isObject()) return p;
    p.name = clipChars(j["name"].asStr(), 24);
    p.status = clipChars(j["status"].asStr(), 60);
    p.color = std::clamp(static_cast<int>(j["color"].asNum()), 0, 7);
    p.frame = std::clamp(static_cast<int>(j["frame"].asNum(1)), 0, Profile::kFrameCount - 1);
    p.avatar = j["avatar"].asStr();
    std::string px;
    const std::size_t need = static_cast<std::size_t>(kAvatarSide) * kAvatarSide * 3u;
    if (!p.avatar.empty() && (!base64Decode(p.avatar, px) || px.size() != need)) p.avatar.clear();  // испорченная — без неё
    p.updated = static_cast<long long>(j["updated"].asNum());
    return p;
}

std::string displayName(const Profile& p, const std::string& email) {
    std::string n = trim(p.name);
    if (!n.empty()) return n;
    std::string e = email.substr(0, email.find('@'));
    return e.empty() ? std::string("Слушатель") : e;
}

std::string clipChars(const std::string& s, std::size_t n) {
    std::size_t i = 0, k = 0;
    while (i < s.size() && k < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        ++k;
    }
    return s.substr(0, std::min(i, s.size()));
}

namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const std::string& b) {
    std::string out;
    out.reserve((b.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < b.size(); i += 3) {
        unsigned v = static_cast<unsigned char>(b[i]) << 16;
        if (i + 1 < b.size()) v |= static_cast<unsigned char>(b[i + 1]) << 8;
        if (i + 2 < b.size()) v |= static_cast<unsigned char>(b[i + 2]);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < b.size() ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < b.size() ? kB64[v & 63] : '=';
    }
    return out;
}

bool base64Decode(const std::string& t, std::string& out) {
    out.clear();
    unsigned v = 0;
    int bits = 0;
    for (char ch : t) {
        if (ch == '=' || ch == '\n' || ch == '\r') continue;
        const char* p = std::strchr(kB64, ch);
        if (!p || !ch) return false;
        v = (v << 6) | static_cast<unsigned>(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((v >> bits) & 0xFF);
        }
    }
    return true;
}

}  // namespace mx
