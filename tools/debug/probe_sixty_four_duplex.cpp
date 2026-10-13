/* SPDX-License-Identifier: MIT */
// Opt-in six-second duplex test. No driver installation or CoreAudio changes.
#include "../../src/transport/SixtyFourDuplex.h"
#include "../../src/playback/ProbeSignal.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <algorithm>
static volatile sig_atomic_t interrupted=0;
static void stopSignal(int) {interrupted=1;}
int main(int argc,char** argv) {
    if(argc!=2 || std::strcmp(argv[1],"--tone-deck1-6s")) {
        std::fprintf(stderr,"Usage: %s --tone-deck1-6s\nActive simultaneous input/output, quiet Deck 1 tone; close audio apps first.\n",argv[0]);return 2;
    }
    std::signal(SIGINT,stopSignal);std::signal(SIGTERM,stopSignal);setvbuf(stdout,nullptr,_IOLBF,0);
    auto engine=std::make_unique<SixtyFourDuplex>();
    if(!engine->start()) {std::puts("Duplex start failed");return 1;}
    std::puts("DUPLEX TEST: six seconds, quiet Deck 1 tone plus all 12 recording inputs");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);
    uint64_t sent=0,read=0,attempts=0;double peaks[12]={};bool valid=true;
    constexpr uint64_t first=8192,total=240000;
    while(!interrupted && engine->healthy() && std::chrono::steady_clock::now()<deadline) {
        if(sent<total) {
            unsigned count=unsigned(std::min<uint64_t>(256,total-sent));std::array<float,256*10> data{};
            for(unsigned i=0;i<count;++i) {auto f=sixtyfour::playback::probeFrame(sent+i,true);std::memcpy(data.data()+i*10,f.data(),40);}
            ++attempts;if(engine->write(first+sent,count,data.data()))sent+=count;
        }
        double sample;uint64_t host;
        if(engine->timestamp(&sample,&host)) {
            while(read+512<=uint64_t(sample)) {
                std::array<float,512*12> data{};engine->read(double(read),512,data.data());
                for(unsigned i=0;i<data.size();++i) {
                    if(!std::isfinite(data[i]))valid=false;
                    peaks[i%12]=std::max(peaks[i%12],std::fabs(double(data[i])));
                }
                read+=512;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bool good=engine->healthy() && !interrupted && valid && sent==total && read>200000 && engine->missing.load()==0;
    auto stats=engine->outputStats();engine->stop();good=good && engine->cleanlyStopped();
    std::printf("queued=%llu read=%llu input-missing=%llu late-output=%llu write-attempts=%llu\n",
        (unsigned long long)sent,(unsigned long long)read,(unsigned long long)engine->missing.load(),
        (unsigned long long)stats.late,(unsigned long long)attempts);
    for(unsigned i=0;i<12;++i)std::printf("input %u peak %.7f\n",i+1,peaks[i]);
    std::puts("Output silence before/after the finite signal is expected; packet-count coupling remains experimental.");
    return good?0:1;
}
