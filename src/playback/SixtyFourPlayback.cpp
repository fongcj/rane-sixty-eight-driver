/* SPDX-License-Identifier: MIT */
#include "SixtyFourPlayback.h"
#include <cmath>
#include <cstring>
#include <limits>
namespace sixtyfour::playback {
bool encode(const float* samples,size_t frames,uint8_t* bytes,size_t capacity,size_t& written) {
    written=0;
    if(frames>maxFramesPerPacket || capacity<frames*frameBytes) return false;
    if(!frames) return true;
    if(!samples || !bytes) return false;
    std::array<uint8_t,maxPacketBytes> temporary{};
    for(size_t i=0;i<frames*channels;++i) {
        if(!std::isfinite(samples[i])) return false;
        uint32_t bits;static_assert(sizeof(float)==4);std::memcpy(&bits,samples+i,4);
        for(unsigned b=0;b<4;++b) temporary[i*4+b]=uint8_t(bits>>(8*b));
    }
    written=frames*frameBytes;std::memcpy(bytes,temporary.data(),written);return true;
}
bool Queue::push(uint64_t first,const float* samples,size_t frames) {
    if(frames>capacity || frames>UINT64_MAX-first || (frames && !samples) || first<nextWrite_) {
        invalid_.fetch_add(1,std::memory_order_relaxed);return false;
    }
    if(!frames) return true;
    for(size_t i=0;i<frames*channels;++i) if(!std::isfinite(samples[i])) {
        invalid_.fetch_add(1,std::memory_order_relaxed);return false;
    }
    auto head=head_.load(std::memory_order_relaxed),tail=tail_.load(std::memory_order_acquire);
    if(frames>capacity-(head-tail) || frames>UINT64_MAX-head) {
        rejected_.fetch_add(frames,std::memory_order_relaxed);return false;
    }
    for(size_t i=0;i<frames;++i) {
        auto& f=frames_[(head+i)%capacity];f.sample=first+i;
        std::memcpy(f.values.data(),samples+i*channels,channels*sizeof(float));
    }
    auto occupied=head-tail+frames;
    if(occupied>highWater_.load(std::memory_order_relaxed))highWater_.store(occupied,std::memory_order_relaxed);
    nextWrite_=first+frames;
    head_.store(head+frames,std::memory_order_release);return true;
}
bool Queue::read(uint64_t first,float* samples,size_t frames) {
    if(frames>capacity || frames>UINT64_MAX-first || (frames && !samples) || first<nextRead_) return false;
    if(!frames) return true;
    std::memset(samples,0,frames*channels*sizeof(float));
    auto tail=tail_.load(std::memory_order_relaxed);
    const auto head=head_.load(std::memory_order_acquire);
    uint64_t missing=0,late=0,startup=0;
    for(size_t i=0;i<frames;++i) {
        auto wanted=first+i;
        while(tail<head && frames_[tail%capacity].sample<wanted) {++tail;++late;}
        if(tail<head && frames_[tail%capacity].sample==wanted) {
            std::memcpy(samples+i*channels,frames_[tail%capacity].values.data(),channels*sizeof(float));++tail;deliveredAny_=true;
        } else {++missing;if(!deliveredAny_)++startup;}
    }
    nextRead_=first+frames;
    tail_.store(tail,std::memory_order_release);
    startupMissing_.fetch_add(startup,std::memory_order_relaxed);
    missing_.fetch_add(missing,std::memory_order_relaxed);late_.fetch_add(late,std::memory_order_relaxed);
    return true;
}
void Queue::reset() {
    head_.store(0);tail_.store(0);nextWrite_=nextRead_=0;deliveredAny_=false;
    startupMissing_.store(0);highWater_.store(0);
    missing_.store(0);late_.store(0);rejected_.store(0);invalid_.store(0);
}
Queue::Stats Queue::stats() const {return {missing_.load(),late_.load(),rejected_.load(),invalid_.load(),startupMissing_.load(),highWater_.load()};}
bool prepare(Queue& queue,uint64_t first,const std::array<unsigned,microframes>& counts,Batch& output) {
    size_t total=0;
    for(auto n:counts) {if(n>maxFramesPerPacket) return false;total+=n;}
    if(total>UINT64_MAX-first) return false;
    std::array<float,microframes*maxFramesPerPacket*channels> samples{};
    if(!queue.read(first,samples.data(),total)) return false;
    Batch next{};size_t offset=0;
    for(size_t i=0;i<microframes;++i) {
        size_t written=0;
        // Queue admits only finite samples, and all lengths were checked above.
        if(!encode(samples.data()+offset*channels,counts[i],next.packets[i].data(),maxPacketBytes,written)) return false;
        next.lengths[i]=uint16_t(written);offset+=counts[i];
    }
    next.frames=total;output=next;return true;
}
}
