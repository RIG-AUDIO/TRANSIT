// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lean {

// Mix recorder: captures the post-master stereo bus to 32-bit float WAV.
// Audio thread only touches pushBlock (lock-free ring copy); a writer thread
// owns the FILE*. Pure C++, no dependencies.
class Recorder {
public:
    Recorder();
    ~Recorder() { stop(); }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Opens path (UTF-8) and starts the writer thread. False on failure.
    bool start(const std::string& pathUtf8, double sampleRate);
    // Copies one block into the ring. Never blocks; counts drops on overflow.
    void pushBlock(const float* left, const float* right, int numSamples);
    void stop();

    bool isRecording() const { return recording.load(); }
    long long droppedFrames() const { return dropped.load(); }
    long long recordedFrames() const { return totalWritten.load(); }

private:
    void writerLoop();

    static constexpr uint64_t kCapFrames = 1ULL << 22; // ~4M frames (~32 MB)
    static constexpr uint64_t kMask = kCapFrames - 1;

    std::vector<float> ring; // sized in the constructor (see below)
    std::atomic<uint64_t> writePos{ 0 };
    std::atomic<uint64_t> readPos{ 0 }; // written by writer, peeked by producer
    std::atomic<long long> dropped{ 0 };
    std::atomic<bool> recording{ false };
    std::atomic<bool> writerDone{ true };

    std::mutex mutex;
    std::condition_variable wake;
    std::thread writer;

    void* file = nullptr; // FILE*, writer thread only (void* keeps header clean)
    // Writer thread stores; message thread reads for UI counters (relaxed).
    std::atomic<long long> totalWritten{ 0 };
    double rate = 44100.0;
    std::string path;
};

} // namespace lean
