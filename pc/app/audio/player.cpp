#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "player.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#include "mixer.hpp"

namespace mx {

namespace {
constexpr ma_uint32 kRate = 48000;

// Сырые float-данные упаковываем в WAV (заголовок 44 байта): так их читает обычный декодер miniaudio.
std::string wrapWavF32(const std::vector<float>& pcm, unsigned channels, unsigned rate) {
    std::string w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); };
    auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) w.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); };
    uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(float));
    w += "RIFF"; u32(36 + dataBytes); w += "WAVEfmt "; u32(16);
    u16(3); u16(static_cast<uint16_t>(channels)); u32(rate); u32(rate * channels * 4); u16(static_cast<uint16_t>(channels * 4)); u16(32);
    w += "data"; u32(dataBytes);
    w.append(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return w;
}

// Одна загруженная песня: байты, декодер и микшер живут вместе.
struct Loaded : ISource {
    std::string bytes, ext;
    std::string wav;  // если декодировал Media Foundation: PCM в обёртке WAV
    ma_decoder dec{};
    bool decOk = false;
    std::unique_ptr<Mixer> mixer;

    ~Loaded() override {
        if (decOk) ma_decoder_uninit(&dec);
    }
    uint64_t read(float* out, uint64_t frames) override {
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&dec, out, frames, &got);
        return got;
    }
    bool seek(uint64_t frame) override { return ma_decoder_seek_to_pcm_frame(&dec, frame) == MA_SUCCESS; }
    uint64_t length() const override {
        ma_uint64 n = 0;
        if (ma_decoder_get_length_in_pcm_frames(const_cast<ma_decoder*>(&dec), &n) != MA_SUCCESS) return 0;
        return n;
    }
};

bool openDecoder(Loaded& t, const std::string& ext, std::string* err) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, kRate);
    if (ma_decoder_init_memory(t.bytes.data(), t.bytes.size(), &cfg, &t.dec) == MA_SUCCESS) {
        t.decOk = true;
        return true;
    }
#ifdef _WIN32
    std::string e2;
    std::vector<float> pcm;
    if (mfDecode(t.bytes, ext, pcm, &e2) && !pcm.empty()) {
        t.wav = wrapWavF32(pcm, 2, kRate);
        if (ma_decoder_init_memory(t.wav.data(), t.wav.size(), &cfg, &t.dec) == MA_SUCCESS) {
            t.decOk = true;
            return true;
        }
    }
    if (err) *err = e2.empty() ? "Не удалось прочитать звук этого формата" : e2;
#else
    (void)ext;
    if (err) *err = "Не удалось прочитать звук этого формата";
#endif
    return false;
}
}  // namespace

struct Player::Impl {
    ma_device device{};
    bool deviceOk = false;
    std::mutex mu;
    std::unique_ptr<Loaded> cur;
    std::atomic<bool> playing{false};
    std::atomic<double> pos{0}, dur{0};
    std::atomic<bool> ended{false}, has{false};
    float volume = 0.8f;
    double rate = 1;

    static void callback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
        auto* d = static_cast<Impl*>(dev->pUserData);
        float* o = static_cast<float*>(out);
        if (!d->playing.load() || !d->mu.try_lock()) {
            std::fill(o, o + static_cast<std::size_t>(frames) * 2, 0.0f);
            return;
        }
        std::lock_guard<std::mutex> lk(d->mu, std::adopt_lock);
        if (!d->cur) {
            std::fill(o, o + static_cast<std::size_t>(frames) * 2, 0.0f);
            return;
        }
        d->cur->mixer->render(o, frames);
        d->pos = d->cur->mixer->position();
        if (d->cur->mixer->ended()) d->ended = true;
    }
};

Player::Player() : d_(new Impl) {}

Player::~Player() {
    if (d_->deviceOk) ma_device_uninit(&d_->device);
}

bool Player::start(std::string* err) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kRate;
    cfg.dataCallback = &Impl::callback;
    cfg.pUserData = d_.get();
    cfg.performanceProfile = ma_performance_profile_conservative;
    if (ma_device_init(nullptr, &cfg, &d_->device) != MA_SUCCESS) {
        if (err) *err = "Не удалось открыть звуковое устройство";
        return false;
    }
    d_->deviceOk = true;
    if (ma_device_start(&d_->device) != MA_SUCCESS) {
        if (err) *err = "Не удалось запустить звук";
        return false;
    }
    return true;
}

