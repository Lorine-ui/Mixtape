#include "draw.hpp"
#include "i18n.hpp"

#include "imgui_internal.h"  // ImTextCharFromUtf8

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>
#include <vector>

namespace mx::ui {

constexpr float kPi = 3.14159265358979f;

// ---------- цвета и величины ----------

void Anim::step(float dt, float speed) { v += (t - v) * (1.f - std::exp(-speed * dt)); if (std::fabs(t - v) < 0.0005f) v = t; }

ImVec4 hsl(float h, float s, float l, float a) {
    h = std::fmod(h, 360.f);
    if (h < 0) h += 360.f;
    float c = (1.f - std::fabs(2.f * l - 1.f)) * s, hp = h / 60.f;
    float x = c * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f)), r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    float m = l - c / 2.f;
    return ImVec4(r + m, g + m, b + m, a);
}

ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
ImU32 alpha(ImU32 c, float a) {
    float al = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF) * a;
    al = std::max(0.f, std::min(255.f, al));
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al) << IM_COL32_A_SHIFT);
}
ImU32 white(float a) { return IM_COL32(255, 255, 255, static_cast<int>(clamp01(a) * 255.f)); }
float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float easeOut(float t) { t = clamp01(t); return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float hueDist(float from, float to) {
    float d = std::fmod(to - from, 360.f);
    if (d > 180.f) d -= 360.f;
    if (d < -180.f) d += 360.f;
    return d;
}

// ---------- текст ----------

namespace {
Fonts g_fonts;
float g_textK = 1.f, g_uiK = 1.f;

// Крупный текст — из шрифта, запечённого в 34 px (чётче, чем увеличенный мелкий). Но в крупных шрифтах нет
// японских/китайских символов: если они есть в строке, остаёмся на мелком шрифте.
ImFont* pick(ImFont* f, float size, const std::string& s) {
    if (size * g_uiK < 22.f) return f;
    ImFont* big = f == g_fonts.bold ? g_fonts.boldL : f == g_fonts.regular ? g_fonts.regularL : nullptr;
    if (!big) return f;
    const char* p = s.c_str();
    const char* end = p + s.size();
    while (p < end) {
        unsigned int c = 0;
        int n = ImTextCharFromUtf8(&c, p, end);
        if (n <= 0) break;
        if (c >= 0x80 && !big->FindGlyphNoFallback(static_cast<ImWchar>(c))) return f;
        p += n;
    }
    return big;
}
}  // namespace

void setTextFonts(const Fonts& f) { g_fonts = f; }
void setTextScale(float textK, float uiK) { g_textK = textK; g_uiK = uiK; }
float textScale() { return g_textK; }

void drawText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const std::string& s0, float wrap) {
    if (s0.empty()) return;
    const std::string s = tr(s0);  // язык интерфейса
    size *= g_textK;
    dl->AddText(pick(f, size, s), size, pos, col, s.c_str(), nullptr, wrap);
}

namespace {
ImVec2 measureRaw(ImFont* f, float size, const std::string& s, float wrap = 0) {
    size *= g_textK;
    if (s.empty()) return ImVec2(0, size);
    return pick(f, size, s)->CalcTextSizeA(size, FLT_MAX, wrap, s.c_str());
}
}  // namespace

ImVec2 measure(ImFont* f, float size, const std::string& s0, float wrap) {
    const std::string s = tr(s0);
    size *= g_textK;
    if (s.empty()) return ImVec2(0, size);
    return pick(f, size, s)->CalcTextSizeA(size, FLT_MAX, wrap, s.c_str());
}

std::string ellipsize(ImFont* f, float size, const std::string& s0, float maxW) {
    const std::string s = tr(s0);  // режем уже переведённую строку
    if (measureRaw(f, size, s).x <= maxW) return s;
    std::size_t n = s.size();
    while (n > 0) {
        --n;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;  // не режем символ посередине
        std::string t = s.substr(0, n) + "...";
        if (measureRaw(f, size, t).x <= maxW) return t;
    }
    return "...";
}

// ---------- иконки ----------
// Нарисованы по сетке 24×24 (как наборы Lucide/Feather): одинаковая толщина линий, круглые концы и стыки,
// скруглённые углы. Линии с полупрозрачным цветом «запекаются» в непрозрачный цвет поверх фона темы,
// иначе в местах стыков проступали бы тёмные пятна.

