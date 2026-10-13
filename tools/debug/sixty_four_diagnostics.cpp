/* SPDX-License-Identifier: MIT */
// Read-only CoreAudio telemetry: no StartIO, property setters or USB access.
#include <CoreAudio/CoreAudio.h>
#include "../../src/diagnostics/DuplexDiagnostics.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <ctime>
static bool snapshot() {
    AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,0};UInt32 size=0;
    auto status=AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,nullptr,&size);
    if(status) {std::fprintf(stderr,"Device enumeration failed: %d\n",int(status));return false;}
    std::vector<AudioObjectID> ids(size/sizeof(AudioObjectID));
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,nullptr,&size,ids.data()))return false;
    for(auto id:ids) {
        a={kAudioDevicePropertyDeviceUID,kAudioObjectPropertyScopeGlobal,0};CFStringRef uid=nullptr;size=sizeof(uid);
        if(AudioObjectGetPropertyData(id,&a,0,nullptr,&size,&uid) || !uid)continue;
        bool match=CFEqual(uid,CFSTR("org.openrane.sixtyfour.duplex"));CFRelease(uid);if(!match)continue;
        a.mSelector=sixtyfour::diagnosticsSelector;
        if(!AudioObjectHasProperty(id,&a)) {
            std::fputs("Installed duplex driver has no diagnostics property; install the diagnostics build after approval.\n",stderr);return false;
        }
        CFStringRef json=nullptr;size=sizeof(json);
        status=AudioObjectGetPropertyData(id,&a,0,nullptr,&size,&json);
        if(status || !json) {std::fprintf(stderr,"Diagnostics query failed: %d\n",int(status));return false;}
        auto capacity=CFStringGetMaximumSizeForEncoding(CFStringGetLength(json),kCFStringEncodingUTF8)+1;
        if(capacity<=0 || capacity>1024*1024) {CFRelease(json);return false;}
        std::vector<char> text(size_t(capacity),0);
        bool good=CFStringGetCString(json,text.data(),capacity,kCFStringEncodingUTF8);CFRelease(json);
        if(!good)return false;
        std::printf("{\"unix_time\":%lld,\"diagnostics\":%s}\n",(long long)std::time(nullptr),text.data());
        std::fflush(stdout);return true;
    }
    std::fputs("Rane Sixty-Four duplex device not found.\n",stderr);return false;
}
int main(int argc,char** argv) {
    long seconds=1;
    if(argc!=1) {
        char* end=nullptr;
        if(argc!=3 || std::strcmp(argv[1],"--watch")) {std::fprintf(stderr,"Usage: %s [--watch SECONDS]\n",argv[0]);return 2;}
        seconds=std::strtol(argv[2],&end,10);
        if(!end || *end || seconds<1 || seconds>3600) {std::fputs("Watch duration must be 1..3600 seconds.\n",stderr);return 2;}
    }
    for(long i=0;i<seconds;++i) {
        if(!snapshot())return 1;
        if(i+1<seconds)std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
