#include "discord_win32.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>

namespace mx {

namespace {

using Clock = std::chrono::steady_clock;

std::uint32_t le32(const char* p) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}

bool writeAll(HANDLE h, const std::string& data) {
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        DWORD wrote = 0;
        if (!WriteFile(h, p, static_cast<DWORD>(std::min<std::size_t>(left, 1u << 16)), &wrote, nullptr) || wrote == 0) return false;
        p += wrote;
        left -= wrote;
    }
    return true;
}

// Читает ровно n байт; ждёт не дольше timeoutMs.
bool readExact(HANDLE h, char* buf, std::size_t n, int timeoutMs) {
    const auto t0 = Clock::now();
    std::size_t got = 0;
    while (got < n) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail == 0) {
            if (Clock::now() - t0 > std::chrono::milliseconds(timeoutMs)) return false;
            Sleep(10);
            continue;
        }
        DWORD r = 0;
        if (!ReadFile(h, buf + got, static_cast<DWORD>(std::min<std::size_t>(n - got, avail)), &r, nullptr) || r == 0) return false;
        got += r;
    }
    return true;
}

bool readFrame(HANDLE h, std::uint32_t& op, std::string& body, int timeoutMs) {
    char hd[8];
    if (!readExact(h, hd, 8, timeoutMs)) return false;
    op = le32(hd);
    const std::uint32_t len = le32(hd + 4);
    if (len > (1u << 20)) return false;
    body.assign(len, '\0');
    return len == 0 || readExact(h, &body[0], len, 2000);
}

// Читает всё, что Discord успел прислать (ответы, пинги, закрытие). false — связь потеряна.
bool drain(HANDLE h) {
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail < 8) return true;
        std::uint32_t op = 0;
        std::string body;
        if (!readFrame(h, op, body, 2000)) return false;
        if (op == kOpClose) return false;
        if (op == kOpPing && !writeAll(h, discordFrame(kOpPong, body))) return false;
    }
}

}  // namespace

DiscordPresence::DiscordPresence(std::string appId, std::wstring pipeBase) : appId_(std::move(appId)), pipeBase_(std::move(pipeBase)) {
    th_ = std::thread([this] { run(); });
}

DiscordPresence::~DiscordPresence() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    th_.join();
}

void DiscordPresence::set(const DiscordActivity& a) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        act_ = a;
        wantActive_ = true;
        dirty_ = true;
    }
    cv_.notify_all();
}

void DiscordPresence::clear() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!wantActive_) return;  // и так ничего не показываем
        wantActive_ = false;
        dirty_ = true;
    }
    cv_.notify_all();
}

bool DiscordPresence::connected() const { return connected_.load(); }

void DiscordPresence::run() {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    auto nextTry = Clock::now();
    const long pid = static_cast<long>(GetCurrentProcessId());
    int nonce = 0;

    auto dropPipe = [&](int retrySec) {
        if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
        connected_ = false;
        nextTry = Clock::now() + std::chrono::seconds(retrySec);
        std::lock_guard<std::mutex> lk(mu_);
        if (wantActive_) dirty_ = true;  // после переподключения покажем статус заново
    };
    // Подключиться к первой же трубе Discord, на которой он ответит READY на приветствие.
    auto connect = [&]() -> HANDLE {
        for (int i = 0; i < 10; ++i) {
            HANDLE h = CreateFileW((pipeBase_ + std::to_wstring(i)).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            std::uint32_t op = 0;
            std::string body;
            if (writeAll(h, discordFrame(kOpHandshake, discordHandshake(appId_))) && readFrame(h, op, body, 3000) && op == kOpFrame &&
                body.find("\"READY\"") != std::string::npos)
                return h;
            CloseHandle(h);
        }
        return INVALID_HANDLE_VALUE;
    };

    for (;;) {
        bool send = false, active = false;
        DiscordActivity act;
        {
            std::unique_lock<std::mutex> lk(mu_);
            // нет связи — спим до следующей попытки; есть связь — просыпаемся раз в полсекунды, чтобы читать ответы Discord
            const auto wait = pipe == INVALID_HANDLE_VALUE ? std::max(Clock::duration(std::chrono::milliseconds(100)), nextTry - Clock::now())
                                                           : Clock::duration(std::chrono::milliseconds(500));
            cv_.wait_for(lk, wait, [&] { return stop_ || (dirty_ && pipe != INVALID_HANDLE_VALUE); });
            if (stop_) break;
            if (dirty_ && pipe != INVALID_HANDLE_VALUE) {
                dirty_ = false;
                send = true;
                active = wantActive_;
                act = act_;
            }
        }
        if (pipe == INVALID_HANDLE_VALUE) {
            if (Clock::now() < nextTry) continue;
            pipe = connect();
            if (pipe == INVALID_HANDLE_VALUE) {
                nextTry = Clock::now() + std::chrono::seconds(15);
                continue;
            }
            connected_ = true;
            std::lock_guard<std::mutex> lk(mu_);
            if (wantActive_) dirty_ = true;
            continue;
        }
        if (send) {
            const std::string n = "mx" + std::to_string(++nonce);
            const std::string json = active ? discordSetActivity(act, pid, n) : discordClearActivity(pid, n);
            if (!writeAll(pipe, discordFrame(kOpFrame, json))) {
                dropPipe(5);
                continue;
            }
        }
        if (!drain(pipe)) dropPipe(5);
    }
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
}

}  // namespace mx
