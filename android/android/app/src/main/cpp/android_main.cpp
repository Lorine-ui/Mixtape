// Mixtape для Android: окно (OpenGL ES 3 через EGL), сенсорный ввод, кнопка «Назад» и всё, что идёт через Java.
// Логика приложения и интерфейс — тот же общий код, что у версии для Windows (app/, core/).
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "app.hpp"
#include "imgui_impl_opengl3.h"
#include "imgui_inc.hpp"
#include "jni_env.hpp"
#include "ui.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Mixtape", __VA_ARGS__)

namespace {

// ---------- состояние ----------
android_app* g_and = nullptr;
mx::App* g_mx = nullptr;
mx::ui::Ui* g_ui = nullptr;
float g_density = 2.f;  // пикселей на «точку» интерфейса (dpi / 160)
// Видимая часть окна без строки состояния и кнопок навигации. Окно NativeActivity занимает весь экран,
// системные полосы лежат поверх него — интерфейс рисуем только внутри этого прямоугольника.
ARect g_rect{0, 0, 0, 0};
std::atomic<float> g_offX{0.f}, g_offY{0.f};  // смещение видимой части, в пикселях (для касаний)
std::atomic<unsigned> g_bg{0xFF0E0A14u};       // цвет фона темы: им закрашены полосы под системными кнопками

EGLDisplay g_dpy = EGL_NO_DISPLAY;
EGLContext g_ctx = EGL_NO_CONTEXT;
EGLConfig g_cfg = nullptr;
EGLSurface g_win = EGL_NO_SURFACE, g_pbuf = EGL_NO_SURFACE;  // pbuffer — чтобы контекст жил и без окна

// ответы из Java приходят в других потоках: ждущие функции храним здесь
std::mutex g_cbMu;
int g_nextId = 1;
std::map<int, std::function<void(std::string)>> g_textCb;
std::map<int, std::function<void(std::vector<std::string>)>> g_pickCb;

// ---------- EGL ----------
bool eglInit() {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, nullptr, nullptr)) return false;
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, attrs, &g_cfg, 1, &n) || n < 1) return false;
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctxAttrs);
    const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    g_pbuf = eglCreatePbufferSurface(g_dpy, g_cfg, pb);
    return g_ctx != EGL_NO_CONTEXT && eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);
}

void attachWindow(ANativeWindow* w) {
    if (g_win != EGL_NO_SURFACE) return;
    g_win = eglCreateWindowSurface(g_dpy, g_cfg, w, nullptr);
    eglMakeCurrent(g_dpy, g_win, g_win, g_ctx);
}

void detachWindow() {
    if (g_win == EGL_NO_SURFACE) return;
    eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);  // контекст и все картинки остаются живы
    eglDestroySurface(g_dpy, g_win);
    g_win = EGL_NO_SURFACE;
}

// ---------- картинки ----------
class GlGfx : public mx::ui::Gfx {
public:
    ImTextureID create(const unsigned char* rgba, int w, int h) override {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return static_cast<ImTextureID>(t);
    }
    void destroy(ImTextureID id) override {
        GLuint t = static_cast<GLuint>(id);
        if (t) glDeleteTextures(1, &t);
    }
};

// ---------- сенсорный ввод ----------
// Кнопки ImGui срабатывают, только если палец «навёлся» на них кадром раньше нажатия. У мыши так и есть,
// а палец появляется и нажимает сразу. Поэтому касание раскладываем по кадрам: сначала положение,
// в следующем кадре нажатие; после отпускания уводим «курсор» прочь, чтобы не залипала подсветка.
struct TouchEv { enum Kind { Move, Down, Press, Up, Leave } kind; float x, y; };
std::deque<TouchEv> g_touch;
std::mutex g_touchMu;

