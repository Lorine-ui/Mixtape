// Аккаунты Mixtape: проект Firebase (вход по почте и паролю + Realtime Database).
// Это не секреты: Firebase так и задуман — ключ проекта лежит внутри приложения, а данные защищают
// правила базы (каждый пользователь читает и пишет только users/<свой id>). Пусто — аккаунты выключены.
#pragma once

namespace mx {

inline constexpr const char* kFirebaseApiKey = "AIzaSyBYvVTLIxgaVkQR2xZ9M6RR7Q_ccXTQBcA";
inline constexpr const char* kFirebaseDbUrl = "https://mixtape-music-default-rtdb.europe-west1.firebasedatabase.app";  // https://<проект>-default-rtdb.<регион>.firebasedatabase.app

}  // namespace mx
