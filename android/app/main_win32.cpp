// Mixtape для Windows: окно Win32, DirectX 11, Dear ImGui. Весь остальной код платформонезависим.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wininet.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <list>
#include <string>
#include <vector>

#include "app.hpp"
#include "ui/imgui_inc.hpp"  // до backends: включает операторы ImVec2
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
#include "imgui_internal.h"  // очередь событий ввода — для масштаба интерфейса
#include "ui/ui.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr int kResTeto = 101;
constexpr int kHotkeyBase = 0x4D58;  // «MX»

std::string toUtf8(const wchar_t* w, int n = -1) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), len, nullptr, nullptr);
    if (n == -1 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
std::wstring toWide(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    w.pop_back();
    return w;
}

// ---------- DirectX 11 ----------
struct Dx {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

    bool init(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC d{};
        d.BufferCount = 2;
        d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.OutputWindow = hwnd;
        d.SampleDesc.Count = 1;
        d.Windowed = TRUE;
        d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL lv;
        const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2, D3D11_SDK_VERSION, &d, &sc, &dev, &lv, &ctx);
        if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, want, 2, D3D11_SDK_VERSION, &d, &sc, &dev, &lv, &ctx);
        if (FAILED(hr)) return false;
        makeTarget();
        return true;
    }
    void makeTarget() {
        ID3D11Texture2D* back = nullptr;
        sc->GetBuffer(0, IID_PPV_ARGS(&back));
        if (back) { dev->CreateRenderTargetView(back, nullptr, &rtv); back->Release(); }
    }
    void dropTarget() { if (rtv) { rtv->Release(); rtv = nullptr; } }
    void resize(UINT w, UINT h) {
        if (!sc || w == 0 || h == 0) return;
        dropTarget();
        sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        makeTarget();
    }
};

class DxGfx : public mx::ui::Gfx {
public:
    explicit DxGfx(Dx& dx) : dx_(dx) {}
    ImTextureID create(const unsigned char* rgba, int w, int h) override {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(w);
        td.Height = static_cast<UINT>(h);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = rgba;
        sd.SysMemPitch = static_cast<UINT>(w) * 4;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(dx_.dev->CreateTexture2D(&td, &sd, &tex)) || !tex) return 0;
        ID3D11ShaderResourceView* srv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = td.Format;
        vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MipLevels = 1;
        dx_.dev->CreateShaderResourceView(tex, &vd, &srv);
        tex->Release();
        return reinterpret_cast<ImTextureID>(srv);
    }
    void destroy(ImTextureID id) override {
        if (id) reinterpret_cast<ID3D11ShaderResourceView*>(id)->Release();
    }

private:
    Dx& dx_;
};

// ---------- глобальное состояние окна ----------
Dx g_dx;
mx::App* g_app = nullptr;
mx::ui::Ui* g_ui = nullptr;
bool g_minimized = false;

// ---------- заголовок окна в цвет приложения (вместо белой полосы Windows) ----------
// Windows 10 20H1+: тёмный режим заголовка; Windows 11: ещё и точный цвет фона, текста и рамки.
// Значения атрибутов заданы числами: в старых SDK этих констант нет. На старых системах вызовы просто не срабатывают.
void setTitleBarColor(HWND hwnd, int r, int g, int b) {
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    const COLORREF caption = RGB(r, g, b), text = RGB(235, 232, 245), border = RGB(r + (255 - r) / 8, g + (255 - g) / 8, b + (255 - b) / 8);
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);
    DwmSetWindowAttribute(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof text);
    DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
}

// ---------- полноэкранный режим: окно без рамки поверх всего монитора ----------
bool g_fullscreen = false;
WINDOWPLACEMENT g_prevPlacement{sizeof(WINDOWPLACEMENT)};

