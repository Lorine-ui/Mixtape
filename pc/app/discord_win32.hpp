// Клиент Discord Rich Presence для Windows: в фоновом потоке держит связь с запущенным Discord через именованную
// трубу и показывает в профиле «Слушает Mixtape» с названием и минутой. Если Discord не запущен, тихо ждёт и
// пробует подключиться снова; на работу приложения это не влияет.
#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "discord_rpc.hpp"

namespace mx {

class DiscordPresence {
public:
    // pipeBase — начало имени трубы; к нему добавляется номер 0..9 (проверки подставляют свою трубу)
    explicit DiscordPresence(std::string appId, std::wstring pipeBase = L"\\\\.\\pipe\\discord-ipc-");
    ~DiscordPresence();
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    void set(const DiscordActivity& a);  // показать песню
    void clear();                        // убрать статус (пауза, ничего не играет)
    bool connected() const;              // готова ли связь с Discord (для проверок)

private:
    void run();

    std::string appId_;
    std::wstring pipeBase_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool dirty_ = false;       // есть что отправить
    bool wantActive_ = false;  // последнее пожелание: показывать статус или нет
    DiscordActivity act_;
    std::atomic<bool> connected_{false};
    std::thread th_;
};

}  // namespace mx
