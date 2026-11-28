// Проигрыватель на весь экран: виниловая вертушка, текст в такт музыке, редактор фрагментов, полоса и кнопки.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "text.hpp"
#include "ui.hpp"
#include "imgui_internal.h"  // BringWindowToDisplayFront; после ui.hpp, чтобы были операторы ImVec2

namespace mx::ui {

namespace {
constexpr float kPi = 3.14159265f;
ImVec2 rotAround(ImVec2 o, ImVec2 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return ImVec2(o.x + v.x * c - v.y * s, o.y + v.x * s + v.y * c);
}
}  // namespace

// ---------- полоса и кнопки ----------

void Ui::drawProgress(ImVec2 pos, float w, bool big) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    double len = app_.visibleLength();
    double posSec = dragSeek_ ? dragFrac_ * len : app_.position();
    float frac = len > 0 ? static_cast<float>(std::min(1.0, std::max(0.0, posSec / len))) : 0.f;
    float tw = big ? 52.f : 40.f, fs = big ? 13.f : 12.f;
    std::string a = fmtTime(posSec), b = len > 0 ? fmtTime(len) : "--:--";
    drawText(dl, F_.regular, fs, ImVec2(pos.x, pos.y - 5), white(0.6f), a);
    ImVec2 bs = measure(F_.regular, fs, b);
    drawText(dl, F_.regular, fs, ImVec2(pos.x + w - bs.x, pos.y - 5), white(0.6f), b);
    float x0 = pos.x + tw, bw = std::max(20.f, w - tw * 2);
    if (app_.loading) {
        // идёт загрузка: бегущий блик вместо полосы
        dl->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + bw, pos.y + 5), white(0.14f), 3.f);
        float p = static_cast<float>(app_.loadProgress.load());
        dl->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + bw * std::max(0.04f, p), pos.y + 5), alpha(pal_.acc, 0.8f), 3.f);
        return;
    }
    SliderOut s = slider(big ? "##seekBig" : "##seekMini", dl, ImVec2(x0, pos.y), bw, frac, big ? 6.f : 5.f);
    if (s.active) { dragSeek_ = true; dragFrac_ = s.value; }
    if (s.released && dragSeek_) { dragSeek_ = false; if (len > 0) app_.seek(dragFrac_ * len); }
}

void Ui::drawTransport(ImVec2 c, float sc) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float g = 46.f * sc;
    if (iconButton("##tShuf", dl, ImVec2(c.x - 2 * g, c.y), 34.f * sc, Icon::Shuffle, app_.queue.shuffle, "Вперемешку")) app_.toggleShuffle();
    if (iconButton("##tPrev", dl, ImVec2(c.x - g, c.y), 38.f * sc, Icon::Prev, false, "Предыдущая")) app_.prev();
    // главная кнопка: белый круг, плавно меняет размер при наведении
    ImVec2 pc(c.x, c.y);
    float r = 21.f * sc;
    Hit h = hit("##tPlay", ImVec2(pc.x - r - 2, pc.y - r - 2), ImVec2(r * 2 + 4, r * 2 + 4));
    Fx f = fx(h);
    float k = f.k;
    float rr = r * (1.f + 0.07f * easeOut(k)) * (1.f - 0.08f * f.press);
    // тень снизу и цветное свечение темы при наведении
    glow(dl, pc + ImVec2(0, rr * 0.18f), rr * 1.55f, IM_COL32(0, 0, 0, 255), 0.45f, 10);
    if (k > 0.01f) glow(dl, pc, rr * 2.1f, pal_.acc, 0.35f * k * (1.f - 0.5f * f.press), 12);
    if (f.pulse >= 0.f) dl->AddCircle(pc, rr * (1.f + 0.55f * easeOut(f.pulse)), alpha(pal_.acc, 0.6f * (1.f - f.pulse)), 40, 2.f);
    dl->AddCircleFilled(pc, rr, white(0.97f), 48);
    // лёгкий объём: верх светлее, низ чуть темнее
    dl->AddCircleFilled(pc + ImVec2(0, rr * 0.08f), rr * 0.92f, IM_COL32(0, 0, 0, static_cast<int>(10 + 14 * f.press)), 48);
    dl->AddCircleFilled(pc - ImVec2(0, rr * 0.05f), rr * 0.86f, white(0.97f), 48);
    bool playing = app_.isPlaying();
    if (app_.loading) {
        float a0 = time_ * 5.f;
        dl->PathArcTo(pc, rr * 0.5f, a0, a0 + 4.2f, 20);
        dl->PathStroke(IM_COL32(14, 12, 22, 255), ImDrawFlags_None, 2.6f);
    } else {
        icon(dl, playing ? Icon::Pause : Icon::Play, pc + ImVec2(playing ? 0.f : 1.5f, 0), rr * 0.9f, IM_COL32(14, 12, 22, 255));
    }
    if (h.clicked) app_.togglePause();
    if (iconButton("##tNext", dl, ImVec2(c.x + g, c.y), 38.f * sc, Icon::Next, false, "Следующая")) app_.next();
    Loop lp = app_.queue.loop;
    if (iconButton("##tLoop", dl, ImVec2(c.x + 2 * g, c.y), 34.f * sc, lp == Loop::One ? Icon::LoopOne : Icon::Loop, lp != Loop::Off,
                   lp == Loop::Off ? "Повтор: выключен" : lp == Loop::All ? "Повтор: весь список" : "Повтор: одна песня"))
        app_.cycleLoop();
}

