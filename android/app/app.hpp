// Логика приложения без рисования: библиотека, очередь, воспроизведение, поиск, тексты, офлайн, перенос.
// Интерфейс (app/ui) только читает это состояние и вызывает методы. Всё состояние трогается из потока интерфейса;
// рабочие потоки возвращают результат через post().
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "audio/player.hpp"
#include "library.hpp"
#include "share.hpp"
#include "net/net.hpp"
#include "queue.hpp"
#include "cloudsync.hpp"
#include "stats.hpp"

namespace mx {

// То, что умеет только операционная система.
struct Platform {
    std::function<std::vector<std::string>()> pickAudioFiles;                       // пути в UTF-8
    std::function<std::string(const std::string& defName)> pickSavePath;           // пусто — отмена
    std::function<std::string()> pickOpenPath;                                      // файл копии/картинки
    std::function<std::vector<std::string>()> legacyDirs;                          // папки, где может лежать библиотека старой версии
    std::function<bool()> isOnline;
    std::function<void(const std::string&)> copyText;
    std::function<std::string(const std::string& name)> resource;  // встроенные в .exe файлы (teto.png)
    std::function<void(bool)> setFullscreen;                         // окно без рамки на весь монитор
    std::function<bool()> isFullscreen;
    std::function<void(int r, int g, int b)> setTitleBarColor;     // заголовок окна в цвет темы
    std::function<std::string(const std::string& defName)> pickSaveAudio;  // куда сохранить песню; пусто — отмена
    std::function<void(const std::string&)> openUrl;                 // открыть ссылку в браузере
    // yt-dlp (только ПК): песни YouTube Music целиком. Не заданы — используется встроенный способ.
    std::function<AudioBlob(const std::string& videoId, const ProgressFn&, const std::atomic<bool>*)> ytDlpFetch;
    std::function<bool()> hasYtDlp;
    std::function<std::string()> pickImage;                          // png/jpg/gif/bmp; пусто — отмена

    // ---- телефон (Android). На Windows не заданы, и приложение ведёт себя по-старому ----
    bool mobile = false;  // сенсорный экран: крупные кнопки, вкладки снизу, без наведения
    // Выбор файлов без ожидания: kind = "audio" (несколько), "image", "backup". done — в потоке интерфейса.
    std::function<void(const std::string& kind, std::function<void(std::vector<std::string>)> done)> pickAsync;
    // Ввод текста системной клавиатурой (в окне поверх приложения). done(текст) — только если нажали «ОК».
    std::function<void(const std::string& title, const std::string& initial, bool multiline, std::function<void(std::string)> done)> promptText;
    // То же для пароля: символы скрыты, подсказки клавиатуры и автозаполнение текста выключены.
    std::function<void(const std::string& title, std::function<void(std::string)> done)> promptPassword;
    // Отдать готовый файл системе: kind = "audio" — в «Музыка/Mixtape», "backup" — в «Загрузки».
    // Возвращает понятное место для сообщения или пусто при ошибке.
    std::function<std::string(const std::string& path, const std::string& kind)> publishFile;
    // Что играет — для уведомления в шторке и работы в фоне.
    std::function<void(bool playing, const std::string& title, const std::string& artist)> nowPlaying;
};

// Профиль на этом устройстве (никнейм, фото, оформление, рамка). Ничего не уходит на сервер.
struct AccountState {
    Profile prof;
    int avatarVer = 0;  // растёт при каждой смене аватарки (интерфейс перезагружает картинку)
    std::string name() const {
        std::string n = displayName(prof, "");
        return n.empty() ? "Гость" : n;
    }
};

// Друг: добавляется ссылкой-приглашением (без сервера)
struct FriendContact {
    std::string id, name, status;
    int color = 0, frame = 1;
    long long added = 0;
};

// Песня, которую прислал друг
struct Recommendation {
    std::string key, fromName, note;
    Track track;
    long long t = 0;
};

enum class ViewKind { Home, Search, Library, Playlist, Clips, Offline, Stats, ForYou, Artist, Album, Friends };

struct ImportState {
    bool running = false, done = false;
    int total = 0, finished = 0, found = 0;
    std::string name, message;
    std::vector<std::string> missing;
};

struct LyricsState {
    bool open = false, loading = false;
    std::string key, message;  // message: «Текст не найден» и подобное
    Lyrics data;
    double offset = 0;
};

class App {
public:
    App(std::string dataDir, Platform platform);
    ~App();
    bool init(std::string* err);
    void frame();      // раз в кадр
    void shutdown();   // сохранить всё; вызывать перед выходом
    void saveNow();    // сохранить библиотеку сразу (телефон: приложение уходит в фон и может быть закрыто системой)

    // ---- потоки ----
    void post(std::function<void()> fn, double delaySec = 0);   // выполнится в потоке интерфейса
    void async(std::function<void()> fn);  // выполнится в рабочем потоке

