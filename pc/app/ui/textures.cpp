#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include "stb_image.h"

#include "textures.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace mx::ui {

namespace {

struct Decoded {
    std::vector<unsigned char> rgba;  // кадры подряд
    std::vector<int> delays;
    int w = 0, h = 0, frames = 0;
    bool tooBig = false;  // GIF отклонён из-за размера
};

// Крупные картинки уменьшаем на месте: обложке хватает 640 пикселей.
void shrink(Decoded& d, int maxSide) {
    int m = std::max(d.w, d.h);
    if (m <= maxSide) return;
    int f = (m + maxSide - 1) / maxSide;
    int nw = d.w / f, nh = d.h / f;
    std::vector<unsigned char> out(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4u * static_cast<std::size_t>(d.frames));
    for (int k = 0; k < d.frames; ++k)
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                const unsigned char* s = &d.rgba[((static_cast<std::size_t>(k) * static_cast<std::size_t>(d.h) + static_cast<std::size_t>(y * f)) * static_cast<std::size_t>(d.w) + static_cast<std::size_t>(x * f)) * 4u];
                unsigned char* o = &out[((static_cast<std::size_t>(k) * static_cast<std::size_t>(nh) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(nw) + static_cast<std::size_t>(x)) * 4u];
                o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3];
            }
    d.rgba = std::move(out);
    d.w = nw;
    d.h = nh;
}

bool decode(const std::string& bytes, bool allowGif, Decoded& d) {
    const stbi_uc* p = reinterpret_cast<const stbi_uc*>(bytes.data());
    int len = static_cast<int>(bytes.size());
    if (allowGif && bytes.size() > 6 && bytes.compare(0, 3, "GIF") == 0) {
        int* delays = nullptr;
        int x = 0, y = 0, z = 0, comp = 0;
        stbi_uc* data = stbi_load_gif_from_memory(p, len, &delays, &x, &y, &z, &comp, 4);
        if (!data) return false;
        d.w = x; d.h = y; d.frames = z;
        d.rgba.assign(data, data + static_cast<std::size_t>(x) * static_cast<std::size_t>(y) * 4u * static_cast<std::size_t>(z));
        for (int i = 0; i < z; ++i) d.delays.push_back(delays ? std::max(20, delays[i]) : 100);
        stbi_image_free(data);
        STBI_FREE(delays);
        if (z > 60) return false;  // слишком длинная гифка съест видеопамять
    } else {
        int x = 0, y = 0, comp = 0;
        stbi_uc* data = stbi_load_from_memory(p, len, &x, &y, &comp, 4);
        if (!data) return false;
        d.w = x; d.h = y; d.frames = 1;
        d.rgba.assign(data, data + static_cast<std::size_t>(x) * static_cast<std::size_t>(y) * 4u);
        d.delays = {1000};
        stbi_image_free(data);
    }
    shrink(d, allowGif ? 480 : 640);
    return d.w > 0 && d.h > 0;
}

// Сколько кадров в GIF, не распаковывая его: считаем блоки изображений по структуре файла.
int gifFrameCount(const std::string& b) {
    auto at = [&](std::size_t i) { return static_cast<unsigned char>(b[i]); };
    auto skipBlocks = [&](std::size_t& p) {  // цепочка подблоков: длина, данные... до нулевой длины
        while (p < b.size()) {
            std::size_t n = at(p++);
            if (n == 0) break;
            p += n;
        }
    };
    if (b.size() < 14) return 0;
    std::size_t p = 13;
    if (at(10) & 0x80) p += 3u * (1u << ((at(10) & 7) + 1));
    int frames = 0;
    while (p < b.size()) {
        unsigned char c = at(p++);
        if (c == 0x3B) break;  // конец файла
        if (c == 0x21) {  // расширение: метка и подблоки
            ++p;
            skipBlocks(p);
        } else if (c == 0x2C) {  // изображение: описание, своя палитра, код сжатия и подблоки
            if (p + 9 > b.size()) break;
            unsigned char f = at(p + 8);
            p += 9;
            if (f & 0x80) p += 3u * (1u << ((f & 7) + 1));
            ++p;
            skipBlocks(p);
            ++frames;
        } else {
            break;
        }
    }
    return frames;
}

