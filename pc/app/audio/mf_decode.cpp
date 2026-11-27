// Декодирование через Media Foundation (входит в Windows): m4a/aac от YouTube и другие форматы,
// которых нет в miniaudio. Результат: стерео float 48 кГц.
#ifndef _WIN32
#error "mf_decode.cpp собирается только под Windows"
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include "player.hpp"

namespace mx {

using Microsoft::WRL::ComPtr;

namespace {
struct ComScope {
    bool com = false, mf = false;
    ComScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com = SUCCEEDED(hr);  // S_FALSE тоже успех: поток уже был инициализирован
        mf = SUCCEEDED(MFStartup(MF_VERSION));
    }
    ~ComScope() {
        if (mf) MFShutdown();
        if (com) CoUninitialize();
    }
};
}  // namespace

// ---------- «нарезанный» mp4 (DASH) -> обычный AAC (ADTS) ----------
// YouTube отдаёт звук фрагментированным mp4 (ftyp «dash», потом пары moof+mdat). Media Foundation такой файл
// открывает, но не находит в нём ни одного сэмпла («В файле нет звука»). Те же AAC-кадры в обёртке ADTS он
// читает без проблем, поэтому достаём кадры из фрагментов и складываем их друг за другом.
namespace {

struct Mp4Box {
    size_t start = 0, hdr = 8, end = 0;  // end — первый байт после коробки
    uint32_t type = 0;
};

uint32_t be32(const std::string& d, size_t o) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(d[o])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(d[o + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(d[o + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(d[o + 3]));
}
uint64_t be64(const std::string& d, size_t o) { return (static_cast<uint64_t>(be32(d, o)) << 32) | be32(d, o + 4); }
constexpr uint32_t fourcc(const char (&c)[5]) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(c[0])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(c[1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(c[2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(c[3]));
}

// Следующая коробка на [pos, end); false — дальше нет или размер некорректный.
bool nextBox(const std::string& d, size_t pos, size_t end, Mp4Box& b) {
    if (pos + 8 > end) return false;
    uint64_t size = be32(d, pos);
    b.start = pos;
    b.type = be32(d, pos + 4);
    b.hdr = 8;
    if (size == 1) {
        if (pos + 16 > end) return false;
        size = be64(d, pos + 8);
        b.hdr = 16;
    } else if (size == 0) {
        size = end - pos;
    }
    if (size < b.hdr || size > end - pos) return false;
    b.end = pos + static_cast<size_t>(size);
    return true;
}

// Ищет коробку нужного типа среди дочерних на [begin, end), заходя в контейнеры moov/trak/mdia/minf/stbl/stsd/mp4a.
bool findDeep(const std::string& d, size_t begin, size_t end, uint32_t type, Mp4Box& out, int depth = 0) {
    if (depth > 8) return false;
    Mp4Box b;
    for (size_t pos = begin; nextBox(d, pos, end, b); pos = b.end) {
        if (b.type == type) { out = b; return true; }
        size_t inner = b.start + b.hdr;
        if (b.type == fourcc("moov") || b.type == fourcc("trak") || b.type == fourcc("mdia") || b.type == fourcc("minf") || b.type == fourcc("stbl")) {
            if (findDeep(d, inner, b.end, type, out, depth + 1)) return true;
        } else if (b.type == fourcc("stsd")) {
            if (findDeep(d, inner + 8, b.end, type, out, depth + 1)) return true;   // версия/флаги + число записей
        } else if (b.type == fourcc("mp4a")) {
            if (findDeep(d, inner + 28, b.end, type, out, depth + 1)) return true;  // фиксированная часть аудио-записи
        }
    }
    return false;
}

// Длина дескриптора MPEG-4 (1-4 байта по 7 бит).
bool descLen(const std::string& d, size_t& pos, size_t end, size_t& len) {
    len = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= end) return false;
        unsigned char c = static_cast<unsigned char>(d[pos++]);
        len = (len << 7) | (c & 0x7F);
        if (!(c & 0x80)) return true;
    }
    return false;
}

// AudioSpecificConfig из коробки esds: тип объекта, индекс частоты, число каналов.
bool readAsc(const std::string& d, const Mp4Box& esds, int& aot, int& freqIdx, int& chans) {
    size_t pos = esds.start + esds.hdr + 4, end = esds.end;  // версия/флаги
    size_t len;
    if (pos >= end || static_cast<unsigned char>(d[pos]) != 0x03) return false;  // ES_Descriptor
    ++pos;
    if (!descLen(d, pos, end, len)) return false;
    pos += 2;                                                    // ES_ID
    if (pos >= end) return false;
    unsigned char fl = static_cast<unsigned char>(d[pos++]);
    if (fl & 0x80) pos += 2;
    if (fl & 0x40) { if (pos >= end) return false; pos += 1 + static_cast<unsigned char>(d[pos]); }
    if (fl & 0x20) pos += 2;
    if (pos >= end || static_cast<unsigned char>(d[pos]) != 0x04) return false;  // DecoderConfigDescriptor
    ++pos;
    if (!descLen(d, pos, end, len)) return false;
    pos += 13;                                                   // тип, поток, буфер, битрейты
    if (pos >= end || static_cast<unsigned char>(d[pos]) != 0x05) return false;  // DecoderSpecificInfo
    ++pos;
    if (!descLen(d, pos, end, len) || len < 2 || pos + 2 > end) return false;
    unsigned v = (static_cast<unsigned char>(d[pos]) << 8) | static_cast<unsigned char>(d[pos + 1]);
    aot = static_cast<int>(v >> 11);
    freqIdx = static_cast<int>((v >> 7) & 0xF);
    chans = static_cast<int>((v >> 3) & 0xF);
    return true;
}

bool findDeepChild(const std::string& d, const Mp4Box& parent, uint32_t type, Mp4Box& out) {
    Mp4Box b;
    for (size_t pos = parent.start + parent.hdr; nextBox(d, pos, parent.end, b); pos = b.end)
        if (b.type == type) { out = b; return true; }
    return false;
}

bool fmp4ToAdts(const std::string& d, std::string& out) {
    // только если это действительно фрагментированный файл
    bool hasMoof = false;
    {
        Mp4Box b;
        for (size_t pos = 0; nextBox(d, pos, d.size(), b); pos = b.end)
            if (b.type == fourcc("moof")) { hasMoof = true; break; }
    }
    if (!hasMoof) return false;
    Mp4Box moov, esds;
    if (!findDeep(d, 0, d.size(), fourcc("moov"), moov)) return false;
    if (!findDeep(d, moov.start + moov.hdr, moov.end, fourcc("esds"), esds)) return false;
    int aot = 0, freqIdx = 0, chans = 0;
    if (!readAsc(d, esds, aot, freqIdx, chans)) return false;
    if (aot < 1 || aot > 4 || freqIdx > 12 || chans < 1 || chans > 7) return false;  // нужен обычный AAC (LC и т.п.)

    std::string res;
    res.reserve(d.size() + d.size() / 50);
    Mp4Box top;
    for (size_t pos = 0; nextBox(d, pos, d.size(), top); pos = top.end) {
        if (top.type != fourcc("moof")) continue;
        Mp4Box traf;
        if (!findDeepChild(d, top, fourcc("traf"), traf)) continue;
        uint32_t defSize = 0;
        uint64_t base = top.start;
        Mp4Box tfhd;
        if (findDeepChild(d, traf, fourcc("tfhd"), tfhd)) {
            size_t o = tfhd.start + tfhd.hdr;
            if (o + 8 > tfhd.end) return false;
            uint32_t fl = be32(d, o) & 0xFFFFFF;
            o += 8;  // версия/флаги + номер дорожки
            if (fl & 0x1) { if (o + 8 > tfhd.end) return false; base = be64(d, o); o += 8; }
            if (fl & 0x2) o += 4;
            if (fl & 0x8) o += 4;
            if (fl & 0x10) { if (o + 4 > tfhd.end) return false; defSize = be32(d, o); o += 4; }
        }
        uint64_t cursor = top.end + 8;  // запасной вариант: сразу за заголовком следующей mdat
        Mp4Box tr;
        for (size_t pos2 = traf.start + traf.hdr; nextBox(d, pos2, traf.end, tr); pos2 = tr.end) {
            if (tr.type != fourcc("trun")) continue;
            size_t o = tr.start + tr.hdr;
            if (o + 8 > tr.end) return false;
            uint32_t fl = be32(d, o) & 0xFFFFFF, count = be32(d, o + 4);
            o += 8;
            uint64_t at = cursor;
            if (fl & 0x1) {
                if (o + 4 > tr.end) return false;
                int32_t off = static_cast<int32_t>(be32(d, o));
                o += 4;
                at = base + static_cast<int64_t>(off);
            }
            if (fl & 0x4) o += 4;
            for (uint32_t i = 0; i < count; ++i) {
                if (fl & 0x100) o += 4;
                uint32_t sz = defSize;
                if (fl & 0x200) { if (o + 4 > tr.end) return false; sz = be32(d, o); o += 4; }
                if (fl & 0x400) o += 4;
                if (fl & 0x800) o += 4;
                if (o > tr.end || sz == 0 || at + sz > d.size()) return false;
                uint32_t frame = sz + 7;
                if (frame > 0x1FFF) return false;
                unsigned char h[7] = {0xFF, 0xF1,
                                      static_cast<unsigned char>(((aot - 1) << 6) | (freqIdx << 2) | (chans >> 2)),
                                      static_cast<unsigned char>(((chans & 3) << 6) | (frame >> 11)),
                                      static_cast<unsigned char>((frame >> 3) & 0xFF),
                                      static_cast<unsigned char>(((frame & 7) << 5) | 0x1F), 0xFC};
                res.append(reinterpret_cast<const char*>(h), 7);
                res.append(d, static_cast<size_t>(at), sz);
                at += sz;
            }
            cursor = at;
        }
    }
    if (res.size() < 64) return false;
    out = std::move(res);
    return true;
}

}  // namespace

bool mfDecode(const std::string& bytesIn, const std::string& extIn, std::vector<float>& pcm, std::string* err) {
    std::string converted;
    const bool flat = (extIn == "m4a" || extIn == "mp4") && fmp4ToAdts(bytesIn, converted);
    const std::string& bytes = flat ? converted : bytesIn;
    const std::string ext = flat ? std::string("aac") : extIn;
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    ComScope scope;
    if (!scope.mf) return fail("Media Foundation недоступен");

    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
    if (!stream) return fail("Не хватило памяти для декодера");
    ComPtr<IMFByteStream> bs;
    if (FAILED(MFCreateMFByteStreamOnStream(stream.Get(), &bs))) return fail("Не удалось открыть звук");
    ComPtr<IMFAttributes> attr;
    if (SUCCEEDED(bs.As(&attr))) {
        const wchar_t* type = (ext == "m4a" || ext == "mp4") ? L"audio/mp4" : ext == "aac" ? L"audio/aac" : ext == "flac" ? L"audio/flac" : L"audio/mpeg";
        attr->SetString(MF_BYTESTREAM_CONTENT_TYPE, type);
    }

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromByteStream(bs.Get(), nullptr, &reader))) return fail("Windows не умеет читать этот формат звука");
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    ComPtr<IMFMediaType> want;
    MFCreateMediaType(&want);
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    want->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    want->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    want->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
    want->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 8);
    want->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.Get())))
        return fail("Не удалось настроить декодер звука");

    pcm.clear();
    for (;;) {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIndex, &flags, &ts, &sample)))
            return fail("Ошибка при декодировании звука");
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf))) continue;
        BYTE* data = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
            const float* f = reinterpret_cast<const float*>(data);
            pcm.insert(pcm.end(), f, f + len / sizeof(float));
            buf->Unlock();
        }
    }
    if (pcm.size() < 2) return fail("В файле нет звука");
    return true;
}

}  // namespace mx
