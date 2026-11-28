<div align="center">

# Mixtape

**Бесплатный музыкальный плеер для Windows и Android — без рекламы и с открытым кодом**

[![Скачать для Windows](https://img.shields.io/badge/Скачать-Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/Lorine-ui/Mixtape/releases/latest/download/Mixtape.exe)
[![Скачать для Android](https://img.shields.io/badge/Скачать-Android-3DDC84?style=for-the-badge&logo=android&logoColor=white)](https://github.com/Lorine-ui/Mixtape/releases/latest/download/Mixtape.apk)

[![Версия](https://img.shields.io/github/v/release/Lorine-ui/Mixtape?label=версия&color=e85d6c)](https://github.com/Lorine-ui/Mixtape/releases/latest)
[![Скачивания](https://img.shields.io/github/downloads/Lorine-ui/Mixtape/total?label=скачиваний&color=8a5cf6)](https://github.com/Lorine-ui/Mixtape/releases)
[![Лицензия MIT](https://img.shields.io/badge/лицензия-MIT-blue)](LICENSE)
[![Patreon](https://img.shields.io/badge/Patreon-поддержать-F96854?logo=patreon&logoColor=white)](https://www.patreon.com/c/Mixtape696)

<img src="docs/screenshots/home.jpg" alt="Главный экран Mixtape" width="860">

</div>

Один поиск сразу по Audius, SoundCloud, Internet Archive и YouTube Music, офлайн-режим, страницы исполнителей с альбомами, друзья и рекомендации песен ссылкой (без сервера),
12 рамок для аватарки, свой фон (картинка или GIF), темы оформления и 10 языков интерфейса.

## Скриншоты

| | |
|---|---|
| <img src="docs/screenshots/torn.jpg" alt="Тема «Разрыв»"> | <img src="docs/screenshots/halftone.jpg" alt="Тема «Полутон»"> |
| Тема «Разрыв» | Тема «Полутон» |
| <img src="docs/screenshots/spot.jpg" alt="Тема «Прожектор»"> | <img src="docs/screenshots/coral.jpg" alt="Настройки оформления"> |
| Тема «Прожектор» | Настройки: 17 тем и любой цвет |

## Скачать

| Платформа | Файл | Требования |
|---|---|---|
| Windows | [**Mixtape.exe**](https://github.com/Lorine-ui/Mixtape/releases/latest/download/Mixtape.exe) | Windows 10/11, 64-бит, около 26 МБ. Установка не нужна — просто запустите файл |
| Android | [**Mixtape.apk**](https://github.com/Lorine-ui/Mixtape/releases/latest/download/Mixtape.apk) | Android 10 или новее, около 40 МБ (внутри Python и yt-dlp) |

**Windows:** при первом запуске SmartScreen может предупредить о неизвестном издателе (у программы нет платной цифровой подписи) —
нажмите «Подробнее» → «Выполнить в любом случае».

**Android:** откройте APK на телефоне и разрешите установку из этого источника. При первом запуске разрешите уведомления —
так музыка играет с выключенным экраном, а в шторке есть кнопки управления.

## Возможности

- **Поиск везде сразу** — Audius, SoundCloud, Internet Archive и YouTube Music в одном списке. Песни YouTube Music играют целиком
  через вшитый [yt-dlp](https://github.com/yt-dlp/yt-dlp); если он не справился, та же песня берётся из других источников.
- **Офлайн** — скачайте песню или сразу всю «Мою музыку» и слушайте без интернета. Можно добавлять свои MP3, FLAC, WAV.
- **Аккаунт** — вход по почте; «Моя музыка», плейлисты и профиль одинаковые на компьютере и телефоне.
- **Профиль** — аватарка, никнейм, «о себе», цвет и рамка.
- **Оформление** — 17 тем, включая узорные («Коралл», «Разрыв», «Полутон», «Прожектор»…); любую тему можно перекрасить,
  затемнить фон, поменять размер текста, скругление углов и плотность списков.
- **Тексты песен**, которые листаются в такт музыке, и свои тексты.
- **Фрагменты** — вырезать любимый кусок песни или убрать лишний.
- **Перенос плейлистов** из Spotify, YouTube и SoundCloud по ссылке, без ключей и входа.
- **Статистика** — сколько часов вы слушаете, любимые песни и исполнители; **подбор похожей музыки** по вашему вкусу.
- Таймер сна, медиаклавиши, работа в свёрнутом окне, полноэкранный режим, танцующая Тето (можно заменить своей картинкой или GIF).
- **10 языков**: русский, English, українська, español, Deutsch, français, português, italiano, polski, Türkçe.
- **Любые экраны** — узкие и широкие телефоны, горизонтальный режим (вкладки и проигрыватель перестраиваются), планшеты.

## Ограничения

- Песни из интернета зависят от сторонних сайтов: если SoundCloud или YouTube что-то меняют у себя, часть песен может временно не играть.
- YouTube Music: первая песня грузится дольше обычного (запуск yt-dlp, на телефоне ещё распаковка Python и, при необходимости,
  обновление yt-dlp с GitHub). Вшитый yt-dlp со временем устаревает: на ПК положите свежий `yt-dlp.exe` рядом с `Mixtape.exe`.
- Синхронизируются списки песен, а не сами аудиофайлы: свои MP3 остаются на том устройстве, куда их добавили.
- Версий для iPhone и Mac пока нет.

## Исходный код

| Папка | Что внутри |
|---|---|
| [`pc/`](pc) | Версия для Windows (C++17, Dear ImGui, DirectX 11, miniaudio, WinHTTP, Media Foundation) |
| [`android/`](android) | Версия для Android (тот же C++ плюс OpenGL ES, NDK и небольшая обёртка на Java) |

### Сборка для Windows

Нужны Visual Studio 2022 (или Build Tools) с компонентом «Разработка классических приложений на C++», CMake 3.21+ и Git.

```bat
cd pc
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Или просто запустите `pc\build.bat`. При первой настройке CMake сам скачает Dear ImGui, miniaudio и stb.
Чтобы вшить yt-dlp, положите `yt-dlp.exe` в `pc/resources/` перед сборкой (в репозиторий он не входит); без него exe собирается как обычно.
Подробнее — в [pc/README.md](pc/README.md).

### Сборка для Android

Нужны JDK 17, Android SDK 34, NDK 26.3, CMake 3.22 и Gradle 8.7 в папке `%USERPROFILE%\Android`.
Запустите `android\android\build_apk.bat` — готовый `Mixtape.apk` появится в папке `android`. Gradle сам скачает библиотеку
youtubedl-android (около 60 МБ), поэтому нужен интернет.
Подробнее — в [android/ANDROID.md](android/ANDROID.md).

### Друзья без сервера

В Mixtape нет сервера и входа в аккаунт. Друзья добавляются ссылкой-приглашением, песни рекомендуются ссылкой на песню:
всё, что в ней нужно, лежит в её «хвосте» после `#`, и ничего не отправляется в интернет. Профиль и список друзей хранятся
в `profile.json` и `friends.json` в папке данных.

### Тесты

```bat
cd pc
cmake -S . -B b -DMX_BUILD_APP=OFF
cmake --build b --config Release
ctest --test-dir b -C Release
```

## Лицензия

Код Mixtape распространяется по лицензии [MIT](LICENSE): его можно свободно использовать, изменять и распространять с указанием автора.
Сторонние библиотеки, которые скачиваются при сборке (Dear ImGui, miniaudio, stb), — под своими лицензиями.

**Сборка для Windows** содержит [yt-dlp](https://github.com/yt-dlp/yt-dlp) (Unlicense). Состав его официального exe и лицензии компонентов — в [THIRD_PARTY_LICENSES.txt](https://github.com/yt-dlp/yt-dlp/blob/master/THIRD_PARTY_LICENSES.txt) проекта yt-dlp.

**Сборка для Android** содержит библиотеку [youtubedl-android](https://github.com/yausername/youtubedl-android) (Python, yt-dlp, QuickJS), которая распространяется по **GPL-3.0**. Поэтому APK целиком выпускается на условиях GPL-3.0 (текст — [android/LICENSE-GPL-3.0.txt](android/LICENSE-GPL-3.0.txt)); исходный код приложения полностью открыт и лежит в этом репозитории.

## Поддержать автора

Mixtape бесплатный и без рекламы. Если он вам нравится и хочется сказать спасибо — можно поддержать разработку на
[**Patreon**](https://www.patreon.com/c/Mixtape696). Это по желанию: приложение остаётся полностью бесплатным.