// Уменьшает кадр в f раз усреднением блоков f×f (без «лесенки»).
void boxScale(const unsigned char* src, int w, int h, int f, unsigned char* dst) {
    const int nw = w / f, nh = h / f;
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            unsigned acc[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < f; ++dy) {
                const unsigned char* s = src + (static_cast<std::size_t>(y * f + dy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x * f)) * 4u;
                for (int dx = 0; dx < f; ++dx, s += 4) { acc[0] += s[0]; acc[1] += s[1]; acc[2] += s[2]; acc[3] += s[3]; }
            }
            unsigned char* o = dst + (static_cast<std::size_t>(y) * static_cast<std::size_t>(nw) + static_cast<std::size_t>(x)) * 4u;
            const unsigned n = static_cast<unsigned>(f * f);
            o[0] = static_cast<unsigned char>(acc[0] / n); o[1] = static_cast<unsigned char>(acc[1] / n);
            o[2] = static_cast<unsigned char>(acc[2] / n); o[3] = static_cast<unsigned char>(acc[3] / n);
        }
}

// Фон: картинка до 2048 пикселей по большей стороне. GIF разбирается по кадрам, и каждый кадр сразу уменьшается так, чтобы все
// кадры вместе занимали не больше ~160 МБ: память не растёт с числом кадров (stbi_load_gif_from_memory держал бы все кадры
// целиком, да ещё дважды). Здесь используются внутренности stb_image (stbi__gif_load_next), они лежат в этом же файле.
bool decodeBackground(const std::string& bytes, Decoded& d) {
    const stbi_uc* p = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int len = static_cast<int>(bytes.size());
    if (bytes.size() > 14 && bytes.compare(0, 3, "GIF") == 0) {
        const int w = static_cast<unsigned char>(bytes[6]) | (static_cast<unsigned char>(bytes[7]) << 8);
        const int h = static_cast<unsigned char>(bytes[8]) | (static_cast<unsigned char>(bytes[9]) << 8);
        const int frames = gifFrameCount(bytes);
        if (w <= 0 || h <= 0 || frames <= 0) return false;
        if (frames > 400) { d.tooBig = true; return false; }
        const double scale = std::sqrt(160e6 / (4.0 * frames) / (static_cast<double>(w) * h));
        int maxSide = std::max(w, h);
        if (scale < 1.0) maxSide = std::max(64, static_cast<int>(maxSide * scale));
        maxSide = std::min(maxSide, 1280);
        const int f = std::max(1, (std::max(w, h) + maxSide - 1) / maxSide);
        const int nw = std::max(1, w / f), nh = std::max(1, h / f);

        stbi__context ctx;
        stbi__start_mem(&ctx, p, len);
        if (!stbi__gif_test(&ctx)) return false;
        stbi__gif* g = static_cast<stbi__gif*>(STBI_MALLOC(sizeof(stbi__gif)));
        if (!g) return false;
        std::memset(g, 0, sizeof(*g));
        std::vector<unsigned char> saved;  // кадр, от которого считается следующий (как в stb: не «вернуть прежний»)
        const unsigned char* savedPtr = nullptr;
        int comp = 0, count = 0;
        bool ok = true;
        d.rgba.reserve(static_cast<std::size_t>(frames) * static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4u);
        for (;;) {
            stbi_uc* px = stbi__gif_load_next(&ctx, g, &comp, 4, const_cast<stbi_uc*>(savedPtr));
            if (!px) { ok = false; break; }
            if (px == reinterpret_cast<stbi_uc*>(&ctx)) break;  // конец анимации
            if (g->w != w || g->h != h || ++count > frames + 1) { ok = false; break; }
            const std::size_t stride = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
            const std::size_t at = d.rgba.size();
            d.rgba.resize(at + static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4u);
            if (f == 1) std::memcpy(&d.rgba[at], px, stride);
            else boxScale(px, w, h, f, &d.rgba[at]);
            d.delays.push_back(std::max(20, g->delay));
            if ((g->eflags & 0x1c) != 0x0c) {
                saved.assign(px, px + stride);
                savedPtr = saved.data();
            }
        }
        STBI_FREE(g->out);
        STBI_FREE(g);
        if (!ok || count == 0) return false;
        d.w = nw; d.h = nh; d.frames = count;
        d.delays.resize(static_cast<std::size_t>(count));
    } else {
        int x = 0, y = 0, comp = 0;
        stbi_uc* data = stbi_load_from_memory(p, len, &x, &y, &comp, 4);
        if (!data) return false;
        d.w = x; d.h = y; d.frames = 1;
        d.rgba.assign(data, data + static_cast<std::size_t>(x) * static_cast<std::size_t>(y) * 4u);
        d.delays = {1000};
        stbi_image_free(data);
        shrink(d, 2048);
    }
    return d.w > 0 && d.h > 0;
}

}  // namespace

