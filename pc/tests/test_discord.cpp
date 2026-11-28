// Проверка связи с Discord на подставном «Discord»: своя именованная труба принимает приветствие, отвечает READY
// и записывает кадры, которые присылает приложение. Настоящий Discord и интернет не нужны, в профиль ничего не попадает.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "discord_win32.hpp"
#include "json.hpp"

using namespace mx;

static int g_fail = 0, g_all = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_all;                                                              \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

static bool waitFor(const std::function<bool()>& pred, int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(ms)) return false;
        Sleep(20);
    }
    return true;
}

static bool readN(HANDLE h, char* buf, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(h, buf + got, static_cast<DWORD>(n - got), &r, nullptr) || r == 0) return false;
        got += r;
    }
    return true;
}

static std::uint32_t le32(const char* p) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}

// Подставной Discord: принимает подключения по одному, на каждом читает приветствие и кадры.
struct FakeDiscord {
    std::wstring pipeName;
    std::thread th;
    std::mutex mu;
    std::vector<std::string> handshakes;
    std::vector<std::string> frames;  // тела кадров SET_ACTIVITY
    std::atomic<int> connections{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> dropAfterFrame{false};  // закрыть связь после ближайшего кадра (как будто Discord перезапустили)

    explicit FakeDiscord(std::wstring name) : pipeName(std::move(name)) { th = std::thread([this] { serve(); }); }
    ~FakeDiscord() {
        stop = true;
        HANDLE poke = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);  // будим ConnectNamedPipe
        if (poke != INVALID_HANDLE_VALUE) CloseHandle(poke);
        th.join();
    }
    size_t frameCount() {
        std::lock_guard<std::mutex> lk(mu);
        return frames.size();
    }

    void serve() {
        while (!stop) {
            HANDLE p = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
            if (p == INVALID_HANDLE_VALUE) return;
            if (!ConnectNamedPipe(p, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) { CloseHandle(p); continue; }
            if (stop) { CloseHandle(p); return; }
            char hd[8];
            if (readN(p, hd, 8)) {
                std::string body(le32(hd + 4), '\0');
                if (readN(p, &body[0], body.size())) {
                    { std::lock_guard<std::mutex> lk(mu); handshakes.push_back(body); }
                    std::string ready = "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}";
                    std::string fr(8, '\0');
                    fr[0] = 1;
                    fr[4] = static_cast<char>(ready.size());
                    fr += ready;
                    DWORD w = 0;
                    WriteFile(p, fr.data(), static_cast<DWORD>(fr.size()), &w, nullptr);
                    ++connections;
                    while (readN(p, hd, 8)) {
                        std::string b(le32(hd + 4), '\0');
                        if (!readN(p, &b[0], b.size())) break;
                        { std::lock_guard<std::mutex> lk(mu); frames.push_back(b); }
                        if (dropAfterFrame.exchange(false)) break;
                    }
                }
            }
            DisconnectNamedPipe(p);
            CloseHandle(p);
        }
    }
};

int main() {
    const std::wstring base = L"\\\\.\\pipe\\mx-test-discord-" + std::to_wstring(GetCurrentProcessId()) + L"-";

    // 1) приложение подключается, здоровается и шлёт статус
    {
        FakeDiscord srv(base + L"0");
        DiscordPresence dp("123456789012345678", base);
        CHECK(waitFor([&] { return dp.connected(); }, 5000));
        CHECK(srv.connections == 1);
        {
            std::lock_guard<std::mutex> lk(srv.mu);
            CHECK(srv.handshakes.size() == 1);
            if (!srv.handshakes.empty()) CHECK(Json::parse(srv.handshakes[0])["client_id"].asStr() == "123456789012345678");
        }

        DiscordActivity a;
        a.title = "Дьявол";
        a.artist = "ARKUSHA";
        a.startSec = 1700000000;
        a.endSec = 1700000200;
        dp.set(a);
        CHECK(waitFor([&] { return srv.frameCount() >= 1; }, 3000));
        if (srv.frameCount() >= 1) {
            std::lock_guard<std::mutex> lk(srv.mu);
            Json j = Json::parse(srv.frames[0]);
            CHECK(j["cmd"].asStr() == "SET_ACTIVITY");
            CHECK(j["args"]["activity"]["details"].asStr() == "Дьявол");
            CHECK(j["args"]["activity"]["state"].asStr() == "ARKUSHA");
            CHECK(static_cast<long>(j["args"]["pid"].asNum()) == static_cast<long>(GetCurrentProcessId()));
        }

        dp.clear();  // пауза: статус убирается
        CHECK(waitFor([&] { return srv.frameCount() >= 2; }, 3000));
        if (srv.frameCount() >= 2) {
            std::lock_guard<std::mutex> lk(srv.mu);
            CHECK(!Json::parse(srv.frames[1])["args"].has("activity"));
        }
        const size_t before = srv.frameCount();
        dp.clear();  // уже убрано — второй раз ничего не шлём
        Sleep(700);
        CHECK(srv.frameCount() == before);

        // 2) Discord перезапустили: связь рвётся, приложение переподключается и показывает статус заново
        srv.dropAfterFrame = true;
        DiscordActivity b = a;
        b.title = "Вторая песня";
        dp.set(b);
        CHECK(waitFor([&] { return srv.connections >= 2; }, 15000));
        CHECK(waitFor([&] { return srv.frameCount() >= before + 2; }, 5000));
        if (srv.frameCount() >= before + 2) {
            std::lock_guard<std::mutex> lk(srv.mu);
            CHECK(Json::parse(srv.frames.back())["args"]["activity"]["details"].asStr() == "Вторая песня");
        }
        std::printf("%s связь с подставным Discord\n", g_fail == 0 ? "ok  " : "FAIL");
    }

    // 3) Discord не запущен: ничего не падает, закрытие быстрое
    {
        const auto t0 = std::chrono::steady_clock::now();
        {
            DiscordPresence dp("1", L"\\\\.\\pipe\\mx-test-no-such-discord-");
            DiscordActivity a;
            a.title = "x";
            dp.set(a);
            Sleep(300);
            CHECK(!dp.connected());
        }
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
        std::printf("%s Discord не запущен\n", g_fail == 0 ? "ok  " : "FAIL");
    }

    std::printf("\nПроверок: %d, провалов: %d\n", g_all, g_fail);
    return g_fail ? 1 : 0;
}
