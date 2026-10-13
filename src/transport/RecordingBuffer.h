/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include "../protocol/sixty_four_audio.h"

// Bounded in-memory sink. Preallocated before USB starts; no allocation or
// disk access in completion callbacks. Index by scheduled bus frame so
// out-of-order callbacks cannot reorder audio. Single run-loop owner.
class RecordingBuffer {
public:
    struct Batch {
        std::array<float, 8*7*12> samples{};
        uint64_t callbackTicks = 0;
        uint32_t frames = 0;
        bool complete = false;
    };
    explicit RecordingBuffer(size_t count) : batches_(count) {}
    void setFirstBusFrame(uint64_t frame) { first_ = frame; }
    bool store(uint64_t busFrame, uint64_t ticks, const uint8_t* data,
               const std::array<uint16_t,8>& lengths) {
        if (!data || busFrame < first_ || busFrame-first_ >= batches_.size()) return false;
        auto& b=batches_[busFrame-first_];
        if (b.complete) return false;
        // A failed batch is never published, even if earlier microframes decoded.
        b.frames=0;
        for (size_t i=0;i<8;++i) {
            size_t count=0;
            if (!lengths[i] || !sixty_four_decode_input(data+i*336,lengths[i],
                    b.samples.data()+b.frames*12,b.samples.size()-b.frames*12,&count)) return false;
            b.frames+=static_cast<uint32_t>(count);
        }
        b.callbackTicks=ticks; b.complete=true; ++complete_;
        return true;
    }
    bool complete() const { return complete_==batches_.size() && !batches_.empty(); }
    size_t completed() const { return complete_; }
    const std::vector<Batch>& batches() const { return batches_; }
private:
    uint64_t first_=0;
    size_t complete_=0;
    std::vector<Batch> batches_;
};
