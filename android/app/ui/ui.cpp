#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "imgui_internal.h"  // BringWindowToDisplayFront
#include "imgui_stdlib.h"
#include "text.hpp"
#include "i18n.hpp"

namespace fs = std::filesystem;

namespace mx::ui {

namespace {
constexpr float kSide = 236.f;      // ширина боковой панели
constexpr float kMiniH = 84.f;
const ThemeDef kThemes[] = {
    {"dynamic", "По песне", 268, 0.92f, 9, 8, 14},   {"violet", "Фиолетовая", 268, 0.95f, 9, 8, 14},
    {"ocean", "Океан", 200, 0.90f, 6, 10, 16},       {"sunset", "Закат", 18, 0.95f, 14, 8, 8},
    {"forest", "Лес", 140, 0.60f, 7, 11, 9},         {"rose", "Роза", 335, 0.90f, 14, 7, 11},
    {"gold", "Золото", 42, 0.90f, 12, 10, 6},        {"mono", "Монохром", 0, 0.0f, 8, 8, 8},
    {"teto", "Тето", 355, 0.85f, 14, 6, 8},
    // яркие темы с узором: волны в углах, тонкие линии, точки и значки
    {"coral", "Коралл", 356, 0.78f, 104, 14, 20, 1},   {"mint", "Мята", 166, 0.55f, 8, 62, 60, 1},
    {"lavender", "Лаванда", 268, 0.60f, 50, 28, 92, 1}, {"navy", "Синева", 214, 0.72f, 14, 34, 86, 1},
    {"peach", "Персик", 20, 0.85f, 112, 42, 18, 1},
    {"torn", "Разрыв", 356, 0.80f, 30, 30, 33, 2},     {"halftone", "Полутон", 356, 0.90f, 7, 6, 8, 3},
    {"spot", "Прожектор", 358, 0.95f, 5, 4, 6, 4},
    {"custom", "Свой цвет", 268, 0.92f, 9, 8, 14}};
}  // namespace

const ThemeDef& themeById(const std::string& id) {
    for (const auto& t : kThemes) if (id == t.id) return t;
    return kThemes[0];
}
int themeCount() { return static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0])); }
const ThemeDef& themeAt(int i) { return kThemes[i]; }

Ui::Ui(App& app, Gfx& gfx, const Fonts& fonts) : app_(app), tex_(app, gfx), F_(fonts) {
    mobile_ = app.platform.mobile;
    setTextFonts(fonts);
    pal_.hue = 268;
    pal_.sat = 0.92f;
}

void Ui::init() {
    std::string dir = app_.dataDir;
    for (const char* ext : {"gif", "png", "jpg", "jpeg", "bmp"}) {
        std::string bytes;
        if (readFile((fs::u8path(dir) / (std::string("ui_teto.") + ext)).u8string(), bytes)) { tex_.setTeto(bytes); customTeto_ = true; return; }
    }
    if (app_.platform.resource) tex_.setTeto(app_.platform.resource("teto.png"));
}

namespace {
const char* const kTetoExts[] = {"gif", "png", "jpg", "jpeg", "bmp"};
}

void Ui::pickCustomTeto() {
    pickImageFile([this](std::string src) { useCustomTeto(src); });
}

void Ui::useCustomTeto(const std::string& src) {
    std::string bytes;
    if (!readFile(src, bytes) || bytes.empty()) { app_.toast("Не удалось открыть файл"); return; }
    if (bytes.size() > 40u << 20) { app_.toast("Файл слишком большой (больше 40 МБ)"); return; }
    std::string ext = lower(fs::u8path(src).extension().u8string());
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    bool known = false;
    for (const char* e : kTetoExts) known = known || ext == e;
    if (!known) ext = "png";  // stb всё равно определит формат по содержимому
    // старые свои картинки убираем, чтобы при запуске подхватилась именно новая
    std::error_code ec;
    for (const char* e : kTetoExts) fs::remove(fs::u8path(app_.dataDir) / (std::string("ui_teto.") + e), ec);
    std::string err;
    if (!writeFileAtomic((fs::u8path(app_.dataDir) / ("ui_teto." + ext)).u8string(), bytes, &err)) {
        app_.toast("Не удалось сохранить картинку: " + err);
        return;
    }
    tex_.setTeto(bytes);
    customTeto_ = true;
    if (!app_.tetoOn()) { app_.lib.set("tetoOn", Json(true)); app_.touch(); }
    app_.toast("Готово: теперь в углу танцует ваша картинка");
}

void Ui::resetTeto() {
    std::error_code ec;
    for (const char* e : kTetoExts) fs::remove(fs::u8path(app_.dataDir) / (std::string("ui_teto.") + e), ec);
    if (app_.platform.resource) tex_.setTeto(app_.platform.resource("teto.png"));
    customTeto_ = false;
    app_.toast("Тето вернулась");
}

void Ui::readPrefs() {
    setLanguage(languageFromId(app_.lib.getStr("lang", "ru")));
    static const float kRowHs[] = {54.f, 68.f, 82.f};
    static const float kRads[] = {0.35f, 1.f, 1.45f};
    int d = std::clamp(static_cast<int>(app_.lib.getNum("density", 1)), 0, 2);
    int c = std::clamp(static_cast<int>(app_.lib.getNum("corners", 1)), 0, 2);
    rowH_ = kRowHs[d];
    radK_ = kRads[c];
    panelA_ = std::clamp(static_cast<float>(app_.lib.getNum("panelAlpha", 0.6)), 0.2f, 1.f);
    tetoK_ = std::clamp(static_cast<float>(app_.lib.getNum("tetoScale", 1.0)), 0.5f, 1.5f);
    showNum_ = setting("rowNums", false);
    showBadge_ = setting("badges", true);
    showTips_ = setting("tips", true) && !mobile_;  // на телефоне наведения нет — подсказки только мешают
    liveSearch_ = setting("liveSearch", true);
    // размер текста: по умолчанию «Крупный» — мелкий текст читался плохо
    float textK = std::clamp(static_cast<float>(app_.lib.getNum("textSize", 1.12)), 0.9f, 1.4f);
    float uiK = std::clamp(static_cast<float>(app_.lib.getNum("uiScale", 1.0)), 0.75f, 2.f);
    setTextScale(textK, uiK);
    // поля ввода ImGui: 15 точек при любом размере, в котором запечён шрифт (на телефоне он крупнее)
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = (io.Fonts->Fonts.Size > 0 ? 15.f / io.Fonts->Fonts[0]->FontSize : 15.f / 18.f) * textK;
}

// ---------- примитивы ----------

Ui::Hit Ui::hit(const char* id, ImVec2 pos, ImVec2 size) {
    ImGui::SetCursorScreenPos(pos);
    Hit h;
    // Кнопки рисуются друг поверх друга (сердечко поверх строки и т.п.). По умолчанию ImGui отдаёт нажатие
    // тому, что объявлено ПЕРВЫМ, и верхние кнопки «умирают». AllowOverlap: побеждает объявленное позже,
    // то есть то, что и нарисовано сверху.
    ImGui::SetNextItemAllowOverlap();
    h.clicked = ImGui::InvisibleButton(id, ImVec2(std::max(1.f, size.x), std::max(1.f, size.y)));
    h.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
    h.held = ImGui::IsItemActive();
    h.id = ImGui::GetItemID();
    return h;
}