namespace {
ImU32 g_backdrop = IM_COL32(12, 10, 18, 255);
using Pts = std::vector<ImVec2>;

// Скругляет углы ломаной (координаты в сетке 24×24): у каждого угла — плавная дуга радиусом r.
Pts roundPts(const Pts& g, float r, bool closed) {
    Pts out;
    const std::size_t n = g.size();
    for (std::size_t i = 0; i < n; ++i) {
        bool end = !closed && (i == 0 || i + 1 == n);
        if (end || r <= 0.f) { out.push_back(g[i]); continue; }
        ImVec2 v = g[i], pv = g[(i + n - 1) % n], nx = g[(i + 1) % n];
        ImVec2 d1(pv.x - v.x, pv.y - v.y), d2(nx.x - v.x, nx.y - v.y);
        float l1 = std::sqrt(d1.x * d1.x + d1.y * d1.y), l2 = std::sqrt(d2.x * d2.x + d2.y * d2.y);
        if (l1 < 1e-4f || l2 < 1e-4f) { out.push_back(v); continue; }
        float d = std::min(r, std::min(l1, l2) * 0.5f);
        ImVec2 t1(v.x + d1.x / l1 * d, v.y + d1.y / l1 * d), t2(v.x + d2.x / l2 * d, v.y + d2.y / l2 * d);
        const int seg = 6;
        for (int k = 0; k <= seg; ++k) {
            float t = static_cast<float>(k) / seg, u = 1.f - t;
            out.emplace_back(u * u * t1.x + 2 * u * t * v.x + t * t * t2.x, u * u * t1.y + 2 * u * t * v.y + t * t * t2.y);
        }
    }
    return out;
}

void arcPts(Pts& out, float cx, float cy, float r, float a0deg, float a1deg, int n = 16) {
    for (int i = 0; i <= n; ++i) {
        float a = (a0deg + (a1deg - a0deg) * static_cast<float>(i) / n) * kPi / 180.f;
        out.emplace_back(cx + std::cos(a) * r, cy + std::sin(a) * r);
    }
}

struct Pen {
    ImDrawList* dl;
    ImVec2 c;
    float s, th;
    ImU32 col, sc;   // col — как задано, sc — для линий и составных фигур (непрозрачный, если можно)
    bool joints;
    ImVec2 P(float x, float y) const { return ImVec2(c.x + (x - 12.f) / 24.f * s, c.y + (y - 12.f) / 24.f * s); }
    float R(float r) const { return r / 24.f * s; }
    void stroke(const Pts& g, bool closed = false) const {
        if (g.size() < 2) return;
        Pts v;
        v.reserve(g.size());
        for (auto& p : g) v.push_back(P(p.x, p.y));
        dl->AddPolyline(v.data(), static_cast<int>(v.size()), sc, closed ? ImDrawFlags_Closed : ImDrawFlags_None, th);
        if (joints)   // круглые концы и стыки
            for (auto& p : v) dl->AddCircleFilled(p, th * 0.5f, sc, 10);
    }
    void line(float x1, float y1, float x2, float y2) const { stroke({ImVec2(x1, y1), ImVec2(x2, y2)}); }
    void rstroke(const Pts& g, float r, bool closed = false) const { stroke(roundPts(g, r, closed), closed); }
    void fill(const Pts& g, float r, ImU32 cc) const {
        Pts rp = roundPts(g, r, true), v;
        for (auto& p : rp) v.push_back(P(p.x, p.y));
        dl->AddConvexPolyFilled(v.data(), static_cast<int>(v.size()), cc);
    }
    void circle(float x, float y, float r) const { dl->AddCircle(P(x, y), R(r), sc, 32, th); }
    void dot(float x, float y, float r, ImU32 cc) const { dl->AddCircleFilled(P(x, y), R(r), cc, 24); }
    void rect(float x0, float y0, float x1, float y1, float r, ImU32 cc) const { dl->AddRectFilled(P(x0, y0), P(x1, y1), cc, R(r)); }
};

// контур сердца: квадрат, повёрнутый на 45°, и два круга на его верхних сторонах
Pts heartOutline() {
    Pts g;
    g.emplace_back(12.f, 21.2f);
    g.emplace_back(3.7f, 12.9f);
    arcPts(g, 7.85f, 8.75f, 5.87f, 135.f, 315.f, 20);
    arcPts(g, 16.15f, 8.75f, 5.87f, 225.f, 405.f, 20);
    g.emplace_back(20.3f, 12.9f);
    return g;
}
}  // namespace

void setIconBackdrop(ImU32 bg) { g_backdrop = bg | IM_COL32_A_MASK; }