// Раз в кадр. Движения отдаём все сразу; на касании, нажатии, отпускании и уходе кадр заканчивается.
void feedTouch(ImGuiIO& io) {
    std::lock_guard<std::mutex> lk(g_touchMu);
    while (!g_touch.empty()) {
        TouchEv e = g_touch.front();
        g_touch.pop_front();
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        switch (e.kind) {
            case TouchEv::Move: io.AddMousePosEvent(e.x, e.y); continue;
            case TouchEv::Down:  // этот кадр — только положение, нажатие — в следующем
                io.AddMousePosEvent(e.x, e.y);
                g_touch.push_front({TouchEv::Press, e.x, e.y});
                return;
            case TouchEv::Press:
                io.AddMousePosEvent(e.x, e.y);
                io.AddMouseButtonEvent(0, true);
                return;
            case TouchEv::Up:  // отпускание, а в следующем кадре палец «уходит» с экрана
                io.AddMousePosEvent(e.x, e.y);
                io.AddMouseButtonEvent(0, false);
                g_touch.push_front({TouchEv::Leave, 0, 0});
                return;
            case TouchEv::Leave:
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                return;
        }
    }
}

int32_t onInput(android_app*, AInputEvent* ev) {
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(ev) != AKEYCODE_BACK) return 0;
        if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_UP) {
            bool handled = g_ui && g_ui->onBack();
            if (!handled) {  // закрывать нечего — сворачиваемся, музыка играет дальше
                JNIEnv* e = mxa::env();
                static jmethodID m = e->GetStaticMethodID(mxa::activityClass(), "goBackground", "()V");
                e->CallStaticVoidMethod(mxa::activityClass(), m);
                mxa::takeException(e);
            }
        }
        return 1;
    }
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    float s = g_density * (g_mx ? static_cast<float>(std::clamp(g_mx->lib.getNum("uiScale", 1.0), 0.75, 2.0)) : 1.f);
    int32_t act = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    float x = (AMotionEvent_getX(ev, 0) - g_offX.load()) / s, y = (AMotionEvent_getY(ev, 0) - g_offY.load()) / s;
    std::lock_guard<std::mutex> lk(g_touchMu);
    switch (act) {
        case AMOTION_EVENT_ACTION_DOWN: g_touch.push_back({TouchEv::Down, x, y}); break;
        case AMOTION_EVENT_ACTION_MOVE: g_touch.push_back({TouchEv::Move, x, y}); break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL: g_touch.push_back({TouchEv::Up, x, y}); break;
        default: break;
    }
    return 1;
}

// ---------- события окна ----------
bool g_focused = true;

void onCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) attachWindow(app->window);
            break;
        case APP_CMD_TERM_WINDOW: detachWindow(); break;
        case APP_CMD_CONTENT_RECT_CHANGED: g_rect = app->contentRect; break;
        case APP_CMD_GAINED_FOCUS: g_focused = true; break;
        case APP_CMD_LOST_FOCUS: g_focused = false; break;
        case APP_CMD_PAUSE:
        case APP_CMD_STOP:
        case APP_CMD_SAVE_STATE:
            if (g_mx) g_mx->saveNow();  // в фоне систему может закрыть приложение без предупреждения
            break;
        case APP_CMD_CONFIG_CHANGED:
            if (app->config) g_density = std::max(1.f, static_cast<float>(AConfiguration_getDensity(app->config)) / 160.f);
            break;
        default: break;
    }
}