    // Скачивает небольшой файл (обложку, картинку) и вызывает cb в потоке интерфейса; при ошибке строка пустая.
    void fetchUrl(const std::string& url, std::function<void(std::string)> cb);

    // Популярное на Audius для главной.
    std::vector<Track> trending;

    // ---- статистика прослушивания ----
    ListenStats stats;
    // самые слушаемые песни, готовые к игре (кладутся в кэш, если их нет в библиотеке)
    std::vector<const Track*> topTracks(std::size_t n);
    void resetStats();

    // ---- подбор «Для вас»: похожее на то, что слушаете ----
    std::vector<Track> recs;
    bool recsLoading = false;
    std::string recsBasis;   // «Похоже на: исполнитель, исполнитель»
    void loadRecs(bool force);

    // ---- профиль (social.cpp): хранится на этом устройстве ----
    AccountState acct;
    void profileChanged();                         // правка сохраняется сразу
    void setAvatar(const std::string& base64Rgb);  // пусто — убрать
    bool trendingLoading = false;
    void loadTrending();

    // ---- сообщения ----
    void toast(const std::string& msg, double seconds = 3.8);
    std::string toastText;
    double toastLen = 3.8;  // сколько секунд показывается текущая надпись
    double toastUntil = 0;  // время приложения (App::now())
    double now() const;

    // ---- общее состояние ----
    std::string dataDir;
    Platform platform;
    Library lib;
    Queue queue;
    Player player;
    bool audioOk = false;
    bool online = true;
    std::string importPrefill;     // ссылка, найденная в строке поиска
    bool openImportModal = false;  // интерфейс покажет окно переноса и сбросит флаг

    // ---- навигация и поиск ----
    ViewKind view = ViewKind::Home;
    std::string viewPlaylist;
    bool searching = false;
    std::string searchQuery;
    std::vector<Track> results;
    std::vector<SourceStatus> sourceStatus;
    std::vector<Track> linkTracks;  // найденный по ссылке плейлист SoundCloud
    std::string linkName;

    // ---- страница исполнителя и альбома (YouTube Music) ----
    struct ArtistView {
        std::string name, cover, error;  // name: как просили, после загрузки — как назвал YouTube Music
        std::vector<Track> songs;
        std::vector<YtmAlbum> albums, singles;
        bool loading = false;
    } artist;
    struct AlbumView {
        std::string id, title, artist, year, cover, error;
        std::vector<Track> tracks;
        bool loading = false;
    } album;
    // Имя из подписи песни; из «A & B» берётся первый. strict — имя из чужого источника: открываем, только если
    // на YouTube Music есть исполнитель с точно таким именем, иначе показываем обычный поиск.
    void openArtist(const std::string& names, bool strict = false);
    void openAlbum(const YtmAlbum& a);

    // ---- друзья и рекомендации ссылкой (social.cpp): всё на этом устройстве, без сервера ----
    std::vector<FriendContact> friendList;
    std::vector<Recommendation> inbox;                      // песни от друзей, которые вы ещё не убрали
    int friendsBadge() const { return static_cast<int>(inbox.size()); }
    void openFriends();
    std::string myFriendLink() const;                       // ссылка-приглашение: её отправляют другу
    bool handleShare(const std::string& text);              // вставили ссылку (друг или песня); false — это не ссылка Mixtape
    void shareSong(const Track& t);                         // скопировать ссылку на песню для друга
    void friendRemove(const std::string& id);
    void recPlay(const Recommendation& r);
    void recSave(const Recommendation& r);                  // в «Мою музыку» и убрать из рекомендаций
    void recDismiss(const std::string& key);

    void openView(ViewKind v, const std::string& playlistId = "");
    void search(const std::string& text);  // текст или ссылка
    std::vector<const Track*> currentList() const;  // что показывается в списке

    // ---- воспроизведение ----
    std::optional<Track> cur;
    bool loading = false;
    std::atomic<double> loadProgress{0};
    std::string playError;
    double rate = 1;
    float volume = 0.8f;
    double sleepAt = 0;        // время приложения, 0 — выкл
    bool sleepAtEnd = false;

    const Track* getTrack(const std::string& id) const;
    void playFrom(const std::vector<std::string>& ids, const std::string& id);
    void play(const Track& t);
    void togglePause();
    void next(bool automatic = false);
    void prev();
    void seek(double sec);
    void setVolume(float v);
    void setRate(double r);
    void toggleShuffle();
    void cycleLoop();
    void randomSong();
    bool isPlaying() const { return player.playing(); }
    double position() const;  // в шкале фрагмента: от начала «оставленного» отрезка
    double visibleLength() const;
    Segment segment;           // текущие вырезы и ограничение
    void refreshSegment();

    // ---- библиотека ----
    bool inLibrary(const Track& t) const { return lib.has(t.id); }
    void toggleFavorite(const Track& t);
    void addToPlaylist(const std::string& playlistId, const Track& t);
    void removeFromPlaylist(const std::string& playlistId, const std::string& trackId);
    std::string newPlaylist(const std::string& name);
    void addFiles();                                   // диалог выбора файлов
    void addPaths(const std::vector<std::string>& paths);  // файлы, перетащенные в окно

