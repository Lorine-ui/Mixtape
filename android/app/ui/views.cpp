// Экраны со списками: главная, поиск, «Моя музыка», плейлисты, фрагменты.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "text.hpp"
#include "ui.hpp"

namespace mx::ui {

namespace {
float kPad = 28.f;  // поля по краям; на телефоне 16 (задаётся в drawContent)
constexpr float kMaxW = 1280.f;  // наибольшая ширина колонки со списками
}  // namespace

float Ui::contentWidth(float avail) const {
    // уступаем место Тето, только если списку останется не меньше 520 px
    float w = avail - tetoReserve_ >= 520.f ? avail - tetoReserve_ : avail;
    return std::min(w, kMaxW);
}

void Ui::drawContent(ImVec2 pos, ImVec2 size) {
    kPad = mobile_ ? 16.f : 28.f;
    float top = 76.f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + top));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.f);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, white(0.18f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, white(0.3f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##content", ImVec2(size.x, size.y - top), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (scrollTop_) { ImGui::SetScrollY(0.f); scrollTop_ = false; }
    touchScroll();
    switch (app_.view) {
        case ViewKind::Home: viewHome(ImVec2(size.x, size.y - top)); break;
        case ViewKind::Stats: viewStats(ImVec2(size.x, size.y - top)); break;
        case ViewKind::Friends: viewFriends(ImVec2(size.x, size.y - top)); break;
        default: viewList(ImVec2(size.x, size.y - top)); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

// Блок строк внутри текущего окна: ImGuiListClipper рисует только видимое, большая библиотека не тормозит.
void Ui::rows(const std::vector<const Track*>& list, float w, bool inPlaylist, const char* ctx) {
    ImGui::PushID(ctx);  // «Недавние» и «Популярное» на главной иначе делят одни и те же ID кнопок
    std::vector<std::string> ids;
    ids.reserve(list.size());
    for (auto* t : list) ids.push_back(t->id);
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(list.size()), rowH_);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            ImGui::PushID(i);
            row(*list[static_cast<std::size_t>(i)], ids, ImVec2(o.x + kPad - 12.f, o.y + static_cast<float>(i) * rowH_), w - kPad * 2 + 24.f, i, inPlaylist);
            ImGui::PopID();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y));
    ImGui::Dummy(ImVec2(1, static_cast<float>(list.size()) * rowH_));
    ImGui::PopID();
}

void Ui::row(const Track& t, const std::vector<std::string>& ids, ImVec2 p, float w, int index, bool inPlaylist) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // плавное появление строк: каждая чуть позже предыдущей
    float age = static_cast<float>(app_.now() - viewStart_);
    float a = calm() ? 1.f : easeOut((age - static_cast<float>(std::min(index, 14)) * 0.03f) / 0.35f);
    if (a < 0.01f) return;
    p.y += (1.f - a) * 12.f;

    const float ih = rowH_ - 4.f, mid = p.y + ih / 2;
    bool isCur = app_.cur && app_.cur->id == t.id;
    Hit h = hit("##row", p, ImVec2(w, ih));
    float k = hv(h.id, h.hovered);
    ImVec2 q = p + ImVec2(w, ih);
    if (isCur) dl->AddRectFilled(p, q, alpha(pal_.acc, 0.10f * a), radius(14.f));
    else if (k > 0.01f) dl->AddRectFilled(p, q, white(0.06f * k * a), radius(14.f));

    // номер строки (по желанию)
    float x = p.x + 10.f;
    if (showNum_) {
        std::string n = std::to_string(index + 1);
        ImVec2 ns = measure(F_.regular, 13.f, n);
        drawText(dl, F_.regular, 13.f, ImVec2(x + 14 - ns.x / 2, mid - ns.y / 2), isCur ? pal_.acc : white(0.4f * a), n);
        x += 34.f;
    }

    // обложка; у текущей песни поверх неё «эквалайзер»
    const float cs = ih - 12.f;
    ImVec2 cp(x, mid - cs / 2);
    drawCover(dl, t, cp, cs, radius(10.f));
    if (isCur) {
        dl->AddRectFilled(cp, cp + ImVec2(cs, cs), IM_COL32(0, 0, 0, 120), radius(10.f));
        for (int i = 0; i < 3; ++i) {
            float amp = cs * 0.3f;
            float bh = app_.isPlaying() ? 6.f + amp * (0.5f + 0.5f * std::sin(time_ * (6.f + static_cast<float>(i) * 2.f) + static_cast<float>(i) * 1.7f)) : 6.f;
            float bx = cp.x + cs / 2 - 9.f + static_cast<float>(i) * 7.f;
            dl->AddRectFilled(ImVec2(bx, mid + cs * 0.2f - bh), ImVec2(bx + 4, mid + cs * 0.2f), pal_.acc, 2.f);
        }
    } else if (k > 0.01f) {
        dl->AddRectFilled(cp, cp + ImVec2(cs, cs), IM_COL32(0, 0, 0, static_cast<int>(110 * k)), radius(10.f));
        icon(dl, Icon::Play, ImVec2(cp.x + cs / 2, mid), cs * 0.45f, white(k));
    }

    // справа налево: кнопки, длительность, источник
    float rx = q.x - 10;
    bool fav = app_.inLibrary(t);
    auto btn = [&](const char* id, Icon ic, bool active, const char* tp) {
        rx -= 34.f;
        return iconButton(id, dl, ImVec2(rx + 17, mid), 32.f, ic, active, tp);
    };
    if (inPlaylist) {
        if (btn("##rm", Icon::Close, false, "Убрать из плейлиста")) app_.removeFromPlaylist(app_.viewPlaylist, t.id);
    } else if (t.clip && app_.view == ViewKind::Clips) {
        if (btn("##delClip", Icon::Trash, false, "Удалить фрагмент")) {
            std::string id = t.id;
            confirm("Удалить фрагмент «" + t.title + "»?", [this, id] { app_.lib.remove(id); app_.touch(); });
        }
    }
    if (btn("##pl", Icon::Plus, false, "Добавить в плейлист")) { pickTrack_ = t; openModal(Modal::AddToPlaylist); }
    if (!t.clip && !mobile_) {
        if (app_.exporting.count(t.id)) {
            rx -= 34.f;
            float a0 = time_ * 6.f;
            dl->PathArcTo(ImVec2(rx + 17, mid), 9.f, a0, a0 + 4.2f, 16);
            dl->PathStroke(pal_.acc, ImDrawFlags_None, 2.2f);
        } else if (k > 0.01f || ImGui::IsMouseHoveringRect(ImVec2(rx - 34.f, p.y), ImVec2(rx, q.y))) {
            if (btn("##save", Icon::Save, false, "Сохранить на компьютер (mp3/m4a)")) app_.exportTrack(t);
        } else {
            rx -= 34.f;
        }
    }
    const Track* lt = app_.lib.find(t.id);
    const bool off = lt && lt->off;
    if (t.canOffline()) {
        auto dlIt = app_.downloading.find(t.id);
        if (dlIt != app_.downloading.end()) {
            rx -= 34.f;
            dl->PathArcTo(ImVec2(rx + 17, mid), 9.f, -1.57f, -1.57f + 6.28f * std::max(0.08f, static_cast<float>(dlIt->second)), 20);
            dl->PathStroke(pal_.acc, ImDrawFlags_None, 2.4f);
        } else if (btn("##off", off ? Icon::Check : Icon::Download, off, off ? "Скачано: играет без интернета (нажмите, чтобы удалить копию)" : "Скачать для офлайн")) {
            if (off) app_.dropOffline(t);
            else app_.saveOffline(t);
        }
    }
    if (btn("##fav", fav ? Icon::HeartFilled : Icon::Heart, fav, fav ? "Убрать из моей музыки" : "В мою музыку")) {
        if (fav && !t.clip) { Track copy = t; confirm("Убрать «" + t.title + "» из вашей музыки?", [this, copy] { app_.toggleFavorite(copy); }); }
        else app_.toggleFavorite(t);
    }

    // время и источник
    std::string dur = t.dur > 0 ? fmtTime(t.clip ? std::max(0.0, t.end - t.start) : t.dur) : "";
    float tx = rx - 8.f;
    if (!dur.empty() && !mobile_) {
        ImVec2 ds = measure(F_.regular, 13.f, dur);
        drawText(dl, F_.regular, 13.f, ImVec2(tx - ds.x, mid - ds.y / 2), white(0.5f * a), dur);
        tx -= ds.x + 16.f;
    }
    if (showBadge_ || t.clip) {
        std::string badge = t.clip ? "Фрагмент" : std::string(sourceLabel(t.source));
        ImVec2 bs = measure(F_.bold, 11.5f, badge);
        if (tx - bs.x - 20 > x + cs + 200) {
            ImVec2 b0(tx - bs.x - 16, mid - 11), b1(tx, mid + 11);
            dl->AddRectFilled(b0, b1, t.clip ? alpha(pal_.acc, 0.16f * a) : white(0.07f * a), 11.f);
            drawText(dl, F_.bold, 11.5f, ImVec2(b0.x + 8, mid - bs.y / 2), t.clip ? alpha(pal_.acc, a) : white(0.6f * a), badge);
            tx = b0.x - 12.f;
        }
    }

    // название и исполнитель
    float textX = cp.x + cs + 12.f, textW = std::max(60.f, tx - textX - 8.f);
    bool compact = rowH_ < 60.f;
    float fsT = compact ? 14.f : 15.f, fsS = compact ? 12.5f : 13.f;
    ImU32 tc = isCur ? pal_.acc : white(0.96f * a);
    std::string sub = t.artist;
    if (off) sub += (sub.empty() ? "" : " · ") + std::string("скачано");
    SubLink artLink{"##art", t.artist};
    const bool canLink = !t.artist.empty() && t.source != "local" && !t.clip;
    twoLines(dl, textX, mid, textW, t.title, fsT, tc, sub, fsS, white(0.52f * a), canLink ? &artLink : nullptr);
    if (canLink && artLink.clicked) goArtist(t.artist, t.source != "youtube");

    if (h.clicked) {
        if (isCur) app_.togglePause();
        else app_.playFrom(ids, t.id);
    }
}

