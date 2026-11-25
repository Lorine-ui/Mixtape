// Окна поверх приложения: настройки, перенос плейлиста, новый плейлист, «в плейлист», свой текст, пасхалка, подтверждение.
#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "text.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "imgui_internal.h"  // BringWindowToDisplayFront; после ui.hpp, чтобы были операторы ImVec2

namespace mx::ui {

namespace {
struct SrcInfo { const char* id; const char* name; const char* note; };
const SrcInfo kSources[] = {
    {"audius", "Audius", "Независимая музыка, играет надёжно"},
    {"soundcloud", "SoundCloud", "Огромный каталог; часть песен ограничена правообладателями"},
    {"archive", "Internet Archive", "Старые записи и свободная музыка"},
    {"youtube", "YouTube", "Прямые потоки иногда не отдаются: тогда песня ищется в других источниках"},
};
}  // namespace

void Ui::drawModal() {
    if (app_.openImportModal) {
        app_.openImportModal = false;
        importLink_ = app_.importPrefill;
        importTab_ = 0;
        openModal(Modal::Import);
    }
    if (modal_ == Modal::None) return;
    float a = easeOut(modalA_.v);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(W_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, clamp01(a));
    // закрывающееся (тающее) окно больше не перехватывает клики
    ImGuiWindowFlags mfl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    if (modalA_.t < 0.5f) mfl |= ImGuiWindowFlags_NoInputs;
    ImGui::Begin("##modal", nullptr, mfl);
    // Всегда самое верхнее окно; фокус берём только в первый кадр, иначе сбрасывается нажатая кнопка/поле ввода.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    const bool wasFirst = modalFirst_;
    if (wasFirst) ImGui::SetWindowFocus();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), W_, IM_COL32(0, 0, 0, static_cast<int>(165 * a)));

    float w = std::min(modal_ == Modal::Settings ? 920.f : 560.f, W_.x - 32.f);
    float h = 0;
    switch (modal_) {
        case Modal::Settings: h = 660; break;
        case Modal::Import: h = 560; break;
        case Modal::NewPlaylist: h = 230; break;
        case Modal::AddToPlaylist: h = 440; break;
        case Modal::OwnLyrics: h = 520; break;
        case Modal::TapSync: h = 520; break;
        case Modal::Teto: h = 420; break;
        case Modal::Confirm: h = 210; break;
        case Modal::Account: h = app_.acct.loggedIn() ? 540.f : 450.f; break;
        case Modal::Profile: h = 640; break;
        default: break;
    }
    if (modal_ == Modal::More) h = 640;
    h = std::min(h, W_.y - 32.f);
    if (mobile_) {  // телефон: окна почти во весь экран, настройки и «Ещё» — целиком
        w = W_.x - 16.f;
        if (modal_ == Modal::Settings || modal_ == Modal::More) h = W_.y - 24.f;
    }
    ImVec2 o((W_.x - w) / 2, (W_.y - h) / 2 + (1.f - a) * 24.f);
    // клик по затемнению закрывает окно (кроме окон с вводом, чтобы не потерять текст)
    softShadow(dl, o, o + ImVec2(w, h), 28.f, 40.f, 0.7f);
    dl->AddRectFilled(o, o + ImVec2(w, h), IM_COL32(20, 18, 30, 255), 28.f);
    dl->AddRectFilled(o, o + ImVec2(w, h), alpha(pal_.acc, 0.04f), 28.f);
    dl->AddRect(o, o + ImVec2(w, h), white(0.10f), 28.f);
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool outside = mp.x < o.x || mp.y < o.y || mp.x > o.x + w || mp.y > o.y + h;
    if (!modalFirst_ && ImGui::IsMouseClicked(0) && outside && modal_ != Modal::OwnLyrics && modal_ != Modal::Import && modal_ != Modal::TapSync && modal_ != Modal::Profile) closeModal();
    if (iconButton("##mClose", dl, o + ImVec2(w - 34, 34), 34.f, Icon::Close, false, "Закрыть")) closeModal();

    ImVec2 sz(w, h);
    switch (modal_) {
        case Modal::Settings: modalSettings(o, sz); break;
        case Modal::Import: modalImport(o, sz); break;
        case Modal::NewPlaylist: modalNewPlaylist(o, sz); break;
        case Modal::AddToPlaylist: modalAddToPlaylist(o, sz); break;
        case Modal::OwnLyrics: modalOwnLyrics(o, sz); break;
        case Modal::TapSync: modalTap(o, sz); break;
        case Modal::Teto: modalTeto(o, sz); break;
        case Modal::Confirm: modalConfirm(o, sz); break;
        case Modal::Account: modalAccount(o, sz); break;
        case Modal::Profile: modalProfile(o, sz); break;
        case Modal::More: modalMore(o, sz); break;
        default: break;
    }
    // если в этом кадре открылось следующее окно (например «Новый плейлист» из «В плейлист»), его первый кадр — следующий
    if (wasFirst) modalFirst_ = false;
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ---------- настройки ----------

