#include "rank.hpp"

#include <algorithm>
#include <set>

#include "text.hpp"

namespace mx {

double relScore(const Track& t, const std::string& q) {
    std::string nq = normTitle(q);
    std::vector<std::string> tk;
    for (auto& w : split(nq, ' ')) if (!w.empty()) tk.push_back(w);
    if (tk.empty()) return 0;
    std::string nt = normTitle(t.title), na = normTitle(t.artist);
    std::string all = nt + " " + na;
    int hit = 0;
    for (const auto& w : tk) if (contains(all, w)) ++hit;
    double s = static_cast<double>(hit) / static_cast<double>(tk.size()) * 10;
    if (nt == nq) s += 4;
    else if (!nq.empty() && contains(nt, nq)) s += 2;
    if (!na.empty() && contains(nq, na)) s += 1;
    return s;
}

double matchScore(const Track& x, const std::string& title, const std::string& artist) {
    std::string nt = normTitle(title);
    if (nt.empty()) return 0;
    std::string a0 = artist.substr(0, artist.find(','));
    std::string na = normTitle(a0), xt = normTitle(x.title), xa = normTitle(x.artist);
    double s = 0;
    if (xt == nt) s += 3;
    else if (contains(xt, nt) || (xt.size() > 3 && contains(nt, xt))) s += 2;
    else return 0;
    if (na.empty()) s += 1;
    else if (contains(xa, na) || contains(na, xa) || contains(xt, na)) s += 2;
    return s;
}

int sourceOrder(const std::string& s) {
    static const char* order[] = {"audius", "soundcloud", "archive", "youtube"};
    for (int i = 0; i < 4; ++i) if (s == order[i]) return i;
    return 9;
}

std::vector<Track> mergeResults(std::vector<SearchHit> remote, const std::vector<Track>& local,
                                const std::unordered_map<std::string, bool>& inLibrary) {
    for (auto& h : remote) h.score -= h.rank * 0.12 + sourceOrder(h.track.source) * 0.05;
    std::stable_sort(remote.begin(), remote.end(), [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });

    std::vector<Track> out;
    std::set<std::string> seen;
    auto push = [&](const Track& t) {
        std::string artist = t.artist.substr(0, t.artist.find(','));
        std::string k = normTitle(t.title) + "|" + normTitle(artist);
        bool lib = inLibrary.count(t.id) != 0;
        if (seen.count(k) && !lib) return;
        seen.insert(k);
        out.push_back(t);
    };
    for (const auto& t : local) push(t);
    for (const auto& h : remote) push(h.track);
    return out;
}

}  // namespace mx
