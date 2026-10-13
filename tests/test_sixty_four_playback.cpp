/* SPDX-License-Identifier: MIT */
#include "../src/playback/SixtyFourPlayback.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
using namespace sixtyfour::playback;
#define CHECK(x) do {if(!(x)) {std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
static float decode(const uint8_t* p) {
    uint32_t bits=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
    float f;std::memcpy(&f,&bits,4);return f;
}
static void frame(float* p,unsigned n) {for(unsigned c=0;c<channels;++c)p[c]=float(n*16+c);}
int main() {
    std::array<uint8_t,280> bytes;bytes.fill(0xa5);size_t written=99;
    float golden[10]={0,1,-1,.5f,-0.0f,.25f,-.25f,2,-2,.125f};
    const uint32_t bits[10]={0,0x3f800000,0xbf800000,0x3f000000,0x80000000,0x3e800000,0xbe800000,0x40000000,0xc0000000,0x3e000000};
    CHECK(encode(golden,1,bytes.data(),bytes.size(),written)&&written==40);
    for(unsigned i=0;i<10;++i)for(unsigned b=0;b<4;++b)CHECK(bytes[i*4+b]==uint8_t(bits[i]>>(8*b)));
    CHECK(bytes[40]==0xa5);
    std::array<float,70> samples{};
    for(unsigned i=0;i<70;++i)samples[i]=float(i)-30;
    for(unsigned n:{0u,5u,6u,7u}) {
        CHECK(encode(samples.data(),n,bytes.data(),bytes.size(),written)&&written==n*40);
        for(unsigned i=0;i<n*10;++i)CHECK(decode(bytes.data()+i*4)==samples[i]);
    }
    bytes.fill(0xa5);
    CHECK(!encode(samples.data(),8,bytes.data(),bytes.size(),written)&&written==0);
    CHECK(!encode(samples.data(),7,bytes.data(),279,written));
    CHECK(!encode(nullptr,1,bytes.data(),280,written));
    CHECK(!encode(samples.data(),1,nullptr,280,written));
    for(float bad:{INFINITY,-INFINITY,NAN}) {
        samples[69]=bad;CHECK(!encode(samples.data(),7,bytes.data(),280,written)&&written==0);
        for(auto b:bytes)CHECK(b==0xa5);
    }
    CHECK(encode(nullptr,0,nullptr,0,written)&&written==0);

    auto q=std::make_unique<Queue>();float f[10],out[40];frame(f,1);
    CHECK(q->read(0,out,2));for(unsigned i=0;i<20;++i)CHECK(out[i]==0);
    CHECK(q->push(0,f,1));CHECK(q->push(4,f,1));
    CHECK(q->read(2,out,4));
    for(unsigned i=0;i<40;++i)CHECK(out[i]==(i>=20&&i<30?f[i-20]:0));
    CHECK(q->stats().missing==5 && q->stats().late==1);
    CHECK(!q->read(2,out,1));CHECK(!q->push(3,f,1));
    CHECK(!q->push(UINT64_MAX,f,1));CHECK(!q->read(UINT64_MAX,out,1));
    CHECK(!q->push(5,nullptr,1));CHECK(!q->read(6,nullptr,1));
    q->reset();
    std::vector<float> full(Queue::capacity*channels);
    for(unsigned i=0;i<Queue::capacity;++i)frame(full.data()+i*channels,i);
    CHECK(q->push(0,full.data(),Queue::capacity));
    CHECK(!q->push(Queue::capacity,f,1));CHECK(q->stats().rejectedFrames==1);
    std::vector<float> received(full.size());
    CHECK(q->read(0,received.data(),Queue::capacity));CHECK(received==full);
    CHECK(q->push(Queue::capacity,f,1));CHECK(q->read(Queue::capacity,out,1));
    CHECK(!std::memcmp(out,f,sizeof(f)));
    q->reset();
    float invalid[10]={};invalid[9]=NAN;CHECK(!q->push(0,invalid,1));
    CHECK(q->push(0,f,1));CHECK(q->read(0,out,1));CHECK(!std::memcmp(out,f,sizeof(f)));

    // Explicit variable packet lengths: no dropped lane or requested-size padding.
    q->reset();std::array<unsigned,8> counts{5,6,7,0,6,5,7,6};
    CHECK(q->push(0,full.data(),56));Batch batch{};batch.frames=999;
    auto bad=counts;bad[7]=8;CHECK(!prepare(*q,0,bad,batch)&&batch.frames==999);
    CHECK(prepare(*q,0,counts,batch));unsigned cursor=0;
    for(unsigned p=0;p<8;++p) {
        CHECK(batch.lengths[p]==counts[p]*40);
        for(unsigned n=0;n<counts[p]*10;++n)CHECK(decode(batch.packets[p].data()+n*4)==full[cursor*10+n]);
        for(unsigned b=batch.lengths[p];b<280;++b)CHECK(batch.packets[p][b]==0);
        cursor+=counts[p];
    }
    CHECK(batch.frames==cursor && q->stats().missing==0);
    CHECK(prepare(*q,56,counts,batch)); // Remaining queued frames are now late.
    for(auto& p:batch.packets)for(auto b:p)CHECK(b==0);
    CHECK(q->stats().late==56-cursor && q->stats().missing==cursor);

    // Deterministic producer/consumer stress over many wraps. Publication gates
    // the reader so an underrun cannot hide torn data or an ownership violation.
    q->reset();constexpr unsigned total=100000;std::atomic<unsigned> published{0};
    std::thread producer([&]{float v[10];for(unsigned i=0;i<total;++i){
        frame(v,i);while(!q->push(i,v,1))std::this_thread::yield();published.store(i+1,std::memory_order_release);
    }});
    for(unsigned i=0;i<total;++i) {
        while(published.load(std::memory_order_acquire)<=i)std::this_thread::yield();
        CHECK(q->read(i,out,1));frame(f,i);CHECK(!std::memcmp(out,f,sizeof(f)));
    }
    producer.join();CHECK(q->stats().missing==0 && q->stats().late==0 && q->stats().invalidBlocks==0);
    std::puts("Playback candidate bytes, timestamp gaps, overflow, packet plans and SPSC stress passed");
}
