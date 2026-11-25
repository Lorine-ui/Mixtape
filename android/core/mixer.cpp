#include "mixer.hpp"

#include <algorithm>
#include <cmath>

namespace mx {

namespace {
constexpr int kRamp = 96;       // кадров плавного входа после перехода (убирает щелчок)
constexpr int kFadeOut = 64;    // кадров затухания перед вырезом
}  // namespace

Mixer::Mixer(ISource& src, uint32_t sampleRate) : src_(src), sr_(sampleRate ? sampleRate : 48000) {
    dur_ = static_cast<double>(src.length()) / sr_;
}

void Mixer::setRate(double r) { rate_ = std::min(2.0, std::max(0.5, r)); }

bool Mixer::jumpTo(uint64_t frame) {
    uint64_t len = src_.length();
    if ((len && frame >= len) || !src_.seek(frame)) {
        ended_ = true;
        return false;
    }
    buf_.clear();
    bufStart_ = frame;
    phase_ = 0;
    eof_ = false;
    rampIn_ = kRamp;
    return true;
}

void Mixer::seek(double sec) {
    ended_ = false;
    if (sec < 0) sec = 0;
    if (jumpTo(static_cast<uint64_t>(sec * sr_ + 0.5))) pos_ = sec;
}

void Mixer::fill(uint64_t need) {
    while (bufFrames() < need && !eof_) {
        uint64_t chunk = std::max<uint64_t>(need - bufFrames(), 1024);
        std::size_t old = buf_.size();
        buf_.resize(old + static_cast<std::size_t>(chunk) * 2);
        uint64_t got = src_.read(buf_.data() + old, chunk);
        buf_.resize(old + static_cast<std::size_t>(got) * 2);
        if (got == 0) eof_ = true;
    }
}

uint64_t Mixer::render(float* out, uint64_t frames) {
    std::fill(out, out + frames * 2, 0.0f);
    uint64_t produced = 0;
    for (int guard = 0; produced < frames && !ended_.load() && guard < 64; ++guard) {
        const double posF = static_cast<double>(bufStart_) + phase_;
        const double posS = posF / sr_;

        int c = seg_.cutAt(posS);
        if (c >= 0) {  // попали в вырез: перескакиваем на его конец
            jumpTo(static_cast<uint64_t>(seg_.cuts[static_cast<std::size_t>(c)].e * sr_ + 0.5));
            continue;
        }
        if (seg_.hasLimit && posS >= seg_.limE) {
            ended_ = true;
            break;
        }

        // до ближайшей границы (начало выреза или конец фрагмента) идём без остановки
        double boundary = 1e300;
        for (const auto& cu : seg_.cuts) {
            double s = cu.s * sr_;
            if (s > posF && s < boundary) boundary = s;
        }
        if (seg_.hasLimit) boundary = std::min(boundary, seg_.limE * sr_);

        uint64_t n = frames - produced;
        bool atBoundary = false;
        if (boundary < 1e299) {
            double need = (boundary - posF) / rate_;
            uint64_t nb = need <= 1 ? 1 : static_cast<uint64_t>(std::ceil(need));
            if (nb <= n) { n = nb; atBoundary = true; }
        }

        fill(static_cast<uint64_t>(phase_ + static_cast<double>(n) * rate_) + 3);

        uint64_t made = 0;
        for (; made < n; ++made) {
            uint64_t idx = static_cast<uint64_t>(phase_);
            if (idx >= bufFrames()) { ended_ = true; break; }  // источник кончился
            double fr = phase_ - static_cast<double>(idx);
            uint64_t j = idx + 1 < bufFrames() ? idx + 1 : idx;
            gain_ += (volume_ - gain_) * 0.002f;
            float g = gain_;
            if (rampIn_ > 0) { g *= 1.0f - static_cast<float>(rampIn_) / kRamp; --rampIn_; }
            float* o = out + (produced + made) * 2;
            for (int ch = 0; ch < 2; ++ch) {
                float a = buf_[idx * 2 + static_cast<uint64_t>(ch)], b = buf_[j * 2 + static_cast<uint64_t>(ch)];
                o[ch] = static_cast<float>(a + (b - a) * fr) * g;
            }
            phase_ += rate_;
        }
        if (atBoundary && made == n) {
            uint64_t k = std::min<uint64_t>(kFadeOut, n);
            for (uint64_t i = 0; i < k; ++i) {
                float f = static_cast<float>(i) / kFadeOut;  // i = 0 — последний кадр, он затихает до нуля
                float* o = out + (produced + n - 1 - i) * 2;
                o[0] *= f;
                o[1] *= f;
            }
        }

        uint64_t drop = static_cast<uint64_t>(phase_);
        if (drop > 0) {
            if (drop >= bufFrames()) buf_.clear();
            else buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(drop * 2));
            bufStart_ += drop;
            phase_ -= static_cast<double>(drop);
        }
        produced += made;
    }
    pos_ = (static_cast<double>(bufStart_) + phase_) / sr_;
    return produced;
}

}  // namespace mx
