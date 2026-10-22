// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "Recorder.h"

#include <chrono>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lean {
namespace {

void putU16(unsigned char* b, uint16_t v) {
    b[0] = (unsigned char)(v & 0xff);
    b[1] = (unsigned char)((v >> 8) & 0xff);
}

void putU32(unsigned char* b, uint32_t v) {
    b[0] = (unsigned char)(v & 0xff);
    b[1] = (unsigned char)((v >> 8) & 0xff);
    b[2] = (unsigned char)((v >> 16) & 0xff);
    b[3] = (unsigned char)((v >> 24) & 0xff);
}

void writeHeader(FILE* f, uint32_t sampleRate) {
    unsigned char h[44] = {};
    h[0] = 'R';
    h[1] = 'I';
    h[2] = 'F';
    h[3] = 'F';
    putU32(h + 4, 0); // riff size, patched on stop
    h[8] = 'W';
    h[9] = 'A';
    h[10] = 'V';
    h[11] = 'E';
    h[12] = 'f';
    h[13] = 'm';
    h[14] = 't';
    h[15] = ' ';
    putU32(h + 16, 16);
    putU16(h + 20, 3); // IEEE float
    putU16(h + 22, 2); // stereo
    putU32(h + 24, sampleRate);
    putU32(h + 28, sampleRate * 8);
    putU16(h + 32, 8);
    putU16(h + 34, 32);
    h[36] = 'd';
    h[37] = 'a';
    h[38] = 't';
    h[39] = 'a';
    putU32(h + 40, 0); // data size, patched on stop
    fwrite(h, 1, 44, f);
}

FILE* openWrite(const std::string& utf8) {
#ifdef _WIN32
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 1)
        return nullptr;
    std::vector<wchar_t> w((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
    FILE* f = nullptr;
    _wfopen_s(&f, w.data(), L"wb");
    return f;
#else
    return fopen(utf8.c_str(), "wb");
#endif
}

} // namespace

Recorder::Recorder() {
    // Parentheses on purpose: braces would select the initializer-list
    // constructor and build a 1-element vector (heap overflow on first use).
    ring = std::vector<float>((size_t)(kCapFrames * 2));
}

bool Recorder::start(const std::string& pathUtf8, double sampleRate) {
    if (recording.load())
        return false;
    stop();
    FILE* f = openWrite(pathUtf8);
    if (!f)
        return false;
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    path = pathUtf8;
    writeHeader(f, (uint32_t)rate);
    file = f;
    writePos.store(0);
    readPos.store(0);
    dropped.store(0);
    totalWritten = 0;
    writerDone.store(false);
    recording.store(true);
    writer = std::thread(&Recorder::writerLoop, this);
    return true;
}

void Recorder::pushBlock(const float* left, const float* right, int numSamples) {
    if (!recording.load() || !left || numSamples <= 0)
        return;
    // SPSC ring: writePos is producer-only, readPos consumer-only. On overflow
    // drop the incoming (newest) block rather than moving the consumer index.
    const uint64_t w = writePos.load(std::memory_order_seq_cst);
    const uint64_t r = readPos.load(std::memory_order_seq_cst);
    if (w + (uint64_t)numSamples - r > kCapFrames) {
        dropped.fetch_add((long long)numSamples, std::memory_order_seq_cst);
        wake.notify_one();
        return;
    }
    uint64_t p = w;
    for (int i = 0; i < numSamples; ++i) {
        ring[(p & kMask) * 2] = left[i];
        ring[(p & kMask) * 2 + 1] = right ? right[i] : left[i];
        ++p;
    }
    writePos.store(p, std::memory_order_seq_cst);
    wake.notify_one();
}

void Recorder::stop() {
    if (recording.exchange(false)) {
        wake.notify_all();
        if (writer.joinable())
            writer.join();
    } else if (writer.joinable()) {
        writer.join();
    }
    writerDone.store(true);
}

void Recorder::writerLoop() {
    FILE* f = (FILE*)file;
    std::vector<float> chunk(32768 * 2);
    for (;;) {
        uint64_t w = writePos.load(std::memory_order_seq_cst);
        uint64_t r = readPos.load(std::memory_order_seq_cst);
        while (r < w) {
            uint64_t n = w - r;
            if (n > 32768)
                n = 32768;
            for (uint64_t i = 0; i < n; ++i) {
                chunk[i * 2] = ring[((r + i) & kMask) * 2];
                chunk[i * 2 + 1] = ring[((r + i) & kMask) * 2 + 1];
            }
            readPos.fetch_add(n, std::memory_order_seq_cst);
            r += n;
            fwrite(chunk.data(), sizeof(float), (size_t)n * 2, f);
            totalWritten += (long long)n;
            w = writePos.load(std::memory_order_seq_cst);
        }
        if (!recording.load()) {
            // Drain anything that arrived while writing, then finalize.
            w = writePos.load(std::memory_order_seq_cst);
            if (readPos.load(std::memory_order_seq_cst) >= w)
                break;
            continue;
        }
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return !recording.load()
                || writePos.load(std::memory_order_seq_cst)
                    != readPos.load(std::memory_order_seq_cst);
        });
    }
    // Patch sizes and close. Classic RIFF caps at 4 GB; clamp rather than wrap.
    constexpr uint64_t kMaxRiffData = 0xFFFFFFFFull - 36ull;
    uint64_t dataBytes64 = (uint64_t)totalWritten * 2ull * (uint64_t)sizeof(float);
    if (dataBytes64 > kMaxRiffData)
        dataBytes64 = kMaxRiffData;
    const uint32_t dataBytes = (uint32_t)dataBytes64;
    unsigned char sizes[8];
    putU32(sizes, 36 + dataBytes);
    putU32(sizes + 4, dataBytes);
    fseek(f, 4, SEEK_SET);
    fwrite(sizes, 1, 4, f);
    fseek(f, 40, SEEK_SET);
    fwrite(sizes + 4, 1, 4, f);
    fclose(f);
    file = nullptr;
}

} // namespace lean
