/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <cstdint>
#include <limits>

// Single run-loop owner. Time values are USB bus frames (1 ms), not audio
// samples or high-speed microframes. Each read covers 8 microframes / 1 ms.
// A discontinuity latches a fault rather than silently skipping time.
class RecordingSchedule {
public:
    static constexpr unsigned slots = 64;
    struct Ticket { unsigned slot; uint64_t generation; uint64_t frame; };
    bool begin(uint64_t now) {
        if (running_ || pending() || generation_ == std::numeric_limits<uint64_t>::max() ||
            now > std::numeric_limits<uint64_t>::max() - lead) return false;
        ++generation_;
        next_ = now + lead;
        faults_ = 0;
        running_ = true;
        return true;
    }
    // Reserve before submitting to USB. On submission failure call failed().
    bool reserve(unsigned slot, uint64_t now, Ticket& out) {
        if (!running_ || slot >= slots || busy_[slot]) return false;
        if (next_ <= now || next_ == std::numeric_limits<uint64_t>::max()) {
            ++faults_; running_ = false; return false;
        }
        busy_[slot] = true;
        frames_[slot] = next_;
        out = {slot, generation_, next_++};
        return true;
    }
    bool completed(const Ticket& ticket) { return release(ticket); }
    bool failed(const Ticket& ticket) {
        if (!release(ticket)) return false;
        ++faults_; running_ = false; return true;
    }
    void stop() { running_ = false; } // Pending buffers stay owned until completion.
    bool running() const { return running_; }
    unsigned pending() const {
        unsigned n = 0; for (bool b : busy_) n += b; return n;
    }
    uint64_t faults() const { return faults_; }
private:
    bool release(const Ticket& t) {
        if (t.slot >= slots || t.generation != generation_ || !busy_[t.slot] ||
            frames_[t.slot] != t.frame) return false;
        busy_[t.slot] = false;
        return true;
    }
    static constexpr uint64_t lead = 10;
    std::array<bool, slots> busy_{};
    std::array<uint64_t, slots> frames_{};
    uint64_t generation_ = 0, next_ = 0, faults_ = 0;
    bool running_ = false;
};