float Ui::hv(ImGuiID id, bool hovered, float speed) {
    float& v = hover_[id];
    v += ((hovered ? 1.f : 0.f) - v) * (1.f - std::exp(-speed * dt_));
    return v;
}

// ---------- «ощущение» кнопок ----------
// Все кнопки ведут себя одинаково: плавно светлеют при наведении, слегка сжимаются при нажатии
// и пускают тонкую волну после клика. В «спокойном» режиме волн и подпрыгиваний нет.
Ui::Fx Ui::fx(const Hit& h) {
    Fx f;
    f.k = hv(h.id, h.hovered);
    float& pr = press_[h.id];
    pr += ((h.held ? 1.f : 0.f) - pr) * (1.f - std::exp(-(h.held ? 30.f : 14.f) * dt_));
    f.press = pr;
    if (h.clicked) pulse_[h.id] = time_;
    if (!calm()) {
        auto it = pulse_.find(h.id);
        if (it != pulse_.end()) {
            float t = (time_ - it->second) / 0.5f;
            if (t >= 0.f && t < 1.f) f.pulse = t;
            else pulse_.erase(it);
        }
    }
    return f;
}

Ui::ItemFx Ui::listItem(ImDrawList* dl, const Hit& h, ImVec2 a, ImVec2 b, bool active, float r, bool bar) {
    Fx f = fx(h);
    ItemFx o;
    o.k = f.k;
    o.on = hv(h.id ^ 0x9E3779B9u, active, 16.f);
    float hk = f.k * (1.f - o.on);
    if (hk > 0.01f) dl->AddRectFilled(a, b, white(0.055f * hk + 0.03f * f.press), r);
    if (o.on > 0.01f) {
        dl->AddRectFilled(a, b, alpha(pal_.acc, (0.15f + 0.05f * f.k) * o.on), r);
        dl->AddRect(a, b, alpha(pal_.acc, 0.22f * o.on), r, ImDrawFlags_None, 1.f);
        if (bar) {
            float cy = (a.y + b.y) / 2, hh = (b.y - a.y) * 0.26f * o.on;
            dl->AddRectFilled(ImVec2(a.x + 1, cy - hh), ImVec2(a.x + 4, cy + hh), pal_.acc, 2.f);
        }
    }
    return o;
}

bool Ui::iconButton(const char* id, ImDrawList* dl, ImVec2 c, float box, Icon ic, bool active, const char* tipText, ImU32 col) {
    Hit h = hit(id, ImVec2(c.x - box / 2, c.y - box / 2), ImVec2(box, box));
    Fx f = fx(h);
    // подложка «вырастает» из центра при наведении
    if (f.k > 0.01f) {
        float br = box * 0.5f * (0.78f + 0.22f * easeOut(f.k)) * (1.f - 0.06f * f.press);
        dl->AddCircleFilled(c, br, active && !col ? alpha(pal_.acc, 0.13f * f.k) : white((0.085f + 0.05f * f.press) * f.k), 32);
    }
    if (f.pulse >= 0.f) {
        float e = easeOut(f.pulse);
        dl->AddCircle(c, box * (0.46f + 0.26f * e), alpha(active && !col ? pal_.acc : white(1.f), 0.35f * (1.f - f.pulse)), 32, 1.5f);
    }
    ImU32 cc = col ? col : (active ? mix(pal_.acc, white(1.f), 0.15f * f.k) : white(0.64f + 0.36f * f.k));
    float sz = box * 0.54f * (1.f + 0.07f * f.k) * (1.f - 0.12f * f.press);
    bool toggle = ic == Icon::Shuffle || ic == Icon::Loop || ic == Icon::LoopOne;
    // переключатели (вперемешку, повтор) поднимаются и показывают точку под собой, как в Spotify
    float on = hv(h.id ^ 0x51ED270Bu, active && toggle, 14.f);
    icon(dl, ic, ImVec2(c.x, c.y - box * 0.06f * on), sz, cc);
    if (on > 0.01f) dl->AddCircleFilled(ImVec2(c.x, c.y + box * 0.36f), std::max(1.6f, box * 0.055f) * on, alpha(pal_.acc, on), 12);
    if (h.hovered && tipText) tip(tipText);
    return h.clicked;
}

bool Ui::pill(const char* id, ImDrawList* dl, ImVec2 p, ImVec2 s, const std::string& label, bool primary, bool active, const Icon* ic) {
    Hit h = hit(id, p, s);
    Fx f = fx(h);
    float on = hv(h.id ^ 0x51ED270Bu, active, 16.f);   // плавный переход «выбрано / не выбрано»
    // сжатие при нажатии вокруг центра и лёгкий подъём при наведении
    float sq = 1.f - 0.04f * f.press;
    float lift = calm() ? 0.f : 1.5f * f.k * (1.f - f.press);
    ImVec2 c(p.x + s.x / 2, p.y + s.y / 2 - lift);
    ImVec2 hs(s.x / 2 * sq, s.y / 2 * sq);
    ImVec2 a(c.x - hs.x, c.y - hs.y), b(c.x + hs.x, c.y + hs.y);
    float r = hs.y;
    ImU32 tc;
    if (primary) {
        ImU32 base = mix(pal_.acc, u32(hsl(pal_.hue, pal_.sat, 0.80f)), 0.55f * f.k);
        glowRect(dl, a, b, r, 10.f + 6.f * f.k, pal_.acc, 0.10f + 0.22f * f.k * (1.f - f.press), 3.f);
        dl->AddRectFilled(a, b, base, r);
        // блик сверху: кнопка выглядит объёмной, а не плоской
        dl->AddRectFilled(a, ImVec2(b.x, c.y), white(0.10f + 0.05f * f.k), r, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(a.x, c.y), b, IM_COL32(0, 0, 0, static_cast<int>(18 + 20 * f.press)), r, ImDrawFlags_RoundCornersBottom);
        tc = IM_COL32(14, 10, 20, 255);
    } else {
        ImU32 fillOff = white(0.065f + 0.055f * f.k + 0.04f * f.press);
        ImU32 fillOn = alpha(pal_.acc, 0.16f + 0.08f * f.k + 0.05f * f.press);
        ImU32 lineOff = white(0.11f + 0.14f * f.k), lineOn = alpha(pal_.acc, 0.75f + 0.25f * f.k);
        if (on > 0.01f) glowRect(dl, a, b, r, 8.f, pal_.acc, 0.10f * on, 1.f);
        dl->AddRectFilled(a, b, mix(fillOff, fillOn, on), r);
        dl->AddRectFilled(a, ImVec2(b.x, c.y), white(0.025f + 0.02f * f.k), r, ImDrawFlags_RoundCornersTop);
        dl->AddRect(a, b, mix(lineOff, lineOn, on), r, ImDrawFlags_None, 1.2f);
        tc = mix(white(0.86f + 0.14f * f.k), mix(pal_.acc, white(1.f), 0.2f * f.k), on);
    }
    if (f.pulse >= 0.f) {
        float e = easeOut(f.pulse) * 9.f;
        dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), alpha(primary || active ? pal_.acc : white(1.f), 0.45f * (1.f - f.pulse)),
                    r + e, ImDrawFlags_None, 1.6f);
    }
    float fs = 14.f * sq;
    ImVec2 ts = measure(F_.bold, fs, label);
    float iw = ic ? 16.f + 9.f : 0.f;                       // иконка и зазор до текста
    float x0 = c.x - (ts.x + iw) / 2;
    if (ic) icon(dl, *ic, ImVec2(x0 + 8 + (calm() ? 0.f : 1.5f * f.k), c.y), 16.f * sq, tc);
    drawText(dl, F_.bold, fs, ImVec2(x0 + iw, c.y - ts.y / 2), tc, label);
    return h.clicked;
}

