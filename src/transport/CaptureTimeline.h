/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>

// Sample-addressed, non-consuming history: multiple CoreAudio readers can
// read the same frames. All payload words are atomic, including during wrap.
// Single USB producer; no locks, allocations or USB calls in read().
class CaptureTimeline {
    static_assert(std::atomic<uint32_t>::is_always_lock_free &&
                  std::atomic<uint64_t>::is_always_lock_free, "Real-time timeline requires lock-free atomics");
public:
    static constexpr size_t capacity=65536, channels=12;
    void clear() {
        for(auto& c:cells) {
            auto seq=c.sequence.load(std::memory_order_seq_cst);
            c.sequence.store(seq+1,std::memory_order_seq_cst);
            c.sample.store(UINT64_MAX,std::memory_order_seq_cst);
            c.sequence.store(seq+2,std::memory_order_seq_cst);
        }
    }
    void write(uint64_t sample,const float* data,size_t count) {
        for(size_t i=0;i<count;++i) {
            auto& c=cells[(sample+i)%capacity];
            auto seq=c.sequence.load(std::memory_order_relaxed);
            c.sequence.store(seq+1,std::memory_order_seq_cst);
            for(size_t j=0;j<channels;++j) { uint32_t bits; std::memcpy(&bits,data+i*channels+j,4); c.values[j].store(bits,std::memory_order_seq_cst); }
            c.sample.store(sample+i,std::memory_order_seq_cst);
            c.sequence.store(seq+2,std::memory_order_seq_cst);
        }
    }
    size_t read(double sample,float* out,size_t count) const {
        std::memset(out,0,count*channels*sizeof(float));
        if(!std::isfinite(sample) || sample<0 || sample>=std::ldexp(1.0,64) || count>UINT64_MAX-uint64_t(sample) || std::floor(sample)!=sample) return count;
        size_t missing=0;
        for(size_t i=0;i<count;++i) {
            uint64_t index=uint64_t(sample)+i; auto& c=cells[index%capacity];
            auto before=c.sequence.load(std::memory_order_seq_cst);
            if(!before || (before&1) || c.sample.load(std::memory_order_seq_cst)!=index) { ++missing; continue; }
            float temp[channels];
            for(size_t j=0;j<channels;++j) { uint32_t bits=c.values[j].load(std::memory_order_seq_cst);std::memcpy(temp+j,&bits,4); }
            if(c.sequence.load(std::memory_order_seq_cst)!=before) { ++missing; continue; }
            std::memcpy(out+i*channels,temp,sizeof(temp));
        }
        return missing;
    }
private:
    struct Cell {
        std::atomic<uint64_t> sequence{0},sample{0};
        std::array<std::atomic<uint32_t>,channels> values{};
    };
    std::array<Cell,capacity> cells{};
};
