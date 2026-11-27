// Воспроизведение: miniaudio (устройство и декодеры mp3/wav/flac) + Mixer из ядра (вырезы, фрагмент, скорость).
// Песня целиком лежит в памяти, поэтому перемотка мгновенная, а вырезы точные.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "cuts.hpp"

namespace mx {

class Player {
public:
    Player();
    ~Player();

    bool start(std::string* err);  // открывает звуковое устройство
    // Загружает песню из байтов. Пауза, пока не вызовут play(). ext — подсказка формата (mp3, m4a...).
    bool load(std::string bytes, const std::string& ext, const Segment& seg, double startSec, std::string* err);

    void play();
    void pause();
    bool playing() const;
    bool ended() const;  // дошло до конца (или до конца фрагмента); сбрасывается load() и seek()

    double position() const;  // секунды в исходном треке
    double duration() const;
    void seek(double sec);
    void setSegment(const Segment& seg);
    void setVolume(float v);
    void setRate(double r);

    // Копия байтов загруженной песни (для огибающей громкости): копируется быстро и под замком.
    bool copyBytes(std::string& bytes, std::string& ext) const;

    // Огибающая громкости для редактора фрагментов: buckets чисел 0..1. Можно вызывать из рабочего потока.
    static std::vector<float> peaks(const std::string& bytes, const std::string& ext, int buckets);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

#ifdef _WIN32
// Media Foundation: m4a/aac/wma и всё, что не понял miniaudio. Стерео, float, 48 кГц.
bool mfDecode(const std::string& bytes, const std::string& ext, std::vector<float>& pcm, std::string* err);
#endif

}  // namespace mx
