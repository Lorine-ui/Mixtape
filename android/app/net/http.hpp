// Тонкий интерфейс HTTP. На Windows его реализует WinHTTP (http_winhttp.cpp), в тестах — подставной объект.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mx {

struct HttpOptions {
    std::string method = "GET";  // GET | POST
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    int timeoutMs = 15000;
    std::size_t maxBytes = 32u << 20;
    const std::atomic<bool>* cancel = nullptr;               // true: прервать загрузку
    std::function<void(std::size_t got, std::size_t total)> progress;  // total = 0, если неизвестен
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string contentType;
    bool ok() const { return status >= 200 && status < 300; }
};

class IHttp {
public:
    virtual ~IHttp() = default;
    // Идёт по перенаправлениям. Сетевые ошибки и отмена: std::runtime_error с понятным текстом.
    // Коды 4xx/5xx ошибкой не считаются: их разбирает вызывающий.
    virtual HttpResponse request(const std::string& url, const HttpOptions& opt) = 0;
};

// Реализация для Windows (http_winhttp.cpp).
std::unique_ptr<IHttp> makeHttp();

}  // namespace mx