// ---------- шрифты ----------
// Пекём под плотность экрана: обычный текст ~14 точек, крупный ~30. Японские/китайские символы — только в обычном
// шрифте (иначе атлас занимает сотни мегабайт видеопамяти); drawText сам подставляет его, если нужно.
void addFonts(ImGuiIO& io, mx::ui::Fonts& out) {
    auto exists = [](const char* p) { FILE* f = std::fopen(p, "rb"); if (f) std::fclose(f); return f != nullptr; };
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        static const ImWchar latinExt[] = {0x0100, 0x017F, 0x0218, 0x021B, 0};  // польские, турецкие и др. буквы (ł ś ğ ş ı İ …)
        b.AddRanges(latinExt);
        static const ImWchar extra[] = {0x2010, 0x2027, 0x2190, 0x21FF, 0x2212, 0x2212, 0x00D7, 0x00D7, 0x00B0, 0x00B0, 0};
        b.AddRanges(extra);
        b.BuildRanges(&ranges);
    }
    auto load = [&](std::initializer_list<const char*> files, float px, bool cjk) -> ImFont* {
        for (const char* f : files) {
            if (!exists(f)) continue;
            ImFontConfig cfg;
            cfg.OversampleH = 1;
            cfg.OversampleV = 1;
            ImFont* font = io.Fonts->AddFontFromFileTTF(f, px, &cfg, ranges.Data);
            if (!font) continue;
            if (cjk)
                for (const char* c : {"/system/fonts/NotoSansCJK-Regular.ttc", "/system/fonts/DroidSansFallback.ttf"}) {
                    if (!exists(c)) continue;
                    ImFontConfig mc;
                    mc.MergeMode = true;
                    mc.OversampleH = 1;
                    io.Fonts->AddFontFromFileTTF(c, px, &mc, io.Fonts->GetGlyphRangesJapanese());
                    break;
                }
            return font;
        }
        return nullptr;
    };
    float small = std::min(14.f * g_density, 40.f), big = std::min(30.f * g_density, 90.f);
    out.regular = load({"/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf", "/system/fonts/DroidSans.ttf"}, small, true);
    out.bold = load({"/system/fonts/Roboto-Medium.ttf", "/system/fonts/Roboto-Bold.ttf", "/system/fonts/DroidSans-Bold.ttf"}, small, false);
    out.regularL = load({"/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf"}, big, false);
    out.boldL = load({"/system/fonts/Roboto-Bold.ttf", "/system/fonts/Roboto-Medium.ttf", "/system/fonts/DroidSans-Bold.ttf"}, big, false);
    if (!out.regular) out.regular = io.Fonts->AddFontDefault();
    if (!out.bold) out.bold = out.regular;
    if (!out.boldL) out.boldL = out.regularL;
}

// ---------- Java ----------
std::string assetBytes(const char* name) {
    AAsset* a = AAssetManager_open(g_and->activity->assetManager, name, AASSET_MODE_BUFFER);
    if (!a) return {};
    std::string out(static_cast<const char*>(AAsset_getBuffer(a)), static_cast<std::size_t>(AAsset_getLength(a)));
    AAsset_close(a);
    return out;
}

jmethodID staticMethod(JNIEnv* e, const char* name, const char* sig) {
    return e->GetStaticMethodID(mxa::activityClass(), name, sig);
}

void JNICALL jOnText(JNIEnv* e, jclass, jint id, jstring text) {
    std::function<void(std::string)> fn;
    {
        std::lock_guard<std::mutex> lk(g_cbMu);
        auto it = g_textCb.find(id);
        if (it == g_textCb.end()) return;
        fn = std::move(it->second);
        g_textCb.erase(it);
    }
    if (!text || !g_mx) return;  // отмена
    std::string s = mxa::str(e, text);
    g_mx->post([fn, s] { fn(s); });
}

void JNICALL jOnPicked(JNIEnv* e, jclass, jint id, jobjectArray arr) {
    std::function<void(std::vector<std::string>)> fn;
    {
        std::lock_guard<std::mutex> lk(g_cbMu);
        auto it = g_pickCb.find(id);
        if (it == g_pickCb.end()) return;
        fn = std::move(it->second);
        g_pickCb.erase(it);
    }
    std::vector<std::string> paths;
    for (jsize i = 0; arr && i < e->GetArrayLength(arr); ++i) {
        auto s = static_cast<jstring>(e->GetObjectArrayElement(arr, i));
        paths.push_back(mxa::str(e, s));
        e->DeleteLocalRef(s);
    }
    if (paths.empty() || !g_mx) return;
    g_mx->post([fn, paths] { fn(paths); });
}

void JNICALL jOnMedia(JNIEnv*, jclass, jint action) {
    if (!g_mx) return;
    g_mx->post([action] {
        g_mx->media(action == 1 ? mx::App::Media::Next : action == 2 ? mx::App::Media::Prev : mx::App::Media::PlayPause);
    });
}

void registerNatives(JNIEnv* e) {
    static const JNINativeMethod m[] = {
        {const_cast<char*>("nativeOnText"), const_cast<char*>("(ILjava/lang/String;)V"), reinterpret_cast<void*>(jOnText)},
        {const_cast<char*>("nativeOnPicked"), const_cast<char*>("(I[Ljava/lang/String;)V"), reinterpret_cast<void*>(jOnPicked)},
        {const_cast<char*>("nativeOnMedia"), const_cast<char*>("(I)V"), reinterpret_cast<void*>(jOnMedia)},
    };
    e->RegisterNatives(mxa::activityClass(), m, 3);
    mxa::takeException(e);
}

