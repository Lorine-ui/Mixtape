// Телефон (Android): вкладки снизу, компактный мини-плеер, прокрутка пальцем, кнопка «Назад», меню «Ещё»
// и ввод текста системной клавиатурой. На компьютере ничего отсюда не вызывается (mobile_ == false).
#include <algorithm>
#include <cmath>

#include "text.hpp"
#include "ui.hpp"
#include "i18n.hpp"
#include "imgui_internal.h"  // ClearActiveID, текущее окно — для прокрутки пальцем

namespace mx::ui {

// ---------- прокрутка пальцем ----------
// ImGui прокручивает только колёсиком. На телефоне тянем список пальцем: если палец сдвинулся по вертикали
// больше, чем на несколько точек, нажатие кнопки под ним отменяется и окно едет за пальцем, а после
// отпускания ещё немного катится по инерции.
void Ui::touchScroll() {
    if (!mobile_) return;
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImGuiIO& io = ImGui::GetIO();
    ImGuiID id = w->ID;
    float& vel = scrollVel_[id];
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        dragWin_ = id;
        dragging_ = false;
        vel = 0.f;
    }
    if (dragWin_ == id && io.MouseDown[0]) {
        ImVec2 d = ImGui::GetMouseDragDelta(0, 0.f);
        if (!dragging_ && std::fabs(d.y) > 9.f && std::fabs(d.y) > std::fabs(d.x)) {
            dragging_ = true;
            ImGui::ClearActiveID();  // это не нажатие, а прокрутка
        }
        if (dragging_) {
            float dy = io.MouseDelta.y;
            ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() - dy, 0.f, ImGui::GetScrollMaxY()));
            float inst = -dy / std::max(dt_, 1.f / 240.f);
            vel = vel * 0.6f + inst * 0.4f;
        }
    } else {
        if (dragWin_ == id && !io.MouseDown[0]) dragWin_ = 0;
        if (std::fabs(vel) > 5.f) {
            ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() + vel * dt_, 0.f, ImGui::GetScrollMaxY()));
            vel *= std::exp(-4.5f * dt_);
        } else {
            vel = 0.f;
        }
    }
}

// ---------- «Назад» ----------
bool Ui::onBack() {
    if (modal_ != Modal::None && modalA_.t > 0.5f) {
        if (modal_ == Modal::Settings && mobile_ && settingsTab_ >= 0) { settingsTab_ = -1; return true; }  // к списку разделов
        closeModal();
        return true;
    }
    if (fr_.open) {
        fr_.open = false;
        if (fr_.previewing) { app_.endPreview(); fr_.previewing = false; }
        return true;
    }
    if (wantFull_) { openFull(false); return true; }
    if (app_.view != ViewKind::Home) { app_.openView(ViewKind::Home); return true; }
    return false;
}

// ---------- ввод текста ----------
// Поле, похожее на обычное; касание открывает системное окно с клавиатурой (ImGui сам с клавиатурой телефона
// не дружит: русские буквы и подсказки не доходят). Возвращает true, когда текст подтверждён.
bool Ui::mobileField(const char* id, ImVec2 p, float w, float h, std::string& value, const char* hint, const char* title, bool multiline,
                     bool password) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Hit hh = hit(id, p, ImVec2(w, h));
    float k = hv(hh.id, hh.held);
    float r = multiline ? radius(14.f) : h / 2;
    dl->AddRectFilled(p, p + ImVec2(w, h), white(0.07f + 0.05f * k), r);
    dl->AddRect(p, p + ImVec2(w, h), alpha(pal_.acc, 0.25f + 0.5f * k), r);
    std::string dots;
    if (password) for (std::size_t i = 0; i < value.size() && i < 24; ++i) dots += "\xE2\x80\xA2";  // •
    const std::string& shown = value.empty() ? std::string(hint ? hint : "") : (password ? dots : value);
    ImU32 col = value.empty() ? white(0.4f) : white(0.95f);
    if (multiline) drawText(dl, F_.regular, 14.f, p + ImVec2(16, 12), col, shown, w - 32);
    else drawText(dl, F_.regular, 15.f, ImVec2(p.x + 18, p.y + h / 2 - measure(F_.regular, 15.f, "Ag").y / 2), col,
                  ellipsize(F_.regular, 15.f, shown, w - 36));
    bool done = false;
    if (hh.clicked && password && app_.platform.promptPassword) {
        std::string* target = &value;
        app_.platform.promptPassword(tr(title ? title : "Пароль"), [this, target](std::string s) {
            *target = s;
            textDone_ = target;
        });
    } else if (hh.clicked && app_.platform.promptText) {
        std::string* target = &value;  // поля — члены Ui, живут всё время работы приложения
        app_.platform.promptText(tr(title ? title : (hint ? hint : "")), value, multiline, [this, target](std::string s) {
            *target = s;
            textDone_ = target;
        });
    }
    if (textDone_ == &value) { textDone_ = nullptr; done = true; }
    return done;
}