void Ui::modalSettings(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!mobile_ || settingsTab_ < 0) drawText(dl, F_.bold, 24.f, o + ImVec2(28, 24), white(0.98f), "Настройки");

    // вкладки слева
    struct Tab { const char* name; Icon ic; };
    static const Tab tabs[] = {{"Оформление", Icon::HeartFilled}, {"Интерфейс", Icon::Expand}, {"Воспроизведение", Icon::Volume},
                               {"Источники", Icon::Search}, {"Данные", Icon::Files}};
    if (mobile_ && settingsTab_ < 0) {
        float ly = o.y + 78;
        for (int i = 0; i < 5; ++i) {
            ImGui::PushID(i);
            ImVec2 tp(o.x + 16, ly);
            Hit h = hit("##tabM", tp, ImVec2(sz.x - 32, 58));
            ItemFx it = listItem(dl, h, tp, tp + ImVec2(sz.x - 32, 58), false, radius(14.f), false);
            dl->AddCircleFilled(tp + ImVec2(28, 29), 19.f, alpha(pal_.acc, 0.16f + 0.1f * it.k), 24);
            icon(dl, tabs[i].ic, tp + ImVec2(28, 29), 18.f, pal_.acc);
            drawText(dl, F_.bold, 16.f, tp + ImVec2(60, 29 - measure(F_.bold, 16.f, "Ag").y / 2), white(0.95f), tabs[i].name);
            icon(dl, Icon::Chevron, tp + ImVec2(sz.x - 52, 29), 16.f, white(0.4f));
            if (h.clicked) settingsTab_ = i;
            ImGui::PopID();
            ly += 64;
        }
        return;
    }
    if (mobile_) {  // заголовок раздела со стрелкой «назад» к списку
        if (iconButton("##setBack", dl, o + ImVec2(30, 36), 40.f, Icon::Back, false)) { settingsTab_ = -1; return; }
        drawText(dl, F_.bold, 20.f, o + ImVec2(58, 36 - measure(F_.bold, 20.f, "Ag").y / 2), white(0.98f), tabs[std::clamp(settingsTab_, 0, 4)].name);
    }
    const float navW = mobile_ ? -28.f : 200.f;  // на телефоне колонки разделов нет — содержимое во всю ширину
    float ny = o.y + 78;
    for (int i = 0; i < (mobile_ ? 0 : 5); ++i) {
        ImGui::PushID(i);
        ImVec2 tp(o.x + 16, ny);
        Hit h = hit("##tab", tp, ImVec2(navW, 40));
        bool on = settingsTab_ == i;
        ItemFx it = listItem(dl, h, tp, tp + ImVec2(navW, 40), on, radius(12.f));
        ImU32 c = mix(white(0.65f + 0.35f * it.k), pal_.acc, it.on);
        float nx = calm() ? 0.f : 3.f * it.k * (1.f - it.on);
        icon(dl, tabs[i].ic, tp + ImVec2(24 + nx, 20), 18.f, c);
        drawText(dl, F_.bold, 14.f, tp + ImVec2(44 + nx, 20 - measure(F_.bold, 14.f, "Ag").y / 2), mix(c, white(1.f), it.on), tabs[i].name);
        if (h.clicked) settingsTab_ = i;
        ImGui::PopID();
        ny += 44;
    }
    if (!mobile_) dl->AddLine(ImVec2(o.x + navW + 28, o.y + 78), ImVec2(o.x + navW + 28, o.y + sz.y - 24), white(0.07f), 1.f);

    // содержимое справа — прокручивается, если не помещается
    ImVec2 bp(o.x + navW + 44, o.y + 70);
    ImVec2 bs(sz.x - navW - 44 - 20, sz.y - 70 - 20);
    ImGui::SetCursorScreenPos(bp);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 5.f);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, white(0.18f));
    ImGui::BeginChild("##setBody", bs, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    touchScroll();
    ImDrawList* cd = ImGui::GetWindowDrawList();
    const float x = bp.x, w = bs.x - 24;
    float y = bp.y + 8 - ImGui::GetScrollY();
    auto set = [&](const char* key, Json v) { app_.lib.set(key, std::move(v)); app_.touch(); };

    auto head = [&](const char* t) {
        y += 10;
        drawText(cd, F_.bold, 12.f, ImVec2(x, y), alpha(pal_.acc, 0.9f), t);
        y += 26;
    };
    // строка: заголовок и пояснение слева, справа место под элемент шириной cw
    auto label = [&](const char* title, const char* note, float cw) -> ImVec2 {
        if (mobile_ && cw > w * 0.45f) {
            twoLines(cd, x, y + 23, w, title, 15.f, white(0.95f), note ? note : "", 12.5f, white(0.5f));
            y += 50;
            return ImVec2(x, y);
        }
        float tw = w - cw - 20;
        twoLines(cd, x, y + 23, tw, title, 15.f, white(0.95f), note ? note : "", 12.5f, white(0.5f));
        return ImVec2(x + w - cw, y);
    };
    auto endRow = [&] {
        y += 54;
        cd->AddLine(ImVec2(x, y - 6), ImVec2(x + w, y - 6), white(0.05f), 1.f);
        y += 6;
    };
    auto toggle = [&](const char* title, const char* note, const char* key, bool def) {
        bool v = setting(key, def);
        label(title, note, 50);
        if (toggleSwitch((std::string("##t_") + key).c_str(), cd, ImVec2(x + w - 46, y + 10), v)) { set(key, Json(!v)); v = !v; }
        endRow();
        return v;
    };
    auto segWidth = [&](const std::vector<std::string>& opts) {
        float t = 4.f;
        for (auto& op : opts) t += measure(F_.bold, 13.f, op).x + 32.f;
        return t;
    };
    auto seg = [&](const char* title, const char* note, const char* id, const std::vector<std::string>& opts, int cur) {
        float sw = segWidth(opts);
        ImVec2 cp = label(title, note, sw);
        int r = segmented(id, cd, ImVec2(cp.x, cp.y + 6), 34.f, opts, cur);
        endRow();
        return r;
    };
    auto slide = [&](const char* title, const char* note, const char* id, float v01, const std::string& valueText) {
        const float sw = 220.f;
        ImVec2 cp = label(title, note, sw + 56);
        float slw = mobile_ && cp.x == x ? w - 70.f : sw;  // под заголовком — на всю ширину
        SliderOut so = slider(id, cd, ImVec2(cp.x + 6, cp.y + 20), slw, v01, 6.f);
        ImVec2 ts = measure(F_.bold, 13.f, valueText);
        drawText(cd, F_.bold, 13.f, ImVec2(x + w - ts.x, cp.y + 14), white(0.8f), valueText);
        endRow();
        return so;
    };
    auto pct = [](float v) { return std::to_string(static_cast<int>(std::lround(v * 100.f))) + "%"; };

    if (settingsTab_ == 0) {
        head("ТЕМА");
        std::string cur = app_.lib.getStr("theme", "dynamic");
        const int cols = mobile_ ? 3 : 5;
        float cw = (w - 12.f * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        for (int i = 0; i < themeCount(); ++i) {
            const ThemeDef& th = themeAt(i);
            const float thHue = themeHue(th);
            const ImVec4 thBg = themeBg(th);
            float cx = x + static_cast<float>(i % cols) * (cw + 12.f), cy = y + static_cast<float>(i / cols) * 64.f;
            ImGui::PushID(i);
            Hit h = hit("##th", ImVec2(cx, cy), ImVec2(cw, 54));
            Fx f = fx(h);
            float k = f.k;
            bool on = cur == th.id;
            ImU32 col = std::string(th.id) == "dynamic" ? pal_.acc : u32(hsl(thHue, std::max(0.15f, th.sat), 0.62f));
            cy -= calm() ? 0.f : 2.f * k * (1.f - f.press);
            if (on || k > 0.01f) glowRect(cd, ImVec2(cx, cy), ImVec2(cx + cw, cy + 54), radius(14.f), 8.f, col, on ? 0.22f : 0.14f * k, 2.f);
            cd->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cw, cy + 54), u32(ImVec4(thBg.x + 0.03f, thBg.y + 0.03f, thBg.z + 0.03f, 1.f)), radius(14.f));
            if (th.deco >= 2) {  // уменьшенный узор прямо на плитке
                cd->PushClipRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + 54), true);
                Palette tp = pal_;
                tp.hue = thHue;
                tp.sat = th.sat;
                int v0 = cd->VtxBuffer.Size;
                drawDeco(cd, ImVec2(cw * 2.f, 108.f), 0.f, tp, th.deco);
                for (int vi = v0; vi < cd->VtxBuffer.Size; ++vi) {  // сжимаем вдвое и ставим на плитку
                    ImVec2& vp = cd->VtxBuffer[vi].pos;
                    vp = ImVec2(cx + vp.x * 0.5f, cy + vp.y * 0.5f);
                }
                cd->PopClipRect();
            } else if (th.deco) {  // маленькие волны в углу плитки
                cd->PushClipRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + 54), true);
                cd->AddCircleFilled(ImVec2(cx + cw, cy), 30.f, u32(hsl(thHue, th.sat * 0.72f, 0.42f)), 24);
                cd->AddCircleFilled(ImVec2(cx + cw, cy), 19.f, u32(hsl(thHue, th.sat * 0.8f, 0.58f)), 24);
                cd->AddCircleFilled(ImVec2(cx + cw, cy), 10.f, u32(hsl(thHue, th.sat * 0.85f, 0.78f)), 20);
                for (int d = 0; d < 4; ++d) cd->AddCircleFilled(ImVec2(cx + cw - 40 + d * 5.f, cy + 40), 1.1f, white(0.6f), 6);
                cd->PopClipRect();
            }
            cd->AddRectFilled(ImVec2(cx + 10, cy + 10), ImVec2(cx + 26, cy + 26), col, radius(8.f));
            if (on) icon(cd, Icon::Check, ImVec2(cx + cw - 16, cy + 18), 14.f, col);
            cd->AddRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + 54), on ? col : white(0.08f + 0.12f * k), radius(14.f), ImDrawFlags_None, on ? 2.f : 1.f);
            drawText(cd, F_.bold, 12.f, ImVec2(cx + 10, cy + 33), white(0.85f), ellipsize(F_.bold, 12.f, th.name, cw - 16));
            if (h.clicked) set("theme", Json(std::string(th.id)));
            ImGui::PopID();
        }
        y += static_cast<float>((themeCount() + cols - 1) / cols) * 64.f + 10;
        if (cur == "custom") {
            double hue = app_.lib.getNum("hue", 268);
            SliderOut so = slide("Свой оттенок", "Цвет кнопок и подсветки", "##hue", static_cast<float>(hue / 360.0),
                                 std::to_string(static_cast<int>(hue)) + "°");
            if (so.active) set("hue", Json(static_cast<double>(so.value) * 360.0));
        }
        if (themeTintable(cur)) {
            // перекраска выбранной темы: готовые цвета и точный оттенок
            const ThemeDef& ct = themeById(cur);
            std::string key = "tint_" + cur;
            double tv = app_.lib.getNum(key.c_str(), -1);
            twoLines(cd, x, y + 14, w, "Цвет темы", 15.f, white(0.95f), "Перекрасьте тему — узор, фон и кнопки поменяют цвет", 12.5f, white(0.5f));
            y += 40;
            static const float kHues[] = {-1, 356, 12, 26, 42, 58, 90, 135, 160, 182, 198, 214, 232, 252, 270, 290, 318, 338};
            const int nH = static_cast<int>(sizeof(kHues) / sizeof(kHues[0]));
            float d = 30.f, gap = 8.f;
            int perRow = std::max(1, static_cast<int>((w + gap) / (d + gap)));
            for (int i = 0; i < nH; ++i) {
                ImGui::PushID(i + 500);
                ImVec2 c(x + d / 2 + static_cast<float>(i % perRow) * (d + gap), y + d / 2 + static_cast<float>(i / perRow) * (d + gap));
                Hit h = hit("##tint", c - ImVec2(d / 2, d / 2), ImVec2(d, d));
                float hk = hv(h.id, h.hovered);
                float hu = kHues[i] < 0 ? ct.hue : kHues[i];
                bool sel = kHues[i] < 0 ? tv < 0 : (tv >= 0 && std::fabs(tv - kHues[i]) < 0.5);
                cd->AddCircleFilled(c, d / 2 - 2 + 2 * hk, u32(hsl(hu, std::max(0.55f, ct.sat), 0.62f)), 28);
                if (kHues[i] < 0) icon(cd, Icon::Sparkle, c, 13.f, IM_COL32(14, 10, 20, 210));
                if (sel) cd->AddCircle(c, d / 2 + 2.5f, white(0.95f), 28, 2.f);
                if (h.hovered) tip(kHues[i] < 0 ? "Как задумано в теме" : "Перекрасить тему");
                if (h.clicked) set(key.c_str(), Json(static_cast<double>(kHues[i])));
                ImGui::PopID();
            }
            y += static_cast<float>((nH + perRow - 1) / perRow) * (d + gap) + 14;
            float hv0 = static_cast<float>((tv >= 0 ? tv : ct.hue) / 360.0);
            SliderOut so2 = slide("Точный оттенок", "Любой цвет по кругу", "##tintS", hv0,
                                  tv >= 0 ? std::to_string(static_cast<int>(tv)) + "°" : std::string("тема"));
            if (so2.active) set(key.c_str(), Json(static_cast<double>(so2.value) * 359.0));
        }
        {
            int lv = std::clamp(static_cast<int>(app_.lib.getNum("bgLevel", 1)), 0, 2);
            int r = seg("Глубина фона", "Темнее или ярче основной цвет фона", "##bgLv", {"Темнее", "Обычно", "Ярче"}, lv);
            if (r >= 0 && r != lv) set("bgLevel", Json(static_cast<double>(r)));
        }
        {
            float dm = static_cast<float>(app_.lib.getNum("bgDim", 0));
            SliderOut so3 = slide("Затемнение фона", "Приглушить фон и узор, чтобы текст читался легче", "##bgDim", dm / 0.85f,
                                  dm < 0.005f ? std::string("нет") : pct(dm));
            if (so3.active) set("bgDim", Json(static_cast<double>(so3.value) * 0.85));
        }

        head("ФОН И ПАНЕЛИ");
        if (themeById(cur).deco) toggle("Узоры на фоне", "Рисунок на фоне этой темы", "pattern", true);
        toggle("Цветные пятна на фоне", themeById(cur).deco ? "Для тем без узора и когда узор выключен" : "Мягкое свечение за интерфейсом", "blobs", true);
        float pa = static_cast<float>(app_.lib.getNum("panelAlpha", 0.6));
        SliderOut so = slide("Плотность панелей", "Насколько непрозрачны боковая панель, поиск и мини-плеер", "##panelA", (pa - 0.2f) / 0.8f, pct(pa));
        if (so.active) set("panelAlpha", Json(0.2 + 0.8 * static_cast<double>(so.value)));
        int corners = static_cast<int>(app_.lib.getNum("corners", 1));
        int r = seg("Углы", "Скругление карточек, строк и панелей", "##corners", {"Строгие", "Обычные", "Мягкие"}, corners);
        if (r >= 0) set("corners", Json(static_cast<double>(r)));

        head("ТАНЦУЮЩАЯ КАРТИНКА");
        bool tOn = app_.tetoOn();
        label(customTeto_ ? "Танцующая картинка" : "Танцующая Тето", "Появляется в углу, пока играет музыка", 50);
        if (toggleSwitch("##tetoOn", cd, ImVec2(x + w - 46, y + 10), tOn)) { set("tetoOn", Json(!tOn)); tOn = !tOn; }
        endRow();
        {
            // своя картинка или GIF вместо Тето
            static const Icon kPick = Icon::Files;
            std::string pickLabel = customTeto_ ? "Другая картинка" : "Выбрать картинку";
            float pw = pillWidth(pickLabel, true), rw = customTeto_ ? pillWidth("Вернуть Тето", false) + 8.f : 0.f;
            ImVec2 cp = label("Своя картинка или GIF", customTeto_ ? "Сейчас танцует ваша картинка" : "PNG, JPG или анимированный GIF вместо Тето", pw + rw);
            if (customTeto_ && pill("##tetoReset", cd, ImVec2(cp.x, cp.y + 6), ImVec2(rw - 8.f, 36), "Вернуть Тето")) resetTeto();
            if (pill("##tetoPick", cd, ImVec2(cp.x + rw, cp.y + 6), ImVec2(pw, 36), pickLabel, !customTeto_, false, &kPick)) pickCustomTeto();
            endRow();
        }
        if (tOn) {
            std::string mode = app_.lib.getStr("tetoMode", "play"), side = app_.lib.getStr("tetoSide", "right");
            int m = seg("Когда показывать", nullptr, "##tmode", {"Пока играет", "Всегда"}, mode == "always" ? 1 : 0);
            if (m >= 0) set("tetoMode", Json(std::string(m == 1 ? "always" : "play")));
            int sd = seg("Сторона", nullptr, "##tside", {"Слева", "Справа"}, side == "left" ? 0 : 1);
            if (sd >= 0) set("tetoSide", Json(std::string(sd == 0 ? "left" : "right")));
            float tk = static_cast<float>(app_.lib.getNum("tetoScale", 1.0));
            SliderOut ts = slide("Размер", nullptr, "##tscale", tk - 0.5f, pct(tk));
            if (ts.active) set("tetoScale", Json(0.5 + static_cast<double>(ts.value)));
        }
    } else if (settingsTab_ == 1) {
        head("ЯЗЫК · LANGUAGE");
        {
            // названия языков — каждое на своём языке; кнопки переносятся на новую строку, если не помещаются
            int cur = languageFromId(app_.lib.getStr("lang", "ru"));
            twoLines(cd, x, y + 14, w, "Язык", 15.f, white(0.95f), "Language · Мова", 12.5f, white(0.5f));
            y += 40;
            float px = x;
            for (int i = 0; i < languageCount(); ++i) {
                std::string nm = languageName(i);
                float pw = pillWidth(nm, i == cur);
                if (px + pw > x + w + 0.5f) { px = x; y += 44; }
                ImGui::PushID(i + 900);
                if (pill("##lng", cd, ImVec2(px, y), ImVec2(pw, 36), nm, i == cur) && i != cur) set("lang", Json(std::string(languageId(i))));
                ImGui::PopID();
                px += pw + 8;
            }
            y += 52;
        }
        head("РАЗМЕР");
        static const double kTexts[] = {1.0, 1.12, 1.25};
        double tk = app_.lib.getNum("textSize", 1.12);
        int ti = 1;
        for (int i = 0; i < 3; ++i) if (std::fabs(kTexts[i] - tk) < 0.01) ti = i;
        int tr = seg("Размер текста", "Только надписи, остальное как было", "##textSize", {"Обычный", "Крупный", "Огромный"}, ti);
        if (tr >= 0) set("textSize", Json(kTexts[tr]));
        static const double kScales[] = {0.9, 1.0, 1.1, 1.25, 1.5};
        double sc = app_.lib.getNum("uiScale", 1.0);
        int si = 1;
        for (int i = 0; i < 5; ++i) if (std::fabs(kScales[i] - sc) < 0.01) si = i;
        int r = seg("Масштаб интерфейса", "Всё сразу: текст, кнопки, обложки", "##scale", {"90%", "100%", "110%", "125%", "150%"}, si);
        if (r >= 0) set("uiScale", Json(kScales[r]));
        int d = seg("Плотность списков", "Высота строк с песнями", "##density", {"Компактно", "Обычно", "Просторно"},
                    static_cast<int>(app_.lib.getNum("density", 1)));
        if (d >= 0) set("density", Json(static_cast<double>(d)));

        head("СПИСКИ");
        toggle("Номера песен", "Порядковый номер слева от обложки", "rowNums", false);
        toggle("Источник у песен", "Плашка «Audius», «SoundCloud» и т. п.", "badges", true);

        head("ПОВЕДЕНИЕ");
        int sv = seg("Открывать при запуске", nullptr, "##start", {"Главная", "Моя музыка"},
                     app_.lib.getStr("startView", "home") == "library" ? 1 : 0);
        if (sv >= 0) set("startView", Json(std::string(sv == 1 ? "library" : "home")));
        toggle("Искать при наборе", "Не нужно нажимать Enter: поиск начнётся сам через мгновение", "liveSearch", true);
        toggle("Подсказки при наведении", "Пояснения к кнопкам", "tips", true);
        toggle("Спокойный режим", "Без движения фона и анимаций — меньше нагрузка", "calm", false);
        bool fs = app_.platform.isFullscreen && app_.platform.isFullscreen();
        label("На весь экран", "То же, что F11 или Alt+Enter", 50);
        if (toggleSwitch("##fs", cd, ImVec2(x + w - 46, y + 10), fs)) toggleFullscreen();
        endRow();
    } else if (settingsTab_ == 2) {
        head("ПРОИГРЫВАТЕЛЬ");
        int ds = seg("Вид проигрывателя", "Что показывать на весь экран", "##deck", {"Пластинка", "Обложка"},
                     app_.lib.getStr("deckStyle", "vinyl") == "cover" ? 1 : 0);
        if (ds >= 0) set("deckStyle", Json(std::string(ds == 1 ? "cover" : "vinyl")));
        toggle("Скачивать при добавлении", "Песни из «Моей музыки» сразу сохраняются для офлайна", "autoOff", false);
        toggle("Случайная песня — по вашему вкусу", "Похожее на «Мою музыку» и то, что вы слушаете чаще всего. Выключено — случайный жанр", "tasteRandom", true);

        head("ТАЙМЕР СНА");
        bool on = app_.sleepAt > 0 || app_.sleepAtEnd;
        const int mins[] = {15, 30, 60};
        float px = x;
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            std::string lb = std::to_string(mins[i]) + " мин";
            float pw = pillWidth(lb, false);
            if (pill("##sl", cd, ImVec2(px, y), ImVec2(pw, 36), lb)) {
                app_.sleepAt = app_.now() + mins[i] * 60.0;
                app_.sleepAtEnd = false;
                app_.toast("Музыка остановится через " + std::to_string(mins[i]) + " мин");
            }
            px += pw + 8;
            ImGui::PopID();
        }
        if (pill("##slEnd", cd, ImVec2(px, y), ImVec2(pillWidth("После песни", false), 36), "После песни", false, app_.sleepAtEnd)) {
            app_.sleepAtEnd = true;
            app_.sleepAt = 0;
        }
        y += 50;
        if (on) {
            std::string st = app_.sleepAtEnd ? "Остановлюсь после этой песни" : "Осталось: " + fmtTime(std::max(0.0, app_.sleepAt - app_.now()));
            drawText(cd, F_.regular, 13.f, ImVec2(x, y + 8), alpha(pal_.acc, 0.9f), st);
            if (pill("##slOff", cd, ImVec2(x + w - 120, y), ImVec2(120, 32), "Отменить")) { app_.sleepAt = 0; app_.sleepAtEnd = false; }
            y += 44;
        }

        head("КЛАВИШИ");
        static const std::pair<const char*, const char*> keys[] = {
            {"Пробел", "Пауза / пуск"}, {"← →", "Перемотка на 5 секунд"}, {"↑ ↓", "Громкость"}, {"Ctrl+F или /", "Поиск"},
            {"F11, Alt+Enter", "На весь экран"}, {"Esc", "Свернуть проигрыватель, закрыть окно"},
            {"Медиаклавиши", "Работают и в свёрнутом окне"}};
        for (auto& kv : keys) {
            ImVec2 ks = measure(F_.bold, 12.5f, kv.first);
            cd->AddRectFilled(ImVec2(x, y), ImVec2(x + ks.x + 20, y + 26), white(0.08f), radius(8.f));
            cd->AddRect(ImVec2(x, y), ImVec2(x + ks.x + 20, y + 26), white(0.12f), radius(8.f));
            drawText(cd, F_.bold, 12.5f, ImVec2(x + 10, y + 13 - ks.y / 2), white(0.9f), kv.first);
            drawText(cd, F_.regular, 13.f, ImVec2(x + 150, y + 4), white(0.6f), kv.second);
            y += 34;
        }
    } else if (settingsTab_ == 3) {
        drawText(cd, F_.regular, 13.f, ImVec2(x, y), white(0.55f), "Поиск идёт сразу по всем включённым источникам.", w);
        y += 30;
        for (auto& s : kSources) {
            bool v = app_.lib.srcOn(s.id);
            label(s.name, s.note, 50);
            if (toggleSwitch((std::string("##src_") + s.id).c_str(), cd, ImVec2(x + w - 46, y + 10), v)) {
                app_.lib.setSrc(s.id, !v);
                app_.touch();
            }
            endRow();
        }
    } else {
        head("АККАУНТ");
        label(app_.acct.loggedIn() ? (acName_ = app_.acct.name()).c_str() : "Не выполнен вход",
              app_.acct.loggedIn() ? "Песни и плейлисты сохраняются в аккаунте" : "Войдите, чтобы музыка была на всех устройствах", 150);
        if (pill("##acctOpen", cd, ImVec2(x + w - 150, y + 6), ImVec2(150, 36), app_.acct.loggedIn() ? "Аккаунт" : "Войти", !app_.acct.loggedIn()))
            openModal(Modal::Account);
        endRow();
        head("РЕЗЕРВНАЯ КОПИЯ");
        drawText(cd, F_.regular, 13.f, ImVec2(x, y), white(0.55f),
                 "Песни, плейлисты, фрагменты и тексты в одном файле. Подходит и копия из старого приложения.", w);
        y += 44;
        static const Icon kSave = Icon::Download, kLoad = Icon::Import;
        float bw = pillWidth("Сохранить копию", true);
        if (pill("##bkS", cd, ImVec2(x, y), ImVec2(bw, 40), "Сохранить копию", true, false, &kSave)) app_.saveBackup();
        if (pill("##bkL", cd, ImVec2(x + bw + 10, y), ImVec2(pillWidth("Загрузить копию", true), 40), "Загрузить копию", false, false, &kLoad))
            app_.loadBackup();
        y += 60;
        head("ГДЕ ВСЁ ЛЕЖИТ");
        drawText(cd, F_.regular, 13.f, ImVec2(x, y), white(0.7f), app_.dataDir, w);
        y += 28;
        if (pill("##cpDir", cd, ImVec2(x, y), ImVec2(pillWidth("Скопировать путь", false), 36), "Скопировать путь")) {
            if (app_.platform.copyText) app_.platform.copyText(app_.dataDir);
            app_.toast("Путь скопирован");
        }
        y += 56;
        head("СТАТИСТИКА");
        label("Сбросить статистику", ("Сейчас: " + std::to_string(static_cast<long long>(app_.stats.totalSec() / 3600)) + " ч прослушано").c_str(), 130);
        if (pill("##statReset", cd, ImVec2(x + w - 130, y + 6), ImVec2(130, 36), "Сбросить"))
            confirm("Стереть всю статистику прослушивания? Это нельзя отменить.", [this] { app_.resetStats(); app_.toast("Статистика сброшена"); });
        endRow();
        head("СБРОС");
        label("Вернуть вид по умолчанию", "Тема, масштаб, плотность, углы, Тето и прочее. Музыка не тронется.", 130);
        if (pill("##reset", cd, ImVec2(x + w - 130, y + 6), ImVec2(130, 36), "Сбросить")) {
            confirm("Вернуть все настройки вида по умолчанию?", [this] {
                static const std::pair<const char*, double> nums[] = {{"hue", 268}, {"panelAlpha", 0.6}, {"corners", 1}, {"uiScale", 1},
                                                                      {"density", 1}, {"tetoScale", 1}, {"bgLevel", 1}, {"bgDim", 0}, {"textSize", 1.12}};
                static const std::pair<const char*, bool> flags[] = {{"blobs", true}, {"pattern", true}, {"calm", false}, {"rowNums", false},
                                                                     {"badges", true}, {"tips", true}, {"liveSearch", true}};
                for (auto& kv : nums) app_.lib.set(kv.first, Json(kv.second));
                for (auto& kv : flags) app_.lib.set(kv.first, Json(kv.second));
                app_.lib.set("theme", Json(std::string("dynamic")));
                app_.lib.set("deckStyle", Json(std::string("vinyl")));
                app_.lib.set("startView", Json(std::string("home")));
                app_.touch();
                app_.toast("Настройки вида сброшены");
            });
        }
        endRow();
    }
    float contentH = y + ImGui::GetScrollY() - bp.y + 10;
    ImGui::SetCursorPos(ImVec2(0, 0));  // высота считается от начала области, а не от последней кнопки
    ImGui::Dummy(ImVec2(1, std::max(1.f, contentH)));
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// ---------- перенос плейлиста ----------

