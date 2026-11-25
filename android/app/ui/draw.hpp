// Рисование: цвета, плавные величины, иконки, текст и свечение. Всё через ImDrawList, шрифты с символами не нужны.
#pragma once
#include <string>

#include "imgui_inc.hpp"

namespace mx::ui {

struct Fonts {
    ImFont* regular = nullptr;   // запечены в 18 px: для обычного текста
    ImFont* bold = nullptr;
    ImFont* regularL = nullptr;  // запечены в 34 px: для заголовков (можно nullptr)
    ImFont* boldL = nullptr;
};

// Плавно догоняет цель (экспоненциальное сглаживание, не зависит от частоты кадров).
struct Anim {
    float v = 0, t = 0;
    void set(float target) { t = target; }
    void snap(float x) { v = t = x; }
    void step(float dt, float speed = 12.f);
};

struct Palette {
    float hue = 265, sat = 0.95f;
    ImU32 acc = 0, acc2 = 0, bg = 0;
    ImVec4 accV{}, acc2V{}, bgV{};
};

ImVec4 hsl(float hueDeg, float s, float l, float a = 1.f);
ImU32 u32(const ImVec4& c);
ImU32 alpha(ImU32 c, float a);   // умножает прозрачность
ImU32 white(float a = 1.f);
float clamp01(float x);
float easeOut(float t);          // быстрое начало, мягкая остановка
float lerp(float a, float b, float t);
float hueDist(float from, float to);  // кратчайшая разность по кругу, градусы

// ---------- текст ----------
// Один раз при запуске: крупные версии шрифтов. Каждый кадр: множитель размера текста и масштаб интерфейса
// (нужен, чтобы выбрать шрифт по реальному размеру в пикселях).
void setTextFonts(const Fonts& f);
void setTextScale(float textScale, float uiScale);
float textScale();
void drawText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const std::string& s, float wrap = 0);
ImVec2 measure(ImFont* f, float size, const std::string& s, float wrap = 0);
std::string ellipsize(ImFont* f, float size, const std::string& s, float maxW);

// ---------- иконки ----------
enum class Icon {
    Play, Pause, Next, Prev, Shuffle, Loop, LoopOne, Heart, HeartFilled, Plus, Download, Check, Close, Search,
    Scissors, Note, Dice, Gear, Home, Files, Trash, Volume, Import, Expand, Shrink, Save, Menu, Back, Chevron, Chart, Sparkle, User
};
void icon(ImDrawList* dl, Icon ic, ImVec2 center, float size, ImU32 col);
void setIconBackdrop(ImU32 bg);   // раз в кадр: цвет фона, чтобы полупрозрачные линии иконок не давали пятен на стыках

// ---------- картинки и свечение ----------
void textureCircle(ImDrawList* dl, ImTextureID tex, ImVec2 c, float r, float angle, ImU32 col, int seg = 64);
void glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float strength, int steps = 12);
void softShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float spread, float strength);
// Мягкое цветное свечение вокруг скруглённого прямоугольника (для кнопок), со сдвигом вниз yOff.
void glowRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float spread, ImU32 col, float strength, float yOff = 0.f);
ImU32 mix(ImU32 a, ImU32 b, float t);   // смешивание двух цветов
void drawBlobs(ImDrawList* dl, ImVec2 size, float t, const Palette& p, float strength);
// Узор ярких тем: волнистые слои в двух углах, тонкие линии, пунктирный круг, сетки точек, кружки, крестики, плюсы.
void drawWaves(ImDrawList* dl, ImVec2 size, float t, const Palette& p, float scale = 1.f);
// Узор темы по номеру (ThemeDef::deco): 1 — волны, 2 — рваная бумага, 3 — полутон в углах, 4 — прожектор из точек.
void drawDeco(ImDrawList* dl, ImVec2 size, float t, const Palette& p, int style);
void drawBaguette(ImDrawList* dl, ImVec2 c, float len, float angle);

}  // namespace mx::ui