// ---------- винил ----------

void Ui::drawDeck(ImVec2 c, float ds) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool playing = app_.isPlaying() && !app_.loading;
    spinSpeed_.set(playing ? static_cast<float>(app_.rate) : 0.f);
    spinSpeed_.step(dt_, 2.2f);   // пластинка разгоняется и тормозит плавно, как настоящая
    spin_ = std::fmod(spin_ + spinSpeed_.v * dt_ * 2.09f, 2.f * kPi);  // ~33 об/мин
    armA_.set(playing || app_.loading ? 1.f : 0.f);
    armA_.step(dt_, 4.f);

    float R = ds / 2;
    if (app_.lib.getStr("deckStyle", "vinyl") == "cover") {
        // вид «Обложка»: большая картинка, которая чуть «дышит» в такт, пока играет музыка
        float pulse = (playing && !calm()) ? 1.f + 0.012f * std::sin(time_ * 4.f) : 1.f;
        float half = R * pulse;
        ImVec2 a0 = c - ImVec2(half, half), a1 = c + ImVec2(half, half);
        glow(dl, c, R * 1.35f, pal_.acc, 0.16f);
        softShadow(dl, a0, a1, radius(24.f), 34.f, 0.65f);
        if (app_.cur) drawCover(dl, *app_.cur, a0, half * 2, radius(24.f));
        dl->AddRect(a0, a1, white(0.10f), radius(24.f));
        Hit ch = hit("##coverClick", a0, a1 - a0);
        if (ch.clicked) app_.togglePause();
        if (ch.hovered) tip(playing ? "Пауза" : "Играть");
        return;
    }
    // тумба проигрывателя
    ImVec2 a = c - ImVec2(R + 34, R + 30), b = c + ImVec2(R + 34, R + 30);
    softShadow(dl, a, b, 28.f, 34.f, 0.6f);
    dl->AddRectFilled(a, b, IM_COL32(22, 20, 30, 255), 28.f);
    dl->AddRectFilled(a, b, alpha(pal_.acc, 0.05f), 28.f);
    dl->AddRect(a, b, white(0.09f), 28.f);
    glow(dl, c, R * 1.15f, pal_.acc, 0.10f + 0.10f * spinSpeed_.v);

    // пластинка
    dl->AddCircleFilled(c, R, IM_COL32(10, 9, 14, 255), 96);
    for (int i = 0; i < 9; ++i) dl->AddCircle(c, R * (0.40f + 0.06f * static_cast<float>(i)), white(i % 2 ? 0.035f : 0.02f), 96);
    // блики на канавках: два вращающихся сектора
    for (int k = 0; k < 2; ++k) {
        float a0 = -0.9f + static_cast<float>(k) * kPi, w = 0.35f;
        dl->PathClear();
        dl->PathLineTo(c);
        dl->PathArcTo(c, R * 0.97f, a0, a0 + w, 12);
        dl->PathFillConvex(white(0.045f));
    }
    // центральная этикетка — обложка, вращается вместе с пластинкой
    float lr = R * 0.38f;
    ImTextureID tx = app_.cur ? coverOf(*app_.cur) : 0;
    if (tx) textureCircle(dl, tx, c, lr, spin_, IM_COL32_WHITE, 64);
    else dl->AddCircleFilled(c, lr, u32(hsl(pal_.hue, 0.5f, 0.30f)), 48);
    dl->AddCircle(c, lr, white(0.15f), 64, 1.5f);
    dl->AddCircleFilled(c, R * 0.035f, IM_COL32(14, 12, 20, 255), 16);
    dl->AddCircleFilled(c, R * 0.014f, white(0.4f), 12);

    // тонарм: поворачивается из положения «над краем» на дорожку
    ImVec2 piv = c + ImVec2(R + 6, -R + 4);
    float ang = lerp(-0.18f, 0.42f, easeOut(armA_.v));
    float armLen = R * 1.28f;
    ImVec2 elbow = rotAround(piv, ImVec2(0, armLen * 0.78f), ang);
    ImVec2 head = rotAround(piv, ImVec2(-armLen * 0.12f, armLen), ang + 0.02f);
    dl->AddCircleFilled(piv, 17.f, IM_COL32(40, 38, 52, 255), 24);
    dl->AddCircle(piv, 17.f, white(0.15f), 24);
    dl->AddCircleFilled(piv, 6.f, white(0.5f), 16);
    dl->AddLine(piv, elbow, IM_COL32(190, 188, 205, 255), 5.f);
    dl->AddLine(elbow, head, IM_COL32(190, 188, 205, 255), 5.f);
    dl->AddCircleFilled(elbow, 4.f, IM_COL32(120, 118, 140, 255), 12);
    ImVec2 d = head - elbow;
    float dn = std::sqrt(d.x * d.x + d.y * d.y) + 1e-4f;
    ImVec2 nrm(-d.y / dn, d.x / dn);
    ImVec2 h0 = head - ImVec2(nrm.x * 7, nrm.y * 7), h1 = head + ImVec2(nrm.x * 7, nrm.y * 7), h2 = head + ImVec2(d.x / dn * 16, d.y / dn * 16);
    dl->AddQuadFilled(h0, h1, h2 + ImVec2(nrm.x * 5, nrm.y * 5), h2 - ImVec2(nrm.x * 5, nrm.y * 5), IM_COL32(60, 58, 76, 255));
    dl->AddCircleFilled(h2, 2.2f, pal_.acc, 8);

    // щелчок по пластинке — пауза / пуск
    Hit hh = hit("##deckClick", c - ImVec2(R, R), ImVec2(ds, ds));
    if (hh.clicked) app_.togglePause();
    if (hh.hovered) tip(playing ? "Пауза" : "Играть");
}

