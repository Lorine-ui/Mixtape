#include "stats.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "text.hpp"

namespace mx {

bool ListenStats::add(const Track& t, double dt, const std::string& day, double& sessionSec) {
    if (dt <= 0 || dt > 5) return false;  // большой скачок времени (сон компьютера) не считаем
    std::string key = t.clip ? t.source + ":" + t.ref : t.id;
    TrackStat& s = tracks_[key];
    if (s.track.id.empty() || !t.clip) {
        s.track = t;
        s.track.off = false;
        s.track.cuts.clear();
        if (t.clip) {  // фрагмент засчитываем исходной песне
            s.track = Track::make(t.source, t.ref);
            s.track.title = t.lyricTitle();
            s.track.artist = t.lyricArtist();
            s.track.cover = t.cover;
            s.track.url = t.url;
        }
    }
    s.sec += dt;
    total_ += dt;
    days_[day] += dt;
    double before = sessionSec;
    sessionSec += dt;
    if (before < kPlaySec && sessionSec >= kPlaySec) {
        ++s.plays;
        return true;
    }
    return false;
}

double ListenStats::daySec(const std::string& day) const {
    auto it = days_.find(day);
    return it == days_.end() ? 0.0 : it->second;
}

int ListenStats::playCount() const {
    int n = 0;
    for (auto& kv : tracks_) n += kv.second.plays;
    return n;
}

std::vector<const TrackStat*> ListenStats::topTracks(std::size_t n) const {
    std::vector<const TrackStat*> v;
    for (auto& kv : tracks_)
        if (kv.second.plays > 0 || kv.second.sec >= 10) v.push_back(&kv.second);
    std::sort(v.begin(), v.end(), [](const TrackStat* a, const TrackStat* b) {
        if (a->plays != b->plays) return a->plays > b->plays;
        return a->sec > b->sec;
    });
    if (v.size() > n) v.resize(n);
    return v;
}

std::vector<ArtistStat> ListenStats::topArtists(std::size_t n) const {
    std::map<std::string, ArtistStat> by;  // ключ — имя без регистра и лишних пробелов
    for (auto& kv : tracks_) {
        const Track& t = kv.second.track;
        std::string name = trim(t.artist);
        if (name.empty()) continue;
        ArtistStat& a = by[lower(name)];
        if (a.name.empty()) a.name = name;
        a.sec += kv.second.sec;
        a.plays += kv.second.plays;
    }
    std::vector<ArtistStat> v;
    for (auto& kv : by) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](const ArtistStat& a, const ArtistStat& b) { return a.sec > b.sec; });
    if (v.size() > n) v.resize(n);
    return v;
}

Json ListenStats::toJson() const {
    Json j = Json::object();
    j.set("total", Json(total_));
    Json days = Json::object();
    for (auto& kv : days_) days.set(kv.first, Json(kv.second));
    j.set("days", days);
    Json tr = Json::object();
    for (auto& kv : tracks_) {
        Json o = Json::object();
        o.set("sec", Json(kv.second.sec));
        o.set("plays", Json(kv.second.plays));
        o.set("t", mx::toJson(kv.second.track));
        tr.set(kv.first, o);
    }
    j.set("tracks", tr);
    return j;
}

void ListenStats::fromJson(const Json& j) {
    *this = ListenStats();
    total_ = j["total"].asNum();
    const Json& days = j["days"];
    for (std::size_t i = 0; i < days.keys().size(); ++i) days_[days.keys()[i]] = days.items()[i].asNum();
    const Json& tr = j["tracks"];
    for (std::size_t i = 0; i < tr.keys().size(); ++i) {
        const Json& o = tr.items()[i];
        TrackStat s;
        s.sec = o["sec"].asNum();
        s.plays = static_cast<int>(o["plays"].asNum());
        s.track = trackFromJson(o["t"]);
        if (s.track.id.empty()) continue;
        tracks_[tr.keys()[i]] = s;
    }
}

std::string localDay(int back) {
    std::time_t now = std::time(nullptr) - static_cast<std::time_t>(back) * 24 * 3600;
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return buf;
}

}  // namespace mx
