// Интерфейс Mixtape на Dear ImGui. Всё рисуется через ImDrawList, поэтому вид полностью наш (тёмная тема в духе Spotify).
// Файлы: ui.cpp (каркас, боковая панель, мини-плеер), views.cpp (списки), fullplayer.cpp (проигрыватель, текст,
// фрагменты), modals.cpp (настройки, перенос, плейлисты, пасхалка).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "app.hpp"
#include "draw.hpp"
#include "textures.hpp"

namespace mx::ui {

enum class Modal { None, Settings, Import, NewPlaylist, AddToPlaylist, OwnLyrics, TapSync, Teto, Confirm, Profile };

struct ThemeDef {
    const char* id;
    const char* name;
    float hue, sat;
    int r, g, b;  // цвет фона
    int deco = 0;  // узор на фоне: 0 — нет, 1 — волны, 2 — рваная бумага, 3 — полутон в углах, 4 — прожектор из точек
};

const ThemeDef& themeById(const std::string& id);
int themeCount();
const ThemeDef& themeAt(int i);

class Ui {
public:
    Ui(App& app, Gfx& gfx, const Fonts& fonts);
    void init();                                  // после создания окна: загружает картинку Тето
    void frame(float dt, ImVec2 display);         // между NewFrame() и Render()
    void onFilesDropped(const std::vector<std::string>& paths);

private:
    // ----- примитивы (ui.cpp) -----
    struct Hit { bool hovered = false, clicked = false, held = false; ImGuiID id = 0; };
    Hit hit(const char* id, ImVec2 pos, ImVec2 size);
    float hv(ImGuiID id, bool hovered, float speed = 14.f);   // плавное наведение 0..1
    // «Ощущение» кнопки: наведение, нажатие (плавно) и волна после клика (0..1, или -1 если волны нет).
    struct Fx { float k = 0, press = 0, pulse = -1; };
    Fx fx(const Hit& h);
    // Пункт списка/меню: подложка при наведении, выбранный — цветная подложка и полоска слева, растущая плавно.
    struct ItemFx { float k = 0, on = 0; };
    ItemFx listItem(ImDrawList* dl, const Hit& h, ImVec2 a, ImVec2 b, bool active, float rounding, bool bar = true);
    bool iconButton(const char* id, ImDrawList* dl, ImVec2 center, float box, Icon ic, bool active = false,
                    const char* tip = nullptr, ImU32 col = 0);
    bool pill(const char* id, ImDrawList* dl, ImVec2 pos, ImVec2 size, const std::string& label, bool primary = false,
              bool active = false, const Icon* ic = nullptr);
    float pillWidth(const std::string& label, bool withIcon) const;   // ширина кнопки по тексту
    bool card(const char* id, ImDrawList* dl, ImVec2 pos, ImVec2 size, Icon ic, const std::string& title,
              const std::string& sub, bool primary);
    bool toggleSwitch(const char* id, ImDrawList* dl, ImVec2 pos, bool value);
    struct SliderOut { bool active = false, released = false, hovered = false; float value = 0; };
    SliderOut slider(const char* id, ImDrawList* dl, ImVec2 pos, float w, float value01, float trackH = 5.f);
    bool textField(const char* id, ImVec2 pos, float w, std::string& value, const char* hint, bool multiline = false, float h = 0,
                   bool password = false);
    void tip(const char* s) { if (showTips_) tip_ = s; }
    bool calm() const { return setting("calm", false); }       // без лишнего движения
    float radius(float r) const { return r * radK_; }           // скругление углов из настроек
    void readPrefs();                                           // раз в кадр: настройки вида из библиотеки
    int segmented(const char* id, ImDrawList* dl, ImVec2 pos, float h, const std::vector<std::string>& opts, int cur, float* outW = nullptr);
    ImTextureID coverOf(const Track& t) { return tex_.cover(t.cover); }
    void drawCover(ImDrawList* dl, const Track& t, ImVec2 p, float size, float rounding);
    float hueOf(const std::string& s) const;
    // Заголовок и подпись под ним, по центру midY с учётом настоящей высоты текста (она зависит от размера текста).
    // Начало подписи можно сделать ссылкой на исполнителя: link->text — это имя, id — id кнопки, clicked — нажали ли.
    struct SubLink { const char* id = nullptr; std::string text; bool clicked = false; };
    void twoLines(ImDrawList* dl, float x, float midY, float maxW, const std::string& title, float fsT, ImU32 colT,
                  const std::string& sub, float fsS, ImU32 colS, SubLink* link = nullptr);
    void goArtist(const std::string& names, bool strict = false);   // открыть страницу исполнителя (со следующего кадра)
    bool artistLabel(const char* id, ImDrawList* dl, ImVec2 pos, const std::string& shown, float fs, ImU32 col, const Track& t);
    void albumShelf(const char* id, const char* heading, const std::vector<YtmAlbum>& list, bool& open, float w, float& y);
    void updatePalette();
    const ThemeDef& theme() const;
    bool setting(const char* k, bool def) const { return app_.lib.getBool(k, def); }

