// Очередь воспроизведения: переход вперёд/назад, повтор, перемешивание, цепочка «случайных песен».
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace mx {

enum class Loop { Off = 0, All = 1, One = 2 };

struct Queue {
    std::vector<std::string> ids;
    int index = -1;
    Loop loop = Loop::Off;
    bool shuffle = false;
    bool randomChain = false;  // играет «случайная песня»: в конце запрашиваем следующую

    void set(std::vector<std::string> list, const std::string& current);
    const std::string* current() const;
};

struct StepResult {
    enum Kind { Nothing, Play, Stop, NeedRandom, NoPlayable } kind = Nothing;
    int index = -1;
};

// dir = +1/-1; automatic = true, когда трек закончился сам.
// playable(id) = можно ли играть прямо сейчас (например, нет интернета: только скачанное).
// rnd(n) возвращает число в [0,n).
StepResult step(Queue& q, int dir, bool automatic, const std::function<bool(const std::string&)>& playable,
                const std::function<int(int)>& rnd);

// Ближайший играбельный трек в направлении dir, либо -1.
int nextPlayable(const Queue& q, int dir, const std::function<bool(const std::string&)>& playable);

Loop cycleLoop(Loop l);

}  // namespace mx
