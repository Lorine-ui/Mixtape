#include "queue.hpp"

#include <algorithm>

namespace mx {

void Queue::set(std::vector<std::string> list, const std::string& current) {
    ids = std::move(list);
    randomChain = false;
    auto it = std::find(ids.begin(), ids.end(), current);
    index = it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
}

const std::string* Queue::current() const {
    if (index < 0 || index >= static_cast<int>(ids.size())) return nullptr;
    return &ids[static_cast<std::size_t>(index)];
}

int nextPlayable(const Queue& q, int dir, const std::function<bool(const std::string&)>& playable) {
    int n = static_cast<int>(q.ids.size());
    for (int k = 1; k <= n; ++k) {
        int i = ((q.index + dir * k) % n + n) % n;
        if (playable(q.ids[static_cast<std::size_t>(i)])) return i;
    }
    return -1;
}

StepResult step(Queue& q, int dir, bool automatic, const std::function<bool(const std::string&)>& playable,
                const std::function<int(int)>& rnd) {
    int n = static_cast<int>(q.ids.size());
    if (n == 0) return {q.randomChain && dir > 0 ? StepResult::NeedRandom : StepResult::Nothing, -1};
    if (q.randomChain && dir > 0 && n == 1) return {StepResult::NeedRandom, -1};

    int i = (q.shuffle && dir > 0) ? rnd(n) : q.index + dir;
    if (i >= n) {
        if (q.loop == Loop::All || !automatic) i = 0;
        else return {StepResult::Stop, -1};
    }
    if (i < 0) i = n - 1;
    if (!playable(q.ids[static_cast<std::size_t>(i)])) {
        Queue probe = q;
        probe.index = i;
        // ищем дальше от выбранной позиции; сам i уже не годится
        int j = nextPlayable(probe, dir == 0 ? 1 : dir, playable);
        if (j < 0) return {StepResult::NoPlayable, -1};
        i = j;
    }
    q.index = i;
    return {StepResult::Play, i};
}

Loop cycleLoop(Loop l) { return static_cast<Loop>((static_cast<int>(l) + 1) % 3); }

}  // namespace mx
