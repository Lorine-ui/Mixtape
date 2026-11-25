// Тесты микшера на подставном источнике: значение отсчёта равно его номеру, поэтому ошибку видно сразу.
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "mixer.hpp"

using namespace mx;

static int g_fail = 0, g_all = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_all;                                                              \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)
#define NEAR(a, b, eps) (std::fabs((a) - (b)) <= (eps))

static void run(const char* name, const std::function<void()>& f) {
    int before = g_fail;
    f();
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name);
}

struct Ramp : ISource {
    uint64_t len, pos = 0;
    explicit Ramp(uint64_t n) : len(n) {}
    uint64_t read(float* out, uint64_t frames) override {
        uint64_t n = 0;
        while (n < frames && pos < len) { out[n * 2] = out[n * 2 + 1] = static_cast<float>(pos); ++pos; ++n; }
        return n;
    }
    bool seek(uint64_t f) override { pos = f; return true; }
    uint64_t length() const override { return len; }
};

// Прогоняет микшер до конца, возвращает левый канал.
static std::vector<float> drain(Mixer& m, uint64_t chunk, uint64_t maxFrames = 100000) {
    std::vector<float> all, buf(chunk * 2);
    uint64_t total = 0;
    while (!m.ended() && total < maxFrames) {
        uint64_t got = m.render(buf.data(), chunk);
        for (uint64_t i = 0; i < got; ++i) all.push_back(buf[i * 2]);
        total += chunk;
    }
    return all;
}

int main() {
    run("обычное воспроизведение", [] {
        Ramp s(5000);
        Mixer m(s, 1000);
        auto o = drain(m, 300);
        CHECK(o.size() >= 4998 && o.size() <= 5001);
        CHECK(NEAR(o[10], 10, 0.01));
        CHECK(NEAR(o[2500], 2500, 0.01));
        CHECK(NEAR(o[4999 - 1], 4998, 1.01));
        CHECK(m.ended());
        CHECK(NEAR(m.duration(), 5.0, 1e-9));
    });

    run("вырез пропускается точно", [] {
        Ramp s(5000);
        Mixer m(s, 1000);
        Segment seg;
        seg.cuts = {{2.0, 3.0}};
        m.setSegment(seg);
        auto o = drain(m, 256);
        CHECK(o.size() >= 3995 && o.size() <= 4005);
        CHECK(NEAR(o[1500], 1500, 0.01));   // до выреза
        CHECK(NEAR(o[2100], 3100, 0.01));   // после выреза: сразу 3.1 с исходного трека
        CHECK(NEAR(o[3900], 4900, 0.01));
        // из выреза не просочилось ничего (в окне затухания и плавного входа значения умножаются на коэффициент, его пропускаем)
        for (std::size_t i = 0; i < o.size(); ++i)
            if (i < 1930 || i > 2100) CHECK(!(o[i] > 1999.f && o[i] < 3000.f));
    });

    run("вырез не зависит от размера буфера", [] {
        std::vector<float> a, b;
        for (int pass = 0; pass < 2; ++pass) {
            Ramp s(5000);
            Mixer m(s, 1000);
            Segment seg;
            seg.cuts = {{1.0, 1.5}, {3.0, 3.5}};
            m.setSegment(seg);
            (pass ? b : a) = drain(m, pass ? 37 : 1024);
        }
        CHECK(std::abs(static_cast<long>(a.size()) - static_cast<long>(b.size())) <= 2);
        // вдали от границ (там короткое затухание зависит от буфера) данные совпадают
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            bool near = false;
            for (double edge : {1000.0, 2500.0}) if (std::fabs(static_cast<double>(i) - edge) < 200) near = true;
            if (!near) CHECK(NEAR(a[i], b[i], 0.5));
        }
    });

    run("фрагмент «оставить»", [] {
        Ramp s(5000);
        Mixer m(s, 1000);
        Segment seg;
        seg.hasLimit = true; seg.limS = 1.0; seg.limE = 2.0;
        m.setSegment(seg);
        m.seek(seg.startPos());
        auto o = drain(m, 200);
        CHECK(o.size() >= 998 && o.size() <= 1002);
        CHECK(NEAR(o[300], 1300, 0.01));
        CHECK(m.ended());
    });

    run("вырез до самого конца заканчивает трек", [] {
        Ramp s(5000);
        Mixer m(s, 1000);
        Segment seg;
        seg.cuts = {{4.0, 5.0}};
        m.setSegment(seg);
        auto o = drain(m, 500);
        CHECK(o.size() >= 3998 && o.size() <= 4002);
        CHECK(m.ended());
    });

    run("скорость 2x и 0.5x", [] {
        Ramp s(4000);
        Mixer m(s, 1000);
        m.setRate(2.0);
        auto o = drain(m, 128);
        CHECK(o.size() >= 1995 && o.size() <= 2005);
        CHECK(NEAR(o[500], 1000, 0.5));
        Ramp s2(2000);
        Mixer m2(s2, 1000);
        m2.setRate(0.5);
        auto o2 = drain(m2, 128);
        CHECK(o2.size() >= 3995 && o2.size() <= 4005);
        CHECK(NEAR(o2[1001], 500.5, 0.5));  // линейная интерполяция
    });

    run("перемотка и позиция", [] {
        Ramp s(5000);
        Mixer m(s, 1000);
        std::vector<float> buf(400);
        m.render(buf.data(), 200);
        CHECK(NEAR(m.position(), 0.2, 0.002));
        m.seek(3.0);
        auto o = drain(m, 100);
        CHECK(o.size() >= 1995 && o.size() <= 2005);
        CHECK(NEAR(o[200], 3200, 0.01));
        CHECK(NEAR(o[0], 0, 0.01));  // плавный вход, а не щелчок
        CHECK(o[10] < o[90]);
    });

    run("громкость плавно меняется", [] {
        Ramp s(8000);
        Mixer m(s, 1000);
        m.setVolume(0.5f);
        auto o = drain(m, 512);
        CHECK(NEAR(o[7000] / 7000.0, 0.5, 0.01));
        CHECK(o[10] / 10.0 > 0.9);  // в начале ещё почти полная громкость: изменение не скачком
    });

    run("пауза без звука: render заполняет тишиной", [] {
        Ramp s(100);
        Mixer m(s, 1000);
        std::vector<float> buf(2000, 9.f);
        uint64_t got = m.render(buf.data(), 1000);
        CHECK(got <= 101);
        CHECK(buf[1998] == 0.f);  // хвост после конца обнулён
    });

    std::printf("\nПроверок: %d, провалов: %d\n", g_all, g_fail);
    return g_fail ? 1 : 0;
}
