package com.mixtape.app;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * HTTP для C++ (http_android.cpp): открываем соединение, C++ читает тело кусками через read(),
 * поэтому у него работают прогресс загрузки и отмена. Перенаправления (в том числе http→https) — здесь.
 */
public class Http {
    public int status;
    public String contentType = "";
    public long length = -1;
    private HttpURLConnection conn;
    private InputStream in;

    public static Http open(String url, String method, byte[] body, String[] headers, int timeoutMs) throws IOException {
        Http h = new Http();
        String cur = url;
        for (int hop = 0; hop < 8; ++hop) {
            HttpURLConnection c = (HttpURLConnection) new URL(cur).openConnection();
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestMethod(method);
            for (int i = 0; i + 1 < headers.length; i += 2) c.setRequestProperty(headers[i], headers[i + 1]);
            if (body != null && body.length > 0) {
                c.setDoOutput(true);
                try (OutputStream o = c.getOutputStream()) { o.write(body); }
            }
            int code = c.getResponseCode();
            if (code >= 300 && code < 400 && c.getHeaderField("Location") != null) {
                cur = new URL(new URL(cur), c.getHeaderField("Location")).toString();
                c.disconnect();
                if (code == 303) method = "GET";
                continue;
            }
            h.conn = c;
            h.status = code;
            String ct = c.getContentType();
            h.contentType = ct == null ? "" : ct;
            h.length = c.getContentLengthLong();
            h.in = code >= 400 ? c.getErrorStream() : c.getInputStream();
            return h;
        }
        throw new IOException("Слишком много перенаправлений");
    }

    /** Сколько байт прочитано в buf; -1 — конец. */
    public int read(byte[] buf) throws IOException {
        if (in == null) return -1;
        return in.read(buf);
    }

    public void close() {
        try { if (in != null) in.close(); } catch (IOException ignored) { }
        if (conn != null) conn.disconnect();
    }
}