mx::Platform makePlatform() {
    mx::Platform pf;
    pf.mobile = true;
    pf.isOnline = [] {
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "isOnline", "()Z");
        bool r = e->CallStaticBooleanMethod(mxa::activityClass(), m);
        mxa::takeException(e);
        return r;
    };
    pf.copyText = [](const std::string& s) {
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "copyText", "(Ljava/lang/String;)V");
        jstring js = mxa::jstr(e, s);
        e->CallStaticVoidMethod(mxa::activityClass(), m, js);
        e->DeleteLocalRef(js);
        mxa::takeException(e);
    };
    pf.resource = [](const std::string& name) { return assetBytes(name.c_str()); };
    pf.setTitleBarColor = [](int r, int g, int b) {
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "setBarsColor", "(I)V");
        jint argb = static_cast<jint>(0xFF000000u | (static_cast<unsigned>(r) << 16) | (static_cast<unsigned>(g) << 8) | static_cast<unsigned>(b));
        g_bg = static_cast<unsigned>(argb);
        e->CallStaticVoidMethod(mxa::activityClass(), m, argb);
        mxa::takeException(e);
    };
    pf.promptText = [](const std::string& title, const std::string& initial, bool multiline, std::function<void(std::string)> done) {
        int id;
        {
            std::lock_guard<std::mutex> lk(g_cbMu);
            id = g_nextId++;
            g_textCb[id] = std::move(done);
        }
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "promptText", "(ILjava/lang/String;Ljava/lang/String;Z)V");
        jstring t = mxa::jstr(e, title), i = mxa::jstr(e, initial);
        e->CallStaticVoidMethod(mxa::activityClass(), m, id, t, i, static_cast<jboolean>(multiline));
        e->DeleteLocalRef(t);
        e->DeleteLocalRef(i);
        mxa::takeException(e);
    };
    pf.promptPassword = [](const std::string& title, std::function<void(std::string)> done) {
        int id;
        {
            std::lock_guard<std::mutex> lk(g_cbMu);
            id = g_nextId++;
            g_textCb[id] = std::move(done);
        }
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "promptPassword", "(ILjava/lang/String;)V");
        jstring t = mxa::jstr(e, title);
        e->CallStaticVoidMethod(mxa::activityClass(), m, id, t);
        e->DeleteLocalRef(t);
        mxa::takeException(e);
    };
    pf.pickAsync = [](const std::string& kind, std::function<void(std::vector<std::string>)> done) {
        int id;
        {
            std::lock_guard<std::mutex> lk(g_cbMu);
            id = g_nextId++;
            g_pickCb[id] = std::move(done);
        }
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "pick", "(ILjava/lang/String;)V");
        jstring k = mxa::jstr(e, kind);
        e->CallStaticVoidMethod(mxa::activityClass(), m, id, k);
        e->DeleteLocalRef(k);
        mxa::takeException(e);
    };
    pf.openUrl = [](const std::string& url) {
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "openUrl", "(Ljava/lang/String;)V");
        jstring u = mxa::jstr(e, url);
        e->CallStaticVoidMethod(mxa::activityClass(), m, u);
        e->DeleteLocalRef(u);
        mxa::takeException(e);
    };
    pf.publishFile = [](const std::string& path, const std::string& kind) {
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "publish", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        jstring p = mxa::jstr(e, path), k = mxa::jstr(e, kind);
        auto r = static_cast<jstring>(e->CallStaticObjectMethod(mxa::activityClass(), m, p, k));
        std::string out = mxa::takeException(e).empty() ? mxa::str(e, r) : std::string();
        e->DeleteLocalRef(p);
        e->DeleteLocalRef(k);
        if (r) e->DeleteLocalRef(r);
        return out;
    };
    pf.nowPlaying = [](bool playing, const std::string& title, const std::string& artist) {
        JNIEnv* e = mxa::env();
        static jmethodID m = staticMethod(e, "nowPlaying", "(ZLjava/lang/String;Ljava/lang/String;)V");
        jstring t = mxa::jstr(e, title), a = mxa::jstr(e, artist);
        e->CallStaticVoidMethod(mxa::activityClass(), m, static_cast<jboolean>(playing), t, a);
        e->DeleteLocalRef(t);
        e->DeleteLocalRef(a);
        mxa::takeException(e);
    };
    return pf;
}

}  // namespace