void icon(ImDrawList* dl, Icon ic, ImVec2 c, float s, ImU32 col) {
    float a = static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.f;
    Pen p{dl, c, s, std::max(1.5f, s * 0.085f), col, col, true};
    if (a < 0.999f) {
        if (a >= 0.45f) p.sc = mix(g_backdrop, col | IM_COL32_A_MASK, a);   // непрозрачный — стыки не видны
        else p.joints = false;                                              // почти прозрачный (появляется) — без стыков
    }
    const ImU32 F = p.sc;   // заливка составных фигур
    switch (ic) {
        case Icon::Play: p.fill({ImVec2(6.5f, 4.f), ImVec2(20.5f, 12.f), ImVec2(6.5f, 20.f)}, 2.4f, col); break;
        case Icon::Pause:
            p.rect(5.5f, 4.f, 10.f, 20.f, 1.6f, col);
            p.rect(14.f, 4.f, 18.5f, 20.f, 1.6f, col);
            break;
        case Icon::Next:
            p.fill({ImVec2(4.5f, 4.5f), ImVec2(16.5f, 12.f), ImVec2(4.5f, 19.5f)}, 2.f, col);
            p.rect(17.f, 4.5f, 20.f, 19.5f, 1.5f, col);
            break;
        case Icon::Prev:
            p.fill({ImVec2(19.5f, 4.5f), ImVec2(7.5f, 12.f), ImVec2(19.5f, 19.5f)}, 2.f, col);
            p.rect(4.f, 4.5f, 7.f, 19.5f, 1.5f, col);
            break;
        case Icon::Shuffle:
            p.rstroke({ImVec2(2, 18), ImVec2(6.5f, 18), ImVec2(14.5f, 6), ImVec2(21, 6)}, 4.f);
            p.rstroke({ImVec2(2, 6), ImVec2(6.5f, 6), ImVec2(14.5f, 18), ImVec2(21, 18)}, 4.f);
            p.stroke({ImVec2(18, 2.5f), ImVec2(21.5f, 6), ImVec2(18, 9.5f)});
            p.stroke({ImVec2(18, 14.5f), ImVec2(21.5f, 18), ImVec2(18, 21.5f)});
            break;
        case Icon::Loop:
        case Icon::LoopOne:
            p.rstroke({ImVec2(3, 11), ImVec2(3, 6), ImVec2(21, 6)}, 4.f);
            p.stroke({ImVec2(17, 2), ImVec2(21, 6), ImVec2(17, 10)});
            p.rstroke({ImVec2(21, 13), ImVec2(21, 18), ImVec2(3, 18)}, 4.f);
            p.stroke({ImVec2(7, 22), ImVec2(3, 18), ImVec2(7, 14)});
            if (ic == Icon::LoopOne) p.stroke({ImVec2(10.8f, 10.2f), ImVec2(12.2f, 9.4f), ImVec2(12.2f, 14.6f)});
            break;
        case Icon::Heart: p.stroke(heartOutline(), true); break;
        case Icon::HeartFilled: {
            // сердце не выпуклое: заливаем двумя кругами и ромбом
            p.dot(7.85f, 8.75f, 5.87f, F);
            p.dot(16.15f, 8.75f, 5.87f, F);
            p.fill({ImVec2(12.f, 21.2f), ImVec2(3.7f, 12.9f), ImVec2(12.f, 4.6f), ImVec2(20.3f, 12.9f)}, 1.2f, F);
            break;
        }
        case Icon::Plus: p.line(5, 12, 19, 12); p.line(12, 5, 12, 19); break;
        case Icon::Close: p.line(6, 6, 18, 18); p.line(18, 6, 6, 18); break;
        case Icon::Check: p.stroke({ImVec2(4.5f, 12.5f), ImVec2(9.5f, 17.5f), ImVec2(19.5f, 6.5f)}); break;
        case Icon::Search:
            p.circle(10.5f, 10.5f, 7.f);
            p.line(15.6f, 15.6f, 20.5f, 20.5f);
            break;
        case Icon::Download:
            p.line(12, 3.5f, 12, 15);
            p.stroke({ImVec2(7, 10), ImVec2(12, 15), ImVec2(17, 10)});
            p.rstroke({ImVec2(21, 15), ImVec2(21, 20.5f), ImVec2(3, 20.5f), ImVec2(3, 15)}, 2.5f);
            break;
        case Icon::Files:   // загрузить свои файлы
            p.line(12, 15, 12, 3.5f);
            p.stroke({ImVec2(7, 8.5f), ImVec2(12, 3.5f), ImVec2(17, 8.5f)});
            p.rstroke({ImVec2(21, 15), ImVec2(21, 20.5f), ImVec2(3, 20.5f), ImVec2(3, 15)}, 2.5f);
            break;
        case Icon::Import:  // список с плюсом: перенос плейлиста
            p.line(3, 6, 16, 6);
            p.line(3, 12, 11, 12);
            p.line(3, 18, 16, 18);
            p.line(18, 9, 18, 15);
            p.line(15, 12, 21, 12);
            break;
        case Icon::Scissors:
            p.circle(6, 6, 3);
            p.circle(6, 18, 3);
            p.line(20, 4, 8.3f, 15.7f);
            p.line(14.5f, 14.5f, 20, 20);
            p.line(8.3f, 8.3f, 11.8f, 11.8f);
            break;
        case Icon::Note:
            p.rstroke({ImVec2(9, 18), ImVec2(9, 5), ImVec2(20, 3), ImVec2(20, 16)}, 1.f);
            p.dot(6.2f, 18.f, 3.2f, F);
            p.dot(17.2f, 16.f, 3.2f, F);
            break;
        case Icon::Dice:
            p.rstroke({ImVec2(3, 3), ImVec2(21, 3), ImVec2(21, 21), ImVec2(3, 21)}, 4.f, true);
            for (auto d : {ImVec2(8, 8), ImVec2(16, 8), ImVec2(12, 12), ImVec2(8, 16), ImVec2(16, 16)}) p.dot(d.x, d.y, 1.6f, F);
            break;
        case Icon::Gear: {
            Pts g;
            for (int i = 0; i < 8; ++i) {
                float t = static_cast<float>(i) * 45.f;
                for (auto [da, rr] : {std::pair<float, float>{-15.f, 7.4f}, {-8.f, 10.f}, {8.f, 10.f}, {15.f, 7.4f}}) {
                    float an = (t + da) * kPi / 180.f;
                    g.emplace_back(12.f + std::cos(an) * rr, 12.f + std::sin(an) * rr);
                }
            }
            p.rstroke(g, 0.9f, true);
            p.circle(12, 12, 3.f);
            break;
        }
        case Icon::Home:
            p.rstroke({ImVec2(3, 10), ImVec2(12, 2.8f), ImVec2(21, 10), ImVec2(21, 21), ImVec2(3, 21)}, 2.2f, true);
            p.rstroke({ImVec2(9.5f, 21), ImVec2(9.5f, 14), ImVec2(14.5f, 14), ImVec2(14.5f, 21)}, 1.2f);
            break;
        case Icon::Trash:
            p.line(3, 6, 21, 6);
            p.rstroke({ImVec2(19, 6), ImVec2(18, 21.5f), ImVec2(6, 21.5f), ImVec2(5, 6)}, 2.f);
            p.rstroke({ImVec2(8.5f, 6), ImVec2(8.5f, 2.5f), ImVec2(15.5f, 2.5f), ImVec2(15.5f, 6)}, 1.5f);
            p.line(10, 11, 10, 17);
            p.line(14, 11, 14, 17);
            break;
        case Icon::Expand:
            p.rstroke({ImVec2(8, 3), ImVec2(3, 3), ImVec2(3, 8)}, 2.f);
            p.rstroke({ImVec2(16, 3), ImVec2(21, 3), ImVec2(21, 8)}, 2.f);
            p.rstroke({ImVec2(21, 16), ImVec2(21, 21), ImVec2(16, 21)}, 2.f);
            p.rstroke({ImVec2(8, 21), ImVec2(3, 21), ImVec2(3, 16)}, 2.f);
            break;
        case Icon::Shrink:
            p.rstroke({ImVec2(8, 3), ImVec2(8, 8), ImVec2(3, 8)}, 2.f);
            p.rstroke({ImVec2(21, 8), ImVec2(16, 8), ImVec2(16, 3)}, 2.f);
            p.rstroke({ImVec2(3, 16), ImVec2(8, 16), ImVec2(8, 21)}, 2.f);
            p.rstroke({ImVec2(16, 21), ImVec2(16, 16), ImVec2(21, 16)}, 2.f);
            break;
        case Icon::User:
            p.circle(12.f, 8.f, 4.2f);
            p.rstroke({ImVec2(4.f, 21.f), ImVec2(4.f, 18.5f), ImVec2(8.f, 14.5f), ImVec2(16.f, 14.5f), ImVec2(20.f, 18.5f), ImVec2(20.f, 21.f)}, 3.f);
            break;
        case Icon::Chart:
            p.rect(3.5f, 13.f, 8.f, 20.5f, 1.4f, F);
            p.rect(9.8f, 8.f, 14.2f, 20.5f, 1.4f, F);
            p.rect(16.f, 3.5f, 20.5f, 20.5f, 1.4f, F);
            break;
        case Icon::Sparkle:  // четырёхлучевая искра: два вытянутых ромба крест-накрест
            p.fill({ImVec2(12, 1.5f), ImVec2(14.4f, 12), ImVec2(12, 22.5f), ImVec2(9.6f, 12)}, 0.8f, F);
            p.fill({ImVec2(1.5f, 12), ImVec2(12, 9.6f), ImVec2(22.5f, 12), ImVec2(12, 14.4f)}, 0.8f, F);
            p.dot(19.f, 5.f, 1.6f, F);
            break;
        case Icon::Save:
            p.rstroke({ImVec2(3, 3), ImVec2(16, 3), ImVec2(21, 8), ImVec2(21, 21), ImVec2(3, 21)}, 2.2f, true);
            p.rstroke({ImVec2(17, 21), ImVec2(17, 13.5f), ImVec2(7, 13.5f), ImVec2(7, 21)}, 1.2f);
            p.rstroke({ImVec2(7.5f, 3), ImVec2(7.5f, 8), ImVec2(15, 8)}, 1.2f);
            break;
        case Icon::Volume: {
            p.fill({ImVec2(2.5f, 9), ImVec2(6.5f, 9), ImVec2(6.5f, 15), ImVec2(2.5f, 15)}, 1.2f, F);
            p.fill({ImVec2(6, 9.2f), ImVec2(11.5f, 4.2f), ImVec2(11.5f, 19.8f), ImVec2(6, 14.8f)}, 1.2f, F);
            Pts a1, a2;
            arcPts(a1, 12.5f, 12.f, 4.6f, -40.f, 40.f, 12);
            arcPts(a2, 12.5f, 12.f, 8.5f, -45.f, 45.f, 16);
            p.stroke(a1);
            p.stroke(a2);
            break;
        }
    }
}

