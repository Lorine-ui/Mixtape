// Приглашения в друзья и рекомендации песен ссылкой: без сервера. Ссылка ведёт на страницу Mixtape на GitHub, а сами данные
// лежат в её «хвосте» (после #): друг вставляет ссылку в приложение и получает то, что в ней закодировано.
#pragma once
#include <string>

#include "model.hpp"

namespace mx {

struct ShareFriend {
    std::string id, name, status;  // id — случайный номер человека (чтобы повторное приглашение обновило, а не задвоило)
    int color = 0, frame = 1;
};

struct ShareSong {
    Track track;
    std::string fromId, fromName, note;
};

enum class ShareKind { None, Friend, Song };

struct ShareParsed {
    ShareKind kind = ShareKind::None;
    ShareFriend person;
    ShareSong song;
};

std::string makeFriendLink(const ShareFriend& f);
std::string makeSongLink(const ShareSong& s);
// Ищет ссылку Mixtape в любом тексте (её можно вставить вместе с подписью из чата). None — ссылки нет или она повреждена.
ShareParsed parseShareText(const std::string& text);

std::string base64UrlEncode(const std::string& bytes);
bool base64UrlDecode(const std::string& text, std::string& out);

}  // namespace mx