bool Player::load(std::string bytes, const std::string& ext, const Segment& seg, double startSec, std::string* err) {
    auto t = std::make_unique<Loaded>();
    t->bytes = std::move(bytes);
    t->ext = ext;
    if (!openDecoder(*t, ext, err)) return false;
    t->mixer = std::make_unique<Mixer>(*t, kRate);
    t->mixer->setSegment(seg);
    t->mixer->setVolume(d_->volume);
    t->mixer->setRate(d_->rate);
    t->mixer->seek(startSec);
    std::lock_guard<std::mutex> lk(d_->mu);
    d_->cur = std::move(t);
    d_->dur = d_->cur->mixer->duration();
    d_->pos = startSec;
    d_->ended = false;
    d_->has = true;
    return true;
}

bool Player::copyBytes(std::string& bytes, std::string& ext) const {
    std::lock_guard<std::mutex> lk(d_->mu);
    if (!d_->cur) return false;
    bytes = d_->cur->bytes;
    ext = d_->cur->ext;
    return true;
}

void Player::play() { d_->playing = true; }
void Player::pause() { d_->playing = false; }
bool Player::playing() const { return d_->playing.load() && d_->has.load(); }
bool Player::ended() const { return d_->ended.load(); }
double Player::position() const { return d_->pos.load(); }
double Player::duration() const { return d_->dur.load(); }

void Player::seek(double sec) {
    std::lock_guard<std::mutex> lk(d_->mu);
    if (!d_->cur) return;
    d_->cur->mixer->seek(sec);
    d_->pos = d_->cur->mixer->position();
    d_->ended = false;
}

void Player::setSegment(const Segment& seg) {
    std::lock_guard<std::mutex> lk(d_->mu);
    if (d_->cur) d_->cur->mixer->setSegment(seg);
}

void Player::setVolume(float v) {
    std::lock_guard<std::mutex> lk(d_->mu);
    d_->volume = v;
    if (d_->cur) d_->cur->mixer->setVolume(v);
}

void Player::setRate(double r) {
    std::lock_guard<std::mutex> lk(d_->mu);
    d_->rate = r;
    if (d_->cur) d_->cur->mixer->setRate(r);
}

std::vector<float> Player::peaks(const std::string& bytes, const std::string& ext, int buckets) {
    std::vector<float> out(static_cast<std::size_t>(std::max(1, buckets)), 0.0f);
    constexpr ma_uint32 kPeakRate = 8000;
    ma_decoder dec{};
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, kPeakRate);
    std::vector<float> mfPcm;
    std::string wav;
    bool ok = ma_decoder_init_memory(bytes.data(), bytes.size(), &cfg, &dec) == MA_SUCCESS;
#ifdef _WIN32
    if (!ok && mfDecode(bytes, ext, mfPcm, nullptr) && !mfPcm.empty()) {
        // из стерео 48 кГц делаем моно: для огибающей хватит каждого 6-го кадра
        std::vector<float> mono;
        mono.reserve(mfPcm.size() / 12 + 1);
        for (std::size_t i = 0; i + 1 < mfPcm.size(); i += 12) mono.push_back(0.5f * (mfPcm[i] + mfPcm[i + 1]));
        wav = wrapWavF32(mono, 1, kPeakRate);  // данные должны жить, пока открыт декодер
        ok = ma_decoder_init_memory(wav.data(), wav.size(), &cfg, &dec) == MA_SUCCESS;
    }
#else
    (void)ext;
#endif
    if (!ok) return out;
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) != MA_SUCCESS || total == 0) {
        ma_decoder_uninit(&dec);
        return out;
    }
    std::vector<float> buf(4096);
    ma_uint64 done = 0;
    for (;;) {
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&dec, buf.data(), buf.size(), &got);
        if (got == 0) break;
        for (ma_uint64 i = 0; i < got; ++i) {
            std::size_t b = static_cast<std::size_t>((done + i) * out.size() / total);
            if (b >= out.size()) b = out.size() - 1;
            out[b] = std::max(out[b], std::fabs(buf[i]));
        }
        done += got;
    }
    ma_decoder_uninit(&dec);
    float mx = 0;
    for (float v : out) mx = std::max(mx, v);
    if (mx > 0) for (auto& v : out) v = std::sqrt(v / mx);  // корень: тихие места тоже видны
    return out;
}

}  // namespace mx
