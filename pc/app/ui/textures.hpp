// Картинки: обложки из интернета и картинка Тето (в том числе анимированная GIF).
// Скачивание и разбор идут в рабочих потоках, видеопамять заполняется в потоке интерфейса.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "app.hpp"
#include "imgui_inc.hpp"

namespace mx::ui {

// Реализуется платформой (DirectX 11).
class Gfx {
public:
    virtual ~Gfx() = default;
    virtual ImTextureID create(const unsigned char* rgba, int w, int h) = 0;
    virtual void destroy(ImTextureID id) = 0;
};

struct ImageSet {
    std::vector<ImTextureID> frames;
    std::vector<int> delaysMs;
    int w = 0, h = 0, total = 0;
    bool ok() const { return !frames.empty(); }
};

class Textures {
public:
    Textures(App& app, Gfx& gfx) : app_(app), gfx_(gfx) {}
    ~Textures();

    // 0, пока картинка грузится или её нет. Повторные вызовы дешёвые.
    ImTextureID cover(const std::string& url);

    void setTeto(const std::string& bytes);  // png, jpg или gif
    // Кадр анимации на момент timeSec (для статичной картинки всегда один).
    ImTextureID teto(double timeSec) const;
    float tetoAspect() const { return teto_.h > 0 ? static_cast<float>(teto_.w) / static_cast<float>(teto_.h) : 1.f; }
    bool hasTeto() const { return teto_.ok(); }

    // Аватарка. makeAvatar (в рабочем потоке): картинка из файла -> квадрат 128x128, RGB в base64; пусто — не картинка.
    static std::string makeAvatar(const std::string& fileBytes);
    void setAvatar(const std::string& base64Rgb);  // пусто — убрать
    ImTextureID avatar() const { return avatar_; }

    void collect();  // раз в кадр: освобождает давно не нужные обложки

private:
    struct Entry {
        ImTextureID id = 0;
        bool loading = false, failed = false;
        unsigned long long used = 0;
    };
    void release(ImageSet& s);

    App& app_;
    Gfx& gfx_;
    std::map<std::string, Entry> covers_;
    ImageSet teto_;
    ImTextureID avatar_ = 0;
    int inflight_ = 0;
    unsigned long long tick_ = 0;
};

}  // namespace mx::ui