// ---------- текст ----------

void Ui::drawLyrics(ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const LyricsState& L = app_.ly;
    dl->PushClipRect(pos, pos + size, true);
    auto centered = [&](const std::string& s, float fs, ImU32 col, float y, ImFont* f) {
        ImVec2 ts = measure(f, fs, s, size.x - 40);
        drawText(dl, f, fs, ImVec2(pos.x + (size.x - ts.x) / 2, y), col, s, size.x - 40);
        return ts.y;
    };
    if (L.loading) {
        centered("Ищу текст…", 18.f, white(0.55f), pos.y + size.y / 2 - 10, F_.regular);
    } else if (L.data.instrumental) {
        centered("Инструментал", 22.f, white(0.6f), pos.y + size.y / 2 - 12, F_.bold);
    } else if (!L.data.lines.empty()) {
        double t = app_.position() + (app_.cur && app_.segment.hasLimit && !app_.segment.preview ? app_.segment.limS : 0.0);
        int cur = app_.lyricLineAt(t);
        // высота строк с учётом переноса, чтобы длинные не налезали друг на друга
        float fsA = 30.f, fsB = 22.f, gap = 18.f;
        std::vector<float> hs(L.data.lines.size()), ys(L.data.lines.size());
        float total = 0;
        for (std::size_t i = 0; i < hs.size(); ++i) {
            bool a = static_cast<int>(i) == cur;
            hs[i] = measure(a ? F_.bold : F_.regular, a ? fsA : fsB, L.data.lines[i].text.empty() ? " " : L.data.lines[i].text, size.x - 60).y + gap;
            ys[i] = total;
            total += hs[i];
        }
        float target = cur >= 0 ? ys[static_cast<std::size_t>(cur)] + hs[static_cast<std::size_t>(cur)] / 2 : 0.f;
        float k = 1.f - std::exp(-6.f * dt_);
        lyScroll_ += (target - lyScroll_) * k;
        float base = pos.y + size.y * 0.42f - lyScroll_;
        for (std::size_t i = 0; i < hs.size(); ++i) {
            float y = base + ys[i];
            if (y + hs[i] < pos.y - 10 || y > pos.y + size.y + 10) continue;
            int dist = std::abs(static_cast<int>(i) - cur);
            bool a = static_cast<int>(i) == cur;
            float al = a ? 1.f : std::max(0.18f, 0.55f - 0.12f * static_cast<float>(dist));
            // у границ панели текст растворяется
            float edge = std::min(y - pos.y, pos.y + size.y - y) / 80.f;
            al *= clamp01(edge + 0.2f);
            const std::string& s = L.data.lines[i].text;
            if (s.empty()) continue;
            float fs = a ? fsA : fsB;
            ImFont* f = a ? F_.bold : F_.regular;
            ImVec2 ts = measure(f, fs, s, size.x - 60);
            ImVec2 p(pos.x + 30, y);
            Hit h = hit(("##ln" + std::to_string(i)).c_str(), p, ImVec2(size.x - 60, hs[i] - gap / 2));
            if (h.hovered) al = std::max(al, 0.85f);
            if (a) glow(dl, ImVec2(pos.x + size.x / 2, y + ts.y / 2), std::min(size.x * 0.5f, 220.f), pal_.acc, 0.05f, 6);
            drawText(dl, f, fs, p, a ? alpha(pal_.acc, al) : white(al), s, size.x - 60);
            if (h.clicked && L.data.lines[i].t >= 0) {
                double base0 = app_.segment.hasLimit && !app_.segment.preview ? app_.segment.limS : 0.0;
                app_.seek(std::max(0.0, L.data.lines[i].t + L.offset - base0));
            }
        }
    } else if (!L.data.plain.empty()) {
        // без отметок времени: просто прокручиваемый текст
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##plainLy", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        touchScroll();
        ImGui::PushTextWrapPos(size.x - 40);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.82f));
        ImGui::TextUnformatted(L.data.plain.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        centered("Без отметок времени — этот текст не листается сам", 12.f, white(0.35f), pos.y + size.y - 18, F_.regular);
    } else {
        float y = pos.y + size.y / 2 - 50;
        float hh = centered(L.message.empty() ? "Текст не найден" : L.message, 20.f, white(0.6f), y, F_.bold);
        if (pill("##lyOwn", dl, ImVec2(pos.x + size.x / 2 - 100, y + hh + 18), ImVec2(200, 38), "Вставить свой текст")) {
            ownLyrics_.clear();
            openModal(Modal::OwnLyrics);
        }
    }
    dl->PopClipRect();

    // управление: сдвиг, свой текст, настройка «по нажатиям»
    if (!L.data.lines.empty() || !L.data.plain.empty()) {
        float y = pos.y + size.y + 8;
        float x = pos.x + 10;
        if (pill("##lyM", dl, ImVec2(x, y), ImVec2(78, 32), "−0.5 с")) app_.shiftLyrics(-0.5);
        if (pill("##lyP", dl, ImVec2(x + 86, y), ImVec2(78, 32), "+0.5 с")) app_.shiftLyrics(0.5);
        char buf[32];
        std::snprintf(buf, sizeof buf, "сдвиг %+.1f с", L.offset);
        drawText(dl, F_.regular, 12.f, ImVec2(x + 176, y + 8), white(0.45f), buf);
        if (pill("##lyOwn2", dl, ImVec2(pos.x + size.x - 180, y), ImVec2(170, 32), "Свой текст")) { ownLyrics_ = L.data.plain; openModal(Modal::OwnLyrics); }
    }
}

