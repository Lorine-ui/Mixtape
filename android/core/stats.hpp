// Статистика прослушивания: сколько времени играла каждая песня, сколько раз её прослушали (от 30 секунд),
// сколько музыки было в каждый день. Хранится отдельно от библиотеки (stats.json).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "json.hpp"
#include "model.hpp"

namespace mx {

struct TrackStat {
    double sec = 0;   // сколько секунд звучала
    int plays = 0;    // сколько раз прослушана
    Track track;      // чтобы показать и включить песню, даже если её уже нет в библиотеке
};

struct ArtistStat {
    std::string name;
    double sec = 0;
    int plays = 0;
};

class ListenStats {
public:
    static constexpr double kPlaySec = 30.0;  // столько нужно послушать, чтобы засчиталось прослушивание

    // Песня звучала ещё dt секунд в день day ("2026-11-24"). sessionSec — сколько уже звучит текущее
    // включение этой песни (обнуляется при каждом новом включении). true — только что засчитано прослушивание.
    bool add(const Track& t, double dt, const std::string& day, double& sessionSec);

    double totalSec() const { return total_; }
    double daySec(const std::string& day) const;
    int trackCount() const { return static_cast<int>(tracks_.size()); }
    int playCount() const;
    // самые слушаемые: по числу прослушиваний, при равенстве — по времени
    std::vector<const TrackStat*> topTracks(std::size_t n) const;
    std::vector<ArtistStat> topArtists(std::size_t n) const;

    Json toJson() const;
    void fromJson(const Json& j);

private:
    std::map<std::string, TrackStat> tracks_;  // ключ — id песни (у фрагментов — id исходной песни)
    std::map<std::string, double> days_;
    double total_ = 0;
};

// "2026-11-24" для текущего дня (местное время) и для дня, отстоящего на back дней назад
std::string localDay(int back = 0);

}  // namespace mx
