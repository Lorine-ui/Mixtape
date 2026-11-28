#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include "stb_image.h"

#include "textures.hpp"

#include <algorithm>
#include <memory>

namespace mx::ui {

namespace {

struct Decoded {
    std::vector<unsigned char> rgba;  // кадры подряд
    std::vector<int> delays;
    int w = 0, h = 0, frames = 0;
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

}  // namespace

Textures::~Textures() {
    for (auto& kv : covers_) if (kv.second.id) gfx_.destroy(kv.second.id);
    release(teto_);
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
    std::string px;
    const int S = Profile::kAvatarSide;
    if (b64.empty() || !base64Decode(b64, px) || px.size() != static_cast<std::size_t>(S) * S * 3u) return;
    std::vector<unsigned char> rgba(static_cast<std::size_t>(S) * S * 4u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(S) * S; ++i) {
        rgba[i * 4] = static_cast<unsigned char>(px[i * 3]);
        rgba[i * 4 + 1] = static_cast<unsigned char>(px[i * 3 + 1]);
        rgba[i * 4 + 2] = static_cast<unsigned char>(px[i * 3 + 2]);
        rgba[i * 4 + 3] = 255;
    }
    avatar_ = gfx_.create(rgba.data(), S, S);
}

void Textures::collect() {
    ++tick_;
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