    // ---- офлайн ----
    std::map<std::string, double> downloading;  // id -> доля
    bool offlineAvailable(const Track& t) const;
    bool playableNow(const std::string& id) const;
    void saveOffline(const Track& t, bool quiet = false);
    void dropOffline(const Track& t);
    void offlineAll(const std::vector<const Track*>& list);
    void dropAllOffline();                       // удалить все офлайн-копии (песни остаются в «Моей музыке»)
    long long offlineBytes() const;              // сколько места занимают копии
    // Сохранить песню обычным файлом (mp3/m4a/…) туда, куда выберет пользователь.
    void exportTrack(const Track& t);
    std::set<std::string> exporting;             // id песен, которые сейчас сохраняются

    // ---- фрагменты ----
    std::vector<float> peaks;
    bool peaksLoading = false;
    void loadPeaks();
    void previewSegment(bool keepMode, double s, double e);   // предпросмотр в редакторе
    void endPreview();
    void saveClip(bool keepMode, double s, double e);
    void removeCut(const CutRef& ref);

    // ---- тексты ----
    LyricsState ly;
    void openLyrics(bool open);
    void loadLyrics(bool force);
    void setOwnLyrics(const std::string& text);
    void shiftLyrics(double d);
    int lyricLineAt(double t) const;

    // ---- перенос плейлистов ----
    ImportState imp;
    void startImportLink(const std::string& link);
    void startImportList(const std::string& name, const std::string& text);

    // ---- данные ----
    void saveBackup();
    void loadBackup();
    // перенос из старой версии на этом же устройстве
    struct LegacySource { std::string dir; int tracks = 0, playlists = 0; };
    std::vector<LegacySource> legacy;      // найденные библиотеки старых версий
    void scanLegacy();
    void importLegacy(const std::string& dir);
    void importLegacyFile();
    void loadBackupJson(const Json& j);
    void loadBackupFrom(const std::string& path);
    void touch() { dirty_ = true; lastChange_ = now(); }
    std::string audioFilePath(const std::string& name) const;

    // ---- системные кнопки ----
    enum class Media { PlayPause, Next, Prev, Stop };
    void media(Media m);

    // ---- пасхалка ----
    bool tetoOn() const { return lib.getBool("tetoOn", false); }
    void tetoEgg(bool& firstTime);

private:
    struct Prefetch { std::string id; AudioBlob blob; };

    void workerLoop();
    void onEnded();
    void startPlayback(const Track& t, int token);
    void finishLoad(const Track& t, int token, const std::string& err);
    void maybePrefetch();
    std::string offlinePath(const Track& t, const std::string& ext) const;
    void importRun(std::string name, std::vector<ListItem> items);
    void saveLyricsCache();
    std::shared_ptr<Library> snapshot(bool full) const;

    std::unique_ptr<IHttp> http_;
    // профиль и друзья (social.cpp)
    void socialLoad();
    void socialSave();
    std::string myId_;   // мой случайный номер для приглашений
    std::unique_ptr<Net> net_;
    std::map<std::string, Track> cache_;   // треки из поиска (ещё не в библиотеке)
    std::set<std::string> seenRandom_;
    std::map<std::string, Lyrics> lyCache_;
    std::optional<Prefetch> prefetched_;
    std::string prefetching_;

    struct Timed { double at; std::function<void()> fn; };
    std::mutex qmu_;
    std::deque<Timed> uiQueue_;
    std::mutex loadMu_;
    bool previewing_ = false;
    std::mutex wmu_;
    std::condition_variable wcv_;
    std::deque<std::function<void()>> work_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};

    std::atomic<int> loadToken_{0};
    std::atomic<int> searchToken_{0};
    std::atomic<int> artistToken_{0}, albumToken_{0};
    std::atomic<int> lyToken_{0};
    std::shared_ptr<std::atomic<bool>> cancelLoad_ = std::make_shared<std::atomic<bool>>(false);
    int fails_ = 0;
    bool dirty_ = false;
    double lastChange_ = 0, lastOnlineCheck_ = 0;
    std::string peaksFor_;
    std::string loadedBase_;  // id трека, чей звук сейчас в плеере
    bool lyCacheDirty_ = false;
    // статистика
    double lastTick_ = 0, sessionSec_ = 0, statsSavedAt_ = 0;
    bool statsDirty_ = false;
    void saveStats();
    bool tasteRandom();       // «случайная песня» из подборки по вкусу; false — подборки нет, берём жанры
    void randomGenre();       // «случайная песня» из случайного жанра (как раньше)
    bool pendingTaste_ = false;  // подборка грузится ради «случайной песни»
    bool npPlaying_ = false;   // что последним сообщили платформе (уведомление в шторке)
    std::string npId_;
    bool ended_handled_ = false;
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
};

}  // namespace mx
