// Настройки Discord Rich Presence («Слушает Mixtape»).
//
// ID приложения берётся на https://discord.com/developers/applications: создайте приложение с названием Mixtape
// (это название увидят в статусе) и скопируйте «Application ID» со страницы General Information.
// Пока ID пустой, Mixtape ничего не отправляет в Discord и переключателя в настройках нет.
#pragma once

namespace mx {

inline constexpr const char* kDiscordAppId = "1575877913012867243";

// Кнопка под статусом (её видят другие в вашем профиле); пустой адрес — без кнопки.
inline constexpr const char* kDiscordButtonLabel = "Скачать Mixtape";
inline constexpr const char* kDiscordButtonUrl = "https://github.com/Lorine-ui/Mixtape/releases/latest";

}  // namespace mx