void Ui::listHeader(float w, const std::string& title, const std::string& sub, const std::vector<const Track*>& list, float& y) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    float sy = ImGui::GetScrollY();
    float x = o.x + kPad;
    float a = calm() ? 1.f : easeOut(static_cast<float>((app_.now() - viewStart_) / 0.4));
    float yy = o.y + y - sy + (1.f - a) * 10.f;
    // у «Моей музыки», плейлистов и фрагментов — плитка-обложка слева, как у альбома; у поиска — без неё
    const bool tile = app_.view != ViewKind::Search;
    const float ts = tile ? (compact() ? 64.f : mobile_ ? 92.f : 128.f) : 0.f;
    const float tfs = compact() ? 24.f : mobile_ ? 28.f : 36.f;  // размер названия
    float tx = x;
    if (tile) {
        Icon ic = app_.view == ViewKind::Library ? Icon::HeartFilled : app_.view == ViewKind::Clips ? Icon::Scissors
                : app_.view == ViewKind::Offline ? Icon::Download : app_.view == ViewKind::ForYou ? Icon::Sparkle
                : app_.view == ViewKind::Stats ? Icon::Chart : app_.view == ViewKind::Artist ? Icon::User : Icon::Note;
        float hue = app_.view == ViewKind::Playlist ? hueOf(title) : pal_.hue;
        ImVec2 t0(x, yy), t1(x + ts, yy + ts);
        softShadow(dl, t0, t1, radius(18.f), 18.f, 0.5f * a);
        dl->AddRectFilled(t0, t1, u32(hsl(hue, 0.55f, 0.34f, a)), radius(18.f));
        dl->PushClipRect(t0, t1, true);
        glow(dl, t0 + ImVec2(ts * 0.38f, ts * 0.36f), ts * 0.55f, u32(hsl(hue + 30.f, 0.8f, 0.62f)), 0.55f * a, 24);
        dl->PopClipRect();
        const std::string coverUrl = app_.view == ViewKind::Artist ? app_.artist.cover : app_.view == ViewKind::Album ? app_.album.cover : std::string();
        const ImTextureID ctex = coverUrl.empty() ? ImTextureID(0) : tex_.cover(coverUrl);
        if (ctex) dl->AddImageRounded(ctex, t0, t1, ImVec2(0, 0), ImVec2(1, 1), white(a), radius(18.f));
        else icon(dl, ic, ImVec2(x + ts / 2, yy + ts / 2), ts * 0.38f, white(0.95f * a));
        tx = x + ts + 24.f;
    }
    const char* kicker = app_.view == ViewKind::Library ? "КОЛЛЕКЦИЯ" : app_.view == ViewKind::Playlist ? "ПЛЕЙЛИСТ"
                       : app_.view == ViewKind::Clips ? "ФРАГМЕНТЫ" : app_.view == ViewKind::Offline ? "БЕЗ ИНТЕРНЕТА"
                       : app_.view == ViewKind::ForYou ? "ПОДБОРКА" : app_.view == ViewKind::Stats ? "ВАША МУЗЫКА"
                       : app_.view == ViewKind::Artist ? "ИСПОЛНИТЕЛЬ" : app_.view == ViewKind::Album ? "АЛЬБОМ" : "ПОИСК";
    float textW = w - kPad - (tx - o.x);
    float ky = tile ? yy + ts / 2 - (mobile_ ? 40.f : 50.f) : yy;
    drawText(dl, F_.bold, 12.f, ImVec2(tx, ky), alpha(pal_.acc, 0.95f * a), kicker);
    drawText(dl, F_.bold, tfs, ImVec2(tx, ky + 20), white(0.98f * a), ellipsize(F_.bold, tfs, title, textW));
    drawText(dl, F_.regular, 14.f, ImVec2(tx, ky + 20 + measure(F_.bold, tfs, "Ag").y + 2), white(0.6f * a), ellipsize(F_.regular, 14.f, sub, textW));
    // кнопки идут потоком слева направо, ширина — по тексту: ничего не налезает друг на друга
    const float bh = 42.f;
    const float textBlock = 20.f + measure(F_.bold, tfs, "Ag").y + 2 + measure(F_.regular, 14.f, "Ag").y;
    const float headH = tile ? ts : textBlock;
    float by = yy + headH + 22, bx = x;
    bool any = false;
    int extraRows = 0;
    auto btn = [&](const char* id, const std::string& label, bool primary, bool active, Icon ic) {
        float bw = pillWidth(label, true);
        if (bx > x && bx + bw > o.x + w - kPad) { bx = x; by += bh + 10.f; ++extraRows; }  // не влезает — на новую строку
        bool r = pill(id, dl, ImVec2(bx, by), ImVec2(bw, bh), label, primary, active, &ic);
        bx += bw + 10.f;
        any = true;
        return r;
    };
    ImGui::PushID("hdr");
    if (!list.empty()) {
        if (btn("##playAll", "Слушать", true, false, Icon::Play)) {
            std::vector<std::string> ids;
            for (auto* t : list) ids.push_back(t->id);
            bool sh = app_.queue.shuffle;
            app_.queue.shuffle = false;
            app_.playFrom(ids, ids.front());
            app_.queue.shuffle = sh;
        }
        if (btn("##shufAll", "Вперемешку", false, app_.queue.shuffle, Icon::Shuffle)) {
            std::vector<std::string> ids;
            for (auto* t : list) ids.push_back(t->id);
            app_.queue.shuffle = true;
            app_.playFrom(ids, ids[static_cast<std::size_t>(std::rand()) % ids.size()]);
        }
        bool anyOnline = false;
        for (auto* t : list) if (t->canOffline() && !(app_.lib.find(t->id) && app_.lib.find(t->id)->off)) { anyOnline = true; break; }
        if (anyOnline && btn("##offAll", "Скачать все", false, false, Icon::Download)) app_.offlineAll(list);
    }
    if (app_.view == ViewKind::Library && btn("##addFiles", "Добавить файлы", false, false, Icon::Files)) app_.addFiles();
    if (app_.view == ViewKind::ForYou && !app_.recsLoading && btn("##recNew", "Подобрать заново", list.empty(), false, Icon::Sparkle))
        app_.loadRecs(true);
    if (app_.view == ViewKind::Offline) {
        auto mine = app_.lib.list([](const Track& t) { return !t.clip; });
        int left = 0;
        for (auto* t : mine) if (t->canOffline() && !t->off && !app_.downloading.count(t->id)) ++left;
        if (left > 0 && btn("##offMine", "Скачать всю мою музыку (" + std::to_string(left) + ")", list.empty(), false, Icon::Download))
            app_.offlineAll(mine);
        if (!list.empty() && btn("##offDrop", "Удалить копии", false, false, Icon::Trash))
            confirm("Удалить все скачанные копии? Песни останутся в «Моей музыке», но без интернета играть не будут.",
                    [this] { app_.dropAllOffline(); });
    }
    if (app_.view == ViewKind::Album && !app_.album.artist.empty() && btn("##toArtist", "К исполнителю", false, false, Icon::User))
        goArtist(app_.album.artist);
    if (app_.view == ViewKind::Artist && !app_.artist.error.empty() && btn("##artSearch", "Искать в других источниках", true, false, Icon::Search)) {
        const std::string n = app_.artist.name;
        app_.search(n);
    }
    if (app_.view == ViewKind::Playlist) {
        if (btn("##delPl", "Удалить плейлист", false, false, Icon::Trash)) {
            std::string id = app_.viewPlaylist, name = title;
            confirm("Удалить плейлист «" + name + "»? Песни останутся в «Моей музыке».", [this, id] {
                app_.lib.removePlaylist(id);
                app_.touch();
                app_.openView(ViewKind::Library);
            });
        }
    }
    ImGui::PopID();
    y += headH + (any ? 22.f + bh + 26.f + static_cast<float>(extraRows) * (bh + 10.f) : 30.f);
}