// ---------- редактор фрагментов ----------

void Ui::drawFragment(ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!app_.cur) return;
    double dur = app_.player.duration();
    if (dur <= 0) {
        drawText(dl, F_.regular, 15.f, pos + ImVec2(10, 20), white(0.55f), "Дождитесь загрузки песни — длительность ещё неизвестна.");
        return;
    }
    if (!fr_.init) {
        fr_.init = true;
        fr_.s = std::min(dur * 0.25, std::max(0.0, dur - 5));
        fr_.e = std::min(dur, fr_.s + std::max(5.0, dur * 0.25));
        fr_.keep = true;
        fr_.drag = 0;
        app_.loadPeaks();
    }
    drawText(dl, F_.bold, 22.f, pos + ImVec2(0, 0), white(0.97f), "Фрагмент");
    drawText(dl, F_.regular, 13.f, pos + ImVec2(0, 32), white(0.55f), "Потяните края. «Оставить» — играть только этот кусок, «Вырезать» — выбросить его из песни.", size.x);

    // режим
    float y = pos.y + 74;
    if (pill("##frKeep", dl, ImVec2(pos.x, y), ImVec2(130, 36), "Оставить", false, fr_.keep)) fr_.keep = true;
    if (pill("##frCut", dl, ImVec2(pos.x + 140, y), ImVec2(130, 36), "Вырезать", false, !fr_.keep)) fr_.keep = false;
    y += 56;

    // волна
    float wh = 120.f, wx = pos.x, ww = size.x;
    ImVec2 w0(wx, y), w1(wx + ww, y + wh);
    dl->AddRectFilled(w0, w1, white(0.05f), 14.f);
    float mid = y + wh / 2;
    const auto& pk = app_.peaks;
    int bars = std::max(8, static_cast<int>(ww / 4.f));
    for (int i = 0; i < bars; ++i) {
        float f = static_cast<float>(i) / static_cast<float>(bars);
        float v = 0.12f;
        if (!pk.empty()) v = std::max(0.06f, pk[std::min(pk.size() - 1, static_cast<std::size_t>(f * static_cast<float>(pk.size())))]);
        double tsec = static_cast<double>(f) * dur;
        bool in = tsec >= fr_.s && tsec <= fr_.e;
        bool sel = fr_.keep ? in : !in;
        float hh = v * (wh - 24) / 2;
        float x = wx + 4 + f * (ww - 8);
        dl->AddRectFilled(ImVec2(x, mid - hh), ImVec2(x + 2.4f, mid + hh), sel ? alpha(pal_.acc, 0.95f) : white(0.18f), 1.2f);
    }
    if (app_.peaksLoading) drawText(dl, F_.regular, 12.f, w0 + ImVec2(12, 8), white(0.4f), "Рисую волну…");
    // уже существующие вырезы
    for (auto& cr : app_.lib.cutList(*app_.cur)) {
        float a = wx + static_cast<float>(cr.cut.s / dur) * ww, b = wx + static_cast<float>(cr.cut.e / dur) * ww;
        dl->AddRectFilled(ImVec2(a, y + wh - 6), ImVec2(b, y + wh), IM_COL32(255, 120, 110, 200), 3.f);
    }
    // ручки
    float xs = wx + static_cast<float>(fr_.s / dur) * ww, xe = wx + static_cast<float>(fr_.e / dur) * ww;
    auto handle = [&](const char* id, float x, int which) {
        Hit h = hit(id, ImVec2(x - 12, y - 8), ImVec2(24, wh + 16));
        float k = hv(h.id, h.hovered || h.held);
        dl->AddRectFilled(ImVec2(x - 2, y), ImVec2(x + 2, y + wh), pal_.acc, 2.f);
        dl->AddCircleFilled(ImVec2(x, y - 2), 7.f + 2.f * k, pal_.acc, 16);
        dl->AddCircleFilled(ImVec2(x, y + wh + 2), 7.f + 2.f * k, pal_.acc, 16);
        if (h.held) {
            double t = std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x - wx) / ww), 0.0, 1.0) * dur;
            if (which == 1) fr_.s = std::min(t, fr_.e - 0.5);
            else fr_.e = std::max(t, fr_.s + 0.5);
            fr_.s = std::max(0.0, fr_.s);
            fr_.e = std::min(dur, fr_.e);
        }
        if (h.hovered) tip(which == 1 ? "Начало" : "Конец");
    };
    handle("##hS", xs, 1);
    handle("##hE", xe, 2);
    // позиция воспроизведения при предпросмотре
    if (fr_.previewing) {
        double p = app_.player.position();
        float px = wx + static_cast<float>(p / dur) * ww;
        dl->AddLine(ImVec2(px, y), ImVec2(px, y + wh), white(0.9f), 2.f);
    }
    y += wh + 14;
    drawText(dl, F_.regular, 14.f, ImVec2(pos.x, y), white(0.75f),
             "от " + fmtTime(fr_.s, true) + "   до " + fmtTime(fr_.e, true) + "   (" + fmtTime(fr_.e - fr_.s, true) + ")");
    y += 34;

    // кнопки
    if (pill("##frPrev", dl, ImVec2(pos.x, y), ImVec2(150, 40), fr_.previewing ? "Остановить" : "Прослушать")) {
        if (fr_.previewing) { app_.endPreview(); fr_.previewing = false; }
        else { app_.previewSegment(fr_.keep, fr_.s, fr_.e); fr_.previewing = true; }
    }
    if (pill("##frSave", dl, ImVec2(pos.x + 160, y), ImVec2(190, 40), fr_.keep ? "Сохранить фрагмент" : "Вырезать навсегда", true)) {
        if (fr_.previewing) { app_.endPreview(); fr_.previewing = false; }
        app_.saveClip(fr_.keep, fr_.s, fr_.e);
        fr_.open = false;
    }
    if (pill("##frClose", dl, ImVec2(pos.x + 360, y), ImVec2(110, 40), "Закрыть")) {
        if (fr_.previewing) { app_.endPreview(); fr_.previewing = false; }
        fr_.open = false;
    }
    y += 62;

    // список выбранных вырезов
    auto cuts = app_.lib.cutList(*app_.cur);
    if (!cuts.empty()) {
        drawText(dl, F_.bold, 15.f, ImVec2(pos.x, y), white(0.85f), "Вырезано из этой песни");
        y += 28;
        for (std::size_t i = 0; i < cuts.size() && y < pos.y + size.y - 30; ++i) {
            std::string s = fmtTime(cuts[i].cut.s, true) + " – " + fmtTime(cuts[i].cut.e, true) + (cuts[i].own ? "  (этот фрагмент)" : "");
            drawText(dl, F_.regular, 13.f, ImVec2(pos.x, y + 6), white(0.65f), s);
            ImGui::PushID(static_cast<int>(i));
            if (pill("##cutBack", dl, ImVec2(pos.x + 250, y), ImVec2(110, 28), "Вернуть")) app_.removeCut(cuts[i]);
            ImGui::PopID();
            y += 36;
        }
    }
}

