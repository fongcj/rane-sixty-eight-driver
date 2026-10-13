/* SPDX-License-Identifier: MIT */
#pragma once
#include "CaptureTimeline.h"
#include "../playback/SixtyFourPlayback.h"
#include <atomic>
#include <thread>
#include <memory>

// Stop joins the USB worker before restart. Failed cleanup blocks further opens
// for this process; undrained DMA storage remains alive until process exit.
class SixtyFourDuplex {
public:
    // AudioServerPlugIn.h requires a zero-timestamp period of at least 10923.
    static constexpr uint64_t clockPeriod=16384;
    static constexpr uint64_t outputLeadBusFrames=64;
    static constexpr uint64_t outputLeadSamples=3072;
    bool start();
    bool write(double sample,unsigned frames,const float* data);
    sixtyfour::playback::Queue::Stats outputStats() const {return output.stats();}
    void stop();
    ~SixtyFourDuplex() { stop(); }
    bool healthy() const { return state.load()==1; }
    bool cleanlyStopped() const {return state.load()==2;}
    void read(double sample,unsigned count,float* out) {
        missing.fetch_add(timeline.read(sample,out,count),std::memory_order_relaxed);
    }
    bool timestamp(double* sample,uint64_t* host) const;
    std::atomic<uint64_t> missing{0};
private:
    void worker();
    CaptureTimeline timeline;
    sixtyfour::playback::Queue output;
    std::atomic<int> state{0}; // 0 starting, 1 ready, -1 failed, 2 stopped
    std::atomic<bool> stopping{false}, poisoned{false};
    std::atomic<uint64_t> clockSeq{0},clockSample{0},clockHost{0};
    std::thread thread;
};
