// Синхронизация библиотеки через аккаунт: что отправлять в облако и как применять пришедшее оттуда.
// Сами аудиофайлы не синхронизируются — только список песен, плейлисты, фрагменты, свои тексты и настройки.
#pragma once
#include <string>

#include "json.hpp"
#include "library.hpp"

namespace mx {

// Настройки, которые относятся к конкретному устройству (масштаб, полный экран, громкость) — не синхронизируются.
bool isDeviceSetting(const std::string& key);

// Что уходит в облако: всё, кроме своих файлов с этого устройства, офлайн-отметок и настроек устройства.
Json cloudPayload(const Library& lib);

// Применить библиотеку из облака.
//  merge = false — облако главнее: песни, плейлисты и тексты становятся как в облаке (удаления тоже переносятся),
//                  но свои файлы, офлайн-копии и настройки этого устройства остаются;
//  merge = true  — изменения были и здесь, и в облаке: объединяем, ничего не удаляя.
// false, если это не библиотека Mixtape.
bool applyCloud(Library& lib, const Json& cloud, bool merge);

// Профиль аккаунта: никнейм, строка «о себе», аватарка и оформление. Хранится в облаке рядом с библиотекой.
struct Profile {
    static constexpr int kAvatarSide = 128;  // аватарка — квадрат 128×128, RGB
    std::string name;    // никнейм; пусто — имя из почты
    std::string status;  // «о себе»
    int color = 0;       // цвет кружка: 0 — цвет темы, 1..7 — свои
    int frame = 1;       // рамка аватарки: 0 — нет, 1 — кольцо, 2 — неон
    std::string avatar;  // RGB-пиксели в base64; пусто — первая буква имени
    long long updated = 0;  // время последней правки (мс) — новее побеждает при синхронизации

    Json toJson() const;
    static Profile fromJson(const Json& j);
};

// Как подписывать пользователя: никнейм, а без него — часть почты до «@».
std::string displayName(const Profile& p, const std::string& email);
// Обрезать строку до n символов (не байт), не разрывая UTF-8.
std::string clipChars(const std::string& s, std::size_t n);

std::string base64Encode(const std::string& bytes);
bool base64Decode(const std::string& text, std::string& out);

}  // namespace mx
