/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// OFFLINE CANDIDATE ONLY. OUT wire encoding and clock policy remain unverified.
// This library has no IOKit dependency and is not linked into an installed HAL.
namespace sixtyfour::playback {
constexpr size_t channels=10, frameBytes=40, maxPacketBytes=280;
constexpr size_t microframes=8, maxFramesPerPacket=7;

// Candidate interleaved little-endian Float32 encoding. No gain or clipping.
// Failure leaves bytes untouched and sets written=0. Capacity is in bytes.
bool encode(const float* samples,size_t frames,uint8_t* bytes,size_t capacity,size_t& written);

// Exactly one HAL producer and one USB consumer. Reset only while both are idle.
// Producer rejects the whole new block on overflow; only consumer advances tail.
// Absolute sample positions preserve gaps rather than shifting later audio early.
class Queue {
public:
    static constexpr size_t capacity=4096;
    bool push(uint64_t first,const float* samples,size_t frames);
    // Read requested positions, substituting silence for absent frames. Late
    // queued frames are discarded by the consumer and counted separately.
    bool read(uint64_t first,float* samples,size_t frames);
    void reset();
    struct Stats { uint64_t missing,late,rejectedFrames,invalidBlocks; };
    Stats stats() const;
private:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    struct Frame { uint64_t sample; std::array<float,channels> values; };
    std::array<Frame,capacity> frames_{};
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
    uint64_t nextWrite_=0,nextRead_=0;
    std::atomic<uint64_t> missing_{0},late_{0},rejected_{0},invalid_{0};
};

struct Batch {
    // Each packet has a fixed storage slot but a distinct actual transfer length.
    std::array<std::array<uint8_t,maxPacketBytes>,microframes> packets{};
    std::array<uint16_t,microframes> lengths{};
    size_t frames=0;
};
// Caller must supply an explicit, validated timing plan. No nominal-six or
// Sixty-Eight feedback assumption is hidden here. Invalid plan does not consume.
bool prepare(Queue& queue,uint64_t first,
             const std::array<unsigned,microframes>& counts,Batch& output);
}