    // ----- каркас -----
    void drawRoot();
    void drawSidebar(ImVec2 pos, ImVec2 size);
    void drawTopBar(ImVec2 pos, float w);
    void drawMini();
    void drawToast();
    void drawTeto();
    void drawRain();
    void shortcuts();
    void toggleFullscreen();
    void pickCustomTeto();    // своя картинка или GIF вместо Тето (копируется в папку данных)
    void pickCustomBg();      // выбрать свою картинку или GIF для заднего фона
    void resetBg();           // убрать свой фон
    bool drawCustomBg(ImDrawList* dl);  // true — свой фон нарисован
    void resetTeto();         // вернуть встроенную Тето  // F11, Alt+Enter или кнопка справа вверху
    void openModal(Modal m);
    void closeModal();

    // ----- списки (views.cpp) -----
    void drawContent(ImVec2 pos, ImVec2 size);
    void viewHome(ImVec2 size);
    void viewList(ImVec2 size);
    void viewStats(ImVec2 size);   // страница «Статистика»
    void viewFriends(ImVec2 size); // страница «Друзья»
    void drawFriendAvatar(ImDrawList* dl, ImVec2 c, float r, const FriendContact& p);
    std::string frLink_;                                          // поле «ссылка от друга»
    void listHeader(float w, const std::string& title, const std::string& sub, const std::vector<const Track*>& list, float& y);
    void rows(const std::vector<const Track*>& list, float w, bool inPlaylist, const char* ctx);
    void row(const Track& t, const std::vector<std::string>& ids, ImVec2 pos, float w, int index, bool inPlaylist);

    // ----- проигрыватель на весь экран (fullplayer.cpp) -----
    void drawFull();
    void drawDeck(ImVec2 center, float ds);
    void drawLyrics(ImVec2 pos, ImVec2 size);
    void drawFragment(ImVec2 pos, ImVec2 size);
    void drawProgress(ImVec2 pos, float w, bool big);
    void drawTransport(ImVec2 center, float scale);
    void openFull(bool open);

    // ----- окна (modals.cpp) -----
    void drawModal();
    void modalSettings(ImVec2 origin, ImVec2 size);
    void modalImport(ImVec2 origin, ImVec2 size);
    void modalNewPlaylist(ImVec2 origin, ImVec2 size);
    void modalAddToPlaylist(ImVec2 origin, ImVec2 size);
    void modalOwnLyrics(ImVec2 origin, ImVec2 size);
    void modalTap(ImVec2 origin, ImVec2 size);
    void modalTeto(ImVec2 origin, ImVec2 size);
    void modalConfirm(ImVec2 origin, ImVec2 size);
    void modalProfile(ImVec2 origin, ImVec2 size);
    // профиль: аватарка (картинка или первая буква на цвете профиля) с рамкой; r — радиус
    void drawAvatar(ImDrawList* dl, ImVec2 center, float r);
    void drawAvatarOf(ImDrawList* dl, ImVec2 center, float r, ImTextureID tex, const std::string& name, int color, int frame);
    void drawFrame(ImDrawList* dl, ImVec2 center, float r, int frame, ImU32 col);
    static const char* const kFrameNames[Profile::kFrameCount];
    ImU32 profileColor(int i) const;
    void pickImageFile(std::function<void(std::string)> done);  // окно выбора картинки (на телефоне — системное)
    void pickAvatar();
    // перекраска темы: свой оттенок (tint_<id>) и глубина фона (bgLevel)
    float themeHue(const ThemeDef& th) const;
    ImVec4 themeBg(const ThemeDef& th) const;
    static bool themeTintable(const std::string& id) { return id != "dynamic" && id != "custom" && id != "mono"; }
    void confirm(const std::string& text, std::function<void()> yes);

