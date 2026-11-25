// Модели данных. Формат JSON совместим с резервной копией старого приложения (mixtape-backup.json).
#pragma once
#include <string>
#include <vector>

#include "json.hpp"

namespace mx {

struct Cut {
    double s = 0, e = 0;
};

// Источники: local, audius, archive, soundcloud, youtube.
const char* sourceLabel(const std::string& source);
bool isValidSource(const std::string& source);

struct Track {
    std::string id, source, ref;
    std::string title, artist, cover;
    std::string url;      // archive: ссылка на файл; local: путь к файлу
    double dur = 0;
    bool set = false;     // плейлист SoundCloud (играет как один «трек»)

    // фрагмент (клип) исходного трека
    bool clip = false;
    std::string mode;     // "keep" | "cut"
    double start = 0, end = 0;
    std::string parent;
    std::string ptitle, partist;  // исходные название и исполнитель (для текстов и перекрёстного поиска)

    long long added = 0;
    bool off = false;     // скачан для офлайн
    long long offSize = 0;
    std::vector<Cut> cuts;  // навсегда вырезанные куски

    static Track make(const std::string& source, const std::string& ref);
    // id трека, к которому относятся вырезы и офлайн-копия
    std::string baseId() const { return clip ? source + ":" + ref : id; }
    const std::string& lyricTitle() const { return ptitle.empty() ? title : ptitle; }
    const std::string& lyricArtist() const { return partist.empty() ? artist : partist; }
    // скачать для офлайна можно всё, что приходит из сети (звук и так целиком грузится в память перед игрой)
    bool canOffline() const { return !clip && (source == "audius" || source == "archive" || source == "soundcloud" || source == "youtube"); }
};

Json toJson(const Track& t);
Track trackFromJson(const Json& j);

struct Playlist {
    std::string id, name;
    std::vector<std::string> ids;
};
Json toJson(const Playlist& p);
Playlist playlistFromJson(const Json& j);

struct LyricLine {
    double t = 0;
    std::string text;
};

struct Lyrics {
    std::string name, artist, src;
    bool instrumental = false;
    std::vector<LyricLine> lines;  // с отметками времени
    std::string plain;             // без синхронизации
    bool empty() const { return lines.empty() && plain.empty() && !instrumental; }
};
Json toJson(const Lyrics& l);
Lyrics lyricsFromJson(const Json& j);

std::string newId();  // случайный короткий идентификатор

}  // namespace mx
