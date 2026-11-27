package com.mixtape.app;

import android.content.Context;
import android.content.SharedPreferences;

import com.yausername.youtubedl_android.YoutubeDL;
import com.yausername.youtubedl_android.YoutubeDLRequest;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.time.LocalDate;
import java.time.temporal.ChronoUnit;

// yt-dlp внутри приложения. YouTube не отдаёт песню целиком обычным способом, а yt-dlp умеет: библиотека
// youtubedl-android вшивает в APK Python, yt-dlp и движок QuickJS (он нужен yt-dlp для разбора YouTube).
// Вызывается из C++ (android_main.cpp) через MainActivity.ytFetch на рабочем потоке; любая неудача возвращается
// текстом «ERR:...» (с настоящей причиной), и тогда приложение ищет ту же песню в других источниках.
//
// Вшитый yt-dlp быстро устаревает: YouTube меняет защиту, и старая версия получает «403 Forbidden». Поэтому
// если вшитая/скачанная версия старше 45 дней, перед первой песней она обновляется (не чаще раза в 6 часов),
// а если обновления не вышло через GitHub API (у него жёсткие лимиты), качается прямая ссылка на последний выпуск.
final class YtDlp {
    private static final String PREFS = "mixtape_ytdlp";
    private static final long UPDATE_EVERY_MS = 6L * 60 * 60 * 1000;
    private static final long STALE_DAYS = 45;
    private static final String BUNDLED_VERSION = "2025.11.12";  // версия yt-dlp, вшитая в библиотеку 0.18.1
    private static final String DIRECT_URL = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp";

    private static boolean ready;
    private static boolean initFailed;
    private static String initError = "";
    private static volatile String updateNote = "";  // что получилось при последнем обновлении (для текста ошибки)

    private YtDlp() {}

    private static synchronized boolean init(Context c) {
        if (ready) return true;
        if (initFailed) return false;
        try {
            YoutubeDL.getInstance().init(c);  // при первом запуске распаковывает Python (несколько секунд)
            ready = true;
        } catch (Throwable t) {
            initFailed = true;
            initError = String.valueOf(t.getMessage());
        }
        return ready;
    }

    private static boolean stale(Context c) {
        String v = null;
        try {
            v = YoutubeDL.getInstance().version(c);
        } catch (Throwable ignored) {
        }
        if (v == null || v.isEmpty()) v = BUNDLED_VERSION;
        try {
            String[] p = v.split("\\.");  // 2026.11.23
            LocalDate d = LocalDate.of(Integer.parseInt(p[0]), Integer.parseInt(p[1]), Integer.parseInt(p[2]));
            return ChronoUnit.DAYS.between(d, LocalDate.now()) > STALE_DAYS;
        } catch (Throwable t) {
            return true;
        }
    }

    // Обновлять не чаще, чем раз в 6 часов; отметка времени ставится сразу, чтобы не долбить сеть при неудаче.
    private static boolean updateDue(Context c) {
        SharedPreferences sp = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long now = System.currentTimeMillis();
        if (now - sp.getLong("lastUpdate", 0) < UPDATE_EVERY_MS) return false;
        sp.edit().putLong("lastUpdate", now).apply();
        return true;
    }