// ---------- окно проигрывателя ----------

void Ui::drawFull() {
    float a = easeOut(fullA_.v);
    ImGui::SetNextWindowPos(ImVec2(0, (1.f - a) * W_.y * 0.6f));
    ImGui::SetNextWindowSize(W_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, clamp01(a * 1.4f));
    ImGuiWindowFlags ffl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    if (fullA_.t < 0.5f) ffl |= ImGuiWindowFlags_NoInputs;  // уезжающий проигрыватель не мешает кликать по списку
    ImGui::Begin("##full", nullptr, ffl);
    // Поверх основного окна и мини-плеера, но без SetWindowFocus() каждый кадр: тот сбрасывал нажатую кнопку
    // в окне поверх (подтверждение, «Свой текст»), и в них ничего не нажималось, пока открыт проигрыватель.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    if (fullFocus_ && modal_ == Modal::None) { ImGui::SetWindowFocus(); fullFocus_ = false; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // свой фон поверх основного: плотная заливка и два цветных пятна
    dl->AddRectFilled(ImVec2(0, 0), W_, alpha(pal_.bg, 0.985f));
    if (drawCustomBg(dl)) {  // свой фон и в проигрывателе; поверх него затемнение, чтобы текст читался
        const float dim = std::max(0.35f, std::clamp(static_cast<float>(app_.lib.getNum("bgDim", 0)), 0.f, 0.85f));
        dl->AddRectFilled(ImVec2(0, 0), W_, IM_COL32(0, 0, 0, static_cast<int>(255 * dim)));
    } else if (setting("blobs", true)) drawBlobs(dl, W_, setting("calm", false) ? 0.f : time_, pal_, 0.38f);

    if (!app_.cur) { ImGui::End(); ImGui::PopStyleVar(3); return; }
    const Track& t = *app_.cur;

    // верх
    if (iconButton("##fullClose", dl, ImVec2(46, 40), 40.f, Icon::Close, false, "Свернуть (Esc)")) openFull(false);
    std::string top = t.clip ? "Фрагмент" : std::string(sourceLabel(t.source));
    ImVec2 ts = measure(F_.bold, 13.f, top);
    drawText(dl, F_.bold, 13.f, ImVec2((W_.x - ts.x) / 2, 30), white(0.55f), top);

    // Телефон лёжа (низкий экран): две колонки. Колонка управления — название, полоса, кнопки, действия;
    // в другой — вертушка, а когда открыт текст или фрагмент, — эта панель (тогда управление уезжает налево).
    if (compact()) {
        const bool panel = fr_.open || app_.ly.open;
        const float pad = 18.f, top = 56.f;
        const float colW = W_.x * 0.46f;
        const float cx0 = panel ? pad : W_.x - colW + 8.f;
        const float cw = colW - pad - 8.f;
        const float vx0 = panel ? colW + 8.f : 0.f;
        const float vw = panel ? W_.x - vx0 - pad : W_.x - colW;
        if (!panel) {
            // «плита» вертушки примерно в 1.2 раза больше самой пластинки
            float ds = std::clamp(std::min(vw - 2 * pad, W_.y - top - 20.f) / 1.22f, 110.f, 300.f);
            drawDeck(ImVec2(vw / 2, top + (W_.y - top - 12.f) / 2), ds);
        } else {
            if (fr_.open) drawFragment(ImVec2(vx0, top + 4.f), ImVec2(vw, W_.y - top - 16.f));
            else {
                if (app_.ly.key.empty() || app_.ly.key != lyricKey(t)) app_.loadLyrics(false);
                drawLyrics(ImVec2(vx0, top + 4.f), ImVec2(vw, W_.y - top - 16.f));
            }
        }
        const float y0 = std::max(top - 8.f, (W_.y - 232.f) / 2.f + 14.f);
        const float mid = cx0 + cw / 2;
        std::string tt = ellipsize(F_.bold, 22.f, t.title, cw);
        ImVec2 tsz = measure(F_.bold, 22.f, tt);
        drawText(dl, F_.bold, 22.f, ImVec2(mid - tsz.x / 2, y0), white(0.98f), tt);
        std::string ar = ellipsize(F_.regular, 15.f, t.artist, cw);
        ImVec2 asz = measure(F_.regular, 15.f, ar);
        if (artistLabel("##fpArt", dl, ImVec2(mid - asz.x / 2, y0 + 32), ar, 15.f, white(0.6f), t)) { goArtist(t.artist, t.source != "youtube"); openFull(false); }
        drawProgress(ImVec2(cx0, y0 + 80), cw, true);
        drawTransport(ImVec2(mid, y0 + 142), std::min(1.2f, cw / 330.f));
        // ряд действий: значки и кнопки разной ширины, промежутки делим поровну (в узкой колонке ничего не слипается)
        const float ry = y0 + 206;
        static const float kW[5] = {42.f, 42.f, 54.f, 62.f, 42.f};
        const float gap = std::max(2.f, (cw - (kW[0] + kW[1] + kW[2] + kW[3] + kW[4])) / 4.f);
        auto at = [&](int i) {
            float x = cx0;
            for (int j = 0; j < i; ++j) x += kW[j] + gap;
            return x + kW[i] / 2.f;
        };
        bool fav = app_.inLibrary(t);
        if (iconButton("##fullFav", dl, ImVec2(at(0), ry), 42.f, fav ? Icon::HeartFilled : Icon::Heart, fav)) {
            if (fav && !t.clip) { Track c = t; confirm("Убрать «" + c.title + "» из вашей музыки?", [this, c] { app_.toggleFavorite(c); }); }
            else app_.toggleFavorite(t);
        }
        bool busy = app_.exporting.count(t.id) > 0;
        if (!t.clip && iconButton("##fullSave", dl, ImVec2(at(1), ry), 42.f, Icon::Save, busy) && !busy) app_.exportTrack(t);
        static const double kRates2[] = {0.75, 1.0, 1.25, 1.5};
        char rb2[16];
        std::snprintf(rb2, sizeof rb2, "%.2g×", app_.rate);
        if (pill("##fullRate", dl, ImVec2(at(2) - 27, ry - 17), ImVec2(54, 34), rb2, false, std::fabs(app_.rate - 1.0) > 0.01)) {
            int idx = 1;
            for (int i = 0; i < 4; ++i) if (std::fabs(kRates2[i] - app_.rate) < 0.01) idx = i;
            app_.setRate(kRates2[(idx + 1) % 4]);
        }
        bool lyOn = app_.ly.open && !fr_.open;
        if (pill("##fullLy", dl, ImVec2(at(3) - 31, ry - 17), ImVec2(62, 34), "Текст", false, lyOn)) { fr_.open = false; app_.openLyrics(!lyOn); }
        if (iconButton("##fullCut", dl, ImVec2(at(4), ry), 42.f, Icon::Scissors, fr_.open)) {
            fr_.open = !fr_.open;
            fr_.init = false;
            if (!fr_.open && fr_.previewing) { app_.endPreview(); fr_.previewing = false; }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        return;
    }

    bool wide = W_.x >= 980.f;
    bool showRight = fr_.open || app_.ly.open;
    float bottomH = mobile_ ? 236.f : 150.f;  // на телефоне кнопки в два ряда
    float areaTop = 80.f, areaH = W_.y - areaTop - bottomH;
    float leftW = showRight && wide ? W_.x * 0.46f : W_.x;
    float ds = std::max(140.f, std::min(std::min(leftW - 140.f, areaH - 130.f), 430.f));
    ImVec2 dc(leftW / 2, areaTop + (areaH - 90.f) / 2 + 6.f);
    if (!(showRight && !wide)) drawDeck(dc, ds);

    // название под вертушкой
    float ty = dc.y + ds / 2 + 52.f;
    float tmax = std::min(leftW - 60.f, 620.f);
    if (!(showRight && !wide)) {
        std::string tt = ellipsize(F_.bold, 26.f, t.title, tmax);
        ImVec2 tsz = measure(F_.bold, 26.f, tt);
        drawText(dl, F_.bold, 26.f, ImVec2(leftW / 2 - tsz.x / 2, ty), white(0.98f), tt);
        std::string ar = ellipsize(F_.regular, 16.f, t.artist, tmax);
        ImVec2 asz = measure(F_.regular, 16.f, ar);
        if (artistLabel("##fpArt2", dl, ImVec2(leftW / 2 - asz.x / 2, ty + 36), ar, 16.f, white(0.6f), t)) { goArtist(t.artist, t.source != "youtube"); openFull(false); }
    }

    // правая панель
    if (showRight) {
        ImVec2 rp = wide ? ImVec2(leftW + 10, areaTop + 10) : ImVec2(30, areaTop + 10);
        ImVec2 rs = wide ? ImVec2(W_.x - leftW - 50, areaH - 50) : ImVec2(W_.x - 60, areaH - 50);
        if (fr_.open) drawFragment(rp, rs);
        else {
            if (app_.ly.key.empty() || app_.ly.key != lyricKey(t)) app_.loadLyrics(false);
            drawLyrics(rp, rs);
        }
    }

    // низ на телефоне: полоса, большие кнопки, ниже — второй ряд (сердечко, сохранить, скорость, текст, ножницы).
    // Громкость не показываем: на телефоне её меняют кнопками сбоку.
    if (mobile_) {
        float bw = W_.x - 40.f, bx = 20.f;
        drawProgress(ImVec2(bx, W_.y - bottomH + 28), bw, true);
        drawTransport(ImVec2(W_.x / 2, W_.y - bottomH + 98), std::min(1.3f, W_.x / 360.f));
        float ry = W_.y - bottomH + 178, step = bw / 5.f;
        auto at = [&](int i) { return bx + step * (static_cast<float>(i) + 0.5f); };
        bool fav = app_.inLibrary(t);
        if (iconButton("##fullFav", dl, ImVec2(at(0), ry), 46.f, fav ? Icon::HeartFilled : Icon::Heart, fav)) {
            if (fav && !t.clip) { Track c = t; confirm("Убрать «" + c.title + "» из вашей музыки?", [this, c] { app_.toggleFavorite(c); }); }
            else app_.toggleFavorite(t);
        }
        bool busy = app_.exporting.count(t.id) > 0;
        if (!t.clip && iconButton("##fullSave", dl, ImVec2(at(1), ry), 46.f, Icon::Save, busy) && !busy) app_.exportTrack(t);
        static const double kRates[] = {0.75, 1.0, 1.25, 1.5};
        char rb[16];
        std::snprintf(rb, sizeof rb, "%.2g×", app_.rate);
        if (pill("##fullRate", dl, ImVec2(at(2) - 27, ry - 18), ImVec2(54, 36), rb, false, std::fabs(app_.rate - 1.0) > 0.01)) {
            int idx = 1;
            for (int i = 0; i < 4; ++i) if (std::fabs(kRates[i] - app_.rate) < 0.01) idx = i;
            app_.setRate(kRates[(idx + 1) % 4]);
        }
        bool lyOn = app_.ly.open && !fr_.open;
        if (pill("##fullLy", dl, ImVec2(at(3) - 31, ry - 18), ImVec2(62, 36), "Текст", false, lyOn)) { fr_.open = false; app_.openLyrics(!lyOn); }
        if (iconButton("##fullCut", dl, ImVec2(at(4), ry), 46.f, Icon::Scissors, fr_.open)) {
            fr_.open = !fr_.open;
            fr_.init = false;
            if (!fr_.open && fr_.previewing) { app_.endPreview(); fr_.previewing = false; }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        return;
    }

    // низ: полоса, кнопки, действия
    float bw = std::min(780.f, W_.x - 60.f), bx = (W_.x - bw) / 2;
    drawProgress(ImVec2(bx, W_.y - bottomH + 24), bw, true);
    drawTransport(ImVec2(W_.x / 2, W_.y - bottomH + 80), 1.25f);
    // слева: сердечко; справа: текст / ножницы / скорость
    bool fav = app_.inLibrary(t);
    if (iconButton("##fullFav", dl, ImVec2(bx + 20, W_.y - bottomH + 80), 40.f, fav ? Icon::HeartFilled : Icon::Heart, fav, fav ? "Убрать из моей музыки" : "В мою музыку")) {
        if (fav && !t.clip) { Track c = t; confirm("Убрать «" + c.title + "» из вашей музыки?", [this, c] { app_.toggleFavorite(c); }); }
        else app_.toggleFavorite(t);
    }
    float rx = bx + bw - 20;
    if (iconButton("##fullCut", dl, ImVec2(rx, W_.y - bottomH + 80), 40.f, Icon::Scissors, fr_.open, "Фрагмент: оставить или вырезать")) {
        fr_.open = !fr_.open;
        fr_.init = false;
        if (!fr_.open && fr_.previewing) { app_.endPreview(); fr_.previewing = false; }
    }
    if (!t.clip) {
        bool busy = app_.exporting.count(t.id) > 0;
        if (iconButton("##fullSave", dl, ImVec2(bx + 64, W_.y - bottomH + 80), 40.f, Icon::Save, busy,
                       busy ? "Сохраняю…" : "Сохранить на компьютер (mp3/m4a)") && !busy)
            app_.exportTrack(t);
    }
    {
        bool on = app_.ly.open && !fr_.open;
        if (pill("##fullLy", dl, ImVec2(rx - 114, W_.y - bottomH + 64), ImVec2(64, 32), "Текст", false, on)) { fr_.open = false; app_.openLyrics(!on); }
    }
    // скорость: нажатие по кругу 0.75× → 1× → 1.25× → 1.5×
    {
        static const double kRates[] = {0.75, 1.0, 1.25, 1.5};
        char rb[16];
        std::snprintf(rb, sizeof rb, "%.2g×", app_.rate);
        bool rateOn = std::fabs(app_.rate - 1.0) > 0.01;   // скорость не обычная — кнопка подсвечена
        bool rateClick = pill("##fullRate", dl, ImVec2(rx - 190, W_.y - bottomH + 64), ImVec2(66, 32), rb, false, rateOn);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem)) tip("Скорость (как у пластинки, тон меняется)");
        if (rateClick) {
            int idx = 1;
            for (int i = 0; i < 4; ++i) if (std::fabs(kRates[i] - app_.rate) < 0.01) idx = i;
            app_.setRate(kRates[(idx + 1) % 4]);
        }
    }
    // громкость
    // слева направо: сердечко, сохранить, громкость (раньше «сохранить» стояла справа и слипалась с кнопкой повтора)
    icon(dl, Icon::Volume, ImVec2(bx + 116, W_.y - bottomH + 80), 20.f, white(0.7f));
    // в узком окне ползунок укорачивается, чтобы не подойти к кнопкам в центре
    float volW = std::clamp(W_.x / 2 - 57.5f * 2 - 34.f - (bx + 134), 36.f, 84.f);
    SliderOut vs = slider("##fullVol", dl, ImVec2(bx + 134, W_.y - bottomH + 77), volW, app_.volume);
    if (vs.active) app_.setVolume(vs.value);

    ImGui::End();
    ImGui::PopStyleVar(3);
}

}  // namespace mx::ui