void Ui::modalImport(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 28, w = sz.x - 56, y = o.y + 24;
    drawText(dl, F_.bold, 24.f, ImVec2(x, y), white(0.98f), "Перенос плейлиста");
    y += 46;
    if (pill("##iT0", dl, ImVec2(x, y), ImVec2(130, 34), "По ссылке", false, importTab_ == 0)) importTab_ = 0;
    if (pill("##iT1", dl, ImVec2(x + 138, y), ImVec2(130, 34), "Списком", false, importTab_ == 1)) importTab_ = 1;
    y += 52;
    const ImportState& s = app_.imp;
    if (s.running || s.done) {
        drawText(dl, F_.bold, 16.f, ImVec2(x, y), white(0.95f), s.name.empty() ? "Перенос" : s.name);
        y += 30;
        float p = s.total > 0 ? static_cast<float>(s.finished) / static_cast<float>(s.total) : 0.f;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 8), white(0.12f), 4.f);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w * (s.running && s.total == 0 ? 0.04f : p), y + 8), pal_.acc, 4.f);
        y += 24;
        std::string msg = s.message;
        if (s.total > 0) msg += (msg.empty() ? "" : "  ") + std::string("Найдено ") + std::to_string(s.found) + " из " + std::to_string(s.total);
        drawText(dl, F_.regular, 14.f, ImVec2(x, y), white(0.75f), msg, w);
        y += 40;
        if (!s.missing.empty()) {
            drawText(dl, F_.bold, 13.f, ImVec2(x, y), white(0.6f), "Не нашлось:");
            y += 24;
            std::string all;
            for (std::size_t i = 0; i < s.missing.size() && i < 12; ++i) all += s.missing[i] + "\n";
            if (s.missing.size() > 12) all += "…и ещё " + std::to_string(s.missing.size() - 12);
            drawText(dl, F_.regular, 12.5f, ImVec2(x, y), white(0.5f), all, w);
        }
        if (s.done && pill("##iOk", dl, ImVec2(x + w - 140, o.y + sz.y - 64), ImVec2(130, 40), "Готово", true)) { app_.imp = ImportState(); closeModal(); }
        return;
    }
    if (importTab_ == 0) {
        drawText(dl, F_.regular, 13.f, ImVec2(x, y), white(0.55f), "Вставьте ссылку на плейлист или трек Spotify, YouTube или SoundCloud. Ключи и вход не нужны.", w);
        y += 54;
        if (modalFirst_) ImGui::SetKeyboardFocusHere();
        bool enter = textField("##impLink", ImVec2(x, y), w, importLink_, "https://…");
        y += 64;
        if ((pill("##iGo", dl, ImVec2(x, y), ImVec2(160, 42), "Перенести", true) || enter) && !importLink_.empty()) app_.startImportLink(importLink_);
        if (pill("##iPaste", dl, ImVec2(x + 172, y), ImVec2(120, 42), "Очистить")) importLink_.clear();
    } else {
        drawText(dl, F_.regular, 13.f, ImVec2(x, y), white(0.55f), "По одной песне в строке: «Исполнитель - Название».", w);
        y += 30;
        textField("##impName", ImVec2(x, y), w, importName_, "Название плейлиста");
        y += 52;
        textField("##impList", ImVec2(x, y), w, importList_, "", true, 190.f);
        y += 206;
        if (pill("##iGo2", dl, ImVec2(x, y), ImVec2(160, 42), "Перенести", true) && !importList_.empty()) app_.startImportList(importName_, importList_);
    }
}