// ---------- картинки и свечение ----------

void textureCircle(ImDrawList* dl, ImTextureID tex, ImVec2 c, float r, float angle, ImU32 col, int seg) {
    dl->PushTextureID(tex);
    dl->PrimReserve(seg * 3, seg * 3);
    for (int i = 0; i < seg; ++i) {
        float a0 = static_cast<float>(i) * 2.f * kPi / static_cast<float>(seg);
        float a1 = static_cast<float>(i + 1) * 2.f * kPi / static_cast<float>(seg);
        auto uv = [&](float a) { return ImVec2(0.5f + 0.5f * std::cos(a - angle), 0.5f + 0.5f * std::sin(a - angle)); };
        dl->PrimVtx(c, ImVec2(0.5f, 0.5f), col);
        dl->PrimVtx(ImVec2(c.x + std::cos(a0) * r, c.y + std::sin(a0) * r), uv(a0), col);
        dl->PrimVtx(ImVec2(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r), uv(a1), col);
    }
    dl->PopTextureID();
}

void glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float strength, int steps) {
    float layer = 1.f - std::pow(1.f - clamp01(strength), 1.f / static_cast<float>(steps));
    for (int i = 0; i < steps; ++i) {
        float k = 1.f - static_cast<float>(i) / static_cast<float>(steps);
        dl->AddCircleFilled(c, r * k, alpha(col, layer), 40);
    }
}

void softShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float spread, float strength) {
    const int n = 7;
    for (int i = n; i >= 1; --i) {
        float e = spread * static_cast<float>(i) / n;
        float al = strength / n * (1.f - static_cast<float>(i - 1) / n);
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e + spread * 0.4f), ImVec2(b.x + e, b.y + e + spread * 0.4f), IM_COL32(0, 0, 0, static_cast<int>(clamp01(al) * 255)), rounding + e);
    }
}

void glowRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float spread, ImU32 col, float strength, float yOff) {
    if (strength <= 0.005f) return;
    const int n = 8;
    for (int i = n; i >= 1; --i) {
        float e = spread * static_cast<float>(i) / n;
        float al = strength / n * (1.f - static_cast<float>(i - 1) / n) * 1.6f;
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e + yOff), ImVec2(b.x + e, b.y + e + yOff), alpha(col, clamp01(al)), rounding + e);
    }
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    t = clamp01(t);
    auto ch = [&](int sh) {
        float x = static_cast<float>((a >> sh) & 0xFF), y = static_cast<float>((b >> sh) & 0xFF);
        return static_cast<ImU32>(x + (y - x) * t + 0.5f) << sh;
    };
    return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | ch(IM_COL32_A_SHIFT);
}

void drawBlobs(ImDrawList* dl, ImVec2 size, float t, const Palette& p, float strength) {
    float m = std::max(size.x, size.y);
    struct B { float off, l, a, r, px, py, ax, ay, per; } bs[3] = {
        {0.f, 0.52f, 0.5f, 0.62f, 0.12f, 0.02f, 0.16f, 0.12f, 26.f},
        {60.f, 0.50f, 0.32f, 0.55f, 0.92f, 0.34f, -0.14f, 0.16f, 32.f},
        {-50.f, 0.45f, 0.30f, 0.6f, 0.42f, 1.0f, 0.12f, -0.12f, 38.f}};
    for (auto& b : bs) {
        float k = 0.5f - 0.5f * std::cos(t * 2.f * kPi / b.per);
        ImVec2 c(size.x * (b.px + b.ax * k), size.y * (b.py + b.ay * k));
        glow(dl, c, m * b.r, u32(hsl(p.hue + b.off, p.sat, b.l)), b.a * strength);
    }
}

namespace {

// Волнистое пятно из угла corner: dir — куда «смотрит» угол внутрь окна (±1 по x и y), rx/ry — размеры.
void waveBlob(ImDrawList* dl, ImVec2 corner, ImVec2 dir, float rx, float ry, float ph, ImU32 col) {
    std::vector<ImVec2> pts;
    pts.push_back(corner - ImVec2(dir.x * 4.f, dir.y * 4.f));
    const int n = 64;
    for (int i = 0; i <= n; ++i) {
        float a = -0.08f + (kPi / 2 + 0.16f) * static_cast<float>(i) / n;  // от края по x до края по y
        float r = 1.f + 0.13f * std::sin(3.f * a + ph) + 0.06f * std::sin(7.f * a - ph * 1.3f);
        pts.push_back(ImVec2(corner.x + dir.x * std::cos(a) * rx * r, corner.y + dir.y * std::sin(a) * ry * r));
    }
    dl->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), col);
}

