// Библиотека пользователя: треки, плейлисты, настройки, свои тексты. Хранится одним JSON-файлом
// (тот же формат, что у резервной копии старого приложения, так что копию можно просто загрузить).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cuts.hpp"
#include "json.hpp"
#include "model.hpp"

namespace mx {

bool readFile(const std::string& path, std::string& out);
// Пишет во временный файл и переименовывает; прежний файл остаётся как path + ".bak".
bool writeFileAtomic(const std::string& path, const std::string& data, std::string* err = nullptr);
long long nowMs();

struct CutRef {
    Cut cut;
    bool own = false;  // вырез самого клипа (иначе — исходного трека)
    int index = 0;
};

class Library {
public:
    std::map<std::string, Track> tracks;
    std::vector<Playlist> playlists;
    Json settings = Json::object();
    std::map<std::string, double> lyOff;       // сдвиг текста по ключу «название|исполнитель»
    std::map<std::string, Lyrics> lyrics;      // свои тексты

    // ---- треки ----
    Track* find(const std::string& id);
    const Track* find(const std::string& id) const;
    bool has(const std::string& id) const { return tracks.count(id) != 0; }
    Track& add(const Track& t);  // «в мою музыку»: если уже есть, возвращает существующий
    // Удаляет трек вместе с его клипами и убирает из плейлистов. Возвращает все удалённые id.
    std::vector<std::string> remove(const std::string& id);
    std::vector<const Track*> list(const std::function<bool(const Track&)>& pred) const;  // новые сверху

    // ---- вырезы ----
    void addCut(const Track& cur, Cut c);
    std::vector<CutRef> cutList(const Track& cur) const;
    bool removeCut(const Track& cur, const CutRef& ref);
    Segment segmentFor(const Track& t) const;
    int cutCount(const Track& t) const;

    // ---- плейлисты ----
    Playlist* playlist(const std::string& id);
    Playlist& createPlaylist(const std::string& name);
    bool removePlaylist(const std::string& id);

    // ---- настройки ----
    bool getBool(const std::string& k, bool d) const;
    double getNum(const std::string& k, double d) const;
    std::string getStr(const std::string& k, const std::string& d) const;
    void set(const std::string& k, Json v) { settings.set(k, std::move(v)); }
    bool srcOn(const std::string& source) const;
    void setSrc(const std::string& source, bool on);

    // ---- хранение ----
    Json toJson() const;
    bool fromJson(const Json& j, std::string* err = nullptr);  // заменяет содержимое
    bool load(const std::string& path, std::string* err = nullptr);
    bool save(const std::string& path, std::string* err = nullptr) const;

    struct MergeStats {
        int tracks = 0, playlists = 0, lyrics = 0;
        bool ok = false;
    };
    // Резервная копия: добавляет то, чего нет; существующее и настройки не трогает. Локальные файлы пропускаются.
    MergeStats mergeBackup(const Json& j);
};

std::string lyricKey(const Track& t);  // «название|исполнитель»

}  // namespace mx