void android_main(android_app* app) {
    g_and = app;
    app->onAppCmd = onCmd;
    app->onInputEvent = onInput;
    if (app->config) g_density = std::max(1.f, static_cast<float>(AConfiguration_getDensity(app->config)) / 160.f);

    mxa::jniInit(app->activity->vm, app->activity->clazz);
    registerNatives(mxa::env());

    if (!eglInit()) { LOGI("EGL не запустился"); return; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = true;
    ImGui::StyleColorsDark();
    mx::ui::Fonts fonts;
    addFonts(io, fonts);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    mx::App mxApp(app->activity->internalDataPath ? app->activity->internalDataPath : ".", makePlatform());
    g_mx = &mxApp;
    std::string err;
    if (!mxApp.init(&err)) LOGI("init: %s", err.c_str());
    GlGfx gfx;
    mx::ui::Ui ui(mxApp, gfx, fonts);
    g_ui = &ui;
    ui.init();

    auto last = std::chrono::steady_clock::now();
    while (true) {
        int events = 0;
        android_poll_source* src = nullptr;
        // без окна (экран выключен, приложение свёрнуто) рисовать нечего: просыпаемся редко, музыка играет сама
        int timeout = g_win != EGL_NO_SURFACE ? 0 : 100;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&src)) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) {
                g_ui = nullptr;
                mxApp.shutdown();
                g_mx = nullptr;
                ImGui_ImplOpenGL3_Shutdown();
                ImGui::DestroyContext();
                return;
            }
            timeout = 0;
        }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        if (g_win == EGL_NO_SURFACE || !app->window) {
            mxApp.frame();
            continue;
        }

        const float s = g_density * static_cast<float>(std::clamp(mxApp.lib.getNum("uiScale", 1.0), 0.75, 2.0));
        int pw = ANativeWindow_getWidth(app->window), ph = ANativeWindow_getHeight(app->window);
        // видимая часть (без системных полос); пока система её не сообщила — всё окно
        ARect r = g_rect;
        if (r.right <= r.left || r.bottom <= r.top || r.right > pw || r.bottom > ph) r = ARect{0, 0, pw, ph};
        const float ox = static_cast<float>(r.left), oy = static_cast<float>(r.top);
        g_offX = ox;
        g_offY = oy;
        io.DisplaySize = ImVec2(static_cast<float>(r.right - r.left) / s, static_cast<float>(r.bottom - r.top) / s);
        io.DeltaTime = std::max(dt, 1.f / 1000.f);
        feedTouch(io);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        mxApp.frame();
        ui.frame(dt, io.DisplaySize);
        ImGui::Render();

        // интерфейс считается в «точках», а рисуется в пикселях экрана
        ImDrawData* dd = ImGui::GetDrawData();
        for (ImDrawList* l : dd->CmdLists) {
            for (ImDrawVert& v : l->VtxBuffer) { v.pos.x = v.pos.x * s + ox; v.pos.y = v.pos.y * s + oy; }
            for (ImDrawCmd& c : l->CmdBuffer) {
                c.ClipRect.x = c.ClipRect.x * s + ox; c.ClipRect.y = c.ClipRect.y * s + oy;
                c.ClipRect.z = c.ClipRect.z * s + ox; c.ClipRect.w = c.ClipRect.w * s + oy;
            }
        }
        dd->DisplaySize = ImVec2(static_cast<float>(pw), static_cast<float>(ph));

        glViewport(0, 0, pw, ph);
        unsigned bg = g_bg.load();  // под системными полосами — цвет фона темы
        glClearColor(static_cast<float>((bg >> 16) & 0xFF) / 255.f, static_cast<float>((bg >> 8) & 0xFF) / 255.f,
                     static_cast<float>(bg & 0xFF) / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(dd);
        eglSwapBuffers(g_dpy, g_win);
    }
}
