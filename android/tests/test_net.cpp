// Тесты сетевого слоя на подставном HTTP: никакого интернета.
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>

#include "net.hpp"

using namespace mx;

static int g_fail = 0, g_all = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_all;                                                              \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)
#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        ++g_all;                                                                              \
        auto va = (a); auto vb = (b);                                                         \
        if (!(va == vb)) { ++g_fail; std::printf("  FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, #a, #b); } \
    } while (0)

static void run(const char* name, const std::function<void()>& f) {
    int before = g_fail;
    f();
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name);
}

// Ответы по подстроке адреса; первое подходящее правило побеждает.
struct FakeHttp : IHttp {
    struct Rule { std::string part; int status; std::string body; std::string type; };
    std::vector<Rule> rules;
    std::vector<std::string> log;
    std::mutex mu;
    void on(const std::string& part, const std::string& body, int status = 200, const std::string& type = "application/json") {
        rules.push_back({part, status, body, type});
    }
    HttpResponse request(const std::string& url, const HttpOptions&) override {
        std::lock_guard<std::mutex> lk(mu);
        log.push_back(url);
        for (auto& r : rules)
            if (url.find(r.part) != std::string::npos) {
                HttpResponse h;
                h.status = r.status;
                h.body = r.body;
                h.contentType = r.type;
                return h;
            }
        throw std::runtime_error("нет сети: " + url);
    }
    bool called(const std::string& part) {
        for (auto& u : log) if (u.find(part) != std::string::npos) return true;
        return false;
    }
};

int main() {
    run("resolveUrl и HLS", [] {
        CHECK_EQ(resolveUrl("https://a.com/x/y.m3u8?q=1", "seg1.mp3"), std::string("https://a.com/x/seg1.mp3"));
        CHECK_EQ(resolveUrl("https://a.com/x/y.m3u8", "/root/s.mp3"), std::string("https://a.com/root/s.mp3"));
        CHECK_EQ(resolveUrl("https://a.com/x/y.m3u8", "https://b.com/s.mp3"), std::string("https://b.com/s.mp3"));
        CHECK_EQ(resolveUrl("https://a.com/x/y.m3u8", "//cdn.com/s.mp3"), std::string("https://cdn.com/s.mp3"));
        auto segs = parseM3u8("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:10,\nseg1.m4s\n#EXTINF:10,\n\nseg2.m4s\n#EXT-X-ENDLIST\n",
                              "https://a.com/p/list.m3u8");
        CHECK_EQ(segs.size(), 3u);
        CHECK_EQ(segs[0], std::string("https://a.com/p/init.mp4"));
        CHECK_EQ(segs[2], std::string("https://a.com/p/seg2.m4s"));
        CHECK_EQ(extForMime("audio/mpeg", ""), std::string("mp3"));
        CHECK_EQ(extForMime("audio/MP4; codecs=x", ""), std::string("m4a"));
        CHECK_EQ(extForMime("application/octet-stream", "https://x/a/b.flac?t=1"), std::string("flac"));
        CHECK_EQ(extForMime("", "https://x/noext"), std::string("mp3"));
    });

    run("Audius: поиск и смена сервера", [] {
        FakeHttp h;
        h.on("api.audius.co/v1/tracks/search", "", 502);
        h.on("https://api.audius.co", R"({"data":["https://node1.example"]})");
        h.on("node1.example/v1/tracks/search", R"({"data":[{"id":"A1","title":"Numb","user":{"name":"LP"},"duration":180}]})");
        Net n(h);
        auto r = n.audiusSearch("numb", 5);
        CHECK_EQ(r.size(), 1u);
        CHECK(h.called("node1.example"));
    });

    run("Audius: скачивание", [] {
        FakeHttp h;
        h.on("/v1/tracks/A1/stream", std::string(5000, 'x'), 200, "audio/mpeg");
        Net n(h);
        Track t = Track::make("audius", "A1");
        AudioBlob b = n.fetchAudio(t);
        CHECK_EQ(b.ext, std::string("mp3"));
        CHECK_EQ(b.bytes.size(), 5000u);
        FakeHttp bad;
        bad.on("/stream", "<html>", 200, "text/html");
        bad.on("https://api.audius.co", "", 500);
        Net n2(bad);
        bool threw = false;
        try { n2.fetchAudio(t); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    });

    run("SoundCloud: поиск, повтор при 401, потоки", [] {
        FakeHttp h;
        h.on("https://soundcloud.com/", R"(<script src="https://a-v2.sndcdn.com/assets/0-a.js"></script>)", 200, "text/html");
        h.on("a-v2.sndcdn.com/assets/0-a.js", "x={client_id:\"abcdefghijklmnopqrstuvwxyz012345\"}", 200, "text/javascript");
        h.on("/search/tracks",
             R"({"collection":[{"kind":"track","permalink_url":"https://soundcloud.com/a/b","title":"B","user":{"username":"A"},"duration":90000}]})");
        Net n(h);
        auto r = n.scSearch("b", 10);
        CHECK_EQ(r.size(), 1u);
        CHECK_EQ(r[0].ref, std::string("https://soundcloud.com/a/b"));

        // поток: resolve -> transcoding -> url (progressive)
        FakeHttp s;
        s.on("https://soundcloud.com/", R"(<script src="https://a-v2.sndcdn.com/assets/0-a.js"></script>)", 200, "text/html");
        s.on("a-v2.sndcdn.com/assets/0-a.js", "x={client_id:\"abcdefghijklmnopqrstuvwxyz012345\"}", 200, "text/javascript");
        s.on("/resolve", R"({"kind":"track","track_authorization":"AU","permalink_url":"https://soundcloud.com/a/b","title":"B",
             "media":{"transcodings":[{"url":"https://api-v2.soundcloud.com/media/1/prog","format":{"protocol":"progressive","mime_type":"audio/mpeg"}}]}})");
        s.on("/media/1/prog", R"({"url":"https://cf-media.sndcdn.com/file.mp3"})");
        s.on("cf-media.sndcdn.com/file.mp3", std::string(4000, 'm'), 200, "audio/mpeg");
        Net n2(s);
        Track t = Track::make("soundcloud", "https://soundcloud.com/a/b");
        AudioBlob b = n2.fetchAudio(t);
        CHECK_EQ(b.bytes.size(), 4000u);
        CHECK(s.called("track_authorization=AU"));

        // HLS: главный список -> вариант -> сегменты
        FakeHttp hl;
        hl.on("https://soundcloud.com/", R"(<script src="https://a-v2.sndcdn.com/assets/0-a.js"></script>)", 200, "text/html");
        hl.on("a-v2.sndcdn.com/assets/0-a.js", "x={client_id:\"abcdefghijklmnopqrstuvwxyz012345\"}", 200, "text/javascript");
        hl.on("/resolve", R"({"kind":"track","permalink_url":"https://soundcloud.com/a/b","title":"B",
             "media":{"transcodings":[{"url":"https://api-v2.soundcloud.com/media/2/hls","format":{"protocol":"hls","mime_type":"audio/mpeg"}}]}})");
        hl.on("/media/2/hls", R"({"url":"https://cdn.example/master.m3u8"})");
        hl.on("master.m3u8", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv1/list.m3u8\n", 200, "application/x-mpegURL");
        hl.on("v1/list.m3u8", "#EXTM3U\n#EXTINF:5,\ns1.mp3\n#EXTINF:5,\ns2.mp3\n#EXT-X-ENDLIST\n", 200, "application/x-mpegURL");
        hl.on("s1.mp3", "AAAA", 200, "audio/mpeg");
        hl.on("s2.mp3", "BBBB", 200, "audio/mpeg");
        Net n3(hl);
        double lastP = 0;
        AudioBlob hb = n3.fetchAudio(t, [&](double p) { lastP = p; });
        CHECK_EQ(hb.bytes, std::string("AAAABBBB"));
        CHECK(lastP > 0.99);

        // progressive отвечает 404 (так SoundCloud делает всё чаще) -> берётся HLS того же трека;
        // зашифрованный поток (DRM) не пробуется вовсе
        FakeHttp fb;
        fb.on("https://soundcloud.com/", R"(<script src="https://a-v2.sndcdn.com/assets/0-a.js"></script>)", 200, "text/html");
        fb.on("a-v2.sndcdn.com/assets/0-a.js", "x={client_id:\"abcdefghijklmnopqrstuvwxyz012345\"}", 200, "text/javascript");
        fb.on("/resolve", R"({"kind":"track","track_authorization":"AU","permalink_url":"https://soundcloud.com/a/b","title":"B",
             "media":{"transcodings":[
               {"url":"https://api-v2.soundcloud.com/media/3/drm","format":{"protocol":"ctr-encrypted-hls","mime_type":"audio/mp4"}},
               {"url":"https://api-v2.soundcloud.com/media/3/prog","format":{"protocol":"progressive","mime_type":"audio/mpeg"}},
               {"url":"https://api-v2.soundcloud.com/media/3/hls","format":{"protocol":"hls","mime_type":"audio/mpeg"}}]}})");
        fb.on("/media/3/prog", "", 404);
        fb.on("/media/3/hls", R"({"url":"https://cdn.example/only.m3u8"})");
        fb.on("only.m3u8", "#EXTM3U\n#EXTINF:5,\nz1.mp3\n#EXT-X-ENDLIST\n", 200, "application/x-mpegURL");
        fb.on("z1.mp3", "ZZZZ", 200, "audio/mpeg");
        Net n4(fb);
        AudioBlob fbb = n4.fetchAudio(t);
        CHECK_EQ(fbb.bytes, std::string("ZZZZ"));
        CHECK(!fb.called("/media/3/drm"));
    });

    run("SoundCloud: плейлист с заглушками", [] {
        FakeHttp h;
        h.on("https://soundcloud.com/", R"(<script src="https://a-v2.sndcdn.com/assets/0-a.js"></script>)", 200, "text/html");
        h.on("a-v2.sndcdn.com/assets/0-a.js", "x={client_id:\"abcdefghijklmnopqrstuvwxyz012345\"}", 200, "text/javascript");
        h.on("/resolve", R"({"kind":"playlist","title":"Сет","tracks":[
            {"id":1,"kind":"track","title":"Полный","permalink_url":"https://soundcloud.com/a/one","user":{"username":"A"}},
            {"id":2}]})");
        h.on("/tracks?ids=2", R"([{"id":2,"kind":"track","title":"Догруженный","permalink_url":"https://soundcloud.com/a/two","user":{"username":"B"}}])");
        Net n(h);
        LinkResult r = n.scFromLink("https://soundcloud.com/a/sets/s");
        CHECK(r.isPlaylist);
        CHECK_EQ(r.name, std::string("Сет"));
        CHECK_EQ(r.tracks.size(), 2u);
        if (r.tracks.size() == 2) CHECK_EQ(r.tracks[1].title, std::string("Догруженный"));
    });

    run("поиск по всем источникам: сбой одного не мешает", [] {
        FakeHttp h;
        h.on("api.audius.co/v1/tracks/search", R"({"data":[{"id":"A1","title":"Numb","user":{"name":"Linkin Park"},"duration":180}]})");
        h.on("https://soundcloud.com/", "", 500);
        h.on("advancedsearch", R"({"response":{"docs":[]}})");
        h.on("youtube.com/results", "<html>без данных</html>", 200, "text/html");
        Net n(h);
        Library lib;
        lib.add([] { Track t = Track::make("local", "f"); t.title = "Numb"; t.artist = "Linkin Park"; return t; }());
        SearchOutcome o = n.searchAll("linkin park numb", lib);
        CHECK_EQ(o.status.size(), 4u);
        int errors = 0;
        for (auto& s : o.status) errors += s.error;
        CHECK_EQ(errors, 2);  // SoundCloud и YouTube
        CHECK(!o.tracks.empty());
        CHECK_EQ(o.tracks[0].source, std::string("local"));  // свои треки идут первыми

        Library noSc;
        noSc.setSrc("soundcloud", false);
        noSc.setSrc("youtube", false);
        noSc.setSrc("archive", false);
        FakeHttp h2;
        h2.on("api.audius.co/v1/tracks/search", R"({"data":[]})");
        Net n2(h2);
        CHECK_EQ(n2.searchAll("x", noSc).status.size(), 1u);
    });

    run("тексты: LRCLIB, запасной сервис, ошибка сети", [] {
        FakeHttp h;
        h.on("lrclib.net/api/get", R"({"trackName":"Numb","artistName":"Linkin Park","syncedLyrics":"[00:01.00] a\n[00:02.00] b","plainLyrics":"a\nb"})");
        Net n(h);
        Lyrics l = n.findLyrics("Numb", "Linkin Park", 187);
        CHECK_EQ(l.lines.size(), 2u);

        FakeHttp h2;
        h2.on("lrclib.net/api/get", "", 404);
        h2.on("lrclib.net/api/search", "[]");
        h2.on("lyrics.ovh", R"({"lyrics":"первая строка достаточно длинного текста песни для проверки работы"})");
        Net n2(h2);
        Lyrics l2 = n2.findLyrics("Песня", "Группа", 0);
        CHECK_EQ(l2.src, std::string("lyrics.ovh"));
        CHECK(!l2.plain.empty());

        FakeHttp h3;
        h3.on("lrclib.net", "[]");
        h3.on("lyrics.ovh", "", 404);
        Net n3(h3);
        CHECK(n3.findLyrics("Нет такой", "Никто", 0).empty());

        FakeHttp off;  // ни один сервис не отвечает
        Net n4(off);
        bool threw = false;
        try { n4.findLyrics("A", "B", 0); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    });

    run("перенос: подбор по источникам", [] {
        FakeHttp h;
        h.on("api.audius.co/v1/tracks/search", R"({"data":[{"id":"A9","title":"Numb","user":{"name":"Linkin Park"},"duration":180}]})");
        h.on("https://soundcloud.com/", "", 500);
        Net n(h);
        Library lib;
        lib.setSrc("youtube", false);
        ListItem it;
        it.title = "Numb";
        it.artist = "Linkin Park, Jay-Z";
        auto t = n.matchTrack(it, lib);
        CHECK(t.has_value());
        if (t) {
            CHECK_EQ(t->id, std::string("audius:A9"));
            CHECK_EQ(t->ptitle, std::string("Numb"));
            CHECK_EQ(t->partist, std::string("Linkin Park"));
        }
        ListItem none;
        none.title = "Совсем другая песня";
        none.artist = "Никто";
        CHECK(!n.matchTrack(none, lib).has_value());

        // YouTube как запасной вариант: если известен id видео, поиск не нужен
        Library withYt;
        ListItem v;
        v.title = "Редкая";
        v.artist = "Кто-то";
        v.vid = "abcdefghijk";
        FakeHttp h2;
        h2.on("api.audius.co/v1/tracks/search", R"({"data":[]})");
        h2.on("https://soundcloud.com/", "", 500);
        Net n2(h2);
        auto yt = n2.matchTrack(v, withYt);
        CHECK(yt.has_value() && yt->source == "youtube" && yt->ref == "abcdefghijk");
    });

    run("YouTube: прямая ссылка", [] {
        FakeHttp h;
        h.on("youtubei/v1/player", R"({"playabilityStatus":{"status":"OK"},"streamingData":{"adaptiveFormats":[
            {"mimeType":"video/mp4; codecs=\"avc1\"","url":"https://g/v","bitrate":900000},
            {"mimeType":"audio/mp4; codecs=\"mp4a.40.2\"","url":"https://g/a?x=1","bitrate":130000,"contentLength":"6000"},
            {"mimeType":"audio/webm; codecs=\"opus\"","url":"https://g/w","bitrate":160000}]}})");
        h.on("g/a?x=1&range=0-5999", std::string(6000, 'a'), 200, "audio/mp4");
        Net n(h);
        Track t = Track::make("youtube", "dQw4w9WgXcQ");
        AudioBlob b = n.fetchAudio(t);
        CHECK_EQ(b.bytes.size(), 6000u);
        CHECK_EQ(b.ext, std::string("m4a"));

        FakeHttp blocked;
        blocked.on("youtubei/v1/player", R"({"playabilityStatus":{"status":"LOGIN_REQUIRED","reason":"Нужен вход"}})");
        Net n2(blocked);
        std::string msg;
        try { n2.fetchAudio(t); } catch (const std::exception& e) { msg = e.what(); }
        CHECK(msg.find("Нужен вход") != std::string::npos);
    });

    std::printf("\nПроверок: %d, провалов: %d\n", g_all, g_fail);
    return g_fail ? 1 : 0;
}
