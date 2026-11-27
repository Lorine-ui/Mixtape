#include "cuts.hpp"

#include <algorithm>

namespace mx {

std::vector<Cut> mergeCuts(std::vector<Cut> in) {
    std::vector<Cut> s;
    for (const auto& c : in) if (c.e - c.s > 0.2) s.push_back(c);
    std::sort(s.begin(), s.end(), [](const Cut& a, const Cut& b) { return a.s < b.s; });
    std::vector<Cut> o;
    for (const auto& c : s) {
        if (!o.empty() && c.s <= o.back().e + 0.05) o.back().e = std::max(o.back().e, c.e);
        else o.push_back(c);
    }
    return o;
}

int Segment::cutAt(double t) const {
    for (std::size_t i = 0; i < cuts.size(); ++i)
        if (t >= cuts[i].s && t < cuts[i].e - 0.05) return static_cast<int>(i);
    return -1;
}

double Segment::startPos() const {
    double p = hasLimit ? limS : 0;
    for (int k = 0; k < 4; ++k) {
        int c = cutAt(p);
        if (c < 0) break;
        p = cuts[static_cast<std::size_t>(c)].e;
    }
    return p;
}

Segment buildSegment(const Track& t, const std::vector<Cut>& baseCuts, const std::vector<Cut>& ownCuts) {
    Segment s;
    std::vector<Cut> all = baseCuts;
    all.insert(all.end(), ownCuts.begin(), ownCuts.end());
    if (t.clip && t.mode == "cut") all.push_back({t.start, t.end});
    s.cuts = mergeCuts(all);
    if (t.clip && t.mode != "cut") { s.hasLimit = true; s.limS = t.start; s.limE = t.end; }
    return s;
}

}  // namespace mx
