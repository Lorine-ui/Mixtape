cmake_minimum_required(VERSION 3.21)
project(Mixtape VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MX_BUILD_APP "Собрать приложение (только Windows)" ON)
option(MX_BUILD_TESTS "Собрать тесты" ON)

# Один .exe без внешних DLL: статическая библиотека времени выполнения
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8)
endif()

# ---------- ядро: чистый C++17, собирается и проверяется где угодно ----------
add_library(mxcore STATIC
    core/json.cpp core/text.cpp core/model.cpp core/parsers.cpp core/rank.cpp
    core/cuts.cpp core/queue.cpp core/library.cpp core/mixer.cpp core/stats.cpp core/cloudsync.cpp core/share.cpp core/discord_rpc.cpp)
target_include_directories(mxcore PUBLIC core)

# ---------- сеть: HTTP через интерфейс IHttp ----------
add_library(mxnet STATIC app/net/net.cpp)
target_include_directories(mxnet PUBLIC app/net)
target_link_libraries(mxnet PUBLIC mxcore)

if(NOT MSVC)
    foreach(t mxcore mxnet)
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endforeach()
endif()

if(MX_BUILD_TESTS)
    enable_testing()
    add_executable(mxtests tests/test_core.cpp)
    target_link_libraries(mxtests PRIVATE mxcore)
    add_executable(test_net tests/test_net.cpp)
    target_link_libraries(test_net PRIVATE mxnet)
    add_executable(test_mixer tests/test_mixer.cpp)
    target_link_libraries(test_mixer PRIVATE mxcore)
    add_test(NAME core COMMAND mxtests)
    add_test(NAME net COMMAND test_net)
    add_test(NAME mixer COMMAND test_mixer)
    if(WIN32)  # связь с Discord идёт по именованной трубе Windows: проверяем на подставном «Discord»
        add_executable(test_discord tests/test_discord.cpp app/discord_win32.cpp)
        target_include_directories(test_discord PRIVATE app)
        target_link_libraries(test_discord PRIVATE mxcore)
        add_test(NAME discord COMMAND test_discord)
    endif()
endif()

# ---------- приложение ----------
if(MX_BUILD_APP AND WIN32)
    include(FetchContent)
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5 GIT_SHALLOW TRUE)
    FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.21 GIT_SHALLOW TRUE)
    FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui miniaudio stb)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)

    add_executable(Mixtape WIN32
        app/main_win32.cpp app/app.cpp app/social.cpp
        app/audio/player.cpp app/audio/mf_decode.cpp app/net/http_winhttp.cpp app/discord_win32.cpp
        app/ui/ui.cpp app/ui/views.cpp app/ui/fullplayer.cpp app/ui/modals.cpp
        app/ui/draw.cpp app/ui/textures.cpp app/ui/i18n.cpp
        resources/mixtape.rc)
    target_include_directories(Mixtape PRIVATE app app/ui app/audio app/net ${miniaudio_SOURCE_DIR} ${stb_SOURCE_DIR})
    target_compile_definitions(Mixtape PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    # Если рядом с mixtape.rc лежит yt-dlp.exe, он вшивается в Mixtape.exe (иначе приложение собирается без него)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/yt-dlp.exe")
        target_compile_definitions(Mixtape PRIVATE MX_EMBED_YTDLP=1)
        set_source_files_properties(resources/mixtape.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/yt-dlp.exe")
    endif()
    target_link_libraries(Mixtape PRIVATE mxnet imgui_lib
        d3d11 dxgi dwmapi winhttp wininet mfplat mfreadwrite mfuuid shlwapi ole32 shell32 comdlg32 user32 gdi32 advapi32 winmm)
    if(MSVC)
        target_compile_options(Mixtape PRIVATE /W3)
        target_link_options(Mixtape PRIVATE /SUBSYSTEM:WINDOWS)
    endif()
elseif(MX_BUILD_APP)
    message(STATUS "Приложение собирается только под Windows; здесь собраны ядро и тесты.")
endif()