// Сетка альбомов (или синглов): по умолчанию один ряд, «Показать все» раскрывает остальные.
void Ui::albumShelf(const char* id, const char* heading, const std::vector<YtmAlbum>& list, bool& open, float w, float& y) {
    if (list.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    const float gap = 16.f, avail = w - kPad * 2.f;
    const int cols = std::max(2, static_cast<int>((avail + gap) / (160.f + gap)));
    const float cw = (avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float cardH = cw + 46.f;
    const int total = static_cast<int>(list.size());
    const int shown = open ? total : std::min(total, cols);
    ImGui::PushID(id);
    const float hy = o.y + y - ImGui::GetScrollY();
    drawText(dl, F_.bold, 20.f, ImVec2(o.x + kPad, hy), white(0.97f), heading);
    const ImVec2 hs = measure(F_.bold, 20.f, heading);
    drawText(dl, F_.regular, 14.f, ImVec2(o.x + kPad + hs.x + 10.f, hy + hs.y - 17.f), white(0.45f), std::to_string(total));
    if (total > cols) {
        const char* lab = open ? "Свернуть" : "Показать все";
        const float bw = pillWidth(lab, false);
        if (pill("##more", dl, ImVec2(o.x + w - kPad - bw, hy - 7.f), ImVec2(bw, 34.f), lab)) open = !open;
    }
    y += 44.f;
    for (int i = 0; i < shown; ++i) {
        const int r = i / cols, c = i % cols;
        const ImVec2 p(o.x + kPad + static_cast<float>(c) * (cw + gap),
                       o.y + y - ImGui::GetScrollY() + static_cast<float>(r) * (cardH + 8.f));
        if (!ImGui::IsRectVisible(p, p + ImVec2(cw, cardH))) continue;  // обложки грузятся только у видимых карточек
        const YtmAlbum& al = list[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        Hit h = hit("##al", p, ImVec2(cw, cardH));
        const float k = hv(h.id, h.hovered);
        if (k > 0.01f) dl->AddRectFilled(p - ImVec2(6.f, 6.f), p + ImVec2(cw + 6.f, cardH + 2.f), white(0.06f * k), radius(14.f));
        Track tr;
        tr.title = al.title;
        tr.cover = al.cover;
        drawCover(dl, tr, p, cw, radius(12.f));
        drawText(dl, F_.bold, 13.5f, ImVec2(p.x, p.y + cw + 8.f), white(0.95f), ellipsize(F_.bold, 13.5f, al.title, cw));
        drawText(dl, F_.regular, 12.f, ImVec2(p.x, p.y + cw + 27.f), white(0.5f), ellipsize(F_.regular, 12.f, al.sub, cw));
        if (h.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (h.clicked) app_.openAlbum(al);
        ImGui::PopID();
    }
    const int rowsN = (shown + cols - 1) / cols;
    y += static_cast<float>(rowsN) * (cardH + 8.f) + 14.f;
    ImGui::PopID();
}

void Ui::viewList(ImVec2 size) {
    auto list = app_.currentList();
    float w = contentWidth(size.x);  // не на весь монитор и не под Тето
    ImVec2 o = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = 8.f;
    std::string title, sub, empty;
    bool inPl = false;
    switch (app_.view) {
        case ViewKind::Search:
            title = app_.searchQuery.empty() ? "Поиск" : "Результаты";
            sub = app_.searching ? "Ищу…" : (app_.searchQuery.empty() ? "Введите название песни или исполнителя" : "По запросу «" + app_.searchQuery + "»: " + std::to_string(list.size()));
            empty = app_.searching ? "" : (app_.searchQuery.empty() ? "" : "Ничего не нашлось. Попробуйте другое написание или включите другие источники в настройках.");
            break;
        case ViewKind::Library:
            title = "Моя музыка";
            sub = songsCount(list.size());
            empty = "Здесь пока пусто. Найдите песню и нажмите на сердечко, либо добавьте свои файлы.";
            break;
        case ViewKind::Clips:
            title = "Фрагменты";
            sub = "Куски песен, которые вы сохранили кнопкой с ножницами";
            empty = "Фрагментов нет. Откройте песню, нажмите на ножницы и выберите кусок.";
            break;
        case ViewKind::ForYou:
            app_.loadRecs(false);
            title = "Для вас";
            sub = app_.recsLoading ? "Подбираю похожую музыку…"
                : list.empty() ? "Похожие песни на то, что вы слушаете"
                : (app_.recsBasis.empty() ? std::string("Похоже на вашу музыку") : app_.recsBasis) + " · " + songsCount(list.size());
            empty = app_.recsLoading ? "" : app_.online
                ? "Добавьте несколько песен в «Мою музыку» или послушайте что-нибудь — и здесь появится похожая музыка."
                : "Для подбора нужен интернет.";
            break;
        case ViewKind::Offline: {
            title = "Офлайн";
            char mb[32];
            std::snprintf(mb, sizeof mb, "%.0f МБ", static_cast<double>(app_.offlineBytes()) / (1024.0 * 1024.0));
            sub = songsCount(list.size()) + " · " + mb + " на диске";
            if (!app_.downloading.empty()) sub += " · скачивается " + std::to_string(app_.downloading.size());
            empty = "Скачанные песни играют без интернета. Нажмите стрелку ↓ у песни или «Скачать всю мою музыку».";
            break;
        }
        case ViewKind::Playlist: {
            inPl = true;
            const Playlist* p = nullptr;
            for (auto& x : app_.lib.playlists) if (x.id == app_.viewPlaylist) p = &x;
            if (!p) { app_.openView(ViewKind::Library); return; }
            title = p->name;
            sub = songsCount(list.size());
            empty = "Плейлист пуст. Нажмите «+» у любой песни, чтобы добавить её сюда.";
            break;
        }
        case ViewKind::Artist: {
            const auto& A = app_.artist;
            title = A.name;
            sub = A.loading ? std::string("Загружаю…") : A.error.empty() ? songsCount(list.size()) : std::string();
            empty = A.loading ? "" : !A.error.empty() ? A.error : "У исполнителя нет песен на YouTube Music.";
            break;
        }
        case ViewKind::Album: {
            const auto& B = app_.album;
            title = B.title;
            if (B.loading) sub = "Загружаю…";
            else {
                if (!B.artist.empty()) sub = B.artist + (B.year.empty() ? "" : " · " + B.year) + " · ";
                sub += songsCount(list.size());
            }
            empty = B.loading ? "" : !B.error.empty() ? B.error : "В альбоме нет доступных песен.";
            break;
        }
        default: break;
    }
    listHeader(w, title, sub, list, y);

    if (app_.view == ViewKind::Search && (!app_.sourceStatus.empty() || !app_.linkTracks.empty())) {
        // состояние источников: своя строка под кнопками (раньше плашки налезали на «Слушать»)
        float x = o.x + kPad, sy = o.y + y - ImGui::GetScrollY();
        drawText(dl, F_.bold, 11.f, ImVec2(x, sy + 6), white(0.4f), "ИСТОЧНИКИ");
        x += measure(F_.bold, 11.f, "ИСТОЧНИКИ").x + 14.f;
        for (auto& s : app_.sourceStatus) {
            std::string label = s.name + (s.error ? " — не ответил" : " · " + std::to_string(s.count));
            ImVec2 ts = measure(F_.bold, 12.f, label);
            float cw = ts.x + 30.f;
            ImU32 bg = s.error ? IM_COL32(255, 120, 100, 36) : white(0.06f);
            dl->AddRectFilled(ImVec2(x, sy), ImVec2(x + cw, sy + 26), bg, 13.f);
            dl->AddCircleFilled(ImVec2(x + 12, sy + 13), 3.f, s.error ? IM_COL32(255, 120, 100, 255) : (s.count > 0 ? pal_.acc : white(0.3f)), 10);
            drawText(dl, F_.bold, 12.f, ImVec2(x + 21, sy + 13 - ts.y / 2), s.error ? IM_COL32(255, 160, 145, 255) : white(0.7f), label);
            ImVec2 mp = ImGui::GetIO().MousePos;
            if (s.error && mp.x > x && mp.x < x + cw && mp.y > sy && mp.y < sy + 26) tip(s.message.c_str());
            x += cw + 8;
        }
        if (!app_.linkTracks.empty()) {
            if (pill("##saveSet", dl, ImVec2(x + 8, sy - 4), ImVec2(pillWidth("Сохранить как плейлист", false), 34), "Сохранить как плейлист")) {
                Playlist& pl = app_.lib.createPlaylist(app_.linkName);
                for (auto& t : app_.linkTracks) { app_.lib.add(t); pl.ids.push_back(t.id); }
                app_.touch();
                app_.toast("Плейлист «" + app_.linkName + "» сохранён");
            }
        }
        y += 46.f;
    }
    if (app_.view == ViewKind::Artist && !app_.artist.loading && app_.artist.error.empty()) {
        albumShelf("alb", "Альбомы", app_.artist.albums, albumsOpen_, w, y);
        albumShelf("sin", "Синглы и EP", app_.artist.singles, singlesOpen_, w, y);
        if (!list.empty()) {
            drawText(dl, F_.bold, 20.f, ImVec2(o.x + kPad, o.y + y - ImGui::GetScrollY()), white(0.97f), "Песни");
            y += 44.f;
        }
    }
    ImGui::SetCursorPos(ImVec2(0, y));

    if (app_.view == ViewKind::Search || app_.view == ViewKind::Artist || app_.view == ViewKind::Album) {
        if ((app_.view == ViewKind::Search && app_.searching) || (app_.view == ViewKind::Artist && app_.artist.loading) ||
            (app_.view == ViewKind::Album && app_.album.loading)) {
            // бегущая точка вместо «колёсика»
            ImVec2 c(o.x + kPad + 14, o.y + y - ImGui::GetScrollY() + 26);
            for (int i = 0; i < 3; ++i) {
                float ph = std::sin(time_ * 6.f - static_cast<float>(i) * 0.8f) * 0.5f + 0.5f;
                dl->AddCircleFilled(ImVec2(c.x + static_cast<float>(i) * 16.f, c.y), 4.f + 2.f * ph, alpha(pal_.acc, 0.4f + 0.6f * ph), 12);
            }
            ImGui::Dummy(ImVec2(1, 90));
        }
    }

    if (list.empty()) {
        if (!empty.empty()) {
            float yy = o.y + y - ImGui::GetScrollY() + 20.f;
            drawText(dl, F_.regular, 15.f, ImVec2(o.x + kPad, yy), white(0.5f), empty, w - kPad * 2);
        }
        ImGui::Dummy(ImVec2(1, 120));
    } else {
        rows(list, w, inPl, "list");
    }
    ImGui::Dummy(ImVec2(1, bottomPad()));
}

void Ui::viewHome(ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    float w = contentWidth(size.x), y = 8.f;
    float a = easeOut(static_cast<float>((app_.now() - viewStart_) / 0.5));

    // баннер с приветствием по времени суток
    {
        std::time_t now = std::time(nullptr);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        int hr = lt.tm_hour;
        const char* hello = hr < 5 ? "Доброй ночи" : hr < 12 ? "Доброе утро" : hr < 18 ? "Добрый день" : "Добрый вечер";
        const float heroH = compact() ? 112.f : 168.f;
        ImVec2 b0(o.x + kPad, o.y + y - ImGui::GetScrollY()), b1(o.x + w - kPad, b0.y + heroH);
        dl->AddRectFilled(b0, b1, alpha(pal_.acc, 0.16f * a), radius(24.f));
        dl->PushClipRect(b0, b1, true);
        float drift = calm() ? 0.f : std::sin(time_ * 0.4f) * 20.f;
        // радиусы подобраны так, чтобы свечение гасло раньше скруглённых углов (клип — прямоугольный)
        float mid = (b0.y + b1.y) / 2;
        glow(dl, ImVec2(b1.x - 190.f + drift, mid), 165.f, pal_.acc2, 0.5f * a, 28);
        glow(dl, ImVec2(b0.x + 150.f - drift, mid + 20.f), 130.f, pal_.acc, 0.35f * a, 28);
        dl->PopClipRect();
        dl->AddRect(b0, b1, alpha(pal_.acc, 0.25f * a), radius(24.f));
        const float padX = (b1.x - b0.x) < 340.f ? 20.f : 32.f;  // hero fit: на узком экране поля поменьше
        float tx = b0.x + padX, lift = (1.f - a) * 12.f;
        // заголовок ужимается, если не помещается в карточку (узкие телефоны)
        const float avail = b1.x - tx - 16.f;
        float tfs = compact() ? 30.f : 40.f;
        ImVec2 tm = measure(F_.bold, tfs, "Что послушаем?");
        if (tm.x > avail) tfs = std::max(20.f, tfs * avail / tm.x);
        const float k = compact() ? 0.6f : 1.f;
        drawText(dl, F_.bold, 13.f, ImVec2(tx, b0.y + 30.f * k + lift), white(0.75f * a),
                 !app_.acct.prof.name.empty() ? std::string(hello) + ", " + app_.acct.name() : std::string(hello));
        drawText(dl, F_.bold, tfs, ImVec2(tx, b0.y + 50.f * k + lift), white(0.99f * a), "Что послушаем?");
        drawText(dl, F_.regular, 15.f, ImVec2(tx, b0.y + 50.f * k + 8.f + measure(F_.bold, tfs, "Ag").y + lift), white(0.72f * a),
                 ellipsize(F_.regular, 15.f, "Ищите по названию — сразу в Audius, SoundCloud, Internet Archive и YouTube Music.", b1.x - tx - 16.f));
        y += heroH + (compact() ? 14.f : 22.f);
    }

    // быстрые действия — три карточки в ряд (на узком окне — столбиком)
    ImGui::PushID("home");
    float x = o.x + kPad, by = o.y + y - ImGui::GetScrollY();
    float inner = w - kPad * 2, gap = 14.f;
    bool narrow = inner < 640.f;
    float cw = narrow ? inner : std::min(330.f, (inner - gap * 2) / 3.f), ch = 78.f;
    auto pos = [&](int i) { return narrow ? ImVec2(x, by + static_cast<float>(i) * (ch + 10.f)) : ImVec2(x + static_cast<float>(i) * (cw + gap), by); };
    if (card("##hRnd", dl, pos(0), ImVec2(cw, ch), Icon::Dice, "Случайная песня", "Удивите себя", true)) app_.randomSong();
    if (card("##hImp", dl, pos(1), ImVec2(cw, ch), Icon::Import, "Перенести плейлист", "Spotify, YouTube, SoundCloud", false)) { importLink_.clear(); openModal(Modal::Import); }
    if (card("##hFiles", dl, pos(2), ImVec2(cw, ch), Icon::Files, "Свои файлы", "MP3, FLAC, WAV и другие", false)) app_.addFiles();
    ImGui::PopID();
    y += narrow ? (ch + 10.f) * 3 + 26.f : ch + 40.f;
    ImGui::SetCursorPos(ImVec2(0, y));

    auto section = [&](const char* title, const char* hint) {
        float yy = ImGui::GetCursorScreenPos().y;
        drawText(dl, F_.bold, 22.f, ImVec2(o.x + kPad, yy), white(0.97f * a), title);
        if (hint) {
            ImVec2 hs = measure(F_.regular, 13.f, hint);
            drawText(dl, F_.regular, 13.f, ImVec2(o.x + w - kPad - hs.x, yy + 8), white(0.4f * a), hint);
        }
        dl->AddLine(ImVec2(o.x + kPad, yy + 36), ImVec2(o.x + w - kPad, yy + 36), white(0.06f), 1.f);
        ImGui::SetCursorScreenPos(ImVec2(o.x, yy + 46.f));
    };

    // недавнее
    auto lib = app_.lib.list([](const Track& t) { return !t.clip; });
    if (lib.size() > 8) lib.resize(8);
    if (!lib.empty()) {
        section("Недавно добавленные", "из «Моей музыки»");
        rows(lib, w, false, "recent");
        ImGui::Dummy(ImVec2(1, 22));

        // подборка похожего — первые песни, остальное во вкладке «Для вас»
        app_.loadRecs(false);
        if (!app_.recs.empty()) {
            std::vector<const Track*> rc;
            for (std::size_t i = 0; i < app_.recs.size() && i < 6; ++i) rc.push_back(&app_.recs[i]);
            section("Подобрано для вас", "похоже на вашу музыку");
            rows(rc, w, false, "recs");
            ImGui::Dummy(ImVec2(1, 22));
        } else if (app_.recsLoading) {
            float yy = ImGui::GetCursorScreenPos().y;
            drawText(dl, F_.regular, 14.f, ImVec2(o.x + kPad, yy), white(0.45f), "Подбираю похожую музыку…");
            ImGui::Dummy(ImVec2(1, 40));
        }
    }

    // популярное на Audius
    app_.loadTrending();
    if (!app_.trending.empty()) {
        std::vector<const Track*> tr;
        for (std::size_t i = 0; i < app_.trending.size() && i < 12; ++i) tr.push_back(&app_.trending[i]);
        section("Популярное на Audius", nullptr);
        rows(tr, w, false, "trend");
    } else if (app_.trendingLoading) {
        float yy = ImGui::GetCursorScreenPos().y;
        drawText(dl, F_.regular, 14.f, ImVec2(o.x + kPad, yy), white(0.45f), "Загружаю популярное…");
        ImGui::Dummy(ImVec2(1, 40));
    }
    ImGui::Dummy(ImVec2(1, bottomPad()));
}

namespace {
// «3 ч 12 мин», «45 мин», «меньше минуты»
std::string hoursText(double sec) {
    long long m = static_cast<long long>(sec / 60.0);
    if (m < 1) return "меньше минуты";
    if (m < 60) return std::to_string(m) + " мин";
    return std::to_string(m / 60) + " ч " + std::to_string(m % 60) + " мин";
}
std::string timesText(int n) {
    int d10 = n % 10, d100 = n % 100;
    const char* w = (d10 >= 2 && d10 <= 4 && (d100 < 12 || d100 > 14)) ? " раза" : " раз";
    return std::to_string(n) + w;
}
// день недели и дата для дня back дней назад
std::string dayLabel(int back, bool full) {
    static const char* const kDays[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
    std::time_t now = std::time(nullptr) - static_cast<std::time_t>(back) * 24 * 3600;
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    if (!full) return back == 0 ? "Сегодня" : kDays[lt.tm_wday];
    char b[32];
    std::snprintf(b, sizeof b, "%s, %02d.%02d", kDays[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1);
    return b;
}
}  // namespace

// Сколько и что вы слушаете: крупные цифры, неделя по дням, самые слушаемые песни и исполнители.
void Ui::drawFriendAvatar(ImDrawList* dl, ImVec2 c, float r, const FriendContact& p) {
    drawAvatarOf(dl, c, r, 0, p.name, p.color, p.frame);
}

// Друзья: моя ссылка, вставка чужой ссылки, рекомендации, список.
void Ui::viewFriends(ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    const float w = contentWidth(size.x), x = o.x + kPad, inner = w - kPad * 2;
    float y = 8.f;
    auto sy = [&] { return o.y + y - ImGui::GetScrollY(); };
    const float a = calm() ? 1.f : easeOut(static_cast<float>((app_.now() - viewStart_) / 0.5));
    const float tfs = mobile_ ? 28.f : 36.f;
    drawText(dl, F_.bold, 12.f, ImVec2(x, sy()), alpha(pal_.acc, 0.95f * a), "ДРУЗЬЯ");
    drawText(dl, F_.bold, tfs, ImVec2(x, sy() + 20.f), white(0.98f * a), "Друзья");
    y += 20.f + measure(F_.bold, tfs, "Ag").y + 8.f;
    drawText(dl, F_.regular, 14.f, ImVec2(x, sy()), white(0.6f * a), "Приглашайте друзей ссылкой и рекомендуйте друг другу песни", inner);
    y += 44.f;
    const bool narrow = inner < 560.f;

    // моя ссылка
    {
        const float ch = narrow ? 168.f : 128.f;
        const ImVec2 c0(x, sy()), c1(x + inner, sy() + ch);
        dl->AddRectFilled(c0, c1, white(0.05f * a), radius(18.f));
        dl->AddRect(c0, c1, white(0.08f * a), radius(18.f));
        drawText(dl, F_.bold, 12.f, ImVec2(x + 20.f, c0.y + 16.f), white(0.5f * a), "ВАША ССЫЛКА-ПРИГЛАШЕНИЕ");
        drawText(dl, F_.regular, 14.f, ImVec2(x + 20.f, c0.y + 40.f), white(0.75f * a),
                 "Отправьте её другу в любой мессенджер. Он вставит её сюда, и вы появитесь в списках друг у друга.", inner - 40.f);
        const float bw = pillWidth("Скопировать ссылку", false);
        if (pill("##frCopy", dl, ImVec2(x + 20.f, c1.y - 58.f), ImVec2(bw, 40), "Скопировать ссылку", true)) {
            if (app_.platform.copyText) app_.platform.copyText(app_.myFriendLink());
            app_.toast("Ссылка скопирована: отправьте её другу");
        }
        if (app_.acct.prof.name.empty()) {
            const float pw = pillWidth("Указать ник", false);
            if (pill("##frNick", dl, ImVec2(x + 20.f + bw + 10.f, c1.y - 58.f), ImVec2(pw, 40), "Указать ник")) openModal(Modal::Profile);
        }
        y += ch + 20.f;
    }

    // вставить ссылку от друга
    drawText(dl, F_.bold, 12.f, ImVec2(x, sy()), white(0.5f * a), "ВСТАВЬТЕ ССЫЛКУ ОТ ДРУГА");
    y += 22.f;
    {
        const float bw = pillWidth("Добавить", false);
        const bool enter = textField("##frLink", ImVec2(x, sy()), inner - bw - 12.f, frLink_, "Ссылка от друга");
        const bool click = pill("##frAdd", dl, ImVec2(x + inner - bw, sy()), ImVec2(bw, 40), "Добавить", true);
        if ((enter || click) && !trim(frLink_).empty()) {
            if (!app_.handleShare(frLink_)) app_.toast("В этом тексте нет ссылки Mixtape");
            frLink_.clear();
        }
        y += 62.f;
    }

    struct Btn { const char* id; const char* label; bool primary; };
    // Кнопки строки: справа (широкий экран) или второй строкой под текстом (узкий). Возвращает номер нажатой или -1.
    auto rowButtons = [&](float top, std::initializer_list<Btn> bs) -> int {
        int clicked = -1, i = 0;
        float total = 0;
        for (const Btn& b : bs) total += pillWidth(b.label, false) + 8.f;
        float bx = narrow ? x + 64.f : x + inner + 12.f - total + 8.f;
        const float by = narrow ? top + 60.f : top + 11.f;
        for (const Btn& b : bs) {
            const float bw = pillWidth(b.label, false);
            if (pill(b.id, dl, ImVec2(bx, by), ImVec2(bw, 38), b.label, b.primary)) clicked = i;
            bx += bw + 8.f;
            ++i;
        }
        return clicked;
    };
    auto textRoom = [&](std::initializer_list<Btn> bs) {
        if (narrow) return inner - 64.f;
        float total = 0;
        for (const Btn& b : bs) total += pillWidth(b.label, false) + 8.f;
        return std::max(80.f, inner - 64.f - total - 12.f);
    };
    const float rowH = narrow ? 112.f : 66.f;
    auto section = [&](const char* title, std::size_t n) {
        drawText(dl, F_.bold, 20.f, ImVec2(x, sy()), white(0.97f * a), title);
        if (n) drawText(dl, F_.regular, 14.f, ImVec2(x + measure(F_.bold, 20.f, title).x + 10.f, sy() + 4.f), white(0.45f), std::to_string(n));
        y += 40.f;
    };
    auto rowBg = [&](float top) {
        dl->AddRectFilled(ImVec2(x - 12.f, top), ImVec2(x + inner + 12.f, top + rowH - 6.f), white(0.04f * a), radius(14.f));
    };

    // рекомендации
    if (!app_.inbox.empty()) {
        section("Вам рекомендуют", app_.inbox.size());
        ImGui::PushID("recs");
        for (std::size_t i = 0; i < app_.inbox.size(); ++i) {
            const Recommendation r = app_.inbox[i];
            ImGui::PushID(static_cast<int>(i));
            const float top = sy();
            rowBg(top);
            drawCover(dl, r.track, ImVec2(x, top + 6.f), 50.f, radius(10.f));
            std::string sub = r.fromName + " рекомендует" + (r.note.empty() ? std::string() : " · «" + r.note + "»");
            const std::initializer_list<Btn> bs = {{"##rcPlay", "Слушать", true}, {"##rcSave", "В мою музыку", false}, {"##rcHide", "Скрыть", false}};
            twoLines(dl, x + 64.f, top + 31.f, textRoom(bs), r.track.title, 15.f, white(0.96f * a), sub, 12.5f, alpha(pal_.acc, 0.9f * a));
            const int c = rowButtons(top, bs);
            ImGui::PopID();
            y += rowH;
            if (c == 0) { app_.recPlay(r); break; }
            if (c == 1) { app_.recSave(r); break; }
            if (c == 2) { app_.recDismiss(r.key); break; }
        }
        ImGui::PopID();
        y += 10.f;
    }

    // друзья
    section("Друзья", app_.friendList.size());
    if (app_.friendList.empty()) {
        drawText(dl, F_.regular, 14.f, ImVec2(x, sy()), white(0.5f), "Пока никого. Отправьте другу свою ссылку или вставьте его ссылку выше.", inner);
        y += 44.f;
    }
    {
        ImGui::PushID("fr");
        const bool canSend = app_.cur && app_.cur->source != "local" && !app_.cur->clip;
        for (std::size_t i = 0; i < app_.friendList.size(); ++i) {
            const FriendContact p = app_.friendList[i];
            ImGui::PushID(static_cast<int>(i));
            const float top = sy();
            rowBg(top);
            drawFriendAvatar(dl, ImVec2(x + 26.f, top + 30.f), 22.f, p);
            const std::initializer_list<Btn> bsSend = {{"##frSend", "Отправить песню", false}, {"##frDel", "Убрать", false}};
            const std::initializer_list<Btn> bsDel = {{"##frDel", "Убрать", false}};
            const auto& bs = canSend ? bsSend : bsDel;
            twoLines(dl, x + 64.f, top + 30.f, textRoom(bs), p.name, 15.f, white(0.96f * a), p.status.empty() ? std::string("друг") : p.status, 12.5f, white(0.55f * a));
            const int c = rowButtons(top, bs);
            const int del = canSend ? 1 : 0;
            if (canSend && c == 0) {
                app_.shareSong(*app_.cur);
            } else if (c == del) {
                const std::string id = p.id, name = p.name;
                confirm("Убрать «" + name + "» из друзей?", [this, id] { app_.friendRemove(id); });
            }
            ImGui::PopID();
            y += rowH;
        }
        ImGui::PopID();
        if (canSend && !app_.friendList.empty()) {
            drawText(dl, F_.regular, 12.5f, ImVec2(x, sy()), white(0.45f), "«Отправить песню» копирует ссылку на то, что играет сейчас: вставьте её другу в чат. Для других песен: «+» у песни, «Порекомендовать другу».", inner);
            y += 44.f;
        }
    }
    ImGui::SetCursorPos(ImVec2(0, y));
    ImGui::Dummy(ImVec2(1, 130));
}

void Ui::viewStats(ImVec2 size) {
    const ListenStats& st = app_.stats;
    auto top = app_.topTracks(10);
    float w = contentWidth(size.x), y = 8.f;
    ImVec2 o = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float a = calm() ? 1.f : easeOut(static_cast<float>((app_.now() - viewStart_) / 0.5));
    listHeader(w, "Статистика", "Сколько и что вы слушаете в Mixtape", top, y);
    float x = o.x + kPad, inner = w - kPad * 2;
    auto sy = [&] { return o.y + y - ImGui::GetScrollY(); };

    if (st.totalSec() < 60) {
        drawText(dl, F_.regular, 15.f, ImVec2(x, sy() + 6), white(0.55f),
                 "Пока мало данных: послушайте музыку — время и любимые песни появятся здесь сами.", inner);
        ImGui::SetCursorPos(ImVec2(0, y + 60));
        ImGui::Dummy(ImVec2(1, bottomPad()));
        return;
    }

    // ---- крупные цифры ----
    double week = 0;
    for (int d = 0; d < 7; ++d) week += st.daySec(localDay(d));
    struct Tile { const char* label; std::string value; };
    const Tile tiles[] = {{"ВСЕГО", hoursText(st.totalSec())}, {"СЕГОДНЯ", hoursText(st.daySec(localDay()))},
                          {"ЗА 7 ДНЕЙ", hoursText(week)}, {"ПРОСЛУШИВАНИЙ", std::to_string(st.playCount())}};
    int perRow = inner >= 640.f ? 4 : 2;
    float gap = 12.f, tw = (inner - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow), th = 92.f;
    for (int i = 0; i < 4; ++i) {
        ImVec2 p0(x + static_cast<float>(i % perRow) * (tw + gap), sy() + static_cast<float>(i / perRow) * (th + gap));
        ImVec2 p1 = p0 + ImVec2(tw, th);
        dl->AddRectFilled(p0, p1, i == 0 ? alpha(pal_.acc, 0.14f * a) : white(0.045f * a), radius(18.f));
        dl->AddRect(p0, p1, i == 0 ? alpha(pal_.acc, 0.35f * a) : white(0.08f * a), radius(18.f));
        drawText(dl, F_.bold, 11.5f, p0 + ImVec2(18, 18), white(0.5f * a), tiles[i].label);
        drawText(dl, F_.bold, 26.f, p0 + ImVec2(18, 40), white(0.97f * a), ellipsize(F_.bold, 26.f, tiles[i].value, tw - 30));
    }
    y += static_cast<float>((4 + perRow - 1) / perRow) * (th + gap) + 18.f;

    // ---- неделя по дням: одна серия, столбики в цвете акцента, подписи — цветом текста ----
    drawText(dl, F_.bold, 20.f, ImVec2(x, sy()), white(0.97f * a), "Последние 7 дней");
    y += 40.f;
    double secs[7], mx = 60.0;
    for (int d = 0; d < 7; ++d) { secs[d] = st.daySec(localDay(6 - d)); mx = std::max(mx, secs[d]); }
    const float chartH = 130.f, labelH = 24.f, colW = inner / 7.f, barW = std::min(46.f, colW - 14.f);
    float base = sy() + chartH;
    dl->AddLine(ImVec2(x, base), ImVec2(x + inner, base), white(0.12f), 1.f);
    int maxIdx = 0;
    for (int d = 1; d < 7; ++d) if (secs[d] > secs[maxIdx]) maxIdx = d;
    ImVec2 mp = ImGui::GetIO().MousePos;
    for (int d = 0; d < 7; ++d) {
        float cx = x + colW * (static_cast<float>(d) + 0.5f);
        float h = static_cast<float>(secs[d] / mx) * (chartH - 26.f) * a;
        bool hov = mp.x >= cx - colW / 2 && mp.x < cx + colW / 2 && mp.y >= base - chartH && mp.y <= base + labelH;
        if (secs[d] > 0) {
            if (hov) dl->AddRectFilled(ImVec2(cx - colW / 2 + 3, base - chartH), ImVec2(cx + colW / 2 - 3, base), white(0.04f), radius(10.f));
            dl->AddRectFilled(ImVec2(cx - barW / 2, base - std::max(4.f, h)), ImVec2(cx + barW / 2, base - 1.f),
                              hov ? mix(pal_.acc, white(1.f), 0.2f) : pal_.acc, 4.f, ImDrawFlags_RoundCornersTop);
        }
        // подписи значений только у самого большого дня и у сегодняшнего — не на каждом столбике
        if (secs[d] > 0 && (d == maxIdx || d == 6)) {
            std::string v = hoursText(secs[d]);
            ImVec2 vs = measure(F_.bold, 12.f, v);
            drawText(dl, F_.bold, 12.f, ImVec2(cx - vs.x / 2, base - std::max(4.f, h) - vs.y - 4), white(0.85f), v);
        }
        std::string dlbl = dayLabel(6 - d, false);
        ImVec2 ls = measure(F_.regular, 12.5f, dlbl);
        drawText(dl, d == 6 ? F_.bold : F_.regular, 12.5f, ImVec2(cx - ls.x / 2, base + 6), d == 6 ? white(0.9f) : white(0.5f), dlbl);
        if (hov) { hoverTip_ = dayLabel(6 - d, true) + " — " + hoursText(secs[d]); tip(hoverTip_.c_str()); }
    }
    y += chartH + labelH + 30.f;

    // ---- чаще всего ----
    drawText(dl, F_.bold, 20.f, ImVec2(x, sy()), white(0.97f * a), "Чаще всего");
    y += 44.f;
    std::vector<std::string> ids;
    for (auto* t : top) ids.push_back(t->id);
    auto stTop = st.topTracks(10);
    ImGui::PushID("stTop");
    for (std::size_t i = 0; i < top.size() && i < stTop.size(); ++i) {
        const Track& t = *top[i];
        ImVec2 p0(x - 12, sy()), p1(x + inner + 12, sy() + 60);
        ImGui::PushID(static_cast<int>(i));
        Hit h = hit("##st", p0, p1 - p0);
        ImGui::PopID();
        bool isCur = app_.cur && app_.cur->id == t.id;
        listItem(dl, h, p0, p1, isCur, radius(14.f), false);
        std::string rank = std::to_string(i + 1);
        ImVec2 rs = measure(F_.bold, 18.f, rank);
        drawText(dl, F_.bold, 18.f, ImVec2(x + 12 - rs.x / 2, p0.y + 30 - rs.y / 2), i < 3 ? pal_.acc : white(0.45f), rank);
        drawCover(dl, t, ImVec2(x + 34, p0.y + 8), 44.f, radius(9.f));
        std::string meta = timesText(stTop[i]->plays) + " · " + hoursText(stTop[i]->sec);
        ImVec2 ms = measure(F_.regular, 13.f, meta);
        drawText(dl, F_.regular, 13.f, ImVec2(x + inner - ms.x, p0.y + 30 - ms.y / 2), white(0.6f), meta);
        SubLink stLink{"##stArt", t.artist};
        const bool stCan = !t.artist.empty() && t.source != "local";
        ImGui::PushID(static_cast<int>(i));
        twoLines(dl, x + 92, p0.y + 30, inner - 92 - ms.x - 24, t.title, 15.f, isCur ? pal_.acc : white(0.96f),
                 t.artist, 13.f, white(0.52f), stCan ? &stLink : nullptr);
        ImGui::PopID();
        if (stCan && stLink.clicked) goArtist(t.artist, t.source != "youtube");
        if (h.clicked) {
            if (isCur) app_.togglePause();
            else app_.playFrom(ids, t.id);
        }
        y += 64.f;
    }
    ImGui::PopID();
    y += 18.f;

    // ---- любимые исполнители: доля времени — тонкая полоса в цвете акцента ----
    auto artists = st.topArtists(6);
    if (!artists.empty()) {
        drawText(dl, F_.bold, 20.f, ImVec2(x, sy()), white(0.97f * a), "Любимые исполнители");
        y += 44.f;
        double amax = std::max(1.0, artists.front().sec);
        for (auto& ar : artists) {
            float ly = sy();
            ImGui::PushID(ar.name.c_str());
            Hit ah = hit("##arRow", ImVec2(x - 8.f, ly - 2.f), ImVec2(inner + 16.f, 32.f));
            ImGui::PopID();
            if (ah.hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                dl->AddRectFilled(ImVec2(x - 8.f, ly - 2.f), ImVec2(x + inner + 8.f, ly + 30.f), white(0.05f), radius(10.f));
                tip("Песни и альбомы исполнителя");
            }
            if (ah.clicked) goArtist(ar.name, true);
            std::string v = hoursText(ar.sec);
            ImVec2 vs = measure(F_.regular, 13.f, v);
            float nameW = std::min(260.f, inner * 0.38f);
            drawText(dl, F_.bold, 15.f, ImVec2(x, ly + 14 - measure(F_.bold, 15.f, "Ag").y / 2), white(0.92f),
                     ellipsize(F_.bold, 15.f, ar.name, nameW - 12));
            float bx0 = x + nameW, bx1 = x + inner - vs.x - 16;
            dl->AddRectFilled(ImVec2(bx0, ly + 11), ImVec2(bx1, ly + 17), white(0.07f), 3.f);
            float frac = static_cast<float>(ar.sec / amax) * a;
            dl->AddRectFilled(ImVec2(bx0, ly + 11), ImVec2(bx0 + std::max(6.f, (bx1 - bx0) * frac), ly + 17), pal_.acc, 3.f);
            drawText(dl, F_.regular, 13.f, ImVec2(x + inner - vs.x, ly + 14 - vs.y / 2), white(0.6f), v);
            y += 36.f;
        }
    }
    ImGui::SetCursorPos(ImVec2(0, y));
    ImGui::Dummy(ImVec2(1, bottomPad()));
}

}  // namespace mx::ui
