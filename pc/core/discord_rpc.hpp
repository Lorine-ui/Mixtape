// Discord Rich Presence: что сейчас слушаете и на какой минуте. Здесь только текст протокола (JSON и кадры) —
// без трубы и потоков, поэтому его легко проверять тестами. Саму трубу открывает app/discord_win32.cpp.
//
// Протокол Discord IPC: к клиенту Discord на компьютере подключаются по именованной трубе discord-ipc-0..9 и
// шлют кадры «4 байта код + 4 байта длина (оба little-endian) + JSON в UTF-8».
#pragma once
#include <cstdint>
#include <string>

namespace mx {

struct DiscordActivity {
    std::string title;      // название песни
    std::string artist;     // исполнитель (или источник, если его нет)
    std::string coverUrl;   // https-адрес обложки; пусто — без картинки
    long long startSec = 0;  // когда песня «началась» (Unix-время, секунды); вместе с endSec даёт полосу прогресса
    long long endSec = 0;
    std::string buttonLabel, buttonUrl;  // кнопка под статусом (её видят другие); пусто — без кнопки
};

enum DiscordOp : std::uint32_t { kOpHandshake = 0, kOpFrame = 1, kOpClose = 2, kOpPing = 3, kOpPong = 4 };

// Строка для Discord: не длиннее maxChars символов (по символам, а не байтам), не короче двух (так требует Discord).
std::string clipForDiscord(const std::string& s, std::size_t maxChars = 128);

std::string discordFrame(std::uint32_t op, const std::string& json);  // заголовок из 8 байт + JSON
std::string discordHandshake(const std::string& appId);              // {"v":1,"client_id":"..."}
std::string discordSetActivity(const DiscordActivity& a, long pid, const std::string& nonce);
std::string discordClearActivity(long pid, const std::string& nonce);

}  // namespace mx
