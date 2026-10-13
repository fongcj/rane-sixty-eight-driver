/* SPDX-License-Identifier: MIT */
#include "../src/transport/CaptureTimeline.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#define CHECK(x) do {if(!(x)) {std::cerr << "Failed line " << __LINE__ << ": " #x "\n";std::exit(1);}} while(0)
int main() {
    auto t=std::make_unique<CaptureTimeline>();
    float data[24],a[24],b[24];
    for(unsigned i=0;i<24;++i) data[i]=float(i+1);
    CHECK(t->read(0,a,2)==2);
    t->write(0,data,2);
    CHECK(t->read(0,a,2)==0 && t->read(0,b,2)==0);
    CHECK(!std::memcmp(a,data,sizeof(a)) && !std::memcmp(a,b,sizeof(a)));
    t->write(CaptureTimeline::capacity,data,2);
    CHECK(t->read(0,a,2)==2);
    CHECK(t->read(CaptureTimeline::capacity,a,2)==0);
    t->clear();CHECK(t->read(CaptureTimeline::capacity,a,2)==2);
    t->write(0,data,2);CHECK(t->read(0,a,2)==0);
    for(double invalid:{-1.0,0.5,INFINITY*1.0,NAN*1.0,std::ldexp(1.0,64)}) {
        CHECK(t->read(invalid,a,2)==2);
        for(float v:a) CHECK(v==0);
    }
    // A reader overlapping wrap/reset may see silence, but never torn channels.
    std::atomic<bool> done{false};
    std::thread writer([&] {
        float f[12];
        for(unsigned n=1;n<30000;++n) {
            if(n%101==0) t->clear();
            for(float& v:f) v=float(n);
            t->write(0,f,1);
        }
        done.store(true);
    });
    // The old initial frame has distinct channels; wait for writer's first uniform frame.
    while(!done.load()) {
        if(t->read(0,a,1)==0 && a[0]>24) for(unsigned i=1;i<12;++i) CHECK(a[i]==a[0]);
    }
    writer.join();
    std::cout << "Timeline repeat reads, wrap, restart, bounds and concurrent reads passed\n";
}