// ---------- вкладки снизу ----------
void Ui::drawTabBar() {
    const float h = kTabH;
    ImGui::SetNextWindowPos(ImVec2(0, W_.y - h));
    ImGui::SetNextWindowSize(ImVec2(W_.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##tabs", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(0, W_.y - h), b(W_.x, W_.y);
    dl->AddRectFilled(a, b, IM_COL32(12, 10, 18, static_cast<int>(200 + 55 * panelA_)));
    dl->AddLine(a, ImVec2(b.x, a.y), white(0.07f), 1.f);
    struct Tab { const char* id; Icon ic; const char* label; bool active; };
    bool moreActive = modal_ == Modal::More || app_.view == ViewKind::Clips || app_.view == ViewKind::Playlist ||
                      app_.view == ViewKind::ForYou || app_.view == ViewKind::Stats;
    const Tab tabs[] = {
        {"##tHome", Icon::Home, "Главная", app_.view == ViewKind::Home || app_.view == ViewKind::Search},
        {"##tLib", Icon::HeartFilled, "Моя музыка", app_.view == ViewKind::Library},
        {"##tOff", Icon::Download, "Офлайн", app_.view == ViewKind::Offline},
        {"##tMore", Icon::Menu, "Ещё", moreActive},
    };
    float tw = W_.x / 4.f;
    for (int i = 0; i < 4; ++i) {
        ImVec2 p(a.x + tw * static_cast<float>(i), a.y);
        Hit hh = hit(tabs[i].id, p, ImVec2(tw, h));
        float on = hv(hh.id ^ 0x7AB5u, tabs[i].active, 14.f);
        float press = hv(hh.id, hh.held, 20.f);
        ImU32 c = mix(white(0.55f), pal_.acc, on);
        float cx = p.x + tw / 2;
        if (on > 0.01f) dl->AddRectFilled(ImVec2(cx - 26, a.y + 8), ImVec2(cx + 26, a.y + 36), alpha(pal_.acc, 0.16f * on), 14.f);
        icon(dl, tabs[i].ic, ImVec2(cx, a.y + 22), 22.f * (1.f - 0.08f * press), c);
        ImVec2 ls = measure(F_.bold, 11.5f, tabs[i].label);
        drawText(dl, F_.bold, 11.5f, ImVec2(cx - ls.x / 2, a.y + 40), c, tabs[i].label);
        if (hh.clicked) {
            if (wantFull_) openFull(false);
            switch (i) {
                case 0: app_.openView(ViewKind::Home); break;
                case 1: app_.openView(ViewKind::Library); break;
                case 2: app_.openView(ViewKind::Offline); break;
                default: openModal(Modal::More); break;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---------- мини-плеер на телефоне ----------
void Ui::drawMiniMobile() {
    if (!app_.cur) return;
    const Track& t = *app_.cur;
    const float h = kMiniHMobile, w = W_.x - 16.f, x = 8.f;
    float y = W_.y - kTabH - h - 8.f + (1.f - easeOut(miniA_.v)) * (h + kTabH);
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##mini", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p(x, y), q(x + w, y + h);
    softShadow(dl, p, q, radius(18.f), 14.f, 0.5f);
    dl->AddRectFilled(p, q, IM_COL32(24, 20, 34, 250), radius(18.f));
    dl->AddRectFilled(p, q, alpha(pal_.acc, 0.10f), radius(18.f));

    // касание карточки (кроме кнопок справа) открывает проигрыватель
    Hit open = hit("##miniOpen", p, ImVec2(w - 96, h));
    drawCover(dl, t, p + ImVec2(10, 10), h - 20, radius(10.f));
    std::string sub = app_.loading ? "Загрузка " + std::to_string(static_cast<int>(app_.loadProgress.load() * 100)) + "%"
                    : !app_.playError.empty() ? app_.playError : (t.artist.empty() ? std::string(sourceLabel(t.source)) : t.artist);
    ImU32 subCol = app_.loading ? alpha(pal_.acc, 0.9f) : !app_.playError.empty() ? IM_COL32(255, 150, 140, 255) : white(0.6f);
    twoLines(dl, p.x + h + 2, p.y + h / 2, w - h - 104, t.title, 15.f, white(0.97f), sub, 12.5f, subCol);
    if (open.clicked) openFull(true);

    bool playing = app_.isPlaying();
    if (iconButton("##mmPlay", dl, ImVec2(q.x - 72, p.y + h / 2), 44.f, playing ? Icon::Pause : Icon::Play, false, nullptr, white(0.95f)))
        app_.togglePause();
    if (iconButton("##mmNext", dl, ImVec2(q.x - 28, p.y + h / 2), 40.f, Icon::Next, false, nullptr, white(0.8f))) app_.next();

    // тонкая полоса прогресса по нижнему краю
    double len = app_.visibleLength();
    float frac = len > 0 ? static_cast<float>(std::clamp(app_.position() / len, 0.0, 1.0)) : 0.f;
    float bx0 = p.x + 14, bx1 = q.x - 14;
    dl->AddRectFilled(ImVec2(bx0, q.y - 3), ImVec2(bx1, q.y - 1), white(0.12f), 1.f);
    if (frac > 0) dl->AddRectFilled(ImVec2(bx0, q.y - 3), ImVec2(bx0 + (bx1 - bx0) * frac, q.y - 1), pal_.acc, 1.f);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---------- меню «Ещё» ----------
void Ui::modalMore(ImVec2 o, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = o.x + 20, w = sz.x - 40, y = o.y + 22;
    drawText(dl, F_.bold, 22.f, ImVec2(x, y), white(0.98f), "Ещё");
    y += 48;
    auto item = [&](const char* id, Icon ic, const std::string& label, const std::string& sub) {
        Hit hh = hit(id, ImVec2(x, y), ImVec2(w, 52));
        ItemFx it = listItem(dl, hh, ImVec2(x, y), ImVec2(x + w, y + 52), false, radius(14.f), false);
        dl->AddCircleFilled(ImVec2(x + 26, y + 26), 18.f, alpha(pal_.acc, 0.16f + 0.1f * it.k), 24);
        icon(dl, ic, ImVec2(x + 26, y + 26), 18.f, pal_.acc);
        twoLines(dl, x + 56, y + 26, w - 64, label, 15.f, white(0.95f), sub, 12.5f, white(0.5f));
        y += 56;
        return hh.clicked;
    };
    if (item("##mAcct", Icon::User, "Аккаунт", app_.acct.loggedIn() ? app_.acct.name() : "Войдите — музыка будет на всех устройствах"))
        openModal(Modal::Account);
    if (item("##mRec", Icon::Sparkle, "Для вас", "Похожее на то, что вы слушаете")) { closeModal(); app_.openView(ViewKind::ForYou); }
    if (item("##mStat", Icon::Chart, "Статистика", "Сколько и что вы слушаете")) { closeModal(); app_.openView(ViewKind::Stats); }
    if (item("##mClips", Icon::Scissors, "Фрагменты", "Кусочки песен")) { closeModal(); app_.openView(ViewKind::Clips); }
    if (item("##mRnd", Icon::Dice, "Случайная песня", "Удивите себя")) { closeModal(); app_.randomSong(); }
    if (item("##mImp", Icon::Import, "Перенести плейлист", "Spotify, YouTube, SoundCloud")) { importLink_.clear(); openModal(Modal::Import); }
    if (item("##mFiles", Icon::Files, "Добавить файлы", "Музыка с телефона")) { closeModal(); app_.addFiles(); }
    if (item("##mSet", Icon::Gear, "Настройки", "Вид, звук, источники, данные")) { settingsTab_ = -1; openModal(Modal::Settings); }

    y += 6;
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), white(0.08f), 1.f);
    y += 12;
    drawText(dl, F_.bold, 12.f, ImVec2(x, y), alpha(pal_.acc, 0.9f), "ПЛЕЙЛИСТЫ");
    if (iconButton("##mNewPl", dl, ImVec2(x + w - 16, y + 7), 36.f, Icon::Plus, false)) { newPlName_.clear(); openModal(Modal::NewPlaylist); }
    y += 28;
    // список плейлистов прокручивается, если не помещается
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::BeginChild("##morePl", ImVec2(w, std::max(60.f, o.y + sz.y - y - 16)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    touchScroll();
    ImDrawList* cd = ImGui::GetWindowDrawList();
    float cy = y - ImGui::GetScrollY();
    for (auto& pl : app_.lib.playlists) {
        ImGui::PushID(pl.id.c_str());
        Hit hh = hit("##pl", ImVec2(x, cy), ImVec2(w, 50));
        listItem(cd, hh, ImVec2(x, cy), ImVec2(x + w, cy + 50), false, radius(12.f), false);
        cd->AddRectFilled(ImVec2(x + 8, cy + 7), ImVec2(x + 44, cy + 43), u32(hsl(hueOf(pl.name), 0.5f, 0.32f)), 9.f);
        icon(cd, Icon::Note, ImVec2(x + 26, cy + 25), 18.f, white(0.85f));
        twoLines(cd, x + 56, cy + 25, w - 64, pl.name, 15.f, white(0.92f), songsCount(pl.ids.size()), 12.f, white(0.45f));
        if (hh.clicked) { closeModal(); app_.openView(ViewKind::Playlist, pl.id); }
        cy += 54;
        ImGui::PopID();
    }
    if (app_.lib.playlists.empty()) drawText(cd, F_.regular, 14.f, ImVec2(x + 4, cy + 6), white(0.45f), "Пока нет плейлистов — нажмите «+»");
    float contentH = cy + ImGui::GetScrollY() - y + 10;
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::Dummy(ImVec2(1, std::max(1.f, contentH)));
    ImGui::EndChild();
}

}  // namespace mx::ui