// ---------- плейлисты ----------

void Ui::modalNewPlaylist(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 28, w = sz.x - 56;
    drawText(dl, F_.bold, 24.f, ImVec2(x, o.y + 24), white(0.98f), "Новый плейлист");
    if (modalFirst_) ImGui::SetKeyboardFocusHere();
    bool enter = textField("##plName", ImVec2(x, o.y + 80), w, newPlName_, "Название");
    bool ok = pill("##plOk", dl, ImVec2(x, o.y + 140), ImVec2(150, 42), "Создать", true);
    if ((ok || enter) && !trim(newPlName_).empty()) {
        std::string id = app_.newPlaylist(trim(newPlName_));
        newPlName_.clear();
        closeModal();
        app_.openView(ViewKind::Playlist, id);
    }
}

void Ui::modalAddToPlaylist(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 28, w = sz.x - 56, y = o.y + 24;
    drawText(dl, F_.bold, 24.f, ImVec2(x, y), white(0.98f), "Добавить в плейлист");
    drawText(dl, F_.regular, 13.f, ImVec2(x, y + 36), white(0.55f), ellipsize(F_.regular, 13.f, pickTrack_.title, w - 40));
    y += 74;
    if (pill("##apNew", dl, ImVec2(x, y), ImVec2(w, 40), "+  Новый плейлист", true)) {
        newPlName_.clear();
        openModal(Modal::NewPlaylist);
        return;
    }
    y += 52;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::BeginChild("##apList", ImVec2(w, sz.y - (y - o.y) - 24), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    touchScroll();
    ImDrawList* cd = ImGui::GetWindowDrawList();
    float cy = y - ImGui::GetScrollY();
    int i = 0;
    for (auto& p : app_.lib.playlists) {
        bool has = std::find(p.ids.begin(), p.ids.end(), pickTrack_.id) != p.ids.end();
        ImGui::PushID(i++);
        Hit h = hit("##apRow", ImVec2(x, cy), ImVec2(w, 46));
        Fx f = fx(h);
        float k = has ? 0.f : f.k;
        cd->AddRectFilled(ImVec2(x, cy), ImVec2(x + w, cy + 46), white(0.05f + 0.06f * k + 0.04f * f.press), radius(12.f));
        cd->AddRect(ImVec2(x, cy), ImVec2(x + w, cy + 46), has ? alpha(pal_.acc, 0.35f) : white(0.05f + 0.12f * k), radius(12.f));
        if (k > 0.01f) icon(cd, Icon::Plus, ImVec2(x + w - 104, cy + 23), 14.f, alpha(pal_.acc, k));
        drawText(cd, F_.bold, 15.f, ImVec2(x + 16, cy + 13), white(0.95f), ellipsize(F_.bold, 15.f, p.name, w - 120));
        drawText(cd, F_.regular, 12.5f, ImVec2(x + w - 90, cy + 15), has ? alpha(pal_.acc, 0.9f) : white(0.45f), has ? "уже тут" : songsCount(p.ids.size()));
        if (h.clicked && !has) {
            app_.addToPlaylist(p.id, pickTrack_);
            app_.toast("Добавлено в «" + p.name + "»");
            closeModal();
        }
        ImGui::PopID();
        cy += 54;
    }
    if (app_.lib.playlists.empty()) drawText(cd, F_.regular, 14.f, ImVec2(x, cy + 6), white(0.5f), "Плейлистов пока нет.");
    ImGui::Dummy(ImVec2(1, 4));
    ImGui::EndChild();
}

// ---------- свой текст и настройка по нажатиям ----------

void Ui::modalOwnLyrics(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 28, w = sz.x - 56;
    drawText(dl, F_.bold, 24.f, ImVec2(x, o.y + 24), white(0.98f), "Свой текст");
    drawText(dl, F_.regular, 13.f, ImVec2(x, o.y + 58), white(0.55f),
             "Вставьте текст. Если он в формате LRC ([мм:сс.xx] строка), он будет листаться сам. Иначе его можно синхронизировать нажатиями.", w);
    textField("##ownLy", ImVec2(x, o.y + 112), w, ownLyrics_, "", true, sz.y - 200.f);
    float by = o.y + sz.y - 64;
    if (pill("##olOk", dl, ImVec2(x, by), ImVec2(140, 40), "Сохранить", true) && !trim(ownLyrics_).empty()) {
        app_.setOwnLyrics(ownLyrics_);
        closeModal();
    }
    if (pill("##olTap", dl, ImVec2(x + 150, by), ImVec2(210, 40), "Расставить по нажатиям") && !trim(ownLyrics_).empty()) {
        tapLines_.clear();
        for (auto& ln : split(ownLyrics_, '\n')) if (!trim(ln).empty()) tapLines_.push_back(trim(ln));
        tapTimes_.clear();
        openModal(Modal::TapSync);
        if (!app_.isPlaying()) app_.togglePause();
    }
}

void Ui::modalTap(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 28, w = sz.x - 56;
    drawText(dl, F_.bold, 24.f, ImVec2(x, o.y + 24), white(0.98f), "Синхронизация");
    drawText(dl, F_.regular, 13.f, ImVec2(x, o.y + 58), white(0.55f), "Слушайте песню и нажимайте пробел или кнопку в начале каждой строки.", w);
    std::size_t n = tapTimes_.size();
    float y = o.y + 110;
    auto line = [&](std::size_t i, float fs, ImU32 col, ImFont* f) {
        if (i < tapLines_.size()) { drawText(dl, f, fs, ImVec2(x, y), col, ellipsize(f, fs, tapLines_[i], w)); }
        y += fs + 14;
    };
    if (n > 0) line(n - 1, 16.f, white(0.4f), F_.regular);
    else y += 30;
    if (n < tapLines_.size()) {
        line(n, 24.f, pal_.acc, F_.bold);
        line(n + 1, 16.f, white(0.55f), F_.regular);
        line(n + 2, 16.f, white(0.35f), F_.regular);
    } else {
        drawText(dl, F_.bold, 20.f, ImVec2(x, y), white(0.9f), "Все строки отмечены");
    }
    drawText(dl, F_.regular, 13.f, ImVec2(x, o.y + sz.y - 130), white(0.5f), std::to_string(n) + " из " + std::to_string(tapLines_.size()));
    bool tap = pill("##tap", dl, ImVec2(x, o.y + sz.y - 100), ImVec2(w, 46), "Следующая строка (пробел)", true) || (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !modalFirst_);
    if (tap && n < tapLines_.size()) tapTimes_.push_back(app_.position() + (app_.segment.hasLimit && !app_.segment.preview ? app_.segment.limS : 0.0));
    float by = o.y + sz.y - 44;
    if (pill("##tapUndo", dl, ImVec2(x, by), ImVec2(120, 36), "Назад") && !tapTimes_.empty()) tapTimes_.pop_back();
    if (n == tapLines_.size() && !tapLines_.empty() && pill("##tapSave", dl, ImVec2(x + w - 150, by), ImVec2(150, 36), "Сохранить", true)) {
        std::string lrc;
        for (std::size_t i = 0; i < tapLines_.size(); ++i) {
            double t = tapTimes_[i];
            int m = static_cast<int>(t / 60);
            char b[32];
            std::snprintf(b, sizeof b, "[%02d:%05.2f] ", m, t - m * 60.0);
            lrc += b + tapLines_[i] + "\n";
        }
        app_.setOwnLyrics(lrc);
        closeModal();
    }
}

// ---------- пасхалка и подтверждение ----------

void Ui::modalTeto(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 28, w = sz.x - 56;
    std::string title = tetoFirst_ ? "Касане Тето пришла!" : "Тето уже здесь";
    ImVec2 ts = measure(F_.bold, 26.f, title);
    drawText(dl, F_.bold, 26.f, ImVec2(o.x + (sz.x - ts.x) / 2, o.y + 40), pal_.acc, title);
    if (tex_.hasTeto()) {
        float hh = 190.f, ww = hh * tex_.tetoAspect();
        float bounce = std::fabs(std::sin(time_ * 3.4f)) * 8.f;
        dl->AddImage(tex_.teto(app_.now()), ImVec2(o.x + (sz.x - ww) / 2, o.y + 92 - bounce), ImVec2(o.x + (sz.x + ww) / 2, o.y + 92 + hh - bounce));
    }
    drawText(dl, F_.regular, 14.f, ImVec2(x, o.y + 300), white(0.7f), "Включена тема «Тето» и танцующая Тето. Всё это можно отключить в настройках, во вкладке «Вид».", w);
    if (pill("##tetoOk", dl, ImVec2(o.x + sz.x / 2 - 70, o.y + sz.y - 62), ImVec2(140, 40), "Багет!", true)) closeModal();
}

void Ui::modalConfirm(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 28, w = sz.x - 56;
    drawText(dl, F_.bold, 18.f, ImVec2(x, o.y + 34), white(0.97f), confirmText_, w - 30);
    float by = o.y + sz.y - 66;
    if (pill("##cfYes", dl, ImVec2(x, by), ImVec2(130, 42), "Да", true)) {
        auto f = std::move(confirmYes_);
        confirmYes_ = nullptr;
        closeModal();
        if (f) f();
    }
    if (pill("##cfNo", dl, ImVec2(x + 140, by), ImVec2(130, 42), "Отмена")) closeModal();
}

// ---------- аккаунт ----------

void Ui::modalAccount(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    AccountState& ac = app_.acct;
    float x = o.x + 28, w = sz.x - 56, y = o.y + 24;
    drawText(dl, F_.bold, 24.f, ImVec2(x, y), white(0.98f), "Аккаунт");
    y += 46;

    if (!app_.cloudConfigured()) {
        drawText(dl, F_.regular, 14.f, ImVec2(x, y), white(0.6f),
                 "Аккаунты ещё не подключены: в приложении не указан проект Firebase (файл app/cloud_config.hpp).", w);
        return;
    }

    if (ac.loggedIn()) {
        // карточка профиля: полоса цвета профиля, аватарка, никнейм и «о себе»
        ImU32 pc = profileColor(ac.prof.color);
        ImVec2 b0(x, y - 6), b1(x + w, y + 64);
        dl->AddRectFilled(b0, b1, alpha(pc, 0.20f), radius(18.f));
        dl->PushClipRect(b0, b1, true);
        glow(dl, ImVec2(b1.x - 90.f, b0.y + 20.f), 150.f, pc, 0.45f, 22);
        glow(dl, ImVec2(b0.x + 200.f, b1.y), 110.f, pal_.acc2, 0.25f, 18);
        dl->PopClipRect();
        ImVec2 c(x + 58, y + 64);
        dl->AddCircleFilled(c, 50.f, IM_COL32(20, 18, 30, 255), 48);  // отделяем аватарку от полосы
        drawAvatar(dl, c, 44.f);
        float tx = x + 122, tw = w - 122;
        drawText(dl, F_.bold, 21.f, ImVec2(tx, y + 72), white(0.98f), ellipsize(F_.bold, 21.f, ac.name(), tw));
        bool noStatus = trim(ac.prof.status).empty();
        drawText(dl, F_.regular, 14.f, ImVec2(tx, y + 102), white(noStatus ? 0.35f : 0.68f),
                 ellipsize(F_.regular, 14.f, noStatus ? "Расскажите о себе в профиле" : ac.prof.status, tw));
        drawText(dl, F_.regular, 12.5f, ImVec2(tx, y + 124), white(0.38f), ellipsize(F_.regular, 12.5f, ac.email, tw));
        y += 158;

        // немного статистики
        {
            const ListenStats& ls = app_.stats;
            double sec = ls.totalSec();
            std::string hours = sec >= 3600 ? std::to_string(static_cast<int>(sec / 3600)) + " ч" : std::to_string(static_cast<int>(sec / 60)) + " мин";
            auto arts = ls.topArtists(1);
            std::string fav = arts.empty() ? "—" : arts[0].name;
            struct Tile { const char* cap; std::string val; } tiles[3] = {
                {"Наслушано", hours}, {"Прослушиваний", std::to_string(ls.playCount())}, {"Любимый исполнитель", fav}};
            float gap = 10.f, tw3 = (w - 2 * gap) / 3.f;
            for (int i = 0; i < 3; ++i) {
                ImVec2 t0(x + static_cast<float>(i) * (tw3 + gap), y), t1(t0.x + tw3, y + 66);
                dl->AddRectFilled(t0, t1, white(0.045f), radius(14.f));
                drawText(dl, F_.bold, 18.f, t0 + ImVec2(14, 12), i == 0 ? alpha(pc, 1.f) : white(0.95f),
                         ellipsize(F_.bold, 18.f, tiles[i].val, tw3 - 24));
                drawText(dl, F_.regular, 12.f, t0 + ImVec2(14, 40), white(0.5f), ellipsize(F_.regular, 12.f, tiles[i].cap, tw3 - 24));
            }
            y += 84;
        }

        std::string st;
        ImU32 stc = white(0.7f);
        if (ac.syncing || ac.profSyncing) { st = "Синхронизация…"; stc = alpha(pal_.acc, 0.95f); }
        else if (!ac.error.empty()) { st = "Не удалось синхронизировать: " + ac.error; stc = IM_COL32(255, 150, 140, 255); }
        else if (ac.lastOk >= 0) {
            int m = static_cast<int>((app_.now() - ac.lastOk) / 60);
            st = m < 1 ? "Всё синхронизировано только что" : "Всё синхронизировано " + std::to_string(m) + " мин назад";
            stc = IM_COL32(140, 220, 160, 255);
        } else st = "Ещё не синхронизировано";
        dl->AddCircleFilled(ImVec2(x + 6, y + 9), 4.f, stc, 12);
        drawText(dl, F_.regular, 14.f, ImVec2(x + 18, y), stc, ellipsize(F_.regular, 14.f, st, w - 20));
        y += 30;
        drawText(dl, F_.regular, 13.f, ImVec2(x, y), white(0.5f),
                 "Музыка, плейлисты и профиль хранятся в аккаунте — войдите с той же почтой на другом устройстве, и всё появится там.", w);
        y += 58;
        static const Icon kSync = Icon::Loop, kOut = Icon::Close, kEdit = Icon::User;
        float ew = pillWidth("Изменить профиль", true), sw = pillWidth("Синхронизировать", false), ow = pillWidth("Выйти", false);
        if (pill("##acEdit", dl, ImVec2(x, y), ImVec2(ew, 42), "Изменить профиль", true, false, &kEdit)) openModal(Modal::Profile);
        float bx = x + ew + 10;
        if (bx + sw + 10 + ow > x + w) { bx = x; y += 52; }  // узкое окно — вторая строка
        if (pill("##acSync", dl, ImVec2(bx, y), ImVec2(sw, 42), "Синхронизировать", false, false, &kSync) && !ac.syncing) app_.cloudSync(true);
        if (pill("##acOut", dl, ImVec2(bx + sw + 10, y), ImVec2(ow, 42), "Выйти", false, false, &kOut))
            confirm("Выйти из аккаунта? Музыка на этом устройстве останется, но перестанет синхронизироваться.", [this] { app_.cloudSignOut(); });
        return;
    }

    // вход / регистрация
    int tab = segmented("##acTab", dl, ImVec2(x, y), 36.f, {"Вход", "Регистрация"}, acctCreate_ ? 1 : 0);
    if (tab >= 0) { acctCreate_ = tab == 1; ac.error.clear(); }
    y += 56;
    drawText(dl, F_.regular, 13.f, ImVec2(x, y), white(0.55f),
             acctCreate_ ? "Новый аккаунт: «Моя музыка» и плейлисты с этого устройства сразу перенесутся в него."
                         : "Войдите — и музыка с других ваших устройств появится здесь.", w);
    y += 40;
    if (modalFirst_ && !mobile_) ImGui::SetKeyboardFocusHere();
    bool e1 = textField("##acMail", ImVec2(x, y), w, acctEmail_, "Почта");
    y += 54;
    bool e2 = textField("##acPass", ImVec2(x, y), w, acctPass_, "Пароль (от 6 символов)", false, 0, true);
    y += 60;
    bool go = pill("##acGo", dl, ImVec2(x, y), ImVec2(w, 44), ac.busy ? ac.status : (acctCreate_ ? "Создать аккаунт" : "Войти"), true);
    // на телефоне поля заполняются в системных окнах: вход — после пароля (или по кнопке), не после почты
    bool submit = go || (mobile_ ? (e2 && !trim(acctEmail_).empty()) : (e1 || e2));
    if (submit && !ac.busy) {
        app_.cloudSignIn(acctEmail_, acctPass_, acctCreate_);
        acctPass_.clear();  // пароль в памяти не держим
    }
    y += 58;
    if (!ac.error.empty()) {
        drawText(dl, F_.regular, 13.5f, ImVec2(x, y), IM_COL32(255, 150, 140, 255), ac.error, w);
        y += 30;
    }
    if (!acctCreate_) {
        Hit h = hit("##acForgot", ImVec2(x, y), ImVec2(measure(F_.regular, 13.5f, "Забыли пароль?").x, 22));
        drawText(dl, F_.regular, 13.5f, ImVec2(x, y), h.hovered ? pal_.acc : white(0.6f), "Забыли пароль?");
        if (h.clicked) app_.cloudResetPassword(acctEmail_);
    }
}

// ---------- профиль ----------

ImU32 Ui::profileColor(int i) const {
    static const ImU32 k[] = {0, IM_COL32(255, 112, 170, 255), IM_COL32(255, 98, 98, 255), IM_COL32(255, 152, 72, 255),
                              IM_COL32(246, 206, 82, 255), IM_COL32(104, 212, 132, 255), IM_COL32(72, 202, 214, 255),
                              IM_COL32(108, 146, 255, 255)};
    return i <= 0 || i > 7 ? pal_.acc : k[i];
}

namespace {
// первая буква имени, заглавная (латиница и кириллица)
std::string initialOf(const std::string& name) {
    std::u32string u = decodeUtf8(name);
    if (u.empty()) return "?";
    char32_t c = u[0];
    if (c >= U'a' && c <= U'z') c -= 32;
    else if (c >= 0x430 && c <= 0x44F) c -= 0x20;
    else if (c == 0x451) c = 0x401;
    return encodeUtf8(std::u32string(1, c));
}
}  // namespace

void Ui::drawAvatar(ImDrawList* dl, ImVec2 c, float r) {
    const AccountState& ac = app_.acct;
    if (avatarVer_ != ac.avatarVer) {
        avatarVer_ = ac.avatarVer;
        tex_.setAvatar(ac.prof.avatar);
    }
    ImU32 col = profileColor(ac.prof.color);
    int fr = ac.prof.frame;
    if (fr == 2) glow(dl, c, r * 1.75f, col, (calm() ? 0.5f : 0.45f + 0.15f * std::sin(time_ * 2.2f)), 16);
    if (ImTextureID t = tex_.avatar()) {
        dl->AddImageRounded(t, c - ImVec2(r, r), c + ImVec2(r, r), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, r);
    } else {
        dl->AddCircleFilled(c, r, col, 48);
        std::string init = initialOf(ac.name());
        float fs = r * 0.95f;
        ImVec2 is = measure(F_.bold, fs, init);
        drawText(dl, F_.bold, fs, c - is * 0.5f, IM_COL32(14, 10, 20, 235), init);
    }
    if (fr >= 1) {
        float th = std::max(1.6f, r * 0.07f);
        dl->AddCircle(c, r + th * 1.4f, fr == 2 ? white(0.9f) : col, 48, th);
    }
}

void Ui::pickImageFile(std::function<void(std::string)> done) {
    if (app_.platform.pickAsync) {
        app_.platform.pickAsync("image", [done](std::vector<std::string> p) { if (!p.empty()) done(p[0]); });
        return;
    }
    if (!app_.platform.pickImage) return;
    std::string src = app_.platform.pickImage();
    if (!src.empty()) done(src);
}

void Ui::pickAvatar() {
    if (avatarBusy_) return;
    pickImageFile([this](std::string src) {
        std::string bytes;
        if (!readFile(src, bytes) || bytes.empty()) { app_.toast("Не удалось открыть файл"); return; }
        if (bytes.size() > 40u << 20) { app_.toast("Файл слишком большой (больше 40 МБ)"); return; }
        avatarBusy_ = true;
        app_.async([this, bytes = std::move(bytes)] {
            std::string av = Textures::makeAvatar(bytes);
            app_.post([this, av] {
                avatarBusy_ = false;
                if (av.empty()) { app_.toast("Не удалось прочитать картинку"); return; }
                app_.setAvatar(av);
                app_.toast("Аватарка обновлена");
            });
        });
    });
}

void Ui::modalProfile(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    AccountState& ac = app_.acct;
    if (!ac.loggedIn()) { closeModal(); return; }
    if (modalFirst_) { profName_ = ac.prof.name; profStatus_ = ac.prof.status; }
    float x = o.x + 28, w = sz.x - 56, y = o.y + 24;
    drawText(dl, F_.bold, 24.f, ImVec2(x, y), white(0.98f), "Профиль");
    y += 54;

    // аватарка: клик — сменить
    ImVec2 c(x + 52, y + 52);
    drawAvatar(dl, c, 48.f);
    Hit ah = hit("##pfAv", c - ImVec2(52, 52), ImVec2(104, 104));
    float k = hv(ah.id, ah.hovered);
    if (k > 0.01f || avatarBusy_) {
        dl->AddCircleFilled(c, 48.f, IM_COL32(0, 0, 0, static_cast<int>(150 * (avatarBusy_ ? 1.f : k))), 48);
        const char* t = avatarBusy_ ? "…" : "Сменить";
        ImVec2 ts = measure(F_.bold, 14.f, t);
        drawText(dl, F_.bold, 14.f, c - ts * 0.5f, white(avatarBusy_ ? 1.f : k), t);
    }
    if (ah.clicked) pickAvatar();
    float tx = x + 126, tw = w - 126;
    drawText(dl, F_.bold, 20.f, ImVec2(tx, y + 6), white(0.98f), ellipsize(F_.bold, 20.f, ac.name(), tw));
    drawText(dl, F_.regular, 13.f, ImVec2(tx, y + 34), white(0.45f), ellipsize(F_.regular, 13.f, ac.email, tw));
    static const Icon kPic = Icon::Import;
    float pw = pillWidth("Сменить фото", true);
    if (pill("##pfPick", dl, ImVec2(tx, y + 60), ImVec2(pw, 38), avatarBusy_ ? "Обработка…" : "Сменить фото", true, false, &kPic)) pickAvatar();
    if (!ac.prof.avatar.empty() &&
        iconButton("##pfDel", dl, ImVec2(tx + pw + 30, y + 79), 38.f, Icon::Trash, false, "Убрать фото"))
        app_.setAvatar("");
    y += 128;

    auto cap = [&](const char* t) {
        drawText(dl, F_.bold, 12.f, ImVec2(x, y), white(0.5f), t);
        y += 22;
    };
    cap("НИКНЕЙМ");
    textField("##pfName", ImVec2(x, y), w, profName_, "Как вас называть");
    y += 56;
    cap("О СЕБЕ");
    textField("##pfStat", ImVec2(x, y), w, profStatus_, "Например: слушаю всё подряд");
    y += 60;
    // правки применяем сразу (в облако уйдут через пару секунд тишины)
    std::string nm = clipChars(profName_, 24), stt = clipChars(profStatus_, 60);
    if (nm != ac.prof.name || stt != ac.prof.status) {
        ac.prof.name = nm;
        ac.prof.status = stt;
        app_.profileChanged();
    }

    cap("ЦВЕТ");
    float d = std::min(40.f, (w - 7 * 10.f) / 8.f);
    for (int i = 0; i < 8; ++i) {
        ImGui::PushID(i);
        ImVec2 cc(x + d / 2 + static_cast<float>(i) * (d + 10.f), y + d / 2);
        Hit h = hit("##pfCol", cc - ImVec2(d / 2, d / 2), ImVec2(d, d));
        float hk = hv(h.id, h.hovered);
        dl->AddCircleFilled(cc, d / 2 - 3 + 2 * hk, profileColor(i), 32);
        if (i == 0) icon(dl, Icon::Sparkle, cc, d * 0.42f, IM_COL32(14, 10, 20, 200));
        if (ac.prof.color == i) dl->AddCircle(cc, d / 2 + 2, white(0.95f), 32, 2.f);
        if (h.hovered) tip(i == 0 ? "Как у темы оформления" : "Цвет профиля");
        if (h.clicked && ac.prof.color != i) { ac.prof.color = i; app_.profileChanged(); }
        ImGui::PopID();
    }
    y += d + 22;

    cap("РАМКА АВАТАРКИ");
    int f = segmented("##pfFrame", dl, ImVec2(x, y), 36.f, {"Без рамки", "Кольцо", "Неон"}, ac.prof.frame);
    if (f >= 0 && f != ac.prof.frame) { ac.prof.frame = f; app_.profileChanged(); }
    y += 58;

    if (pill("##pfDone", dl, ImVec2(x, y), ImVec2(w, 44), "Готово", true)) openModal(Modal::Account);
}

}  // namespace mx::ui
