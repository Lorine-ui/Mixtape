// Декодирование через Media Foundation (входит в Windows): m4a/aac от YouTube и другие форматы,
// которых нет в miniaudio. Результат: стерео float 48 кГц.
#ifndef _WIN32
#error "mf_decode.cpp собирается только под Windows"
#endif
#include <windows.h>
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

bool mfDecode(const std::string& bytes, const std::string& ext, std::vector<float>& pcm, std::string* err) {
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