float Ui::pillWidth(const std::string& label, bool withIcon) const {
    return measure(F_.bold, 14.f, label).x + 44.f + (withIcon ? 25.f : 0.f);
}

// Крупная карточка действия: цветной кружок с иконкой, заголовок и пояснение.
bool Ui::card(const char* id, ImDrawList* dl, ImVec2 p, ImVec2 s, Icon ic, const std::string& title, const std::string& sub, bool primary) {
    Hit h = hit(id, p, s);
    Fx f = fx(h);
    float k = f.k;
    float lift = calm() ? 0.f : 4.f * k * (1.f - f.press), r = radius(18.f);
    float in = 2.f * f.press;   // нажатие: карточка чуть «уходит» внутрь
    ImVec2 a = p - ImVec2(0, lift) + ImVec2(in, in), b = p + s - ImVec2(0, lift) - ImVec2(in, in);
    if (k > 0.01f) softShadow(dl, a, b, r, 16.f, 0.4f * k);
    if (primary) glowRect(dl, a, b, r, 14.f, pal_.acc, 0.08f + 0.12f * k, 4.f);
    dl->AddRectFilled(a, b, primary ? alpha(pal_.acc, 0.16f + 0.08f * k) : white(0.045f + 0.04f * k), r);
    dl->AddRectFilled(a, ImVec2(b.x, (a.y + b.y) / 2), white(0.02f + 0.015f * k), r, ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, primary ? alpha(pal_.acc, 0.45f + 0.3f * k) : white(0.08f + 0.1f * k), r, ImDrawFlags_None, 1.f);
    if (f.pulse >= 0.f) {
        float e = easeOut(f.pulse) * 10.f;
        dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), alpha(pal_.acc, 0.4f * (1.f - f.pulse)), r + e, ImDrawFlags_None, 1.5f);
    }
    ImVec2 c(a.x + 22 + 20, (a.y + b.y) / 2);
    float cr = 21.f * (1.f + 0.06f * k);
    if (k > 0.01f) glow(dl, c, cr * 1.9f, pal_.acc, 0.25f * k, 8);
    dl->AddCircleFilled(c, cr, primary ? mix(pal_.acc, white(1.f), 0.12f * k) : alpha(pal_.acc, 0.18f + 0.12f * k), 32);
    icon(dl, ic, c, 20.f, primary ? IM_COL32(14, 10, 20, 255) : pal_.acc);
    float tx = c.x + 34, tw = b.x - tx - 14;
    twoLines(dl, tx, c.y, tw, title, 16.f, white(0.97f), sub, 13.f, white(0.55f + 0.2f * k));
    return h.clicked;
}

// Переключатель из нескольких вариантов («Компактно | Обычно | Просторно»). Возвращает номер нажатого или -1.
// Цветной бегунок плавно переезжает к выбранному варианту.
int Ui::segmented(const char* id, ImDrawList* dl, ImVec2 p, float h, const std::vector<std::string>& opts, int cur, float* outW) {
    const float fs = 13.f, pad = 16.f;
    float total = 4.f;
    for (auto& o : opts) total += measure(F_.bold, fs, o).x + pad * 2;
    dl->AddRectFilled(p, p + ImVec2(total, h), white(0.055f), radius(h / 2));
    dl->AddRect(p, p + ImVec2(total, h), white(0.09f), radius(h / 2));
    ImGuiID key = ImGui::GetID(id);
    ImGui::PushID(id);
    // где должен стоять бегунок
    float tx = 2.f, tw = 0.f;
    {
        float xx = 2.f;
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            float ow = measure(F_.bold, fs, opts[static_cast<std::size_t>(i)]).x + pad * 2;
            if (i == cur) { tx = xx; tw = ow; }
            xx += ow;
        }
    }
    auto it = seg_.find(key);
    if (it == seg_.end() || calm()) it = seg_.insert_or_assign(key, ImVec2(tx, tw)).first;
    else {
        float kk = 1.f - std::exp(-16.f * dt_);
        it->second.x += (tx - it->second.x) * kk;
        it->second.y += (tw - it->second.y) * kk;
    }
    ImVec2 sl = it->second;
    int clicked = -1;
    float x = p.x + 2;
    std::vector<float> ks(opts.size());
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        float ow = measure(F_.bold, fs, opts[static_cast<std::size_t>(i)]).x + pad * 2;
        ImGui::PushID(i);
        Hit hh = hit("##seg", ImVec2(x, p.y + 2), ImVec2(ow, h - 4));
        ImGui::PopID();
        float k = hv(hh.id, hh.hovered && i != cur);
        ks[static_cast<std::size_t>(i)] = k;
        if (k > 0.01f) dl->AddRectFilled(ImVec2(x, p.y + 2), ImVec2(x + ow, p.y + h - 2), white(0.07f * k), radius((h - 4) / 2));
        if (hh.clicked) clicked = i;
        x += ow;
    }
    if (cur >= 0 && sl.y > 1.f) {
        ImVec2 a(p.x + sl.x, p.y + 2), b(p.x + sl.x + sl.y, p.y + h - 2);
        glowRect(dl, a, b, radius((h - 4) / 2), 6.f, pal_.acc, 0.18f, 1.f);
        dl->AddRectFilled(a, b, pal_.acc, radius((h - 4) / 2));
        dl->AddRectFilled(a, ImVec2(b.x, (a.y + b.y) / 2), white(0.10f), radius((h - 4) / 2), ImDrawFlags_RoundCornersTop);
    }
    x = p.x + 2;
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        const std::string& o = opts[static_cast<std::size_t>(i)];
        float ow = measure(F_.bold, fs, o).x + pad * 2;
        // текст тёмный там, где над ним бегунок
        float cover = clamp01(1.f - std::fabs((p.x + sl.x + sl.y / 2) - (x + ow / 2)) / std::max(1.f, ow * 0.6f));
        ImVec2 ts = measure(F_.bold, fs, o);
        drawText(dl, F_.bold, fs, ImVec2(x + (ow - ts.x) / 2, p.y + (h - ts.y) / 2),
                 mix(white(0.72f + 0.28f * ks[static_cast<std::size_t>(i)]), IM_COL32(14, 10, 20, 255), cover), o);
        x += ow;
    }
    ImGui::PopID();
    if (outW) *outW = total;
    return clicked;
}

