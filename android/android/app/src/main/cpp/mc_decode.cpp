// m4a/AAC и всё, чего не понимает miniaudio, — через системный декодер Android (MediaCodec).
// Та же функция, что mfDecode на Windows: на выходе стерео float 48 кГц.
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaDataSource.h>
#include <media/NdkMediaExtractor.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "player.hpp"

namespace mx {

namespace {
constexpr int kOutRate = 48000;

struct Mem {
    const std::string* bytes;
};

ssize_t readAt(void* ud, off64_t offset, void* buf, size_t size) {
    const std::string& b = *static_cast<Mem*>(ud)->bytes;
    if (offset < 0 || static_cast<std::size_t>(offset) >= b.size()) return -1;
    size_t n = std::min(size, b.size() - static_cast<std::size_t>(offset));
    std::memcpy(buf, b.data() + offset, n);
    return static_cast<ssize_t>(n);
}
ssize_t getSize(void* ud) { return static_cast<ssize_t>(static_cast<Mem*>(ud)->bytes->size()); }
void closeSrc(void*) {}

// Любое число каналов и частота -> стерео 48 кГц (линейная интерполяция: для музыки на слух достаточно)
void toStereo48k(const std::vector<float>& in, int ch, int rate, std::vector<float>& out) {
    if (ch <= 0 || rate <= 0 || in.empty()) return;
    std::size_t frames = in.size() / static_cast<std::size_t>(ch);
    std::vector<float> st(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        float l = in[i * ch], r = ch > 1 ? in[i * ch + 1] : l;
        st[i * 2] = l;
        st[i * 2 + 1] = r;
    }
    if (rate == kOutRate) { out = std::move(st); return; }
    double step = static_cast<double>(rate) / kOutRate;
    std::size_t outFrames = static_cast<std::size_t>(static_cast<double>(frames) / step);
    out.resize(outFrames * 2);
    for (std::size_t i = 0; i < outFrames; ++i) {
        double src = static_cast<double>(i) * step;
        std::size_t a = static_cast<std::size_t>(src);
        std::size_t b = std::min(a + 1, frames - 1);
        float t = static_cast<float>(src - static_cast<double>(a));
        out[i * 2] = st[a * 2] + (st[b * 2] - st[a * 2]) * t;
        out[i * 2 + 1] = st[a * 2 + 1] + (st[b * 2 + 1] - st[a * 2 + 1]) * t;
    }
}
}  // namespace

bool mfDecode(const std::string& bytes, const std::string&, std::vector<float>& pcm, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    Mem mem{&bytes};
    AMediaDataSource* src = AMediaDataSource_new();
    if (!src) return fail("Системный декодер недоступен");
    AMediaDataSource_setUserdata(src, &mem);
    AMediaDataSource_setReadAt(src, readAt);
    AMediaDataSource_setGetSize(src, getSize);
    AMediaDataSource_setClose(src, closeSrc);
    AMediaExtractor* ex = AMediaExtractor_new();
    struct Cleanup {
        AMediaDataSource* s; AMediaExtractor* e; AMediaCodec* c = nullptr;
        ~Cleanup() { if (c) { AMediaCodec_stop(c); AMediaCodec_delete(c); } AMediaExtractor_delete(e); AMediaDataSource_delete(s); }
    } guard{src, ex};
    if (AMediaExtractor_setDataSourceCustom(ex, src) != AMEDIA_OK) return fail("Не удалось прочитать звук этого формата");

    int track = -1;
    std::string mime;
    AMediaFormat* fmt = nullptr;
    for (size_t i = 0; i < AMediaExtractor_getTrackCount(ex); ++i) {
        AMediaFormat* f = AMediaExtractor_getTrackFormat(ex, i);
        const char* m = nullptr;
        if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m) && m && std::strncmp(m, "audio/", 6) == 0) {
            track = static_cast<int>(i);
            mime = m;
            fmt = f;
            break;
        }
        AMediaFormat_delete(f);
    }
    if (track < 0) return fail("В файле нет звуковой дорожки");
    AMediaExtractor_selectTrack(ex, static_cast<size_t>(track));
    int32_t rate = 44100, ch = 2;
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);

    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime.c_str());
    if (!codec) { AMediaFormat_delete(fmt); return fail("Телефон не умеет декодировать этот формат"); }
    guard.c = codec;
    bool configured = AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0) == AMEDIA_OK;
    AMediaFormat_delete(fmt);
    if (!configured || AMediaCodec_start(codec) != AMEDIA_OK) return fail("Не удалось запустить декодер");

    std::vector<float> raw;
    raw.reserve(static_cast<std::size_t>(bytes.size()) * 12);  // AAC ~ в 10-12 раз меньше PCM: меньше перевыделений
    bool inEnd = false, outEnd = false, isFloat = false;
    // Раньше на каждом кусочке ждали по 5 мс входа и выхода — песня декодировалась минуту и дольше
    // («Загрузка 100%» и тишина). Теперь за проход отдаём все свободные входные буферы и забираем все готовые
    // выходные без ожидания; коротко ждём (2 мс) только если за проход ничего не сдвинулось.
    int stall = 0;
    while (!outEnd && stall < 3000) {  // 3000 × 2 мс = 6 с без прогресса — декодер завис, сдаёмся
        bool progressed = false;
        while (!inEnd) {
            ssize_t ib = AMediaCodec_dequeueInputBuffer(codec, 0);
            if (ib < 0) break;
            size_t cap = 0;
            uint8_t* buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(ib), &cap);
            ssize_t n = AMediaExtractor_readSampleData(ex, buf, cap);
            if (n < 0) {
                AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(ib), 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                inEnd = true;
            } else {
                AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(ib), 0, static_cast<size_t>(n),
                                             static_cast<uint64_t>(AMediaExtractor_getSampleTime(ex)), 0);
                AMediaExtractor_advance(ex);
            }
            progressed = true;
        }
        for (;;) {
            AMediaCodecBufferInfo info;
            ssize_t ob = AMediaCodec_dequeueOutputBuffer(codec, &info, progressed ? 0 : 2000);
            if (ob >= 0) {
                progressed = true;
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(ob), &cap);
                if (buf && info.size > 0) {
                    const uint8_t* p = buf + info.offset;
                    if (isFloat) {
                        const float* f = reinterpret_cast<const float*>(p);
                        raw.insert(raw.end(), f, f + info.size / 4);
                    } else {
                        const int16_t* s16 = reinterpret_cast<const int16_t*>(p);
                        std::size_t n = static_cast<std::size_t>(info.size) / 2, old = raw.size();
                        raw.resize(old + n);
                        for (std::size_t i = 0; i < n; ++i) raw[old + i] = static_cast<float>(s16[i]) * (1.f / 32768.f);
                    }
                }
                AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(ob), false);
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) { outEnd = true; break; }
                continue;
            }
            if (ob == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* of = AMediaCodec_getOutputFormat(codec);
                AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
                AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
                int32_t enc = 2;  // 2 — PCM16, 4 — float
                if (AMediaFormat_getInt32(of, "pcm-encoding", &enc)) isFloat = enc == 4;
                AMediaFormat_delete(of);
                progressed = true;
                continue;
            }
            if (ob == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            break;  // готовых данных пока нет
        }
        stall = progressed ? 0 : stall + 1;
    }
    if (raw.empty()) return fail("Не удалось прочитать звук этого формата");
    toStereo48k(raw, ch, rate, pcm);
    return !pcm.empty();
}

}  // namespace mx
