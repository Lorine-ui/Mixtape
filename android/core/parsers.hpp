// Разбор публичных страниц и ответов API. Только текст на входе, без сети и Qt: легко тестировать.
// Ошибки формата страницы бросают std::runtime_error с понятным русским текстом.
#pragma once
#include <string>
#include <vector>

#include "json.hpp"
#include "model.hpp"

namespace mx {

struct ListItem {
    std::string title, artist, vid, cover;
};
struct PlaylistInfo {
    std::string name;
    std::vector<ListItem> items;
};

// ---------- YouTube / Spotify ----------
std::vector<Track> parseYtSearch(const std::string& html);
// YouTube Music: JSON-ответ /youtubei/v1/search (только песни). Бросает, если ответ не похож на поиск.
std::vector<Track> parseYtmSearch(const std::string& json);
PlaylistInfo parseYtPlaylist(const std::string& html);
PlaylistInfo parseSpEmbed(const std::string& html);

// ---------- Audius ----------
std::vector<Track> parseAudiusTracks(const Json& root);

// ---------- SoundCloud ----------
std::vector<std::string> scScriptUrls(const std::string& html);
std::string scClientId(const std::string& js);  // пусто, если не нашли
std::vector<Track> parseScTracks(const Json& j);
struct ScTranscoding {
    std::string url, auth, mime;
    bool progressive = false;
    bool ok() const { return !url.empty(); }
};
// Все варианты потока из ответа /resolve, которые плеер умеет играть, от лучшего к худшему:
// progressive mp3, HLS mp3, HLS/progressive AAC. Зашифрованные (DRM) и opus/ogg отброшены.
std::vector<ScTranscoding> scTranscodings(const Json& track);

// ---------- Internet Archive ----------
double lenSec(const Json& v);
std::vector<Track> parseIaItem(const Json& doc, const Json& meta);

// ---------- ссылки ----------
std::string ytVideoId(const std::string& text);   // 11 символов или пусто
std::string ytListId(const std::string& text);
bool isScLink(const std::string& text);
struct SpotifyLink {
    std::string kind, id;  // kind: playlist | album
    bool ok() const { return !id.empty(); }
};
SpotifyLink parseSpotifyLink(const std::string& text);

// ---------- тексты ----------
struct Query {
    std::string title, artist;
};
std::string cleanTitle(const std::string& s);
Query cleanQuery(const std::string& title, const std::string& artist);
std::vector<Query> lyricVariants(const std::string& title, const std::string& artist);
std::vector<LyricLine> parseLrc(const std::string& lrc);
const Json* pickBestLyrics(const Json& arr, const Query& c, double dur);
Lyrics shapeLyrics(const Json& lrclibItem);
std::string lyricsOvhText(const Json& root);  // пусто, если текста нет

// ---------- безопасность ----------
// Можно ли скачивать этот адрес для офлайн-копии (защита от подмены url в чужой резервной копии).
bool isAllowedDownloadUrl(const std::string& url);

// ---------- импорт списков ----------
std::vector<std::string> csvRow(const std::string& line);
std::vector<ListItem> parseList(const std::string& text);  // CSV Exportify или строки «Артист - Название»
ListItem splitArtistTitle(const std::string& line);

}  // namespace mx