bool Ui::toggleSwitch(const char* id, ImDrawList* dl, ImVec2 p, bool value) {
    Hit h = hit(id, p, ImVec2(44, 24));
    float k = hv(h.id, value, 16.f);
    float hk = hv(h.id ^ 0x9E3779B9u, h.hovered);
    float& pr = press_[h.id];
    pr += ((h.held ? 1.f : 0.f) - pr) * (1.f - std::exp(-24.f * dt_));
    ImU32 track = mix(white(0.14f + 0.05f * hk), mix(pal_.acc, white(1.f), 0.12f * hk), k);
    if (k > 0.01f) glowRect(dl, p, p + ImVec2(44, 24), 12.f, 6.f, pal_.acc, 0.15f * k, 1.f);
    dl->AddRectFilled(p, p + ImVec2(44, 24), track, 12.f);
    // ручка: при нажатии вытягивается, как в iOS; мягкая тень под ней
    float kw = 18.f + 6.f * pr;
    float kx = p.x + 3 + (44 - 6 - kw) * k;
    ImVec2 ka(kx, p.y + 3), kb(kx + kw, p.y + 21);
    if (hk > 0.01f) dl->AddRectFilled(ka - ImVec2(5, 5), kb + ImVec2(5, 5), white(0.07f * hk), 14.f);
    dl->AddRectFilled(ka + ImVec2(0, 1.5f), kb + ImVec2(0, 1.5f), IM_COL32(0, 0, 0, 70), 9.f);
    dl->AddRectFilled(ka, kb, mix(white(0.88f), white(1.f), k), 9.f);
    return h.clicked;
}

Ui::SliderOut Ui::slider(const char* id, ImDrawList* dl, ImVec2 p, float w, float v01, float th) {
    Hit h = hit(id, ImVec2(p.x - 6, p.y - 9), ImVec2(w + 12, 18 + th));
    SliderOut o;
    o.hovered = h.hovered;
    o.active = h.held;
    float cy = p.y + th / 2;
    float v = clamp01(v01);
    if (h.held) {
        v = clamp01((ImGui::GetIO().MousePos.x - p.x) / w);
        o.value = v;
    } else o.value = v;
    o.released = ImGui::IsItemDeactivated();
    float k = hv(h.id, h.hovered || h.held);
    float tk = th + 2.f * k;
    dl->AddRectFilled(ImVec2(p.x, cy - tk / 2), ImVec2(p.x + w, cy + tk / 2), white(0.16f), tk / 2);
    if (v > 0) dl->AddRectFilled(ImVec2(p.x, cy - tk / 2), ImVec2(p.x + w * v, cy + tk / 2), (h.hovered || h.held) ? pal_.acc : white(0.88f), tk / 2);
    if (k > 0.02f) dl->AddCircleFilled(ImVec2(p.x + w * v, cy), 6.5f * k, white(1.f), 16);
    return o;
}

bool Ui::textField(const char* id, ImVec2 p, float w, std::string& value, const char* hint, bool multiline, float hgt, bool password) {
    float h = multiline ? hgt : 40.f;
    if (mobile_) return mobileField(id, p, w, multiline ? h : 46.f, value, hint, hint && *hint ? hint : "Текст", multiline, password);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), white(0.07f), multiline ? 14.f : h / 2);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 16, p.y + (multiline ? 10.f : 10.f)));
    ImGui::SetNextItemWidth(w - 32);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.95f, 1.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImGui::ColorConvertU32ToFloat4(alpha(pal_.acc, 0.4f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    bool r;
    if (multiline) r = ImGui::InputTextMultiline(id, &value, ImVec2(w - 32, h - 20), ImGuiInputTextFlags_None);
    else r = ImGui::InputTextWithHint(id, tr(hint ? hint : "").c_str(), &value, ImGuiInputTextFlags_EnterReturnsTrue | (password ? ImGuiInputTextFlags_Password : 0));
    bool active = ImGui::IsItemActive();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (active) dl->AddRect(p, p + ImVec2(w, h), alpha(pal_.acc, 0.8f), multiline ? 14.f : h / 2, ImDrawFlags_None, 1.5f);
    return r;
}

float Ui::hueOf(const std::string& s) const {
    unsigned h = 0;
    for (unsigned char c : s) h = h * 31u + c;
    return static_cast<float>(h % 360u);
}

const ThemeDef& Ui::theme() const { return themeById(app_.lib.getStr("theme", "dynamic")); }

float Ui::themeHue(const ThemeDef& th) const {
    std::string id = th.id;
    if (id == "custom") return static_cast<float>(app_.lib.getNum("hue", 268));
    if (themeTintable(id)) {
        double t = app_.lib.getNum(("tint_" + id).c_str(), -1);
        if (t >= 0) return static_cast<float>(t);
    }
    return th.hue;
}

ImVec4 Ui::themeBg(const ThemeDef& th) const {
    float r = th.r / 255.f, g = th.g / 255.f, b = th.b / 255.f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), l = (mx + mn) / 2, s = 0, h = th.hue;
    if (mx - mn > 1e-4f) {
        float d = mx - mn;
        s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
        h = mx == r ? 60.f * std::fmod((g - b) / d + 6.f, 6.f) : mx == g ? 60.f * ((b - r) / d + 2) : 60.f * ((r - g) / d + 4);
    }
    // свой оттенок перекрашивает и фон (у почти серых фонов — только чуть подкрашивает)
    std::string id = th.id;
    if (themeTintable(id) && app_.lib.getNum(("tint_" + id).c_str(), -1) >= 0) {
        h = themeHue(th);
        if (s < 0.08f) s = 0.08f;
    }
    static const float kLv[] = {0.6f, 1.f, 1.55f};
    int lv = std::clamp(static_cast<int>(app_.lib.getNum("bgLevel", 1)), 0, 2);
    l = std::min(0.42f, l * kLv[lv]);
    if (lv == 2 && l < 0.06f) l = 0.06f;
    return hsl(h, s, l);
}

void Ui::updatePalette() {
    const ThemeDef& th = theme();
    float target = th.hue;
    std::string id = th.id;
    if (id == "dynamic") target = app_.cur ? hueOf(app_.cur->title + app_.cur->artist) : 268.f;
    else target = themeHue(th);
    float k = 1.f - std::exp(-3.5f * dt_);
    pal_.hue += hueDist(pal_.hue, target) * k;
    pal_.sat += (th.sat - pal_.sat) * k;
    pal_.acc = u32(hsl(pal_.hue, pal_.sat, 0.72f));
    pal_.acc2 = u32(hsl(pal_.hue + 55.f, pal_.sat * 0.9f, 0.68f));
    pal_.accV = hsl(pal_.hue, pal_.sat, 0.72f);
    pal_.acc2V = hsl(pal_.hue + 55.f, pal_.sat * 0.9f, 0.68f);
    pal_.bgV = themeBg(th);
    pal_.bg = u32(pal_.bgV);
    // заголовок окна Windows — в цвет фона темы (меняем только при смене темы)
    if (pal_.bg != titleBg_ && app_.platform.setTitleBarColor) {
        titleBg_ = pal_.bg;
        app_.platform.setTitleBarColor(static_cast<int>(pal_.bgV.x * 255.f + 0.5f), static_cast<int>(pal_.bgV.y * 255.f + 0.5f),
                                       static_cast<int>(pal_.bgV.z * 255.f + 0.5f));
    }
}

void Ui::twoLines(ImDrawList* dl, float x, float midY, float maxW, const std::string& title, float fsT, ImU32 colT,
                  const std::string& sub, float fsS, ImU32 colS) {
    // высоту берём по образцу «Ag», а не по самой строке: так у всех строк одинаковая опорная линия
    float hT = measure(F_.bold, fsT, "Ag").y, hS = sub.empty() ? 0.f : measure(F_.regular, fsS, "Ag").y;
    float gap = sub.empty() ? 0.f : 1.f, top = midY - (hT + gap + hS) / 2;
    drawText(dl, F_.bold, fsT, ImVec2(x, top), colT, ellipsize(F_.bold, fsT, title, maxW));
    if (!sub.empty()) drawText(dl, F_.regular, fsS, ImVec2(x, top + hT + gap), colS, ellipsize(F_.regular, fsS, sub, maxW));
}