void dotGrid(ImDrawList* dl, ImVec2 p, int cols, int rows, float step, ImU32 col) {
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) dl->AddCircleFilled(p + ImVec2(x * step, y * step), step * 0.2f, col, 6);
}

void dashedCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    const int n = 36;
    for (int i = 0; i < n; i += 2) {
        float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
        dl->PathArcTo(c, r, a0, a1, 4);
        dl->PathStroke(col, ImDrawFlags_None, th);
    }
}

void cross(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th, bool plus) {
    if (plus) {
        dl->AddLine(c - ImVec2(s, 0), c + ImVec2(s, 0), col, th);
        dl->AddLine(c - ImVec2(0, s), c + ImVec2(0, s), col, th);
    } else {
        dl->AddLine(c - ImVec2(s, s), c + ImVec2(s, s), col, th);
        dl->AddLine(c + ImVec2(-s, s), c + ImVec2(s, -s), col, th);
    }
}

}  // namespace

void drawWaves(ImDrawList* dl, ImVec2 W, float t, const Palette& p, float k) {
    // размеры пятен — от ширины и высоты окна: занимают углы, середина остаётся спокойной
    float X = W.x * k, Y = W.y * k, s = std::min(W.x, W.y) * k, ph = t * 0.22f;
    ImU32 c1 = u32(hsl(p.hue, p.sat * 0.72f, 0.40f, 0.78f)), c2 = u32(hsl(p.hue, p.sat * 0.78f, 0.56f, 0.70f)),
          c3 = u32(hsl(p.hue, p.sat * 0.85f, 0.76f, 0.58f));
    ImVec2 tr(W.x, 0), bl(0, W.y);
    waveBlob(dl, tr, ImVec2(-1, 1), X * 0.40f, Y * 0.30f, ph, c1);
    waveBlob(dl, tr, ImVec2(-1, 1), X * 0.28f, Y * 0.21f, ph + 1.7f, c2);
    waveBlob(dl, tr, ImVec2(-1, 1), X * 0.16f, Y * 0.13f, ph + 3.1f, c3);
    waveBlob(dl, bl, ImVec2(1, -1), X * 0.42f, Y * 0.34f, -ph + 0.6f, c1);
    waveBlob(dl, bl, ImVec2(1, -1), X * 0.29f, Y * 0.24f, -ph + 2.4f, c2);
    waveBlob(dl, bl, ImVec2(1, -1), X * 0.16f, Y * 0.15f, -ph + 4.0f, c3);

    const ImU32 ln = white(0.45f), sym = white(0.55f), dots = white(0.42f);
    const float th = std::max(1.2f, s * 0.0022f), sw = std::max(5.f, s * 0.010f);
    float wob = std::sin(ph) * Y * 0.02f;
    // тонкие линии вдоль волн
    dl->AddBezierCubic(ImVec2(W.x - X * 0.50f, -4), ImVec2(W.x - X * 0.30f, Y * 0.30f + wob), ImVec2(W.x - X * 0.16f, Y * 0.04f),
                       ImVec2(W.x + 4, Y * 0.36f), ln, th, 48);
    dl->AddBezierCubic(ImVec2(-4, W.y - Y * 0.42f), ImVec2(X * 0.16f, W.y - Y * 0.50f - wob), ImVec2(X * 0.22f, W.y - Y * 0.06f),
                       ImVec2(X * 0.48f, W.y + 4), ln, th, 48);
    // пунктирные круги на светлых слоях
    dashedCircle(dl, ImVec2(W.x - X * 0.06f, Y * 0.02f), s * 0.10f, white(0.5f), th);
    dashedCircle(dl, ImVec2(X * 0.05f, W.y + Y * 0.01f), s * 0.11f, white(0.5f), th);
    // сетки точек
    float st = std::max(6.f, s * 0.012f);
    dotGrid(dl, ImVec2(W.x - X * 0.22f, Y * 0.20f), 8, 4, st, dots);
    dotGrid(dl, ImVec2(X * 0.24f, W.y - Y * 0.17f), 8, 4, st, dots);
    // значки
    dl->AddCircle(ImVec2(W.x - X * 0.33f, Y * 0.17f), sw, sym, 20, th);
    dl->AddCircle(ImVec2(X * 0.06f, W.y - Y * 0.48f), sw, sym, 20, th);
    cross(dl, ImVec2(W.x - X * 0.45f, Y * 0.08f), sw * 0.8f, sym, th, false);
    cross(dl, ImVec2(X * 0.20f, W.y - Y * 0.30f), sw * 0.8f, sym, th, false);
    cross(dl, ImVec2(W.x - X * 0.10f, Y * 0.30f), sw, sym, th, true);
    cross(dl, ImVec2(X * 0.40f, W.y - Y * 0.05f), sw, sym, th, true);
    // пунктирные вертикали у краёв
    for (int i = 0; i < 8; ++i) {
        dl->AddCircleFilled(ImVec2(W.x - X * 0.02f, Y * 0.40f + i * st * 0.9f), st * 0.17f, dots, 6);
        dl->AddCircleFilled(ImVec2(X * 0.015f, W.y - Y * 0.70f + i * st * 0.9f), st * 0.17f, dots, 6);
    }
}