// Кадры фона, которые ещё предстоит отправить в видеопамять.
struct Textures::BgJob {
    Decoded d;
    int next = 0;
};

Textures::~Textures() {
    for (auto& kv : covers_) if (kv.second.id) gfx_.destroy(kv.second.id);
    release(teto_);
    release(bg_);
    if (avatar_) gfx_.destroy(avatar_);
}

void Textures::release(ImageSet& s) {
    for (auto f : s.frames) gfx_.destroy(f);
    s = ImageSet();
}

ImTextureID Textures::cover(const std::string& url) {
    if (url.empty() || url.compare(0, 4, "http") != 0) return 0;
    auto it = covers_.find(url);
    if (it != covers_.end()) { it->second.used = tick_; return it->second.id; }
    if (inflight_ >= 6) return 0;  // подождём: не загружаем всё сразу
    ++inflight_;
    Entry& e = covers_[url];
    e.loading = true;
    e.used = tick_;
    app_.fetchUrl(url, [this, url](std::string body) {
        if (body.empty()) { --inflight_; covers_[url].failed = true; covers_[url].loading = false; return; }
        app_.async([this, url, body = std::move(body)] {
            auto d = std::make_shared<Decoded>();
            bool ok = decode(body, false, *d);
            app_.post([this, url, d, ok] {
                --inflight_;
                Entry& en = covers_[url];
                en.loading = false;
                if (!ok) { en.failed = true; return; }
                en.id = gfx_.create(d->rgba.data(), d->w, d->h);
            });
        });
    });
    return 0;
}

void Textures::setTeto(const std::string& bytes) {
    if (bytes.empty()) return;
    app_.async([this, bytes] {
        auto d = std::make_shared<Decoded>();
        bool ok = decode(bytes, true, *d);
        app_.post([this, d, ok] {
            if (!ok) { app_.toast("Не удалось прочитать картинку"); return; }
            release(teto_);
            std::size_t frameBytes = static_cast<std::size_t>(d->w) * static_cast<std::size_t>(d->h) * 4u;
            for (int i = 0; i < d->frames; ++i) {
                teto_.frames.push_back(gfx_.create(d->rgba.data() + static_cast<std::size_t>(i) * frameBytes, d->w, d->h));
                teto_.delaysMs.push_back(d->delays[static_cast<std::size_t>(i)]);
                teto_.total += d->delays[static_cast<std::size_t>(i)];
            }
            teto_.w = d->w;
            teto_.h = d->h;
        });
    });
}

void Textures::setBackground(const std::string& bytes) {
    ++bgGen_;
    release(bg_);
    bgJob_.reset();
    if (bytes.empty()) return;
    const int gen = bgGen_;
    app_.async([this, bytes, gen] {
        auto j = std::make_shared<BgJob>();
        bool ok = decodeBackground(bytes, j->d);
        app_.post([this, j, ok, gen] {
            if (gen != bgGen_) return;  // пока разбирали, выбрали другую картинку или убрали фон
            if (!ok) { app_.toast(j->d.tooBig ? "GIF слишком тяжёлый: уменьшите размер или число кадров" : "Не удалось прочитать картинку"); return; }
            bg_.w = j->d.w;
            bg_.h = j->d.h;
            bgJob_ = j;
        });
    });
}

ImTextureID Textures::background(double timeSec) const {
    if (bg_.frames.empty()) return 0;
    if (bgJob_ || bg_.frames.size() == 1 || bg_.total <= 0) return bg_.frames[0];  // ещё догружается или картинка одна
    int t = static_cast<int>(static_cast<long long>(timeSec * 1000.0) % bg_.total);
    for (std::size_t i = 0; i < bg_.frames.size(); ++i) {
        if (t < bg_.delaysMs[i]) return bg_.frames[i];
        t -= bg_.delaysMs[i];
    }
    return bg_.frames[0];
}

ImTextureID Textures::teto(double timeSec) const {
    if (!teto_.ok()) return 0;
    if (teto_.frames.size() == 1 || teto_.total <= 0) return teto_.frames[0];
    int t = static_cast<int>(static_cast<long long>(timeSec * 1000.0) % teto_.total);
    for (std::size_t i = 0; i < teto_.frames.size(); ++i) {
        if (t < teto_.delaysMs[i]) return teto_.frames[i];
        t -= teto_.delaysMs[i];
    }
    return teto_.frames[0];
}