void Ui::drawCover(ImDrawList* dl, const Track& t, ImVec2 p, float size, float rounding) {
    ImTextureID tx = coverOf(t);
    if (tx) {
        dl->AddImageRounded(tx, p, p + ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
        return;
    }
    float h = hueOf(t.title + t.artist);
    dl->AddRectFilled(p, p + ImVec2(size, size), u32(hsl(h, 0.45f, 0.22f)), rounding);
    icon(dl, Icon::Note, p + ImVec2(size / 2, size / 2), size * 0.5f, u32(hsl(h, 0.5f, 0.6f, 0.8f)));
}

void Ui::openFull(bool open) {
    wantFull_ = open;
    if (open) fullFocus_ = true;
    fullA_.set(open ? 1.f : 0.f);
    if (!open) {
        if (fr_.open) { fr_.open = false; if (fr_.previewing) app_.endPreview(); fr_.previewing = false; }
    }
}

void Ui::openModal(Modal m) {
    modal_ = m;
    modalA_.set(1.f);
    modalFirst_ = true;
}

void Ui::closeModal() { modalA_.set(0.f); }

void Ui::confirm(const std::string& text, std::function<void()> yes) {
    confirmText_ = text;
    confirmYes_ = std::move(yes);
    openModal(Modal::Confirm);
}

void Ui::toggleFullscreen() {
    if (!app_.platform.setFullscreen || !app_.platform.isFullscreen) return;
    bool on = !app_.platform.isFullscreen();
    app_.platform.setFullscreen(on);
    app_.lib.set("fullscreen", Json(on));  // запоминаем до следующего запуска
    app_.touch();
}

void Ui::onFilesDropped(const std::vector<std::string>& paths) { app_.addPaths(paths); }

// ---------- кадр ----------

void Ui::frame(float dt, ImVec2 display) {
    dt_ = std::min(dt, 0.1f);
    readPrefs();
    if (!started_) {  // стартовый экран из настроек
        started_ = true;
        if (app_.lib.getStr("startView", "home") == "library") app_.openView(ViewKind::Library);
    }
    time_ += dt_;
    W_ = display;
    tip_.clear();
    updatePalette();
    setIconBackdrop(pal_.bg);
    tex_.collect();

    fullA_.step(dt_, 9.f);
    miniA_.set(app_.cur ? 1.f : 0.f);
    miniA_.step(dt_, 9.f);
    modalA_.step(dt_, 16.f);
    tetoA_.step(dt_, 5.f);
    if (modal_ != Modal::None && modalA_.t == 0.f && modalA_.v < 0.02f) { modal_ = Modal::None; modalA_.snap(0.f); }

    // фон: цвет темы и мягкие цветные пятна (отключаются в «спокойном» режиме)
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), W_, pal_.bg);
    if (theme().deco && setting("pattern", true)) drawDeco(bg, W_, calm() ? 0.f : time_, pal_, theme().deco);
    else if (setting("blobs", true) && !setting("calm", false)) drawBlobs(bg, W_, time_, pal_, 0.30f);
    else if (setting("blobs", true)) drawBlobs(bg, W_, 0.f, pal_, 0.22f);
    // затемнение фона из настроек: накрывает фон с узором, интерфейс остаётся ярким
    float dim = std::clamp(static_cast<float>(app_.lib.getNum("bgDim", 0)), 0.f, 0.85f);
    if (dim > 0.005f) bg->AddRectFilled(ImVec2(0, 0), W_, IM_COL32(0, 0, 0, static_cast<int>(255 * dim)));

    shortcuts();
    drawRoot();
    if (mobile_ && fullA_.v < 0.995f) drawTabBar();
    if (miniA_.v > 0.01f && fullA_.v < 0.995f) { if (mobile_) drawMiniMobile(); else drawMini(); }
    if (fullA_.v > 0.002f) drawFull();
    drawModal();
    drawRain();
    drawToast();

    if (!tip_.empty()) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 m = ImGui::GetIO().MousePos;
        ImVec2 ts = measure(F_.regular, 13.f, tip_);
        ImVec2 p(std::min(m.x + 14.f, W_.x - ts.x - 24.f), m.y + 22.f);
        fg->AddRectFilled(p, p + ts + ImVec2(16, 10), IM_COL32(24, 22, 34, 240), 8.f);
        fg->AddRect(p, p + ts + ImVec2(16, 10), white(0.12f), 8.f);
        drawText(fg, F_.regular, 13.f, p + ImVec2(8, 5), white(0.92f), tip_);
    }
}

void Ui::shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (modal_ != Modal::None && modalA_.t > 0.5f) closeModal();
        else if (fr_.open) { fr_.open = false; if (fr_.previewing) { app_.endPreview(); fr_.previewing = false; } }
        else if (wantFull_) openFull(false);
    }
    if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) || (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Slash, false))) {
        focusSearch_ = true;
        if (wantFull_) openFull(false);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) toggleFullscreen();
    if (io.WantTextInput || modal_ != Modal::None) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) app_.togglePause();
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) app_.seek(app_.position() + 5);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) app_.seek(app_.position() - 5);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) app_.setVolume(app_.volume + 0.05f);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) app_.setVolume(app_.volume - 0.05f);
}

// ---------- основная область ----------