namespace {

float hash1(int i) {
    unsigned h = static_cast<unsigned>(i) * 374761393u + 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.f;
}

float vnoise(float x) {  // плавный шум 0..1
    float fl = std::floor(x), f = x - fl;
    int i = static_cast<int>(fl);
    f = f * f * (3.f - 2.f * f);
    return lerp(hash1(i), hash1(i + 1), f);
}

void polyBand(ImDrawList* dl, const std::vector<ImVec2>& a, const std::vector<ImVec2>& b, ImU32 col) {
    std::vector<ImVec2> poly(a);
    poly.insert(poly.end(), b.rbegin(), b.rend());
    dl->AddConcavePolyFilled(poly.data(), static_cast<int>(poly.size()), col);
}

// Рваная бумага: тёмный лист с косой дырой, в дыре — цветная полоса, по краю — белые волокна и тень.
void drawTorn(ImDrawList* dl, ImVec2 W, const Palette& p) {
    float s = std::min(W.x, W.y);
    float half = W.y * 0.17f;
    int n = std::max(60, static_cast<int>(W.x / 4.f));
    std::vector<ImVec2> top, bot, ptop, pbot;
    for (int i = 0; i <= n; ++i) {
        float x = -12.f + (W.x + 24.f) * static_cast<float>(i) / static_cast<float>(n), u = x / s;
        float mid = W.y * (0.40f + 0.16f * x / W.x);
        float yT = mid - half + (vnoise(u * 5.f) - 0.5f) * s * 0.11f + (vnoise(u * 19.f + 3.f) - 0.5f) * s * 0.04f +
                   (hash1(i * 7 + 1) - 0.5f) * s * 0.007f;
        float yB = mid + half + (vnoise(u * 5.f + 40.f) - 0.5f) * s * 0.11f + (vnoise(u * 19.f + 71.f) - 0.5f) * s * 0.04f +
                   (hash1(i * 7 + 5) - 0.5f) * s * 0.007f;
        // белая кромка неровной ширины: местами почти нет, местами широкий клочок
        float w1 = s * (0.003f + 0.018f * std::pow(vnoise(u * 8.f + 11.f), 2.f)) + hash1(i * 5 + 2) * s * 0.004f;
        float w2 = s * (0.003f + 0.018f * std::pow(vnoise(u * 8.f + 23.f), 2.f)) + hash1(i * 5 + 3) * s * 0.004f;
        top.push_back(ImVec2(x, yT));
        bot.push_back(ImVec2(x, yB));
        ptop.push_back(ImVec2(x, yT - w1));
        pbot.push_back(ImVec2(x, yB + w2));
    }
    // мелкая фактура тёмного листа
    for (int i = 0; i < 260; ++i) {
        ImVec2 q(hash1(i * 3 + 101) * W.x, hash1(i * 3 + 102) * W.y);
        dl->AddCircleFilled(q, 0.8f + hash1(i * 3 + 103) * 1.2f, white(0.025f), 6);
    }
    // тень дыры на листе под ней (лист приподнят)
    for (int k = 4; k >= 1; --k) {
        float d = s * 0.006f * static_cast<float>(k);
        std::vector<ImVec2> a, b;
        for (auto q : ptop) a.push_back(q + ImVec2(0, d * 0.6f));
        for (auto q : pbot) b.push_back(q + ImVec2(0, d));
        polyBand(dl, a, b, IM_COL32(0, 0, 0, 22));
    }
    polyBand(dl, ptop, pbot, IM_COL32(118, 112, 108, 215));                  // светлые волокна (приглушены, чтобы не спорить с текстом)
    polyBand(dl, top, bot, u32(hsl(p.hue, p.sat * 0.70f, 0.21f)));            // цветная полоса (тёмная — поверх неё текст)
    // фактура полосы — редкая сетка точек
    float st = std::max(5.f, s * 0.008f);
    ImU32 grain = u32(hsl(p.hue, p.sat * 0.7f, 0.16f, 0.6f));
    for (float y = 0; y < W.y; y += st)
        for (float x = std::fmod(y, 2 * st) < st ? 0.f : st * 0.5f; x < W.x; x += st) {
            float mid = W.y * (0.40f + 0.16f * x / W.x);
            if (std::fabs(y - mid) < half * 0.75f) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.f, y + 1.f), grain);
        }
    // тень от верхнего края листа внутри полосы
    for (int k = 1; k <= 5; ++k) {
        float d = s * 0.007f * static_cast<float>(k);
        std::vector<ImVec2> a(top), b;
        for (auto q : top) b.push_back(q + ImVec2(0, d));
        polyBand(dl, a, b, IM_COL32(0, 0, 0, 26));
    }
    for (int k = 1; k <= 2; ++k) {
        float d = s * 0.004f * static_cast<float>(k);
        std::vector<ImVec2> a, b(bot);
        for (auto q : bot) a.push_back(q - ImVec2(0, d));
        polyBand(dl, a, b, IM_COL32(0, 0, 0, 22));
    }
}