std::string Textures::makeAvatar(const std::string& bytes) {
    int x = 0, y = 0, comp = 0;
    stbi_uc* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &x, &y, &comp, 4);
    if (!data) return {};
    // квадрат из середины, уменьшаем усреднением (без «лесенки»), прозрачность — на тёмный фон
    const int S = Profile::kAvatarSide, side = std::min(x, y), ox = (x - side) / 2, oy = (y - side) / 2;
    std::string px(static_cast<std::size_t>(S) * S * 3u, '\0');
    for (int ty = 0; ty < S; ++ty)
        for (int tx = 0; tx < S; ++tx) {
            int x0 = ox + tx * side / S, x1 = std::max(x0 + 1, ox + (tx + 1) * side / S);
            int y0 = oy + ty * side / S, y1 = std::max(y0 + 1, oy + (ty + 1) * side / S);
            double acc[3] = {0, 0, 0};
            int n = 0;
            for (int sy = y0; sy < y1 && sy < y; ++sy)
                for (int sx = x0; sx < x1 && sx < x; ++sx) {
                    const stbi_uc* s = data + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(x) + static_cast<std::size_t>(sx)) * 4u;
                    double al = s[3] / 255.0;
                    acc[0] += s[0] * al + 20 * (1 - al);
                    acc[1] += s[1] * al + 18 * (1 - al);
                    acc[2] += s[2] * al + 30 * (1 - al);
                    ++n;
                }
            for (int c = 0; c < 3; ++c)
                px[(static_cast<std::size_t>(ty) * S + static_cast<std::size_t>(tx)) * 3u + static_cast<std::size_t>(c)] =
                    static_cast<char>(static_cast<unsigned char>(n ? acc[c] / n + 0.5 : 0));
        }
    stbi_image_free(data);
    return base64Encode(px);
}

void Textures::setAvatar(const std::string& b64) {
    if (avatar_) { gfx_.destroy(avatar_); avatar_ = 0; }
    avatar_ = createAvatar(b64);
}

ImTextureID Textures::createAvatar(const std::string& b64) {
    std::string px;
    const int S = Profile::kAvatarSide;
    if (b64.empty() || !base64Decode(b64, px) || px.size() != static_cast<std::size_t>(S) * S * 3u) return 0;
    std::vector<unsigned char> rgba(static_cast<std::size_t>(S) * S * 4u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(S) * S; ++i) {
        rgba[i * 4] = static_cast<unsigned char>(px[i * 3]);
        rgba[i * 4 + 1] = static_cast<unsigned char>(px[i * 3 + 1]);
        rgba[i * 4 + 2] = static_cast<unsigned char>(px[i * 3 + 2]);
        rgba[i * 4 + 3] = 255;
    }
    return gfx_.create(rgba.data(), S, S);
}

void Textures::collect() {
    ++tick_;
    if (bgJob_) {  // кадры фона уходят в видеопамять по несколько штук за кадр
        const std::size_t frameBytes = static_cast<std::size_t>(bgJob_->d.w) * static_cast<std::size_t>(bgJob_->d.h) * 4u;
        for (int made = 0; made < 6 && bgJob_->next < bgJob_->d.frames; ++made, ++bgJob_->next) {
            bg_.frames.push_back(gfx_.create(bgJob_->d.rgba.data() + static_cast<std::size_t>(bgJob_->next) * frameBytes, bgJob_->d.w, bgJob_->d.h));
            bg_.delaysMs.push_back(bgJob_->d.delays[static_cast<std::size_t>(bgJob_->next)]);
            bg_.total += bgJob_->d.delays[static_cast<std::size_t>(bgJob_->next)];
        }
        if (bgJob_->next >= bgJob_->d.frames) bgJob_.reset();
    }
    if (covers_.size() < 400) return;
    // выбрасываем самые давние обложки, чтобы не раздувать видеопамять
    std::vector<std::pair<unsigned long long, std::string>> order;
    for (auto& kv : covers_) if (!kv.second.loading) order.push_back({kv.second.used, kv.first});
    std::sort(order.begin(), order.end());
    for (std::size_t i = 0; i < order.size() / 2; ++i) {
        auto it = covers_.find(order[i].second);
        if (it->second.id) gfx_.destroy(it->second.id);
        covers_.erase(it);
    }
}

}  // namespace mx::ui
