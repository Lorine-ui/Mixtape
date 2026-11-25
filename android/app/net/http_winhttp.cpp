// HTTP на WinHTTP: входит в Windows, отдельных библиотек не нужно.
#ifndef _WIN32
#error "http_winhttp.cpp собирается только под Windows"
#endif
#include <windows.h>
#include <winhttp.h>

#include <memory>
#include <stdexcept>

#include "http.hpp"

namespace mx {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

struct HandleCloser {
    void operator()(void* h) const { if (h) WinHttpCloseHandle(h); }
};
using Handle = std::unique_ptr<void, HandleCloser>;

std::string describe(DWORD err) {
    switch (err) {
        case ERROR_WINHTTP_TIMEOUT: return "Превышено время ожидания ответа";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "Не удалось найти сайт: проверьте интернет";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "Не удалось подключиться к сайту";
        case ERROR_WINHTTP_CONNECTION_ERROR: return "Соединение прервано";
        case ERROR_WINHTTP_SECURE_FAILURE: return "Ошибка защищённого соединения";
        default: return "Ошибка сети (код " + std::to_string(err) + ")";
    }
}

class WinHttp final : public IHttp {
public:
    WinHttp() {
        session_.reset(WinHttpOpen(L"Mixtape/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session_) throw std::runtime_error("Не удалось запустить сетевой модуль");
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x2000 /* TLS 1.3 */;
        WinHttpSetOption(session_.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }

    HttpResponse request(const std::string& url, const HttpOptions& opt) override {
        std::wstring wurl = widen(url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) throw std::runtime_error("Некорректный адрес: " + url.substr(0, 80));
        std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

        Handle conn(WinHttpConnect(session_.get(), host.c_str(), uc.nPort, 0));
        if (!conn) throw std::runtime_error(describe(GetLastError()));
        Handle req(WinHttpOpenRequest(conn.get(), widen(opt.method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
        if (!req) throw std::runtime_error(describe(GetLastError()));

        DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(req.get(), WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
        DWORD redirects = 10;
        WinHttpSetOption(req.get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects, sizeof(redirects));
        WinHttpSetTimeouts(req.get(), opt.timeoutMs, opt.timeoutMs, opt.timeoutMs, opt.timeoutMs);

        std::wstring headers;
        for (const auto& h : opt.headers) headers += widen(h.first + ": " + h.second) + L"\r\n";
        if (!headers.empty())
            WinHttpAddRequestHeaders(req.get(), headers.c_str(), static_cast<DWORD>(headers.size()),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        LPVOID body = opt.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(opt.body.data());
        DWORD blen = static_cast<DWORD>(opt.body.size());
        if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, blen, blen, 0))
            throw std::runtime_error(describe(GetLastError()));
        if (!WinHttpReceiveResponse(req.get(), nullptr)) throw std::runtime_error(describe(GetLastError()));

        HttpResponse out;
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &sz, WINHTTP_NO_HEADER_INDEX);
        out.status = static_cast<int>(status);

        wchar_t ct[256] = {};
        sz = sizeof(ct);
        if (WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ct, &sz, WINHTTP_NO_HEADER_INDEX)) {
            int n = WideCharToMultiByte(CP_UTF8, 0, ct, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                out.contentType.resize(static_cast<std::size_t>(n - 1));
                WideCharToMultiByte(CP_UTF8, 0, ct, -1, out.contentType.data(), n, nullptr, nullptr);
            }
        }

        DWORD total = 0;
        sz = sizeof(total);
        if (!WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                 &total, &sz, WINHTTP_NO_HEADER_INDEX))
            total = 0;
        if (total > opt.maxBytes) throw std::runtime_error("Файл слишком большой");
        if (total) out.body.reserve(total);

        std::string buf(64 * 1024, '\0');
        for (;;) {
            if (opt.cancel && opt.cancel->load()) throw std::runtime_error("Загрузка отменена");
            DWORD got = 0;
            if (!WinHttpReadData(req.get(), buf.data(), static_cast<DWORD>(buf.size()), &got))
                throw std::runtime_error(describe(GetLastError()));
            if (got == 0) break;
            out.body.append(buf.data(), got);
            if (out.body.size() > opt.maxBytes) throw std::runtime_error("Файл слишком большой");
            if (opt.progress) opt.progress(out.body.size(), total);
        }
        return out;
    }

private:
    Handle session_;
};

}  // namespace

std::unique_ptr<IHttp> makeHttp() { return std::make_unique<WinHttp>(); }

}  // namespace mx