// Полутон: точки в левом верхнем и правом нижнем углах, к середине мельчают; граница — плавная волна.
void drawHalftone(ImDrawList* dl, ImVec2 W, float t, const Palette& p) {
    float s = std::min(W.x, W.y), st = std::max(9.f, s * 0.016f), ph = t * 0.35f;
    int row = 0;
    for (float y = st * 0.5f; y < W.y + st; y += st * 0.88f, ++row) {
        float yy = y / W.y;
        float bL = W.x * (0.22f + 0.07f * std::sin(yy * 5.2f + ph)) * (1.25f - 0.55f * yy);
        float bR = W.x * (0.22f + 0.07f * std::sin(yy * 5.2f + 2.4f - ph)) * (0.70f + 0.55f * yy);
        for (float x = (row & 1) ? st * 0.5f : 0.f; x < W.x + st; x += st) {
            float f = std::max(1.f - x / bL, 1.f - (W.x - x) / bR);
            if (f <= 0.03f) continue;
            f = std::min(1.f, f);
            float r = st * 0.46f * std::pow(f, 0.75f);
            if (r < 0.6f) continue;
            dl->AddCircleFilled(ImVec2(x, y), r, u32(hsl(p.hue, p.sat, 0.22f + 0.30f * f, 0.35f + 0.6f * f)), r < 3.f ? 6 : 10);
        }
    }
}

// Прожектор: круг из точек, яркий в середине и гаснущий к краям; слегка «дышит».
void drawSpot(ImDrawList* dl, ImVec2 W, float t, const Palette& p) {
    float s = std::min(W.x, W.y), st = std::max(9.f, s * 0.015f);
    ImVec2 c(W.x * 0.56f, W.y * 0.5f);
    float R = std::max(W.x, W.y) * 0.52f * (1.f + 0.03f * std::sin(t * 1.6f));
    for (float y = std::fmod(c.y, st) - st; y < W.y + st; y += st)
        for (float x = std::fmod(c.x, st) - st; x < W.x + st; x += st) {
            float dx = x - c.x, dy = y - c.y;
            float f = 1.f - std::sqrt(dx * dx + dy * dy) / R;
            if (f <= 0.02f) continue;
            float r = st * 0.45f * std::pow(f, 0.55f);
            if (r < 0.6f) continue;
            // середина приглушена — поверх неё текст
            dl->AddCircleFilled(ImVec2(x, y), r, u32(hsl(p.hue, p.sat, 0.10f + 0.34f * f * f, 0.25f + 0.55f * f)), r < 3.f ? 6 : 10);
        }
}

}  // namespace

void drawDeco(ImDrawList* dl, ImVec2 W, float t, const Palette& p, int style) {
    switch (style) {
        case 1: drawWaves(dl, W, t, p); break;
        case 2: drawTorn(dl, W, p); break;
        case 3: drawHalftone(dl, W, t, p); break;
        case 4: drawSpot(dl, W, t, p); break;
        default: break;
    }
}

void drawBaguette(ImDrawList* dl, ImVec2 c, float len, float angle) {
    float hw = len * 0.14f, hl = len * 0.5f;
    std::vector<ImVec2> pts;
    auto rot = [&](float x, float y) { return ImVec2(c.x + x * std::cos(angle) - y * std::sin(angle), c.y + x * std::sin(angle) + y * std::cos(angle)); };
    for (int i = 0; i <= 8; ++i) { float a = -kPi / 2 + kPi * static_cast<float>(i) / 8.f; pts.push_back(rot(hl - hw + std::cos(a) * hw, std::sin(a) * hw)); }
    for (int i = 0; i <= 8; ++i) { float a = kPi / 2 + kPi * static_cast<float>(i) / 8.f; pts.push_back(rot(-hl + hw + std::cos(a) * hw, std::sin(a) * hw)); }
    dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), IM_COL32(224, 168, 92, 255));
    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), IM_COL32(168, 108, 48, 255), ImDrawFlags_Closed, std::max(1.f, len * 0.03f));
    for (int i = -1; i <= 1; ++i)
        dl->AddLine(rot(static_cast<float>(i) * hl * 0.45f - hw * 0.4f, hw * 0.55f), rot(static_cast<float>(i) * hl * 0.45f + hw * 0.4f, -hw * 0.55f), IM_COL32(168, 108, 48, 255), std::max(1.f, len * 0.035f));
}

}  // namespace mx::ui
