cmake_minimum_required(VERSION 3.22)
project(mixtape_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    # декодирование и микшер в отладочной сборке слишком медленные для телефона
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()

# общий код — тот же, что у версии для Windows
get_filename_component(MX ${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ABSOLUTE)

include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5 GIT_SHALLOW TRUE)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.21 GIT_SHALLOW TRUE)
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui miniaudio stb)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(mixtape SHARED
    # ядро и сеть
    ${MX}/core/json.cpp ${MX}/core/text.cpp ${MX}/core/model.cpp ${MX}/core/parsers.cpp ${MX}/core/rank.cpp
    ${MX}/core/cuts.cpp ${MX}/core/queue.cpp ${MX}/core/library.cpp ${MX}/core/mixer.cpp ${MX}/core/stats.cpp ${MX}/core/cloudsync.cpp
    ${MX}/app/net/net.cpp
    # приложение и интерфейс
    ${MX}/app/app.cpp ${MX}/app/cloud.cpp ${MX}/app/audio/player.cpp
    ${MX}/app/ui/ui.cpp ${MX}/app/ui/views.cpp ${MX}/app/ui/fullplayer.cpp ${MX}/app/ui/modals.cpp
    ${MX}/app/ui/draw.cpp ${MX}/app/ui/textures.cpp ${MX}/app/ui/i18n.cpp ${MX}/app/ui/mobile.cpp
    # Dear ImGui
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    # только для Android
    android_main.cpp jni_env.cpp http_android.cpp mc_decode.cpp)

target_include_directories(mixtape PRIVATE
    ${MX}/core ${MX}/app ${MX}/app/ui ${MX}/app/audio ${MX}/app/net ${CMAKE_CURRENT_SOURCE_DIR}
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp
    ${miniaudio_SOURCE_DIR} ${stb_SOURCE_DIR})
target_compile_definitions(mixtape PRIVATE IMGUI_IMPL_OPENGL_ES3)
# ANativeActivity_onCreate должна остаться видимой для системы
set_target_properties(mixtape PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
target_link_libraries(mixtape PRIVATE native_app_glue android log EGL GLESv3 mediandk OpenSLES)
