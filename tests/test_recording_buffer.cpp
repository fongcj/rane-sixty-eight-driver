/* SPDX-License-Identifier: MIT */
#include "../src/transport/RecordingBuffer.h"
#include <cstdlib>
#include <cstdio>
static void check(bool b) { if(!b) { std::fputs("Recording buffer test failed\n",stderr); std::exit(1); } }
int main() {
    RecordingBuffer b(3); b.setFirstBusFrame(100);
    std::array<uint8_t,8*336> data{};
    std::array<uint16_t,8> lengths; lengths.fill(288);
    // Mark first sample of each microframe with 1.0 LE; padding is deliberately NaN.
    for(unsigned i=0;i<8;++i) { data[i*336+2]=0x80; data[i*336+3]=0x3f; data[i*336+290]=0xc0; data[i*336+291]=0x7f; }
    check(!b.store(99,1,data.data(),lengths));
    check(!b.store(103,1,data.data(),lengths));
    check(!b.store(100,1,nullptr,lengths));
    check(b.store(102,3,data.data(),lengths));
    check(!b.complete() && b.completed()==1);
    check(!b.store(102,4,data.data(),lengths));
    auto malformed=lengths; malformed[7]=289;
    check(!b.store(100,1,data.data(),malformed) && b.completed()==1);
    malformed[7]=0; check(!b.store(100,1,data.data(),malformed));
    malformed[7]=337; check(!b.store(100,1,data.data(),malformed));
    check(b.store(100,1,data.data(),lengths));
    check(b.store(101,2,data.data(),lengths) && b.complete());
    for(unsigned i=0;i<3;++i) {
        auto& batch=b.batches()[i]; check(batch.callbackTicks==i+1 && batch.frames==48);
        for(unsigned m=0;m<8;++m) check(batch.samples[m*6*12]==1.0f);
    }
    RecordingBuffer corrupt(1); corrupt.setFirstBusFrame(100);
    data[2]=0xc0; data[3]=0x7f;
    check(!corrupt.store(100,1,data.data(),lengths) && !corrupt.complete());
    RecordingBuffer empty(0); check(!empty.complete());
    std::puts("Recording buffer tests passed; no hardware access");
}
