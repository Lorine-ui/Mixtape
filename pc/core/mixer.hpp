// Сердце воспроизведения без привязки к звуковой карте: читает звук из источника, пропускает вырезы
// с точностью до отсчёта, ограничивает фрагмент, меняет скорость и громкость. Источник и устройство
// подключает приложение, а проверяется всё это тестами на подставном источнике.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

#include "cuts.hpp"

namespace mx {

// Стерео, float, чередование каналов (L R L R ...), частота дискретизации у источника и микшера одна.
class ISource {
public:
    virtual ~ISource() = default;
    virtual uint64_t read(float* out, uint64_t frames) = 0;  // возвращает прочитанное; 0 — конец
    virtual bool seek(uint64_t frame) = 0;
    virtual uint64_t length() const = 0;  // 0, если неизвестно
};

class Mixer {
public:
    Mixer(ISource& src, uint32_t sampleRate);

    void setSegment(const Segment& seg) { seg_ = seg; }
    void setRate(double r);            // 0.5 .. 2.0; высота тона меняется вместе со скоростью, как у пластинки
    void setVolume(float v) { volume_ = v < 0 ? 0 : v > 1 ? 1 : v; }
    void seek(double sec);

    // Всегда заполняет все `frames`; то, что не набралось (конец, пауза), — тишина. Возвращает число настоящих кадров.
    uint64_t render(float* out, uint64_t frames);

    double position() const { return pos_.load(); }  // секунды в исходном треке
    double duration() const { return dur_; }
    bool ended() const { return ended_.load(); }
    void clearEnded() { ended_ = false; }

private:
    bool jumpTo(uint64_t frame);
    void fill(uint64_t needFrames);
    uint64_t bufFrames() const { return buf_.size() / 2; }

    ISource& src_;
    uint32_t sr_;
    Segment seg_;
    double rate_ = 1;
    float volume_ = 1, gain_ = 1;
    double dur_ = 0;

    std::vector<float> buf_;   // исходные кадры, ещё не использованные целиком
    uint64_t bufStart_ = 0;    // номер исходного кадра, с которого начинается buf_
    double phase_ = 0;         // положение внутри buf_ (дробный кадр)
    bool eof_ = false;
    int rampIn_ = 0;
    std::atomic<double> pos_{0};
    std::atomic<bool> ended_{false};
};

}  // namespace mx
