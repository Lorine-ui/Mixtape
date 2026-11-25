// Источники музыки, тексты и перенос плейлистов. Синхронный код: приложение вызывает его из рабочих потоков.
// Все ошибки: std::runtime_error с русским текстом, который можно показывать пользователю.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "http.hpp"
#include "library.hpp"
#include "model.hpp"
#include "parsers.hpp"

namespace mx {

struct SourceStatus {
    std::string name, source;
    int count = 0;
    bool error = false;
    std::string message;
};
struct SearchOutcome {
    std::vector<Track> tracks;
    std::vector<SourceStatus> status;
};

struct AudioBlob {
    std::string bytes;
    std::string mime;
    std::string ext;  // mp3, m4a, ogg, flac, wav...
};

struct LinkResult {
    std::string name;
    std::vector<Track> tracks;
    bool isPlaylist = false;
};

using ProgressFn = std::function<void(double fraction)>;

// Помощники, которые удобно проверять отдельно.
std::string resolveUrl(const std::string& base, const std::string& rel);
// Адреса сегментов HLS по порядку; инициализирующий сегмент (EXT-X-MAP) идёт первым.
std::vector<std::string> parseM3u8(const std::string& text, const std::string& baseUrl);
std::string extForMime(const std::string& mime, const std::string& url);

class Net {
public:
    explicit Net(IHttp& http) : http_(http) {}

    // ---- поиск ----
    std::vector<Track> audiusSearch(const std::string& q, int n = 20);
    std::vector<Track> audiusTrending();
    std::vector<Track> scSearch(const std::string& q, int n = 15);
    // Похожие треки, которые сам SoundCloud показывает под песней (по ссылке на песню).
    std::vector<Track> scRelated(const std::string& permalink, int n = 20);
    std::vector<Track> iaSearch(const std::string& q);
    std::vector<Track> ytSearch(const std::string& q, int n = 10);
    // Все включённые в настройках источники параллельно; сбой одного не мешает остальным.
    SearchOutcome searchAll(const std::string& q, const Library& lib);

    // ---- ссылки ----
    LinkResult scFromLink(const std::string& url);  // трек или плейлист SoundCloud
    Track ytFromLink(const std::string& videoId);
    PlaylistInfo ytPlaylist(const std::string& listId);
    PlaylistInfo spPlaylist(const std::string& kind, const std::string& id);

    // ---- звук ----
    // Скачивает песню целиком в память (для проигрывания и офлайн-копии).
    AudioBlob fetchAudio(const Track& t, const ProgressFn& progress = {}, const std::atomic<bool>* cancel = nullptr);
    // Та же песня в другом источнике (если YouTube не отдал поток): Audius, затем SoundCloud.
    std::optional<Track> findAlternative(const Track& t, const Library& lib);

    // ---- тексты ----
    // Пустой результат (empty()) означает «текста нет». Бросает, только если ни один сервис не ответил.
    Lyrics findLyrics(const std::string& title, const std::string& artist, double dur);

    // ---- переносы ----
    std::optional<Track> matchTrack(const ListItem& item, const Library& lib);

private:
    std::string get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& h = {}, int timeoutMs = 15000);
    Json getJson(const std::string& url, int timeoutMs = 15000);
    std::string audiusHost();
    Json audiusJson(const std::string& path);
    std::string scClient(bool force);
    HttpResponse scRaw(const std::string& pathAndQuery);  // добавляет client_id, при 401/403 обновляет его и повторяет
    void audiusRehost();
    Json scResolveJson(const std::string& url);
    std::string scStreamUrl(const ScTranscoding& tc);   // адрес самого потока для одного варианта
    AudioBlob scFetch(const std::string& permalink, const ProgressFn& progress, const std::atomic<bool>* cancel);
    AudioBlob download(const std::string& url, const std::string& hint, const ProgressFn& progress, const std::atomic<bool>* cancel,
                       const std::vector<std::pair<std::string, std::string>>& headers = {});
    AudioBlob downloadHls(const std::string& m3u8Url, const ProgressFn& progress, const std::atomic<bool>* cancel);
    std::string ytDirectUrl(const std::string& videoId, std::size_t& length);

    IHttp& http_;
    std::mutex mu_;
    std::string audiusHost_ = "https://api.audius.co";
    std::string scCid_;
    long long scCidAt_ = 0;
    std::map<std::string, std::pair<long long, Json>> scResolved_;  // permalink -> (время, ответ)
};

}  // namespace mx
