// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Decode smoke test: synthesizes a 16-bit stereo WAV on disk, decodes it
// through the real lean::decodeFile path (miniaudio), and checks rate,
// length, and sample values. Also checks error paths.
#include "Decode.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

void putU16(std::vector<unsigned char>& b, uint16_t v) {
    b.push_back((unsigned char)(v & 0xff));
    b.push_back((unsigned char)((v >> 8) & 0xff));
}

void putU32(std::vector<unsigned char>& b, uint32_t v) {
    b.push_back((unsigned char)(v & 0xff));
    b.push_back((unsigned char)((v >> 8) & 0xff));
    b.push_back((unsigned char)((v >> 16) & 0xff));
    b.push_back((unsigned char)((v >> 24) & 0xff));
}

void putTag(std::vector<unsigned char>& b, const char* t) {
    for (int i = 0; i < 4; ++i)
        b.push_back((unsigned char)t[i]);
}

int run() {
    constexpr int rate = 44100;
    constexpr int frames = 44100;
    std::vector<int16_t> L(frames), R(frames);
    for (int i = 0; i < frames; ++i) {
        const size_t fi = (size_t)i;
        const double t = (double)i / rate;
        L[fi] = (int16_t)(0.5 * 32767.0 * sin(2.0 * 3.141592653589793 * 440.0 * t));
        R[fi] = (int16_t)(0.25 * 32767.0 * sin(2.0 * 3.141592653589793 * 660.0 * t));
    }

    std::vector<unsigned char> wav;
    putTag(wav, "RIFF");
    putU32(wav, 36 + (uint32_t)frames * 4);
    putTag(wav, "WAVE");
    putTag(wav, "fmt ");
    putU32(wav, 16);
    putU16(wav, 1); // PCM
    putU16(wav, 2); // stereo
    putU32(wav, rate);
    putU32(wav, rate * 4);
    putU16(wav, 4);
    putU16(wav, 16);
    putTag(wav, "data");
    putU32(wav, (uint32_t)frames * 4);
    for (int i = 0; i < frames; ++i) {
        const size_t fi = (size_t)i;
        putU16(wav, (uint16_t)L[fi]);
        putU16(wav, (uint16_t)R[fi]);
    }

    const auto path =
        (std::filesystem::temp_directory_path() / "transit_decode_test.wav").string();
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            std::printf("DECODE_TEST FAIL: cannot write temp file\n");
            return 1;
        }
        fwrite(wav.data(), 1, wav.size(), f);
        fclose(f);
    }

    lean::DecodedAudio out;
    std::string error;
    if (!lean::decodeFile(path, out, error)) {
        std::printf("DECODE_TEST FAIL: decode failed: %s\n", error.c_str());
        return 1;
    }
    if ((int)out.sampleRate != rate) {
        std::printf("DECODE_TEST FAIL: rate %f\n", out.sampleRate);
        return 1;
    }
    if ((int)(out.stereo.size() / 2) != frames) {
        std::printf("DECODE_TEST FAIL: frames %d\n", (int)(out.stereo.size() / 2));
        return 1;
    }
    double worst = 0.0;
    for (int i = 0; i < frames; ++i) {
        const size_t fi = (size_t)i;
        const double el = (double)L[fi] / 32768.0;
        const double er = (double)R[fi] / 32768.0;
        worst = std::max(worst, std::abs(out.stereo[fi * 2] - el));
        worst = std::max(worst, std::abs(out.stereo[fi * 2 + 1] - er));
    }
    std::printf("DECODE_TEST wav16 worst diff: %.2e (tolerance 1e-4)\n", worst);
    if (worst > 1e-4) {
        std::printf("DECODE_TEST FAIL: samples off\n");
        return 1;
    }

    // Error paths.
    lean::DecodedAudio junk;
    if (lean::decodeFile("", junk, error)) {
        std::printf("DECODE_TEST FAIL: empty path accepted\n");
        return 1;
    }
    if (lean::decodeFile(path + ".missing", junk, error)) {
        std::printf("DECODE_TEST FAIL: missing file accepted\n");
        return 1;
    }
    std::printf("DECODE_TEST PASS\n");
    std::filesystem::remove(path);
    return 0;
}

} // namespace

int main() {
    return run();
}