void Ui::drawRoot() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(W_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##root", nullptr, fl);

    float sbw = (mobile_ || W_.x < 900.f) ? 0.f : kSide;
    if (sbw > 0) drawSidebar(ImVec2(0, 0), ImVec2(sbw, W_.y));
    float mx = sbw, mw = W_.x - sbw;
    if (viewStart_ == 0 || lastView_ != app_.view || lastPlaylist_ != app_.viewPlaylist) {
        lastView_ = app_.view;
        lastPlaylist_ = app_.viewPlaylist;
        viewStart_ = app_.now();
    }
    if (mobile_) {
        drawContent(ImVec2(0, 0), ImVec2(W_.x, W_.y - kTabH));
        drawTopBar(ImVec2(12, 14), W_.x - 24);
    } else {
        drawContent(ImVec2(mx, 0), ImVec2(mw, W_.y));
        drawTopBar(ImVec2(mx + 24, 16), mw - 48);
    }
    drawTeto();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Ui::drawSidebar(ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(pos.x + 10, pos.y + 10), b(pos.x + size.x - 2, pos.y + size.y - 10 - (miniA_.v > 0.02f ? (kMiniH + 12) * miniA_.v : 0));
    dl->AddRectFilled(a, b, IM_COL32(10, 9, 16, static_cast<int>(255 * panelA_)), radius(22.f));
    dl->AddRect(a, b, white(0.06f), radius(22.f));

    // логотип: 7 быстрых нажатий открывают сюрприз
    ImVec2 lp(a.x + 18, a.y + 18);
    Hit lh = hit("##logo", lp, ImVec2(44, 44));
    float lk = hv(lh.id, lh.hovered);
    dl->AddRectFilled(lp, lp + ImVec2(44, 44), pal_.acc, 13.f);
    dl->AddRectFilled(lp + ImVec2(6, 11), lp + ImVec2(38, 33), IM_COL32(18, 16, 28, 255), 5.f);
    dl->AddRectFilled(lp + ImVec2(9, 14), lp + ImVec2(35, 25), white(0.95f), 4.f);
    dl->AddCircleFilled(lp + ImVec2(16, 19.5f), 3.6f, IM_COL32(18, 16, 28, 255), 14);
    dl->AddCircleFilled(lp + ImVec2(28, 19.5f), 3.6f, IM_COL32(18, 16, 28, 255), 14);
    if (lk > 0.01f) {
        glowRect(dl, lp, lp + ImVec2(44, 44), 13.f, 8.f, pal_.acc, 0.25f * lk, 2.f);
        dl->AddRect(lp - ImVec2(2, 2), lp + ImVec2(46, 46), white(0.25f * lk), 15.f, ImDrawFlags_None, 1.5f);
    }
    if (lh.clicked) {
        double t = app_.now();
        logoClicks_ = (t - logoLast_ < 0.7) ? logoClicks_ + 1 : 1;
        logoLast_ = t;
        if (logoClicks_ >= 7) {
            logoClicks_ = 0;
            app_.tetoEgg(tetoFirst_);
            rainUntil_ = t + 6.0;
            openModal(Modal::Teto);
        }
    }
    drawText(dl, F_.bold, 22.f, ImVec2(lp.x + 56, lp.y + 8), white(0.98f), "Mixtape");

    float x = a.x + 12, w = (b.x - a.x) - 24, y = a.y + 82;
    auto nav = [&](const char* id, Icon ic, const char* label, bool active) {
        Hit h = hit(id, ImVec2(x, y), ImVec2(w, 40));
        ItemFx it = listItem(dl, h, ImVec2(x, y), ImVec2(x + w, y + 40), active, radius(12.f));
        ImU32 c = mix(white(0.65f + 0.35f * it.k), pal_.acc, it.on);
        float nx = calm() ? 0.f : 3.f * it.k * (1.f - it.on);   // при наведении пункт чуть сдвигается вправо
        icon(dl, ic, ImVec2(x + 24 + nx, y + 20), 22.f * (1.f + 0.06f * it.k), c);
        drawText(dl, F_.bold, 15.f, ImVec2(x + 46 + nx, y + 20 - measure(F_.bold, 15.f, "Ag").y / 2), mix(c, white(1.f), it.on),
                 ellipsize(F_.bold, 15.f, label, w - 52.f - nx));  // длинные переводы — с многоточием
        y += 44;
        return h.clicked;
    };
    if (nav("##nHome", Icon::Home, "Главная", app_.view == ViewKind::Home)) app_.openView(ViewKind::Home);
    if (nav("##nLib", Icon::HeartFilled, "Моя музыка", app_.view == ViewKind::Library)) app_.openView(ViewKind::Library);
    if (nav("##nClips", Icon::Scissors, "Фрагменты", app_.view == ViewKind::Clips)) app_.openView(ViewKind::Clips);
    if (nav("##nOff", Icon::Download, "Офлайн", app_.view == ViewKind::Offline)) app_.openView(ViewKind::Offline);
    if (nav("##nRec", Icon::Sparkle, "Для вас", app_.view == ViewKind::ForYou)) app_.openView(ViewKind::ForYou);
    if (nav("##nStat", Icon::Chart, "Статистика", app_.view == ViewKind::Stats)) app_.openView(ViewKind::Stats);
    if (nav("##nRnd", Icon::Dice, "Случайная песня", false)) app_.randomSong();
    if (nav("##nImp", Icon::Import, "Перенести плейлист", false)) { importLink_.clear(); openModal(Modal::Import); }
    if (nav("##nFiles", Icon::Files, "Добавить файлы", false)) app_.addFiles();

    y += 8;
    dl->AddLine(ImVec2(x + 6, y), ImVec2(x + w - 6, y), white(0.08f), 1.f);
    y += 12;
    drawText(dl, F_.bold, 12.f, ImVec2(x + 8, y + 7), white(0.45f), "ПЛЕЙЛИСТЫ");
    if (iconButton("##newPl", dl, ImVec2(x + w - 20, y + 15), 30.f, Icon::Plus, false, "Новый плейлист")) { newPlName_.clear(); openModal(Modal::NewPlaylist); }
    y += 36;

    float bottom = b.y - 58;
    ImGui::SetCursorScreenPos(ImVec2(x - 4, y));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 5.f);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, white(0.18f));
    ImGui::BeginChild("##pls", ImVec2(w + 8, std::max(10.f, bottom - y)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* cd = ImGui::GetWindowDrawList();
    float cy = ImGui::GetCursorScreenPos().y;
    std::string removeId;
    for (auto& p : app_.lib.playlists) {
        ImGui::PushID(p.id.c_str());
        bool active = app_.view == ViewKind::Playlist && app_.viewPlaylist == p.id;
        Hit h = hit("##pl", ImVec2(x - 4, cy), ImVec2(w, 40));
        ItemFx it = listItem(cd, h, ImVec2(x - 4, cy), ImVec2(x - 4 + w, cy + 40), active, radius(12.f), false);
        cd->AddRectFilled(ImVec2(x + 4, cy + 6), ImVec2(x + 32, cy + 34), u32(hsl(hueOf(p.name), 0.5f, 0.3f + 0.08f * it.k)), 8.f);
        icon(cd, it.k > 0.5f ? Icon::Play : Icon::Note, ImVec2(x + 18, cy + 20), 16.f, white(0.8f + 0.2f * it.k));
        twoLines(cd, x + 42, cy + 20, w - 54, p.name, 14.5f, active ? white(1.f) : white(0.85f), songsCount(p.ids.size()), 12.f, white(0.45f));
        if (h.clicked) app_.openView(ViewKind::Playlist, p.id);
        cy += 44;
        ImGui::PopID();
    }
    if (app_.lib.playlists.empty()) drawText(cd, F_.regular, 13.f, ImVec2(x + 8, cy + 4), white(0.4f), "Пока нет плейлистов");
    ImGui::Dummy(ImVec2(1, std::max(1.f, cy - ImGui::GetCursorScreenPos().y + 4)));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // «Настройки» — внизу панели, под списком плейлистов. Раньше y оставался на верху списка, кнопка оказывалась
    // под дочерним окном «##pls», и клики забирал список.
    y = bottom + 8;
    if (nav("##nSet", Icon::Gear, "Настройки", false)) { settingsTab_ = 0; openModal(Modal::Settings); }
}

void Ui::drawTopBar(ImVec2 p, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (mobile_) {
        float fw = app_.online ? w : w - 44.f;
        dl->AddRectFilled(p, p + ImVec2(fw, 46), IM_COL32(24, 20, 34, static_cast<int>(170 + 80 * panelA_)), radius(23.f));
        icon(dl, Icon::Search, p + ImVec2(24, 23), 20.f, white(0.65f));
        Hit sh = hit("##searchTap", p, ImVec2(fw - (searchBuf_.empty() ? 0.f : 46.f), 46));
        const std::string shown = searchBuf_.empty() ? std::string("Песня, исполнитель или ссылка") : searchBuf_;
        drawText(dl, F_.regular, 15.f, ImVec2(p.x + 46, p.y + 23 - measure(F_.regular, 15.f, "Ag").y / 2),
                 searchBuf_.empty() ? white(0.45f) : white(0.95f), ellipsize(F_.regular, 15.f, shown, fw - 100));
        if (sh.clicked && app_.platform.promptText)
            app_.platform.promptText("Поиск", searchBuf_, false, [this](std::string s) {
                searchBuf_ = s;
                searchSent_ = s;
                if (!trim(s).empty()) app_.search(s);
            });
        if (!searchBuf_.empty() && iconButton("##clr", dl, p + ImVec2(fw - 24, 23), 40.f, Icon::Close, false)) {
            searchBuf_.clear();
            searchSent_.clear();
            if (app_.view == ViewKind::Search) app_.openView(ViewKind::Home);
        }
        if (!app_.online) {  // нет интернета — красная точка справа
            ImVec2 c(p.x + w - 18, p.y + 23);
            dl->AddCircleFilled(c, 16.f, IM_COL32(255, 120, 100, 40), 20);
            dl->AddCircleFilled(c, 5.f, IM_COL32(255, 120, 100, 255), 12);
        }
        return;
    }
    float sw = std::min(w, 520.f);
    // поле поиска
    bool searchActive = ImGui::GetActiveID() == ImGui::GetID("##search");
    dl->AddRectFilled(p, p + ImVec2(sw, 44), IM_COL32(20, 18, 30, static_cast<int>(150 + 100 * panelA_)), radius(22.f));
    dl->AddRect(p, p + ImVec2(sw, 44), searchActive ? alpha(pal_.acc, 0.8f) : white(0.10f), radius(22.f), ImDrawFlags_None, searchActive ? 1.5f : 1.f);
    icon(dl, Icon::Search, p + ImVec2(24, 22), 20.f, white(0.6f));
    ImGui::SetCursorScreenPos(p + ImVec2(46, 12));
    ImGui::SetNextItemWidth(sw - 100);
    if (focusSearch_) { ImGui::SetKeyboardFocusHere(); focusSearch_ = false; }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.96f, 1.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    bool enter = ImGui::InputTextWithHint("##search", tr("Название песни, исполнитель или ссылка").c_str(), &searchBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
    bool edited = ImGui::IsItemEdited();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (edited) searchEdit_ = app_.now();
    if (enter) { searchSent_ = searchBuf_; app_.search(searchBuf_); searchEdit_ = 0; }
    // поиск по мере набора, когда пользователь на секунду замер
    if (liveSearch_ && searchEdit_ > 0 && app_.now() - searchEdit_ > 0.7 && trim(searchBuf_).size() >= 2 && searchBuf_ != searchSent_) {
        searchSent_ = searchBuf_;
        searchEdit_ = 0;
        app_.search(searchBuf_);
    }
    if (!searchBuf_.empty() && iconButton("##clr", dl, p + ImVec2(sw - 24, 22), 28.f, Icon::Close, false, "Очистить")) {
        searchBuf_.clear();
        searchSent_.clear();
        if (app_.view == ViewKind::Search) app_.openView(ViewKind::Home);
    }
    // справа вверху: на весь экран и обратно
    bool fs = app_.platform.isFullscreen && app_.platform.isFullscreen();
    if (iconButton("##fullscreen", dl, ImVec2(p.x + w - 20, p.y + 22), 40.f, fs ? Icon::Shrink : Icon::Expand, false,
                   fs ? "Выйти из полноэкранного режима (F11)" : "На весь экран (F11)"))
        toggleFullscreen();
    float rx = p.x + sw + 14;
    if (!app_.online) {
        ImVec2 ts = measure(F_.bold, 13.f, "Нет интернета");
        dl->AddRectFilled(ImVec2(rx, p.y + 8), ImVec2(rx + ts.x + 24, p.y + 36), IM_COL32(255, 120, 100, 40), 14.f);
        dl->AddCircleFilled(ImVec2(rx + 12, p.y + 22), 3.5f, IM_COL32(255, 120, 100, 255), 10);
        drawText(dl, F_.bold, 13.f, ImVec2(rx + 22, p.y + 14), IM_COL32(255, 160, 145, 255), "Нет интернета");
    }
}

// ---------- мини-плеер ----------

void Ui::drawMini() {
    if (!app_.cur) return;
    const Track& t = *app_.cur;
    float sbw = W_.x < 900.f ? 0.f : kSide;
    float w = std::min(1040.f, W_.x - sbw - 36.f), h = kMiniH;
    float x = sbw + (W_.x - sbw - w) / 2.f;
    float y = W_.y - h - 14.f + (1.f - easeOut(miniA_.v)) * (h + 30.f);
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    // Без NoBringToFrontOnFocus: ImGui ставит такие окна в самый НИЗ стопки, и мини-плеер оказывался под главным
    // окном — клики уходили в список песен. Держим его над главным окном (проигрыватель и окна поднимаются позже).
    ImGui::Begin("##mini", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p(x, y);
    softShadow(dl, p, p + ImVec2(w, h), radius(26.f), 18.f, 0.55f);
    dl->AddRectFilled(p, p + ImVec2(w, h), IM_COL32(16, 14, 24, static_cast<int>(170 + 80 * panelA_)), radius(26.f));
    dl->AddRectFilled(p, p + ImVec2(w, h), alpha(pal_.acc, 0.06f), radius(26.f));
    dl->AddRect(p, p + ImVec2(w, h), white(0.10f), radius(26.f));

    // обложка и название
    Hit ch = hit("##miniCover", p + ImVec2(14, 14), ImVec2(56, 56));
    float ck = hv(ch.id, ch.hovered);
    drawCover(dl, t, p + ImVec2(14, 14), 56.f, 12.f);
    if (ck > 0.01f) dl->AddRectFilled(p + ImVec2(14, 14), p + ImVec2(70, 70), IM_COL32(0, 0, 0, static_cast<int>(90 * ck)), 12.f);
    if (ch.clicked) openFull(true);
    if (ch.hovered) tip("Открыть проигрыватель");
    float tw = std::max(120.f, std::min(230.f, w * 0.24f));
    // название, исполнитель и (если есть) строка состояния — по центру карточки
    std::string status;
    ImU32 statusCol = 0;
    if (app_.loading) {
        status = "Загрузка " + std::to_string(static_cast<int>(app_.loadProgress.load() * 100)) + "%";
        statusCol = alpha(pal_.acc, 0.9f);
    } else if (!app_.playError.empty()) {
        status = app_.playError;
        statusCol = IM_COL32(255, 150, 140, 255);
    }
    float stH = status.empty() ? 0.f : measure(F_.regular, 12.f, "Ag").y;
    twoLines(dl, p.x + 82, p.y + h / 2 - stH / 2, tw - 34, t.title, 15.f, white(0.97f),
             t.artist.empty() ? sourceLabel(t.source) : t.artist, 13.f, white(0.55f));
    if (!status.empty()) {
        float hT = measure(F_.bold, 15.f, "Ag").y, hS = measure(F_.regular, 13.f, "Ag").y;
        float top = p.y + h / 2 - stH / 2 - (hT + 1 + hS) / 2;
        drawText(dl, F_.regular, 12.f, ImVec2(p.x + 82, top + hT + 1 + hS), statusCol, ellipsize(F_.regular, 12.f, status, tw - 20));
    }
    bool fav = app_.inLibrary(t);
    if (iconButton("##miniFav", dl, p + ImVec2(82 + tw - 14, 28), 30.f, fav ? Icon::HeartFilled : Icon::Heart, fav, fav ? "Убрать из моей музыки" : "В мою музыку")) {
        if (fav && !t.clip) confirm("Убрать «" + t.title + "» из вашей музыки? Скачанная копия тоже удалится.", [this, t] { app_.toggleFavorite(t); });
        else app_.toggleFavorite(t);
    }

    // середина: кнопки и полоса
    float cx = p.x + w / 2.f + (w > 760 ? 0.f : 30.f);
    drawTransport(ImVec2(cx, p.y + 28), 0.9f);
    float bx0 = p.x + 82 + tw + 20, bx1 = p.x + w - (w > 760 ? 260.f : 90.f);
    if (bx1 - bx0 > 160) drawProgress(ImVec2(bx0, p.y + 58), bx1 - bx0, false);

    // справа
    if (w > 760) {
        float rx = p.x + w - 240;
        if (iconButton("##miniCut", dl, ImVec2(rx + 14, p.y + 30), 34.f, Icon::Scissors, false, "Фрагмент: оставить или вырезать")) {
            openFull(true);
            fr_.open = true;
            fr_.init = false;
        }
        bool lyOn = app_.ly.open;
        if (pill("##miniLy", dl, ImVec2(rx + 34, p.y + 14), ImVec2(62, 32), "Текст", false, lyOn)) { app_.openLyrics(true); openFull(true); }
        icon(dl, Icon::Volume, ImVec2(rx + 112, p.y + 30), 20.f, white(0.7f));
        SliderOut vs = slider("##miniVol", dl, ImVec2(rx + 128, p.y + 27), 76.f, app_.volume);
        if (vs.active) app_.setVolume(vs.value);
        if (vs.hovered) tip("Громкость");
        // «эквалайзер»: пять полосок
        for (int i = 0; i < 4; ++i) {
            float hgt = app_.isPlaying() ? 6.f + 10.f * (0.5f + 0.5f * std::sin(time_ * (5.f + static_cast<float>(i) * 1.7f) + static_cast<float>(i))) : 4.f;
            float bx = rx + 214 + static_cast<float>(i) * 5.f;
            dl->AddRectFilled(ImVec2(bx, p.y + 30 + 8 - hgt), ImVec2(bx + 3, p.y + 38), alpha(pal_.acc, 0.9f), 1.5f);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Ui::drawToast() {
    double left = app_.toastUntil - app_.now();
    if (left <= 0 || app_.toastText.empty()) return;
    float a = static_cast<float>(std::min(1.0, left / 0.35));
    float in = easeOut(static_cast<float>(std::min(1.0, (3.8 - left) / 0.3)));
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float maxW = std::min(560.f, W_.x - 60.f);
    ImVec2 ts = measure(F_.bold, 14.f, app_.toastText, maxW - 40);
    ImVec2 sz(ts.x + 40, ts.y + 26);
    float sbw = W_.x < 900.f ? 0.f : kSide;
    float bottomGap = (miniA_.v > 0.02f && fullA_.v < 0.5f) ? kMiniH + 28.f : 24.f;
    if (mobile_) { sbw = 0.f; bottomGap = fullA_.v < 0.5f ? kTabH + (miniA_.v > 0.02f ? kMiniHMobile + 8.f : 0.f) + 16.f : 40.f; }
    ImVec2 p(sbw + (W_.x - sbw - sz.x) / 2, W_.y - bottomGap - sz.y - (1.f - in) * -16.f);
    fg->AddRectFilled(p, p + sz, IM_COL32(30, 27, 44, static_cast<int>(245 * a)), 16.f);
    fg->AddRect(p, p + sz, alpha(pal_.acc, 0.5f * a), 16.f);
    drawText(fg, F_.bold, 14.f, p + ImVec2(20, 13), white(0.95f * a), app_.toastText, maxW - 40);
}

void Ui::drawTeto() {
    tetoReserve_ = 0.f;
    if (!tex_.hasTeto()) return;
    std::string mode = app_.lib.getStr("tetoMode", "play");
    bool want = app_.tetoOn() && mode != "off" && (mode == "always" || app_.isPlaying());
    tetoA_.set(want ? 1.f : 0.f);
    float h = (mobile_ ? std::min(W_.y * 0.2f, 170.f) : std::min(W_.y * 0.58f, 460.f)) * tetoK_, aspect = tex_.tetoAspect(), w = h * aspect;
    bool left = app_.lib.getStr("tetoSide", "right") == "left";
    // сколько места справа уступить списку, чтобы Тето не закрывала кнопки строк (плавно, вместе с её появлением)
    tetoReserve_ = (left || mobile_) ? 0.f : (w * 0.88f + 24.f) * easeOut(tetoA_.v);  // у картинки прозрачные поля по краям
    if (tetoA_.v < 0.01f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float sbw = W_.x < 900.f ? 0.f : kSide;
    float baseX = left ? sbw + 24.f + w / 2 : W_.x - 28.f - w / 2;
    float baseY = W_.y - (miniA_.v > 0.02f ? kMiniH + 22.f : 20.f);
    if (mobile_) { sbw = 0.f; baseX = left ? 12.f + w / 2 : W_.x - 12.f - w / 2; baseY = W_.y - kTabH - (miniA_.v > 0.02f ? kMiniHMobile + 14.f : 6.f); }
    bool dancing = app_.isPlaying();
    float bounce = dancing ? std::fabs(std::sin(time_ * 3.4f)) * 9.f : 0.f;
    float sway = dancing ? std::sin(time_ * 3.4f) * 0.055f : 0.f;
    float rise = (1.f - easeOut(tetoA_.v)) * 60.f;
    ImVec2 c(baseX, baseY - h / 2 - bounce + rise);
    float cs = std::cos(sway), sn = std::sin(sway);
    // вращение вокруг нижней середины: сдвигаем начало координат вниз
    ImVec2 o(c.x, c.y + h / 2);
    auto rot = [&](float dx, float dy) { return ImVec2(o.x + dx * cs - dy * sn, o.y + dx * sn + dy * cs); };
    ImVec2 p0 = rot(-w / 2, -h), p1 = rot(w / 2, -h), p2 = rot(w / 2, 0), p3 = rot(-w / 2, 0);
    dl->AddImageQuad(tex_.teto(app_.now()), p0, p1, p2, p3, ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                     IM_COL32(255, 255, 255, static_cast<int>(235.f * tetoA_.v)));
}

void Ui::drawRain() {
    double t = app_.now();
    if (t < rainUntil_ && rain_.size() < 90 && (std::fmod(time_ * 1000.f, 3.f) < 1.f || rain_.size() < 20)) {
        float r1 = std::fmod(time_ * 7919.f + static_cast<float>(rain_.size()) * 131.f, 1000.f) / 1000.f;
        float r2 = std::fmod(time_ * 104729.f + static_cast<float>(rain_.size()) * 17.f, 1000.f) / 1000.f;
        rain_.push_back({r1 * W_.x, -60.f, 160.f + r2 * 240.f, r1 * 6.28f, (r2 - 0.5f) * 5.f, 46.f + r2 * 40.f});
    }
    if (rain_.empty()) return;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    for (auto& d : rain_) {
        d.y += d.vy * dt_;
        d.rot += d.vr * dt_;
        drawBaguette(fg, ImVec2(d.x, d.y), d.size, d.rot);
    }
    rain_.erase(std::remove_if(rain_.begin(), rain_.end(), [&](const Drop& d) { return d.y > W_.y + 80.f; }), rain_.end());
}

}  // namespace mx::ui
