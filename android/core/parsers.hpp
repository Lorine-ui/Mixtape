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

// YouTube Music: страница исполнителя, дискография и альбомы (ответы /youtubei/v1/search и /browse).
struct YtmAlbum {
    std::string id, title, sub, cover;  // id — browseId альбома («MPREb_…»); sub — «2025» или «Single • 2025»
};
struct YtmArtistRef {
    std::string id, name, cover;  // id — browseId исполнителя («UC…»); пусто, если не нашли
};
struct YtmArtistPage {
    std::string name, cover;
    std::string songsId;                      // список «все песни» («VLOLAK…»)
    std::vector<Track> songs;                 // несколько лучших песен прямо на странице
    std::vector<YtmAlbum> albums, singles;
    std::string albumsId, albumsParams;       // «вся дискография», если альбомы поместились не все
    std::string singlesId, singlesParams;
};
struct YtmAlbumPage {
    std::string title, artist, year, cover;
    std::vector<Track> tracks;
};
// Первый исполнитель из «A, B & C feat. D».
std::string firstArtist(const std::string& names);
// Поиск с фильтром «Исполнители»: берёт того, чьё имя совпало (без учёта регистра), иначе первого.
// exactOnly: первого не берём — для имён из чужих источников (SoundCloud и т.п.), чтобы не открыть не того.
YtmArtistRef parseYtmArtistSearch(const std::string& json, const std::string& wantName, bool exactOnly = false);
YtmArtistPage parseYtmArtist(const std::string& json);
std::vector<YtmAlbum> parseYtmAlbums(const std::string& json);
std::vector<Track> parseYtmTracks(const std::string& json, const std::string& artist, const std::string& cover);
YtmAlbumPage parseYtmAlbum(const std::string& json);

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