void setFullscreen(HWND hwnd, bool on) {
    if (on == g_fullscreen) return;
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    if (on) {
        MONITORINFO mi{sizeof(mi)};
        if (!GetWindowPlacement(hwnd, &g_prevPlacement) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_prevPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g_fullscreen = on;
}

// Шрифты. ImGui 1.91 рисует текст из заранее «запечённого» атласа: если печь крупно, а рисовать мелко,
// буквы мутнеют и становятся тонкими. Поэтому печём два размера: 18 px для обычного текста (рисуется в 13–20 px)
// и 34 px для заголовков; drawText сам выбирает подходящий.
void addFonts(ImGuiIO& io, mx::ui::Fonts& out) {
    wchar_t winDir[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::string fonts = toUtf8(winDir) + "\\Fonts\\";
    auto exists = [&](const char* name) { return GetFileAttributesW(toWide(fonts + name).c_str()) != INVALID_FILE_ATTRIBUTES; };
    // Диапазоны должны жить, пока жив атлас, и не переезжать в памяти (std::list адреса не меняет).
    static std::list<ImVector<ImWchar>> keep;
    auto ranges = [&] {
        ImVector<ImWchar> r;
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        static const ImWchar extra[] = {0x2010, 0x2027, 0x2190, 0x21FF, 0x2212, 0x2212, 0x00D7, 0x00D7, 0x2026, 0x2026, 0x00B0, 0x00B0, 0};
        b.AddRanges(extra);
        b.BuildRanges(&r);
        keep.push_back(r);
        return keep.back().Data;
    };
    auto load = [&](std::initializer_list<const char*> names, float px, bool cjk) -> ImFont* {
        ImFontConfig cfg;
        cfg.OversampleH = px < 24.f ? 3 : 1;
        cfg.OversampleV = 1;
        ImFont* f = nullptr;
        for (const char* n : names)
            if (exists(n)) { f = io.Fonts->AddFontFromFileTTF((fonts + n).c_str(), px, &cfg, ranges()); break; }
        if (!f) return nullptr;
        // японские и китайские названия песен — только в мелком размере, иначе атлас раздувается до сотни мегабайт;
        // крупный текст с такими символами drawText рисует мелким шрифтом, увеличенным до нужного размера
        if (cjk)
            for (const char* c : {"YuGothR.ttc", "meiryo.ttc", "msyh.ttc", "malgun.ttf"}) {
                if (!exists(c)) continue;
                ImFontConfig mc;
                mc.MergeMode = true;
                mc.OversampleH = 1;
                io.Fonts->AddFontFromFileTTF((fonts + c).c_str(), px, &mc, io.Fonts->GetGlyphRangesJapanese());
                break;
            }
        return f;
    };
    out.regular = load({"segoeui.ttf"}, 18.f, true);
    out.bold = load({"seguisb.ttf", "segoeuib.ttf"}, 18.f, true);   // полужирный читается мягче жирного
    out.regularL = load({"segoeui.ttf"}, 34.f, false);
    out.boldL = load({"segoeuib.ttf", "seguisb.ttf"}, 34.f, false);
    if (!out.regular) out.regular = io.Fonts->AddFontDefault();
    if (!out.bold) out.bold = out.regular;
    io.FontGlobalScale = 15.f / 18.f;  // поля ввода ImGui (ui.cpp поправляет под размер текста)
}

std::vector<std::string> pickFiles(HWND owner, bool multi, const wchar_t* filter, bool save, const std::wstring& defName) {
    std::vector<wchar_t> buf(32768, L'\0');
    if (save) wcsncpy_s(buf.data(), buf.size(), defName.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST) | (multi ? OFN_ALLOWMULTISELECT : 0);
    bool ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    std::vector<std::string> out;
    if (!ok) return out;
    std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (*p == L'\0') { out.push_back(toUtf8(first.c_str())); return out; }  // выбран один файл
    while (*p) {  // каталог, затем имена
        std::wstring name = p;
        out.push_back(toUtf8((first + L"\\" + name).c_str()));
        p += name.size() + 1;
    }
    return out;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
        case WM_SIZE:
            g_minimized = wp == SIZE_MINIMIZED;
            if (wp != SIZE_MINIMIZED) g_dx.resize(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = 760;
            mm->ptMinTrackSize.y = 520;
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // Alt не открывает системное меню
            break;
        case WM_SYSKEYDOWN:  // Alt+Enter — на весь экран и обратно
            if (wp == VK_RETURN && (lp & (1 << 29))) {
                setFullscreen(hwnd, !g_fullscreen);
                if (g_app) { g_app->lib.set("fullscreen", mx::Json(g_fullscreen)); g_app->touch(); }
                return 0;
            }
            break;
        case WM_HOTKEY:  // мультимедийные клавиши работают и в свёрнутом окне
            if (g_app) {
                switch (wp - kHotkeyBase) {
                    case 0: g_app->media(mx::App::Media::PlayPause); break;
                    case 1: g_app->media(mx::App::Media::Next); break;
                    case 2: g_app->media(mx::App::Media::Prev); break;
                    case 3: g_app->media(mx::App::Media::Stop); break;
                    default: break;
                }
            }
            return 0;
        case WM_DROPFILES: {
            HDROP h = reinterpret_cast<HDROP>(wp);
            UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::string> paths;
            for (UINT i = 0; i < n; ++i) {
                UINT len = DragQueryFileW(h, i, nullptr, 0);
                std::wstring w(len + 1, L'\0');
                DragQueryFileW(h, i, w.data(), len + 1);
                paths.push_back(toUtf8(w.c_str()));
            }
            DragFinish(h);
            if (g_ui) g_ui->onFilesDropped(paths);
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);  // окно исчезает сразу, пока сохраняется библиотека
            if (g_app) g_app->shutdown();
            std::_Exit(0);  // фоновые потоки закрываются вместе с процессом
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::string appDataDir() {
    PWSTR p = nullptr;
    std::string base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) base = toUtf8(p);
    CoTaskMemFree(p);
    if (base.empty()) base = ".";
    std::string dir = base + "\\Mixtape";
    CreateDirectoryW(toWide(dir).c_str(), nullptr);
    return dir;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MixtapeWindow";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Mixtape", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    if (!g_dx.init(hwnd)) {
        MessageBoxW(hwnd, L"Не удалось запустить DirectX 11.", L"Mixtape", MB_ICONERROR);
        return 1;
    }
    DragAcceptFiles(hwnd, TRUE);
    RegisterHotKey(hwnd, kHotkeyBase + 0, 0, VK_MEDIA_PLAY_PAUSE);
    RegisterHotKey(hwnd, kHotkeyBase + 1, 0, VK_MEDIA_NEXT_TRACK);
    RegisterHotKey(hwnd, kHotkeyBase + 2, 0, VK_MEDIA_PREV_TRACK);
    RegisterHotKey(hwnd, kHotkeyBase + 3, 0, VK_MEDIA_STOP);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // Клавиатурная навигация ImGui выключена: интерфейс рисуется вручную, а с ней пробел «нажимал»
    // последнюю кликнутую кнопку (например, «Следующая») вместе с паузой, стрелки двигали невидимый фокус.
    ImGui::StyleColorsDark();
    ImGui::GetStyle().AntiAliasedLines = true;
    ImGui::GetStyle().AntiAliasedFill = true;
    mx::ui::Fonts fonts;
    addFonts(io, fonts);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dx.dev, g_dx.ctx);

    // возможности системы для приложения
    mx::Platform pf;
    pf.pickAudioFiles = [hwnd] {
        return pickFiles(hwnd, true, L"Музыка\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.wma;*.opus\0Все файлы\0*.*\0", false, L"");
    };
    pf.pickSavePath = [hwnd](const std::string& def) {
        auto r = pickFiles(hwnd, false, L"Копия Mixtape (*.json)\0*.json\0", true, toWide(def));
        return r.empty() ? std::string() : r[0];
    };
    pf.pickOpenPath = [hwnd] {
        auto r = pickFiles(hwnd, false, L"Копия Mixtape (*.json)\0*.json\0Все файлы\0*.*\0", false, L"");
        return r.empty() ? std::string() : r[0];
    };
    pf.pickSaveAudio = [hwnd](const std::string& def) {
        auto r = pickFiles(hwnd, false, L"Песня\0*.mp3;*.m4a;*.flac;*.ogg;*.wav;*.aac\0Все файлы\0*.*\0", true, toWide(def));
        return r.empty() ? std::string() : r[0];
    };
    pf.pickImage = [hwnd] {
        auto r = pickFiles(hwnd, false, L"Картинка или GIF\0*.gif;*.png;*.jpg;*.jpeg;*.bmp\0Все файлы\0*.*\0", false, L"");
        return r.empty() ? std::string() : r[0];
    };
    pf.isOnline = [] {
        DWORD flags = 0;
        return InternetGetConnectedState(&flags, 0) != FALSE;
    };
    pf.copyText = [hwnd](const std::string& s) {
        std::wstring w = toWide(s);
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (g) {
            memcpy(GlobalLock(g), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            SetClipboardData(CF_UNICODETEXT, g);
        }
        CloseClipboard();
    };
    pf.resource = [inst](const std::string& name) -> std::string {
        if (name != "teto.png") return {};
        HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(kResTeto), RT_RCDATA);
        if (!r) return {};
        HGLOBAL g = LoadResource(inst, r);
        if (!g) return {};
        return std::string(static_cast<const char*>(LockResource(g)), SizeofResource(inst, r));
    };

    pf.setFullscreen = [hwnd](bool on) { setFullscreen(hwnd, on); };
    pf.isFullscreen = [] { return g_fullscreen; };
    pf.setTitleBarColor = [hwnd](int r, int g, int b) { setTitleBarColor(hwnd, r, g, b); };
    setTitleBarColor(hwnd, 9, 8, 14);  // сразу тёмный, ещё до первого кадра — без белой вспышки

    mx::App app(appDataDir(), pf);
    g_app = &app;
    std::string err;
    if (!app.init(&err)) {
        MessageBoxW(hwnd, toWide("Не удалось запустить приложение:\n" + err).c_str(), L"Mixtape", MB_ICONERROR);
        return 1;
    }
    DxGfx gfx(g_dx);
    mx::ui::Ui ui(app, gfx, fonts);
    g_ui = &ui;
    ui.init();

    ShowWindow(hwnd, show);
    if (app.lib.getBool("fullscreen", false)) setFullscreen(hwnd, true);  // как закрыли, так и открываем  // как закрыли, так и открываем
    UpdateWindow(hwnd);

    auto last = std::chrono::steady_clock::now();
    std::wstring lastTitle;
    bool running = true;
    while (running) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
            if (m.message == WM_QUIT) running = false;
        }
        if (!running) break;

        // название окна = что играет (видно и на панели задач)
        std::wstring title = app.cur ? toWide(app.cur->artist.empty() ? app.cur->title : app.cur->artist + " — " + app.cur->title) : L"Mixtape";
        if (title != lastTitle) { SetWindowTextW(hwnd, title.c_str()); lastTitle = title; }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        if (g_minimized) {
            // свёрнуто: рисовать нечего, но музыка, очередь и таймеры продолжают работать
            app.frame();
            Sleep(40);
            continue;
        }
        // Масштаб интерфейса: интерфейс видит экран в s раз меньше (и мышь в тех же единицах),
        // а готовые вершины растягиваются обратно. Так крупнее/мельче становится всё сразу.
        const float s = std::clamp(static_cast<float>(app.lib.getNum("uiScale", 1.0)), 0.75f, 2.f);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        if (s != 1.f) {
            io.DisplaySize = ImVec2(io.DisplaySize.x / s, io.DisplaySize.y / s);
            for (ImGuiInputEvent& e : ImGui::GetCurrentContext()->InputEventsQueue)
                if (e.Type == ImGuiInputEventType_MousePos) { e.MousePos.PosX /= s; e.MousePos.PosY /= s; }
        }
        ImGui::NewFrame();
        app.frame();
        ui.frame(dt, io.DisplaySize);
        ImGui::Render();
        if (s != 1.f) {
            ImDrawData* dd = ImGui::GetDrawData();
            for (ImDrawList* l : dd->CmdLists) {
                for (ImDrawVert& v : l->VtxBuffer) { v.pos.x *= s; v.pos.y *= s; }
                for (ImDrawCmd& c : l->CmdBuffer) { c.ClipRect.x *= s; c.ClipRect.y *= s; c.ClipRect.z *= s; c.ClipRect.w *= s; }
            }
            dd->DisplaySize = ImVec2(dd->DisplaySize.x * s, dd->DisplaySize.y * s);
        }
        const float clear[4] = {0.03f, 0.03f, 0.05f, 1.f};
        g_dx.ctx->OMSetRenderTargets(1, &g_dx.rtv, nullptr);
        g_dx.ctx->ClearRenderTargetView(g_dx.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_dx.sc->Present(1, 0);
    }
    app.shutdown();
    std::_Exit(0);
}
