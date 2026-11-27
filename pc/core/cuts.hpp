// Вырезы и фрагменты: чистая логика воспроизведения без звука.
#pragma once
#include <vector>

#include "model.hpp"

namespace mx {

// Склеивает пересекающиеся куски, отбрасывает короче 0.2 с.
std::vector<Cut> mergeCuts(std::vector<Cut> cuts);

struct Segment {
    std::vector<Cut> cuts;         // что пропускать
    bool hasLimit = false;         // играть только [limS, limE] (режим «оставить»)
    double limS = 0, limE = 0;
    bool preview = false;          // предпросмотр в редакторе: не меняет полосу прогресса

    // Индекс выреза, в который попала позиция, либо -1.
    int cutAt(double t) const;
    // Куда встать при запуске: начало фрагмента или конец выреза, если он в самом начале.
    double startPos() const;
    // Видимая шкала: для «оставить» это сам отрезок, иначе весь трек.
    double visibleBase() const { return hasLimit && !preview ? limS : 0; }
    double visibleLength(double dur) const { return hasLimit && !preview ? limE - limS : dur; }
};

// Вырезы трека: свои (родительского трека) + вырезы клипа + сам клип в режиме «cut».
Segment buildSegment(const Track& t, const std::vector<Cut>& baseCuts, const std::vector<Cut>& ownCuts);

}  // namespace mx