    // ----- состояние -----
    App& app_;
    Textures tex_;
    Fonts F_;
    float time_ = 0, dt_ = 0;
    ImVec2 W_{1280, 800};
    Palette pal_;
    Anim tetoA_, fullA_, miniA_, modalA_, spinSpeed_, armA_;
    float spin_ = 0, lyScroll_ = 0;
    // настройки вида (читаются в readPrefs)
    float rowH_ = 64.f, radK_ = 1.f, panelA_ = 1.f, tetoK_ = 1.f;
    bool showNum_ = false, showBadge_ = true, showTips_ = true, liveSearch_ = true;
    float tetoReserve_ = 0;   // ширина справа, занятая Тето (из прошлого кадра)
    float contentWidth(float avail) const;   // ширина колонки списков с учётом Тето и предела
    std::map<ImGuiID, float> hover_;
    std::map<ImGuiID, float> press_;          // плавное «утапливание» кнопок
    std::map<ImGuiID, float> pulse_;          // время последнего клика (для волны)
    std::map<ImGuiID, ImVec2> seg_;           // положение и ширина бегунка в переключателях
    std::string tip_;
    std::string hoverTip_;  // текст подсказки, собранный на лету (tip() хранит только указатель на время кадра)
    ViewKind lastView_ = ViewKind::Home;
    std::string lastPlaylist_;
    double viewStart_ = 0;
    bool scrollTop_ = false;                  // при смене страницы список прокручивается в начало
    bool albumsOpen_ = false, singlesOpen_ = false;  // раскрыты ли все альбомы / синглы исполнителя

    // поиск, ползунки
    std::string searchBuf_, searchSent_;
    double searchEdit_ = 0;
    bool focusSearch_ = false;
    bool dragSeek_ = false;
    float dragFrac_ = 0;

    // окна
    Modal modal_ = Modal::None;
    int settingsTab_ = 0, importTab_ = 0;
    std::string importLink_, importName_, importList_, newPlName_, ownLyrics_, confirmText_;
    std::string profName_, profStatus_, acName_;  // поля окна «Профиль»
    int avatarVer_ = -1;                // какая версия аватарки загружена в текстуру
    bool avatarBusy_ = false;           // картинка для аватарки обрабатывается
    std::function<void()> confirmYes_;
    Track pickTrack_;
    bool wantFull_ = false;
    bool fullFocus_ = false;
    bool started_ = false;
    bool customTeto_ = false; // вместо Тето — своя картинка
    bool customBg_ = false;   // на заднем фоне — своя картинка или GIF
    ImU32 titleBg_ = 0;       // цвет, в который уже покрашен заголовок окна    // стартовый экран уже выбран  // проигрыватель только что открыт: один раз взять фокус
    bool modalFirst_ = false;

    // редактор фрагментов
    struct Fragment {
        bool open = false, keep = true;
        double s = 0, e = 0;
        int drag = 0;  // 1 — начало, 2 — конец
        bool init = false, previewing = false;
    } fr_;

    // настройка времени текста нажатиями
    std::vector<std::string> tapLines_;
    std::vector<double> tapTimes_;

    // пасхалка
    int logoClicks_ = 0;
    double logoLast_ = 0, rainUntil_ = 0;
    struct Drop { float x, y, vy, rot, vr, size; };
    std::vector<Drop> rain_;
    bool tetoFirst_ = false;
};

}  // namespace mx::ui
