// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "Decode.h"

#include <algorithm>
#include <cctype>
#include <cstring>

// miniaudio: decode-only, no device I/O, no generation, no runtime linking.
// Warning quarantine: third-party code is exempt from our /WX rule.
#define MA_NO_DEVICE_IO
#define MA_NO_GENERATION
#define MA_NO_RUNTIME_LINKING
#define MINIAUDIO_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#endif

namespace lean {
namespace {

bool hasExtension(const std::string& path, const char* ext) {
    if (path.size() < strlen(ext))
        return false;
    std::string tail = path.substr(path.size() - strlen(ext));
    for (char& c : tail)
        c = (char)tolower((unsigned char)c);
    return tail == ext;
}

bool isAacContainer(const std::string& path) {
    return hasExtension(path, ".m4a") || hasExtension(path, ".aac")
        || hasExtension(path, ".mp4") || hasExtension(path, ".m4b");
}

bool decodeWithMiniAudio(const void* fileBytes, size_t fileSize,
                         DecodedAudio& out, std::string& error) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 0);
    ma_decoder decoder;
    if (ma_decoder_init_memory(fileBytes, fileSize, &config, &decoder) != MA_SUCCESS) {
        error = "miniaudio: unsupported or corrupt file";
        return false;
    }
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
    std::vector<float> pcm;
    if (frames > 0 && frames < (1ULL << 32))
        pcm.reserve((size_t)frames * 2);
    float chunk[4096 * 2];
    for (;;) {
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &got);
        if (got > 0)
            pcm.insert(pcm.end(), chunk, chunk + (size_t)got * 2);
        if (r != MA_SUCCESS || got == 0)
            break;
    }
    if (pcm.empty()) {
        ma_decoder_uninit(&decoder);
        error = "miniaudio: no audio frames decoded";
        return false;
    }
    out.sampleRate = (double)decoder.outputSampleRate;
    out.stereo = std::move(pcm);
    ma_decoder_uninit(&decoder);
    return true;
}

#ifdef _WIN32
struct CoTaskMem {
    void* p = nullptr;
    ~CoTaskMem() { if (p) CoTaskMemFree(p); }
};

// Decode AAC/M4A through Windows MediaFoundation into float32.
bool decodeAacMediaFoundation(const std::string& utf8Path, DecodedAudio& out, std::string& error) {
    static bool mfStarted = false;
    if (!mfStarted) {
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
            error = "MediaFoundation: MFStartup failed";
            return false;
        }
        mfStarted = true;
    }

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (wideLen <= 1) {
        error = "MediaFoundation: bad path encoding";
        return false;
    }
    std::vector<wchar_t> wpath((size_t)wideLen);
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, wpath.data(), wideLen);

    IMFSourceReader* reader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(wpath.data(), nullptr, &reader))) {
        error = "MediaFoundation: cannot open file";
        return false;
    }

    IMFMediaType* partial = nullptr;
    bool ok = false;
    double rate = 0.0;
    int channels = 0;
    std::vector<float> pcm;

    if (SUCCEEDED(MFCreateMediaType(&partial))) {
        partial->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        partial->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (SUCCEEDED(reader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, partial))) {
            IMFMediaType* actual = nullptr;
            if (SUCCEEDED(reader->GetCurrentMediaType(
                    (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual))) {
                UINT32 r = 0, c = 0;
                r = MFGetAttributeUINT32(actual, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
                c = MFGetAttributeUINT32(actual, MF_MT_AUDIO_NUM_CHANNELS, 0);
                if (r > 0 && c > 0) {
                    rate = (double)r;
                    channels = (int)c;
                    ok = true;
                }
                actual->Release();
            }
        }
        partial->Release();
    }

    while (ok) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = reader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
            break;
        if (sample) {
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* bytes = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &len)) && bytes && len >= 4) {
                    const size_t nFloats = len / 4;
                    const size_t nFrames = nFloats / (size_t)channels;
                    const float* f = reinterpret_cast<const float*>(bytes);
                    const size_t base = pcm.size();
                    pcm.resize(base + nFrames * 2);
                    for (size_t i = 0; i < nFrames; ++i) {
                        const float l = f[i * (size_t)channels];
                        const float r = channels > 1 ? f[i * (size_t)channels + 1] : l;
                        pcm[base + i * 2] = l;
                        pcm[base + i * 2 + 1] = r;
                    }
                    buffer->Unlock();
                }
                buffer->Release();
            }
            sample->Release();
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
    }
    reader->Release();

    if (!ok || pcm.empty()) {
        error = "MediaFoundation: no audio decoded";
        return false;
    }
    out.sampleRate = rate;
    out.stereo = std::move(pcm);
    return true;
}
#endif

} // namespace

bool decodeFile(const std::string& utf8Path, DecodedAudio& out, std::string& error) {
    if (utf8Path.empty()) {
        error = "empty path";
        return false;
    }
#ifdef _WIN32
    if (isAacContainer(utf8Path))
        return decodeAacMediaFoundation(utf8Path, out, error);
#else
    if (isAacContainer(utf8Path)) {
        error = "AAC decode not implemented on this platform yet (M0 is Windows-first)";
        return false;
    }
#endif
    // Read raw bytes with plain stdio (binary). Unicode paths are handled by
    // the caller loading via JUCE in the app; the offline path below uses
    // fopen which is adequate for the test harness.
    FILE* f = nullptr;
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (wideLen > 1) {
        std::vector<wchar_t> wpath((size_t)wideLen);
        MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, wpath.data(), wideLen);
        _wfopen_s(&f, wpath.data(), L"rb");
    }
#else
    f = fopen(utf8Path.c_str(), "rb");
#endif
    if (!f) {
        error = "cannot open file";
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        error = "file is empty";
        return false;
    }
    std::vector<unsigned char> bytes((size_t)size);
    const size_t got = fread(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    if (got != bytes.size()) {
        error = "short read";
        return false;
    }
    return decodeWithMiniAudio(bytes.data(), bytes.size(), out, error);
}

} // namespace lean