    // true — теперь стоит повторить попытку (yt-dlp обновился).
    private static boolean update(Context c) {
        try {  // 1) встроенное обновление библиотеки (через GitHub API)
            YoutubeDL.getInstance().updateYoutubeDL(c, YoutubeDL.UpdateChannel._STABLE);
            updateNote = "";
            return true;
        } catch (Throwable t) {
            updateNote = "update: " + brief(String.valueOf(t.getMessage()));
        }
        File tmp = new File(c.getCacheDir(), "yt-dlp.download");
        try {  // 2) запасной путь: прямая ссылка на последний выпуск, без GitHub API
            File dir = new File(new File(c.getNoBackupFilesDir(), YoutubeDL.baseName), YoutubeDL.ytdlpDirName);
            dir.mkdirs();
            HttpURLConnection h = (HttpURLConnection) new URL(DIRECT_URL).openConnection();
            h.setConnectTimeout(15000);
            h.setReadTimeout(60000);
            h.setInstanceFollowRedirects(true);
            if (h.getResponseCode() != 200) throw new RuntimeException("HTTP " + h.getResponseCode());
            try (InputStream in = h.getInputStream(); FileOutputStream fo = new FileOutputStream(tmp)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) fo.write(buf, 0, n);
            }
            if (tmp.length() < 1000000) throw new RuntimeException("файл слишком мал");
            File out = new File(dir, YoutubeDL.ytdlpBin);
            if (out.exists()) out.delete();
            if (!tmp.renameTo(out)) throw new RuntimeException("не удалось заменить файл");
            updateNote += " | direct: ok";
            return true;
        } catch (Throwable t) {
            updateNote += " | direct: " + brief(String.valueOf(t.getMessage()));
            tmp.delete();
            return false;
        }
    }

    // Из длинного вывода yt-dlp оставляем суть: с первого «ERROR:», без переносов строк.
    private static String brief(String s) {
        if (s == null) return "";
        int i = s.indexOf("ERROR:");
        if (i >= 0) s = s.substring(i);
        s = s.replaceAll("\\s+", " ").trim();
        return s.length() > 200 ? s.substring(0, 200) : s;
    }

    private static File firstOutput(File dir, String base) {
        File[] list = dir.listFiles();
        if (list == null) return null;
        for (File f : list) {
            String n = f.getName();
            if (n.startsWith(base + ".") && !n.endsWith(".part") && !n.endsWith(".ytdl") && f.length() > 2000) return f;
        }
        return null;
    }

    private static void cleanup(File dir, String base) {
        File[] list = dir.listFiles();
        if (list == null) return;
        for (File f : list) if (f.getName().startsWith(base + ".")) f.delete();
    }

    // Скачивает звук песни YouTube во временный файл. Возвращает путь к файлу или «ERR:причина».
    static String fetch(String videoId, String procId) {
        if (videoId == null || !videoId.matches("[A-Za-z0-9_-]{1,24}")) return "ERR:bad id";
        MainActivity act = MainActivity.inst;
        if (act == null) return "ERR:no activity";
        Context ctx = act.getApplicationContext();
        if (!init(ctx)) return "ERR:init: " + brief(initError);

        if (stale(ctx) && updateDue(ctx)) update(ctx);  // старый yt-dlp заведомо получит 403: обновляем до первой попытки

        File dir = new File(ctx.getCacheDir(), "ytdlp");
        dir.mkdirs();
        String base = "yt_" + videoId + "_" + System.nanoTime();
        String err = "не получилось";
        for (int attempt = 0; attempt < 2; attempt++) {
            try {
                YoutubeDLRequest req = new YoutubeDLRequest("https://www.youtube.com/watch?v=" + videoId);
                req.addOption("-f", "bestaudio[ext=m4a]/bestaudio[acodec^=mp4a]/bestaudio");
                req.addOption("--no-playlist");
                req.addOption("--no-warnings");
                req.addOption("--no-part");
                req.addOption("--no-mtime");
                req.addOption("-o", new File(dir, base + ".%(ext)s").getAbsolutePath());
                YoutubeDL.getInstance().execute(req, procId);
                File out = firstOutput(dir, base);
                if (out != null) return out.getAbsolutePath();
                err = "yt-dlp не создал файл";
            } catch (Throwable t) {
                err = brief(String.valueOf(t.getMessage()));
                if (t instanceof InterruptedException || t.getClass().getSimpleName().contains("Canceled")) {
                    cleanup(dir, base);
                    return "ERR:canceled";
                }
            }
            cleanup(dir, base);
            if (attempt == 0 && updateDue(ctx) && update(ctx)) continue;  // обновились — одна повторная попытка
            break;
        }
        return "ERR:" + err + (updateNote.isEmpty() ? "" : " [" + updateNote + "]");
    }

    static void cancel(String procId) {
        try {
            YoutubeDL.getInstance().destroyProcessById(procId);
        } catch (Throwable ignored) {
        }
    }
}
