// Тесты ядра: без сети и без звука. Запуск: ctest или ./mxtests
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "cuts.hpp"
#include "json.hpp"
#include "library.hpp"
#include "model.hpp"
#include "parsers.hpp"
#include "queue.hpp"
#include "stats.hpp"
#include "cloudsync.hpp"
#include "share.hpp"
#include "rank.hpp"
#include "text.hpp"

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
#define NEAR(a, b) (std::abs((a) - (b)) < 1e-6)

static void run(const char* name, const std::function<void()>& f) {
    int before = g_fail;
    f();
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name);
}

static Track mkTrack(const std::string& src, const std::string& ref, const std::string& title, const std::string& artist) {
    Track t = Track::make(src, ref);
    t.title = title;
    t.artist = artist;
    return t;
}

int main() {
    run("json: разбор и запись", [] {
        Json j = Json::parse(R"({"a":[1,2.5,"x\u0416\n"],"b":{"c":true,"d":null},"e":-3e2})");
        CHECK(j["a"].isArray());
        CHECK_EQ(j["a"].size(), 3u);
        CHECK(NEAR(j["a"].at(1).asNum(), 2.5));
        CHECK_EQ(j["a"].at(2).asStr(), std::string("x\xD0\x96\n"));
        CHECK(j["b"]["c"].asBool());
        CHECK(j["b"]["d"].isNull());
        CHECK(NEAR(j["e"].asNum(), -300));
        CHECK(j["нет"].isNull());
        Json k = Json::parse(j.dump());
        CHECK_EQ(k.dump(), j.dump());
        Json e;
        CHECK(!Json::tryParse("{\"a\":", e));
        CHECK(!Json::tryParse("[1,2,]x", e));
    });

    run("json: суррогатные пары и обход", [] {
        Json j = Json::parse(R"({"s":"\ud83d\ude00","x":{"k":1,"y":[{"k":2}]}})");
        CHECK_EQ(j["s"].asStr(), std::string("\xF0\x9F\x98\x80"));
        std::vector<const Json*> out;
        walk(j, "k", out);
        CHECK_EQ(out.size(), 2u);
        CHECK(walkFirst(j, "k") != nullptr);
        CHECK(walkFirst(j, "zzz") == nullptr);
    });

    run("text: нормализация", [] {
        CHECK_EQ(nrm("  Привет,   МИР!! "), std::string("привет мир"));
        CHECK_EQ(nrm("AC/DC - Back In Black"), std::string("ac dc back in black"));
        CHECK_EQ(normTitle("Song (Remastered 2011) feat. Someone"), std::string("song"));
        CHECK_EQ(trim("  a b \t"), std::string("a b"));
        CHECK_EQ(fmtTime(75.4), std::string("1:15"));
        CHECK_EQ(fmtTime(75.46, true), std::string("1:15.5"));
        CHECK_EQ(fmtTime(-5), std::string("0:00"));
        CHECK_EQ(urlEncode("a b&я"), std::string("a%20b%26%D1%8F"));
        CHECK_EQ(urlDecode("a%20b%26%D1%8F"), std::string("a b&я"));
        auto d = splitDash("Artist - Title - Live");
        CHECK_EQ(d.size(), 3u);
        CHECK_EQ(d[1], std::string("Title"));
        CHECK_EQ(splitDash("A-B").size(), 1u);
    });

    run("cuts: слияние и переходы", [] {
        auto m = mergeCuts({{10, 20}, {19.9, 30}, {50, 50.1}, {40, 45}});
        CHECK_EQ(m.size(), 2u);
        CHECK(NEAR(m[0].s, 10) && NEAR(m[0].e, 30));
        CHECK(NEAR(m[1].s, 40));

        Segment s;
        s.cuts = m;
        CHECK_EQ(s.cutAt(5), -1);
        CHECK_EQ(s.cutAt(10), 0);
        CHECK_EQ(s.cutAt(29.97), -1);  // последние 0.05 с не считаются
        CHECK_EQ(s.cutAt(42), 1);

        Segment c0;
        c0.cuts = {{0, 5}, {5.04, 9}};
        c0.cuts = mergeCuts(c0.cuts);
        CHECK(NEAR(c0.startPos(), 9));
    });

    run("cuts: фрагмент «оставить» и «вырезать»", [] {
        Track keep = mkTrack("audius", "1", "T", "A");
        keep.clip = true; keep.mode = "keep"; keep.start = 30; keep.end = 60;
        Segment s = buildSegment(keep, {{40, 45}}, {});
        CHECK(s.hasLimit && NEAR(s.limS, 30) && NEAR(s.limE, 60));
        CHECK(NEAR(s.startPos(), 30));
        CHECK(NEAR(s.visibleBase(), 30));
        CHECK(NEAR(s.visibleLength(200), 30));

        Track cut = keep;
        cut.mode = "cut";
        Segment c = buildSegment(cut, {}, {});
        CHECK(!c.hasLimit);
        CHECK_EQ(c.cuts.size(), 1u);
        CHECK(NEAR(c.visibleLength(200), 200));
    });

    run("queue: шаги, повтор, перемешивание", [] {
        Queue q;
        q.set({"a", "b", "c"}, "b");
        CHECK_EQ(q.index, 1);
        auto all = [](const std::string&) { return true; };
        auto rnd0 = [](int) { return 0; };

        StepResult r = step(q, 1, true, all, rnd0);
        CHECK(r.kind == StepResult::Play && r.index == 2);
        r = step(q, 1, true, all, rnd0);
        CHECK(r.kind == StepResult::Stop);
        r = step(q, 1, false, all, rnd0);  // кнопка «вперёд» в конце: по кругу
        CHECK(r.kind == StepResult::Play && r.index == 0);
        r = step(q, -1, false, all, rnd0);
        CHECK(r.kind == StepResult::Play && r.index == 2);

        q.loop = Loop::All;
        q.index = 2;
        r = step(q, 1, true, all, rnd0);
        CHECK(r.kind == StepResult::Play && r.index == 0);

        q.shuffle = true;
        r = step(q, 1, true, all, [](int) { return 1; });
        CHECK(r.kind == StepResult::Play && r.index == 1);

        // нет интернета: играть можно только «b»
        q.shuffle = false; q.index = 0;
        auto onlyB = [](const std::string& id) { return id == "b"; };
        r = step(q, 1, true, onlyB, rnd0);
        CHECK(r.kind == StepResult::Play && r.index == 1);
        auto none = [](const std::string&) { return false; };
        r = step(q, 1, true, none, rnd0);
        CHECK(r.kind == StepResult::NoPlayable);

        Queue rc;
        rc.set({"x"}, "x");
        rc.randomChain = true;
        CHECK(step(rc, 1, true, all, rnd0).kind == StepResult::NeedRandom);

        CHECK(cycleLoop(Loop::Off) == Loop::All);
        CHECK(cycleLoop(Loop::One) == Loop::Off);
    });

    run("rank: оценки и дубли", [] {
        Track a = mkTrack("audius", "1", "Numb", "Linkin Park");
        Track b = mkTrack("audius", "2", "Numb (Live)", "Fan");
        Track c = mkTrack("audius", "3", "Other song", "Nobody");
        CHECK(relScore(a, "linkin park numb") > relScore(c, "linkin park numb"));
        CHECK(relScore(a, "numb") > relScore(b, "numb") - 0.01);
        CHECK(NEAR(relScore(a, ""), 0));

        CHECK(matchScore(a, "Numb", "Linkin Park, Jay-Z") >= 4.9);
        CHECK(NEAR(matchScore(c, "Numb", "Linkin Park"), 0));

        std::vector<SearchHit> hits;
        hits.push_back({mkTrack("soundcloud", "x", "Numb", "Linkin Park"), 10, 0});
        hits.push_back({a, 10, 0});
        hits.push_back({c, 3, 1});
        auto out = mergeResults(hits, {}, {});
        CHECK_EQ(out.size(), 2u);  // дубль «Numb|Linkin Park» отсечён
        CHECK_EQ(out[0].source, std::string("audius"));  // при равной оценке Audius выше
    });

    run("parsers: ссылки", [] {
        CHECK_EQ(ytVideoId("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=3"), std::string("dQw4w9WgXcQ"));
        CHECK_EQ(ytVideoId("https://youtu.be/dQw4w9WgXcQ"), std::string("dQw4w9WgXcQ"));
        CHECK_EQ(ytVideoId("https://youtube.com/shorts/dQw4w9WgXcQ"), std::string("dQw4w9WgXcQ"));
        CHECK_EQ(ytVideoId("просто текст"), std::string());
        CHECK_EQ(ytListId("https://www.youtube.com/playlist?list=PLabc_123-x"), std::string("PLabc_123-x"));
        CHECK(isScLink("https://soundcloud.com/artist/track"));
        CHECK(isScLink("https://on.soundcloud.com/AbC"));
        CHECK(!isScLink("https://evilsoundcloud.com/x"));
        SpotifyLink sl = parseSpotifyLink("https://open.spotify.com/intl-ru/playlist/37i9dQZF1DX?si=1");
        CHECK(sl.ok() && sl.kind == "playlist" && sl.id == "37i9dQZF1DX");
        sl = parseSpotifyLink("spotify:album:ABC123");
        CHECK(sl.ok() && sl.kind == "album");
        CHECK(!parseSpotifyLink("https://example.com").ok());
    });

    run("parsers: адреса для офлайна", [] {
        CHECK(isAllowedDownloadUrl("https://archive.org/download/id/file.mp3"));
        CHECK(isAllowedDownloadUrl("https://ia800.us.archive.org/download/id/a%20b.mp3"));
        CHECK(!isAllowedDownloadUrl("http://archive.org/download/id/file.mp3"));
        CHECK(!isAllowedDownloadUrl("https://127.0.0.1/download/x"));
        CHECK(!isAllowedDownloadUrl("https://localhost/v1/tracks/ab/stream"));
        CHECK(!isAllowedDownloadUrl("https://evil.example/download/x.mp3"));
        CHECK(!isAllowedDownloadUrl("file:///C:/Windows/win.ini"));
    });

    run("parsers: LRC", [] {
        auto l = parseLrc("[ti:x]\n[00:12.50] Привет\r\n[01:05] мир\n[00:01.5][00:30.25]хор\nбез времени\n");
        CHECK_EQ(l.size(), 4u);
        CHECK(NEAR(l[0].t, 1.5));
        CHECK_EQ(l[0].text, std::string("хор"));
        CHECK(NEAR(l[1].t, 12.5));
        CHECK(NEAR(l[3].t, 65));
        for (std::size_t i = 1; i < l.size(); ++i) CHECK(l[i - 1].t <= l[i].t);
    });

    run("parsers: названия для поиска текста", [] {
        CHECK_EQ(cleanTitle("Song Name (Official Video) [4K]"), std::string("Song Name"));
        CHECK_EQ(cleanTitle("Song (feat. X)"), std::string("Song (feat. X)"));
        Query q = cleanQuery("Linkin Park - Numb (Official Music Video)", "LinkinParkVEVO");
        CHECK(!q.title.empty());
        q = cleanQuery("Artist - Title", "Artist - Topic");
        CHECK_EQ(q.title, std::string("Title"));
        CHECK_EQ(q.artist, std::string("Artist"));
        q = cleanQuery("Title ft. Someone", "Main, Other");
        CHECK_EQ(q.title, std::string("Title"));
        CHECK_EQ(q.artist, std::string("Main"));
        auto v = lyricVariants("Song - 2011 Remaster", "Band");
        CHECK(!v.empty() && v.size() <= 5);
        bool hasNoArtist = false, hasPlain = false;
        for (auto& x : v) { if (x.artist.empty()) hasNoArtist = true; if (x.title == "Song") hasPlain = true; }
        CHECK(hasNoArtist);
        CHECK(hasPlain);
    });

    run("parsers: выбор текста LRCLIB", [] {
        Json arr = Json::parse(R"([
          {"trackName":"Other","artistName":"X","plainLyrics":"la"},
          {"trackName":"Numb","artistName":"Linkin Park","plainLyrics":"plain","duration":187},
          {"trackName":"Numb","artistName":"Linkin Park","syncedLyrics":"[00:01.00] a","plainLyrics":"a","duration":186}
        ])");
        const Json* best = pickBestLyrics(arr, {"Numb", "Linkin Park"}, 187);
        CHECK(best != nullptr);
        if (best) CHECK(!(*best)["syncedLyrics"].isNull());
        CHECK(pickBestLyrics(arr, {"Nothing here", "Zed"}, 100) == nullptr);
        Lyrics ly = shapeLyrics(*best);
        CHECK_EQ(ly.lines.size(), 1u);
        CHECK_EQ(ly.src, std::string("LRCLIB"));
        CHECK_EQ(lyricsOvhText(Json::parse(R"({"lyrics":"Paroles de la chanson X par Y\nпервая строка достаточно длинного текста песни для проверки"})")),
                 std::string("первая строка достаточно длинного текста песни для проверки"));
        CHECK(lyricsOvhText(Json::parse(R"({"lyrics":"коротко"})")).empty());
    });

    run("parsers: YouTube и Spotify", [] {
        std::string yt =
            "<html><script>var ytInitialData = {\"contents\":{\"a\":[{\"videoRenderer\":{\"videoId\":\"dQw4w9WgXcQ\","
            "\"title\":{\"runs\":[{\"text\":\"Never Gonna \"},{\"text\":\"Give You Up\"}]},"
            "\"ownerText\":{\"runs\":[{\"text\":\"Rick Astley - Topic\"}]},"
            "\"thumbnail\":{\"thumbnails\":[{\"url\":\"http://s/1.jpg\"},{\"url\":\"http://s/2.jpg\"}]}}},"
            "{\"videoRenderer\":{\"title\":{\"simpleText\":\"без id\"}}}]}};</script></html>";
        auto r = parseYtSearch(yt);
        CHECK_EQ(r.size(), 1u);
        if (!r.empty()) {
            CHECK_EQ(r[0].id, std::string("youtube:dQw4w9WgXcQ"));
            CHECK_EQ(r[0].title, std::string("Never Gonna Give You Up"));
            CHECK_EQ(r[0].artist, std::string("Rick Astley"));
            CHECK_EQ(r[0].cover, std::string("http://s/2.jpg"));
        }
        bool threw = false;
        try { parseYtSearch("<html>ничего</html>"); } catch (const std::exception&) { threw = true; }
        CHECK(threw);

        std::string ytm =
            R"({"contents":{"a":[{"musicResponsiveListItemRenderer":{"playlistItemData":{"videoId":"Ib91lUpV7EM"},)"
            R"("thumbnail":{"musicThumbnailRenderer":{"thumbnail":{"thumbnails":[{"url":"http://s/a.jpg"},{"url":"http://s/b.jpg"}]}}},)"
            R"("flexColumns":[{"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Love In Your Eyes"}]}}},)"
            R"({"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Lee"},{"text":" & "},{"text":"Kim"},{"text":" \u2022 "},)"
            R"({"text":"Lo-fi of Lee"},{"text":" \u2022 "},{"text":"3:51"}]}}}]}},)"
            R"({"musicResponsiveListItemRenderer":{"flexColumns":[]}}]}})";
        auto ym = parseYtmSearch(ytm);
        CHECK_EQ(ym.size(), 1u);
        if (!ym.empty()) {
            CHECK_EQ(ym[0].id, std::string("youtube:Ib91lUpV7EM"));
            CHECK_EQ(ym[0].title, std::string("Love In Your Eyes"));
            CHECK_EQ(ym[0].artist, std::string("Lee & Kim"));
            CHECK_EQ(ym[0].dur, 231.0);
            CHECK_EQ(ym[0].cover, std::string("http://s/b.jpg"));
        }
        bool threwM = false;
        try { parseYtmSearch("<html>ничего</html>"); } catch (const std::exception&) { threwM = true; }
        CHECK(threwM);

        // исполнитель: имя из «A, B & C feat. D»
        CHECK_EQ(firstArtist("Lee & Kim"), std::string("Lee"));
        CHECK_EQ(firstArtist("Lee, Kim"), std::string("Lee"));
        CHECK_EQ(firstArtist("Lee Feat. Kim"), std::string("Lee"));
        CHECK_EQ(firstArtist("Lee x Kim"), std::string("Lee"));
        CHECK_EQ(firstArtist("Max Rox"), std::string("Max Rox"));  // « x» внутри слова не режет
        CHECK_EQ(firstArtist("AC/DC"), std::string("AC/DC"));
        CHECK_EQ(firstArtist(""), std::string(""));

        // поиск исполнителя: берётся тот, чьё имя совпало, не просто первый
        std::string ytmA =
            R"({"contents":{"a":[)"
            R"({"musicResponsiveListItemRenderer":{"navigationEndpoint":{"browseEndpoint":{"browseId":"UCaaa"}},"flexColumns":[{"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Arcane"}]}}}]}},)"
            R"({"musicResponsiveListItemRenderer":{"navigationEndpoint":{"browseEndpoint":{"browseId":"UCbbb"}},"thumbnail":{"musicThumbnailRenderer":{"thumbnail":{"thumbnails":[{"url":"http://s/a=w60-h60-p-l90-rj"},{"url":"http://s/a=w120-h120-p-l90-rj"}]}}},"flexColumns":[{"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Imagine Dragons"}]}}}]}},)"
            R"({"musicResponsiveListItemRenderer":{"navigationEndpoint":{"browseEndpoint":{"browseId":"MPREb_x"}},"flexColumns":[{"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Альбом"}]}}}]}}]}})";
        CHECK_EQ(parseYtmArtistSearch(ytmA, "imagine dragons").id, std::string("UCbbb"));
        CHECK_EQ(parseYtmArtistSearch(ytmA, "imagine dragons").cover, std::string("http://s/a=w400-h400-p-l90-rj"));
        CHECK_EQ(parseYtmArtistSearch(ytmA, "Нет такого").id, std::string("UCaaa"));
        CHECK_EQ(parseYtmArtistSearch(ytmA, "Нет такого", true).id, std::string(""));  // строго: только точное имя
        CHECK_EQ(parseYtmArtistSearch(ytmA, "IMAGINE DRAGONS", true).id, std::string("UCbbb"));
        CHECK_EQ(parseYtmArtistSearch(R"({"contents":{}})", "x").id, std::string(""));

        // страница исполнителя: заголовок, песни, альбомы и «Все …»
        std::string ytmP =
            R"({"contents":{"h":{"musicImmersiveHeaderRenderer":{"title":{"runs":[{"text":"Imagine Dragons"}]},)"
            R"("thumbnail":{"musicThumbnailRenderer":{"thumbnail":{"thumbnails":[{"url":"http://s/art.jpg"}]}}}}},)"
            R"("s":{"musicShelfRenderer":{"bottomEndpoint":{"browseEndpoint":{"browseId":"VLOLAKtop"}},"contents":[)"
            R"({"musicResponsiveListItemRenderer":{"playlistItemData":{"videoId":"Kx7B-XvmFtE"},"flexColumns":[)"
            R"({"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Believer"}]}}},)"
            R"({"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Imagine Dragons"}]}}}],)"
            R"("fixedColumns":[{"musicResponsiveListItemFixedColumnRenderer":{"text":{"runs":[{"text":"3:25"}]}}}]}}]}},)"
            R"("c1":{"musicCarouselShelfRenderer":{"header":{"musicCarouselShelfBasicHeaderRenderer":{"title":{"runs":[{"text":"Albums"}]}}},"contents":[)"
            R"({"musicTwoRowItemRenderer":{"title":{"runs":[{"text":"Evolve"}]},"subtitle":{"runs":[{"text":"2017"}]},)"
            R"("navigationEndpoint":{"browseEndpoint":{"browseId":"MPREb_evolve"}},)"
            R"("thumbnailRenderer":{"musicThumbnailRenderer":{"thumbnail":{"thumbnails":[{"url":"http://s/e1.jpg"},{"url":"http://s/e2.jpg"}]}}}}},)"
            R"({"musicTwoRowItemRenderer":{"title":{"runs":[{"text":"Не альбом"}]},"navigationEndpoint":{"browseEndpoint":{"browseId":"UCxxx"}}}}]}},)"
            R"("c2":{"musicCarouselShelfRenderer":{"header":{"musicCarouselShelfBasicHeaderRenderer":{"title":{"runs":[{"text":"Singles & EPs"}]},)"
            R"("moreContentButton":{"buttonRenderer":{"navigationEndpoint":{"browseEndpoint":{"browseId":"MPADUCbbb","params":"ggM"}}}}}},"contents":[)"
            R"({"musicTwoRowItemRenderer":{"title":{"runs":[{"text":"Thunder"}]},"subtitle":{"runs":[{"text":"Single • 2017"}]},)"
            R"("navigationEndpoint":{"browseEndpoint":{"browseId":"MPREb_thunder"}}}}]}},)"
            R"("c3":{"musicCarouselShelfRenderer":{"header":{"musicCarouselShelfBasicHeaderRenderer":{"title":{"runs":[{"text":"Videos"}]}}},"contents":[]}}}})";
        YtmArtistPage ap = parseYtmArtist(ytmP);
        CHECK_EQ(ap.name, std::string("Imagine Dragons"));
        CHECK_EQ(ap.cover, std::string("http://s/art.jpg"));
        CHECK_EQ(ap.songsId, std::string("VLOLAKtop"));
        CHECK_EQ(ap.songs.size(), 1u);
        if (!ap.songs.empty()) {
            CHECK_EQ(ap.songs[0].id, std::string("youtube:Kx7B-XvmFtE"));
            CHECK_EQ(ap.songs[0].artist, std::string("Imagine Dragons"));
            CHECK_EQ(ap.songs[0].dur, 205.0);
        }
        CHECK_EQ(ap.albums.size(), 1u);  // карточка со ссылкой на исполнителя не считается альбомом
        if (!ap.albums.empty()) {
            CHECK_EQ(ap.albums[0].id, std::string("MPREb_evolve"));
            CHECK_EQ(ap.albums[0].title, std::string("Evolve"));
            CHECK_EQ(ap.albums[0].cover, std::string("http://s/e2.jpg"));
        }
        CHECK_EQ(ap.singles.size(), 1u);
        CHECK_EQ(ap.singlesId, std::string("MPADUCbbb"));
        CHECK_EQ(ap.singlesParams, std::string("ggM"));
        CHECK(ap.albumsId.empty());
        bool threwA = false;
        try { parseYtmArtist("<html>ничего</html>"); } catch (const std::exception&) { threwA = true; }
        CHECK(threwA);
        CHECK_EQ(parseYtmAlbums(ytmP).size(), 2u);

        // альбом: у строк нет исполнителя и обложки — берутся из заголовка; повтор одной песни отбрасывается
        std::string ytmAl =
            R"({"contents":{"h":{"musicResponsiveHeaderRenderer":{"title":{"runs":[{"text":"Evolve"}]},"subtitle":{"runs":[{"text":"Album"},{"text":" • "},{"text":"2017"}]},)"
            R"("straplineTextOne":{"runs":[{"text":"Imagine Dragons - Topic"}]},)"
            R"("thumbnail":{"musicThumbnailRenderer":{"thumbnail":{"thumbnails":[{"url":"http://s/cov.jpg"}]}}}}},"l":[)"
            R"({"musicResponsiveListItemRenderer":{"playlistItemData":{"videoId":"aaaaaaaaaaa"},"flexColumns":[)"
            R"({"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Thunder"}]}}},)"
            R"({"musicResponsiveListItemFlexColumnRenderer":{}}],)"
            R"("fixedColumns":[{"musicResponsiveListItemFixedColumnRenderer":{"text":{"runs":[{"text":"3:08"}]}}}]}},)"
            R"({"musicResponsiveListItemRenderer":{"playlistItemData":{"videoId":"aaaaaaaaaaa"},"flexColumns":[{"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Thunder"}]}}}]}},)"
            R"({"musicResponsiveListItemRenderer":{"flexColumns":[{"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Недоступна"}]}}}]}}]}})";
        YtmAlbumPage al = parseYtmAlbum(ytmAl);
        CHECK_EQ(al.title, std::string("Evolve"));
        CHECK_EQ(al.artist, std::string("Imagine Dragons"));
        CHECK_EQ(al.year, std::string("2017"));
        CHECK_EQ(al.tracks.size(), 1u);
        if (!al.tracks.empty()) {
            CHECK_EQ(al.tracks[0].artist, std::string("Imagine Dragons"));
            CHECK_EQ(al.tracks[0].cover, std::string("http://s/cov.jpg"));
            CHECK_EQ(al.tracks[0].dur, 188.0);
        }

        std::string pl =
            "<script>var ytInitialData = {\"header\":{\"playlistHeaderRenderer\":{\"title\":{\"simpleText\":\"Мой микс\"}}},"
            "\"x\":[{\"playlistVideoRenderer\":{\"videoId\":\"aaaaaaaaaaa\",\"title\":{\"simpleText\":\"A - B\"},"
            "\"shortBylineText\":{\"runs\":[{\"text\":\"Chan\"}]}}}]};</script>";
        PlaylistInfo pi = parseYtPlaylist(pl);
        CHECK_EQ(pi.name, std::string("Мой микс"));
        CHECK_EQ(pi.items.size(), 1u);

        std::string sp =
            "<script id=\"__NEXT_DATA__\" type=\"application/json\">{\"props\":{\"entity\":{\"name\":\"Плейлист\"},"
            "\"trackList\":[{\"title\":\"Song\",\"subtitle\":\"Band\"},{\"title\":\"\",\"subtitle\":\"x\"}]}}</script>";
        PlaylistInfo si = parseSpEmbed(sp);
        CHECK_EQ(si.name, std::string("Плейлист"));
        CHECK_EQ(si.items.size(), 1u);
        if (!si.items.empty()) CHECK_EQ(si.items[0].artist, std::string("Band"));
    });

    run("parsers: Audius, SoundCloud, Archive", [] {
        Json au = Json::parse(R"({"data":[
          {"id":"A1","title":"One","duration":120,"user":{"name":"U"},"artwork":{"480x480":"http://c/1.jpg"}},
          {"id":"A2","title":"Two","is_streamable":false}]})");
        auto at = parseAudiusTracks(au);
        CHECK_EQ(at.size(), 1u);
        if (!at.empty()) {
            CHECK_EQ(at[0].id, std::string("audius:A1"));
            CHECK_EQ(at[0].artist, std::string("U"));
            CHECK(NEAR(at[0].dur, 120));
        }

        std::string html = R"(<script crossorigin src="https://a-v2.sndcdn.com/assets/0-abc.js"></script><script src="https://a-v2.sndcdn.com/assets/50-def.js"></script><script src="https://other.com/x.js"></script>)";
        auto urls = scScriptUrls(html);
        CHECK_EQ(urls.size(), 2u);
        CHECK_EQ(scClientId("a={client_id:\"abcdefghijklmnopqrstuvwxyz012345\",b:1}"), std::string("abcdefghijklmnopqrstuvwxyz012345"));
        CHECK_EQ(scClientId("x?client_id=abcdefghijklmnopqrstuvwxyz012345&y"), std::string("abcdefghijklmnopqrstuvwxyz012345"));
        CHECK(scClientId("nothing").empty());

        Json sc = Json::parse(R"({"collection":[
          {"kind":"track","permalink_url":"https://soundcloud.com/a/b","title":"B","user":{"username":"A"},"duration":90000,"artwork_url":"http://i/x-large.jpg"},
          {"kind":"playlist","permalink_url":"https://soundcloud.com/a/sets/p","title":"P"},
          {"kind":"track","permalink_url":"https://soundcloud.com/a/c","title":"C","policy":"SNIP"}]})");
        auto st = parseScTracks(sc);
        CHECK_EQ(st.size(), 1u);
        if (!st.empty()) {
            CHECK(NEAR(st[0].dur, 90));
            CHECK(st[0].cover.find("t300x300") != std::string::npos);
        }
        Json tr = Json::parse(R"({"track_authorization":"AUTH","media":{"transcodings":[
          {"url":"https://api-v2.soundcloud.com/media/x/hls","format":{"protocol":"hls","mime_type":"audio/mpeg"}},
          {"url":"https://api-v2.soundcloud.com/media/x/prog","format":{"protocol":"progressive","mime_type":"audio/mpeg"}}]}})");
        auto tcs = scTranscodings(tr);
        CHECK(!tcs.empty() && tcs.front().progressive && tcs.front().auth == "AUTH");

        CHECK_EQ(songsCount(1), std::string("1 песня"));
        CHECK_EQ(songsCount(3), std::string("3 песни"));
        CHECK_EQ(songsCount(12), std::string("12 песен"));
        CHECK_EQ(songsCount(21), std::string("21 песня"));
        CHECK_EQ(songsCount(25), std::string("25 песен"));
        CHECK(NEAR(lenSec(Json("3:25")), 205));
        CHECK(NEAR(lenSec(Json("1:02:03")), 3723));
        CHECK(NEAR(lenSec(Json("12.5")), 12.5));
        Json doc = Json::parse(R"({"identifier":"id1","title":"Album","creator":"Band"})");
        Json meta = Json::parse(R"({"files":[
          {"name":"d/01 a.mp3","format":"VBR MP3","length":"200.5","title":"First"},
          {"name":"d/02 b.mp3","format":"VBR MP3","length":"5"},
          {"name":"d/03 c.ogg","format":"Ogg Vorbis","length":"100"}]})");
        auto ia = parseIaItem(doc, meta);
        CHECK_EQ(ia.size(), 1u);  // mp3 предпочтительнее, короткие (<20 с) отброшены
        if (!ia.empty()) {
            CHECK_EQ(ia[0].title, std::string("First"));
            CHECK(ia[0].url.find("d/01%20a.mp3") != std::string::npos);
            CHECK(isAllowedDownloadUrl(ia[0].url));
        }
    });

    run("parsers: списки и CSV", [] {
        auto row = csvRow("\"a, b\",\"he said \"\"hi\"\"\",c");
        CHECK_EQ(row.size(), 3u);
        CHECK_EQ(row[0], std::string("a, b"));
        CHECK_EQ(row[1], std::string("he said \"hi\""));
        std::string csv =
            "Track URI,Track Name,Artist Name(s),Album Name\n"
            "spotify:track:1,\"Song, One\",A;B,Alb\n"
            "spotify:track:2,Two,C,Alb\n";
        auto items = parseList(csv);
        CHECK_EQ(items.size(), 2u);
        if (items.size() == 2) {
            CHECK_EQ(items[0].title, std::string("Song, One"));
            CHECK_EQ(items[0].artist, std::string("A, B"));
        }
        auto lines = parseList("Queen - Bohemian Rhapsody\n\nJust a title\n");
        CHECK_EQ(lines.size(), 2u);
        if (lines.size() == 2) {
            CHECK_EQ(lines[0].artist, std::string("Queen"));
            CHECK_EQ(lines[1].artist, std::string());
        }
    });

    run("library: треки, вырезы, плейлисты", [] {
        Library lib;
        Track t = mkTrack("audius", "42", "Song", "Band");
        lib.add(t);
        CHECK(lib.has("audius:42"));
        Track& again = lib.add(t);
        CHECK_EQ(lib.tracks.size(), 1u);
        CHECK(again.added > 0);

        Track clip = mkTrack("audius", "42", "Song", "Band");
        clip.id = "clip1"; clip.clip = true; clip.mode = "keep"; clip.start = 10; clip.end = 20; clip.parent = "audius:42";
        lib.add(clip);
        CHECK_EQ(lib.tracks.size(), 2u);

        const Track* base = lib.find("audius:42");
        lib.addCut(*base, {30, 40});
        lib.addCut(*base, {35, 50});
        auto list = lib.cutList(*base);
        CHECK_EQ(list.size(), 1u);  // пересекающиеся вырезы склеиваются при добавлении
        Segment seg = lib.segmentFor(*base);
        CHECK_EQ(seg.cuts.size(), 1u);  // слились
        CHECK(NEAR(seg.cuts[0].e, 50));

        Playlist& pl = lib.createPlaylist("Мой");
        pl.ids = {"audius:42", "clip1"};
        auto gone = lib.remove("audius:42");
        CHECK_EQ(gone.size(), 2u);  // трек и его клип
        CHECK(lib.tracks.empty());
        CHECK(lib.playlists[0].ids.empty());
        CHECK(lib.removePlaylist(lib.playlists[0].id));
        CHECK(lib.playlists.empty());
    });

    run("library: сохранение, загрузка, резервная копия", [] {
        std::string path = "mx_test_library.json";
        std::remove(path.c_str());
        std::remove((path + ".bak").c_str());
        Library lib;
        lib.add(mkTrack("audius", "1", "Один", "Исп"));
        lib.add(mkTrack("local", "file1", "Мой файл", "Файл на устройстве"));
        lib.createPlaylist("Лист").ids = {"audius:1"};
        lib.set("theme", Json("ocean"));
        lib.setSrc("youtube", false);
        lib.lyOff["a|b"] = 1.5;
        Lyrics ly; ly.plain = "текст"; ly.name = "x";
        lib.lyrics["a|b"] = ly;
        std::string err;
        CHECK(lib.save(path, &err));
        CHECK(lib.save(path, &err));  // второй раз создаёт .bak
        std::string bak;
        CHECK(readFile(path + ".bak", bak));

        Library back;
        CHECK(back.load(path, &err));
        CHECK_EQ(back.tracks.size(), 2u);
        CHECK_EQ(back.playlists.size(), 1u);
        CHECK_EQ(back.getStr("theme", ""), std::string("ocean"));
        CHECK(!back.srcOn("youtube"));
        CHECK(back.srcOn("audius"));
        CHECK(NEAR(back.lyOff["a|b"], 1.5));
        CHECK_EQ(back.lyrics["a|b"].plain, std::string("текст"));

        // резервная копия: добавляет недостающее, настройки не трогает, локальные файлы пропускает
        Library other;
        other.add(mkTrack("audius", "1", "Один", "Исп"));
        other.add(mkTrack("audius", "2", "Два", "Исп"));
        other.add(mkTrack("local", "file2", "Чужой файл", ""));
        other.createPlaylist("Новый").ids = {"audius:2"};
        other.set("theme", Json("gold"));
        Library::MergeStats ms = back.mergeBackup(other.toJson());
        CHECK(ms.ok);
        CHECK_EQ(ms.tracks, 1);
        CHECK_EQ(ms.playlists, 1);
        CHECK(back.has("audius:2"));
        CHECK(!back.has("local:file2"));
        CHECK_EQ(back.getStr("theme", ""), std::string("ocean"));

        // перенос из папки старой версии: свои файлы и офлайн-метки остаются
        Library oldLib;
        Track off1 = mkTrack("audius", "9", "Скачанная", "Исп");
        off1.off = true;
        off1.offSize = 1234;
        oldLib.add(off1);
        oldLib.add(mkTrack("local", "mine.mp3", "Моя запись", ""));
        Library fresh;
        Library::MergeStats mk = fresh.mergeBackup(oldLib.toJson(), true);
        CHECK(mk.ok);
        CHECK_EQ(mk.tracks, 2);
        CHECK(fresh.has("local:mine.mp3"));
        CHECK(fresh.find("audius:9") && fresh.find("audius:9")->off && fresh.find("audius:9")->offSize == 1234);

        Library broken;
        CHECK(!broken.load("нет_такого_файла.json", &err));
        std::remove(path.c_str());
        std::remove((path + ".bak").c_str());
    });

    run("статистика: время, прослушивания, топ, сохранение", [] {
        ListenStats st;
        Track a = Track::make("soundcloud", "https://soundcloud.com/x/a");
        a.title = "A";
        a.artist = "Band";
        Track b = Track::make("audius", "B1");
        b.title = "B";
        b.artist = "band ";  // тот же исполнитель, другое написание
        double sa = 0, sb = 0;
        int counted = 0;
        for (int i = 0; i < 70; ++i) counted += st.add(a, 1.0, "2026-11-24", sa) ? 1 : 0;
        CHECK_EQ(counted, 1);  // одно включение — одно прослушивание, хоть и 70 секунд
        sa = 0;                // включили заново
        for (int i = 0; i < 31; ++i) st.add(a, 1.0, "2026-11-25", sa);
        for (int i = 0; i < 10; ++i) st.add(b, 1.0, "2026-11-25", sb);
        CHECK(!st.add(b, 100.0, "2026-11-25", sb));  // скачок времени (сон компьютера) не считается
        CHECK(NEAR(st.totalSec(), 111));
        CHECK(NEAR(st.daySec("2026-11-25"), 41));
        CHECK_EQ(st.playCount(), 2);
        auto top = st.topTracks(5);
        CHECK_EQ(top.size(), 2u);
        if (!top.empty()) CHECK_EQ(top[0]->track.title, std::string("A"));
        auto ar = st.topArtists(5);
        CHECK_EQ(ar.size(), 1u);  // Band и «band » — один исполнитель
        if (!ar.empty()) CHECK(NEAR(ar[0].sec, 111));
        ListenStats back;
        back.fromJson(Json::parse(st.toJson().dump()));
        CHECK(NEAR(back.totalSec(), 111));
        CHECK_EQ(back.playCount(), 2);
        CHECK_EQ(back.topTracks(1).front()->track.id, a.id);
        CHECK_EQ(localDay().size(), 10u);
    });

    run("профиль аккаунта: base64, никнейм, сохранение", [] {
        std::string raw;
        for (int i = 0; i < 256; ++i) raw += static_cast<char>(i);
        for (std::size_t n : {0u, 1u, 2u, 3u, 100u, 256u}) {
            std::string back;
            CHECK(base64Decode(base64Encode(raw.substr(0, n)), back));
            CHECK(back == raw.substr(0, n));
        }
        CHECK_EQ(base64Encode("Man"), std::string("TWFu"));
        std::string bad;
        CHECK(!base64Decode("ab$c", bad));

        Profile p;
        CHECK_EQ(displayName(p, "teto@mail.ru"), std::string("teto"));
        p.name = "  Тето  ";
        CHECK_EQ(displayName(p, "teto@mail.ru"), std::string("Тето"));
        CHECK_EQ(clipChars("Привет", 3), std::string("При"));
        CHECK_EQ(clipChars("ab", 5), std::string("ab"));

        p.status = "слушаю всё";
        p.color = 3;
        p.frame = 2;
        p.avatar = base64Encode(std::string(static_cast<std::size_t>(Profile::kAvatarSide) * Profile::kAvatarSide * 3u, '\x40'));
        p.updated = 1700000000123LL;
        Profile q = Profile::fromJson(p.toJson());
        CHECK_EQ(q.name, p.name);
        CHECK_EQ(q.status, p.status);
        CHECK_EQ(q.color, 3);
        CHECK_EQ(q.frame, 2);
        CHECK(q.avatar == p.avatar);
        CHECK(q.updated == p.updated);
        // испорченная аватарка и чужие значения не ломают профиль
        Json j = p.toJson();
        j.set("avatar", Json(std::string("QUJD")));
        j.set("color", Json(99.0));
        Profile r = Profile::fromJson(j);
        CHECK(r.avatar.empty());
        CHECK_EQ(r.color, 7);
        CHECK_EQ(Profile::fromJson(Json()).frame, 1);
        p.frame = Profile::kFrameCount - 1;
        CHECK_EQ(Profile::fromJson(p.toJson()).frame, Profile::kFrameCount - 1);
        p.frame = 99;
        CHECK_EQ(Profile::fromJson(p.toJson()).frame, Profile::kFrameCount - 1);  // лишнее обрезается
    });

    run("аккаунт: что уходит в облако, замена и объединение", [] {
        Library pc;
        Track a = Track::make("soundcloud", "https://soundcloud.com/x/a");
        a.title = "A";
        a.off = true;
        a.offSize = 5000;
        Track f = Track::make("local", "f1.mp3");
        f.title = "Мой файл";
        Track b = Track::make("audius", "B1");
        b.title = "B";
        pc.add(a);
        pc.add(f);
        pc.add(b);
        pc.tracks[a.id].off = true;
        pc.tracks[a.id].offSize = 5000;
        Playlist& pl = pc.createPlaylist("Любимое");
        pl.ids = {a.id, f.id};
        pc.set("theme", Json(std::string("ocean")));
        pc.set("uiScale", Json(1.25));

        // в облако: без своего файла, без отметки офлайна и без настроек устройства
        Json up = cloudPayload(pc);
        CHECK(!up["tracks"].has(f.id));
        CHECK(up["tracks"].has(a.id));
        CHECK(!up["tracks"][a.id]["off"].asBool(false));
        CHECK(up["settings"].has("theme"));
        CHECK(!up["settings"].has("uiScale"));
        CHECK_EQ(up["playlists"].at(0)["ids"].size(), 1u);

        // телефон: в облаке песню B удалили, добавили C и сменили тему — облако главнее
        Library phone;
        CHECK(phone.fromJson(up));
        phone.remove(b.id);
        Track c = Track::make("soundcloud", "https://soundcloud.com/x/c");
        c.title = "C";
        phone.add(c);
        phone.playlists[0].ids.push_back(c.id);
        phone.set("theme", Json(std::string("rose")));
        Json fromPhone = cloudPayload(phone);

        Library pc2 = pc;
        CHECK(applyCloud(pc2, fromPhone, false));
        CHECK(!pc2.has(b.id));                        // удаление перенеслось
        CHECK(pc2.has(c.id));
        CHECK(pc2.has(f.id));                         // свой файл остался
        CHECK(pc2.find(a.id) && pc2.find(a.id)->off); // офлайн-копия на ПК осталась
        CHECK_EQ(pc2.getStr("theme", ""), std::string("rose"));
        CHECK(std::abs(pc2.getNum("uiScale", 0) - 1.25) < 1e-9);  // настройка устройства не тронута
        CHECK_EQ(pc2.playlists.size(), 1u);
        if (!pc2.playlists.empty()) CHECK_EQ(pc2.playlists[0].ids.size(), 3u);  // a, c и свой файл f

        // изменения с обеих сторон — объединяем, ничего не теряя
        Library pc3 = pc;
        Track d = Track::make("audius", "D1");
        d.title = "D";
        pc3.add(d);
        CHECK(applyCloud(pc3, fromPhone, true));
        CHECK(pc3.has(b.id) && pc3.has(c.id) && pc3.has(d.id) && pc3.has(f.id));
        CHECK_EQ(pc3.getStr("theme", ""), std::string("ocean"));
        CHECK(std::find(pc3.playlists[0].ids.begin(), pc3.playlists[0].ids.end(), c.id) != pc3.playlists[0].ids.end());
        CHECK(!applyCloud(pc3, Json::parse("{\"x\":1}"), false));
    });


    run("ссылки: друзья и рекомендации без сервера", [] {
        ShareFriend f;
        f.id = "k3x9";
        f.name = "Алиса";
        f.status = "слушаю джаз";
        f.color = 3;
        f.frame = 9;
        std::string link = makeFriendLink(f);
        CHECK(link.rfind("https://github.com/Lorine-ui/Mixtape#mx-", 0) == 0);
        CHECK(link.substr(link.find("#mx-") + 4).find_first_of("+/= ") == std::string::npos);  // хвост без символов, ломающих ссылку
        ShareParsed p = parseShareText("Привет! Добавь меня в Mixtape: " + link + " жду!");
        CHECK(p.kind == ShareKind::Friend);
        CHECK_EQ(p.person.id, std::string("k3x9"));
        CHECK_EQ(p.person.name, std::string("Алиса"));
        CHECK_EQ(p.person.status, std::string("слушаю джаз"));
        CHECK_EQ(p.person.color, 3);
        CHECK_EQ(p.person.frame, 9);

        ShareSong s;
        s.fromId = "k3x9";
        s.fromName = "Алиса";
        s.note = "тебе зайдёт";
        s.track = Track::make("youtube", "Kx7B-XvmFtE");
        s.track.title = "Believer";
        s.track.artist = "Imagine Dragons";
        s.track.dur = 205;
        s.track.off = true;  // чужие офлайн-метки не передаются
        ShareParsed q = parseShareText(makeSongLink(s));
        CHECK(q.kind == ShareKind::Song);
        CHECK_EQ(q.song.track.id, std::string("youtube:Kx7B-XvmFtE"));
        CHECK_EQ(q.song.track.title, std::string("Believer"));
        CHECK_EQ(q.song.fromName, std::string("Алиса"));
        CHECK_EQ(q.song.note, std::string("тебе зайдёт"));
        CHECK(!q.song.track.off);

        s.track = Track::make("local", "file.mp3");
        s.track.title = "Свой файл";
        CHECK(parseShareText(makeSongLink(s)).kind == ShareKind::None);  // файл только у отправителя

        CHECK(parseShareText("").kind == ShareKind::None);
        CHECK(parseShareText("просто текст #mx-").kind == ShareKind::None);
        CHECK(parseShareText("https://x.y#mx-!!!!").kind == ShareKind::None);
        CHECK(parseShareText(link.substr(0, link.size() - 7)).kind == ShareKind::None);  // обрезанная ссылка
        std::string raw;
        CHECK(base64UrlDecode(base64UrlEncode("\xFB\xFF\xFE"), raw) && raw == "\xFB\xFF\xFE");
    });

    std::printf("\nПроверок: %d, провалов: %d\n", g_all, g_fail);
    return g_fail ? 1 : 0;
}
