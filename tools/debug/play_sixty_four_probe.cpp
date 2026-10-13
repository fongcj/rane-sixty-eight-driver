/* SPDX-License-Identifier: MIT */
// Explicit bounded playback experiment. No HAL installation, MIDI or vendor writes.
// Candidate policy: nominal six Float32 frames per microframe, not verified feedback.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach_time.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <new>
#include "../../src/protocol/sixty_four_descriptors.h"
#include "../../src/transport/RecordingSchedule.h"
#include "../../src/playback/SixtyFourPlayback.h"
#include "../../src/playback/ProbeSignal.h"

static volatile sig_atomic_t interrupted;
static void signalStop(int) { interrupted=1; }
static void number(CFMutableDictionaryRef d, CFStringRef key, int value) {
    auto n=CFNumberCreate(nullptr,kCFNumberIntType,&value);
    CFDictionarySetValue(d,key,n); CFRelease(n);
}
class PlaybackDiagnostic {
public:
    static constexpr size_t batchCount=5000; // Five seconds in USB bus time
    explicit PlaybackDiagnostic(bool tone) : tone_(tone) {}
    bool open() {
        auto match=IOServiceMatching("IOUSBHostDevice");
        if (!match) return false;
        number(match,CFSTR("idVendor"),0x1cc5); number(match,CFSTR("idProduct"),0x17);
        io_iterator_t it=0;
        if (!ok(IOServiceGetMatchingServices(kIOMainPortDefault,match,&it),"enumerate")) return false;
        auto service=IOIteratorNext(it), extra=IOIteratorNext(it); IOObjectRelease(it);
        if (!service || extra) {
            if(service) IOObjectRelease(service); if(extra) IOObjectRelease(extra);
            std::fprintf(stderr,"Require exactly one Sixty-Four\n"); return false;
        }
        IOCFPlugInInterface** plugin=nullptr; SInt32 score=0;
        auto result=IOCreatePlugInInterfaceForService(service,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&plugin,&score);
        IOObjectRelease(service);
        if (!ok(result,"device plugin") || !plugin) return false;
        auto hr=(*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID182),(LPVOID*)&device);
        (*plugin)->Release(plugin);
        if (hr!=S_OK || !device) return false;
        IOUSBConfigurationDescriptorPtr desc=nullptr;
        if (!ok((*device)->GetConfigurationDescriptorPtr(device,0,&desc),"descriptor") || !desc) return false;
        auto p=reinterpret_cast<const uint8_t*>(desc);
        if ((p[2]|(unsigned(p[3])<<8))!=sizeof(expectedConfiguration) ||
            std::memcmp(p,expectedConfiguration,sizeof(expectedConfiguration))) {
            std::fprintf(stderr,"Descriptor fingerprint mismatch\n"); return false;
        }
        UInt8 config=255;
        if (!ok((*device)->GetConfiguration(device,&config),"configuration") || config!=0) return false;
        if (!ok((*device)->USBDeviceOpen(device),"non-seizing open")) return false;
        deviceOpen=true;
        if (!ok((*device)->GetConfiguration(device,&config),"configuration recheck") || config!=0) return false;
        changed=true;
        if (!ok((*device)->SetConfiguration(device,1),"configuration 1")) return false;
        IOUSBFindInterfaceRequest request={kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare};
        if (!ok((*device)->CreateInterfaceIterator(device,&request,&it),"interfaces")) return false;
        while ((service=IOIteratorNext(it))) {
            plugin=nullptr;
            result=IOCreatePlugInInterfaceForService(service,kIOUSBInterfaceUserClientTypeID,kIOCFPlugInInterfaceID,&plugin,&score);
            IOObjectRelease(service);
            if(result || !plugin) continue;
            IOUSBInterfaceInterface942** candidate=nullptr;
            hr=(*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID942),(LPVOID*)&candidate);
            (*plugin)->Release(plugin);
            if(hr!=S_OK || !candidate) continue;
            UInt8 index=255;
            result=(*candidate)->GetInterfaceNumber(candidate,&index);
            if(!result && index==1 && !audio) audio=candidate;
            else (*candidate)->Release(candidate);
        }
        IOObjectRelease(it);
        if (!audio || !ok((*audio)->USBInterfaceOpen(audio),"playback open")) return false;
        audioOpen=true;
        UInt8 alt=255;
        if(!ok((*audio)->GetAlternateSetting(audio,&alt),"alternate") || alt!=0) return false;
        alternate=true;
        if(!ok((*audio)->SetAlternateInterface(audio,1),"playback alt 1")) return false;
        UInt8 count=0,dir=0,num=0,type=0,interval=0; UInt16 size=0;
        if(!ok((*audio)->GetNumEndpoints(audio,&count),"endpoint count") || count!=1) return false;
        if(!ok((*audio)->GetPipeProperties(audio,1,&dir,&num,&type,&size,&interval),"endpoint") ||
           dir!=kUSBOut || num!=6 || type!=kUSBIsoc || size!=280 || interval!=1) return false;
        if(!ok((*audio)->CreateInterfaceAsyncEventSource(audio,&source),"event source") || !source) return false;
        CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode);
        return true;
    }
    bool run() {
        UInt64 now; AbsoluteTime at;
        if(!ok((*audio)->GetBusFrameNumber(audio,&now,&at),"start clock") || !schedule.begin(now)) return false;
        for(unsigned i=0;i<slots.size();++i) {
            slots[i].owner=this;
            if(!submit(i)) {failed=true; break;}
        }
        std::printf("PLAYBACK: five seconds, %s, nominal six samples/microframe\n",tone_?"440 Hz -42 dBFS on USB 3/4 (Deck 1), with silence/fades":"digital silence");
        auto deadline=CFAbsoluteTimeGetCurrent()+8;
        while(schedule.pending() && !failed && !interrupted && CFAbsoluteTimeGetCurrent()<deadline)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);
        if(schedule.pending() || interrupted) failed=true;
        schedule.stop();
        std::printf("Batches=%zu/%zu, packet errors=%llu, scheduling faults=%llu\n",
            completed,batchCount,(unsigned long long)packetErrors,(unsigned long long)schedule.faults());
        return !failed && completed==batchCount;
    }
    bool close() {
        schedule.stop();
        bool restored=true;
        if(schedule.pending() && audio) {
            restored=ok((*audio)->AbortPipe(audio,1),"abort output") && restored;
            auto deadline=CFAbsoluteTimeGetCurrent()+2;
            while(schedule.pending() && CFAbsoluteTimeGetCurrent()<deadline)
                CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);
        }
        if(alternate) {
            UInt8 alt=255;
            bool reset=ok((*audio)->SetAlternateInterface(audio,0),"restore alt 0");
            reset=ok((*audio)->GetAlternateSetting(audio,&alt),"verify alternate") && alt==0 && reset;
            std::printf("Playback alternate restored: %s\n",reset?"yes":"NO"); restored=reset && restored;
        }
        // If callbacks fail to drain, keep owner, DMA buffers and interfaces alive
        // until this standalone process exits. Never free memory still owned by USB.
        drained=!schedule.pending();
        if(!drained) {
            // Still attempt device configuration restoration without freeing pending buffers.
            if(changed) {
                UInt8 config=255;
                bool reset=ok((*device)->SetConfiguration(device,0),"restore configuration 0");
                reset=ok((*device)->GetConfiguration(device,&config),"verify configuration") && config==0 && reset;
                std::printf("Configuration restored: %s\n",reset?"yes":"NO");
            }
            std::fprintf(stderr,"Callback drain NOT verified; retained until process exit\n"); return false;
        }
        if(source) { CFRunLoopRemoveSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode); CFRelease(source); source=nullptr; }
        if(audioOpen) restored=ok((*audio)->USBInterfaceClose(audio),"close interface") && restored;
        if(audio) { (*audio)->Release(audio); audio=nullptr; }
        if(changed) {
            UInt8 config=255;
            bool reset=ok((*device)->SetConfiguration(device,0),"restore configuration 0");
            reset=ok((*device)->GetConfiguration(device,&config),"verify configuration") && config==0 && reset;
            std::printf("Configuration restored: %s\n",reset?"yes":"NO"); restored=reset && restored;
        }
        if(deviceOpen) restored=ok((*device)->USBDeviceClose(device),"close device") && restored;
        if(device) { (*device)->Release(device); device=nullptr; }
        return restored;
    }
    bool drained=true;
private:
    struct Slot {
        PlaybackDiagnostic* owner=nullptr;
        RecordingSchedule::Ticket ticket{};
        std::array<uint8_t,8*240> data{};
        std::array<IOUSBIsocFrame,8> frames{};
    };
    static bool ok(IOReturn code,const char* step) {
        if(code) std::fprintf(stderr,"%s failed: 0x%x\n",step,code);
        return code==kIOReturnSuccess;
    }
    bool submit(unsigned index) {
        if(submitted==batchCount) return true;
        UInt64 now; AbsoluteTime at;
        if(!ok((*audio)->GetBusFrameNumber(audio,&now,&at),"bus clock")) return false;
        auto& s=slots[index];
        if(!schedule.reserve(index,now,s.ticket)) return false;
        s.frames={};s.data={};
        for(unsigned m=0;m<8;++m) {
            s.frames[m].frReqCount=240;
            std::array<float,60> samples{};
            for(unsigned f=0;f<6;++f) {
                auto frame=sixtyfour::playback::probeFrame(submitted*48+m*6+f,tone_);
                std::memcpy(samples.data()+f*10,frame.data(),sizeof(float)*10);
            }
            size_t written=0;
            if(!sixtyfour::playback::encode(samples.data(),6,s.data.data()+m*240,240,written) || written!=240) {
                schedule.failed(s.ticket);return false;
            }
        }
        auto result=(*audio)->WriteIsochPipeAsync(audio,1,s.data.data(),s.ticket.frame,8,s.frames.data(),completion,&s);
        if(!ok(result,"submit output")) { schedule.failed(s.ticket); return false; }
        ++submitted; return true;
    }
    static void completion(void* context,IOReturn result,void*) {
        auto& s=*static_cast<Slot*>(context); auto& r=*s.owner;
        if(!r.schedule.completed(s.ticket)) { r.failed=true; return; }
        if(!r.schedule.running() || r.failed) return;
        if(result!=kIOReturnSuccess) { ++r.packetErrors; r.failed=true; return; }
        for(auto& f:s.frames) {
            if(f.frStatus!=kIOReturnSuccess) { ++r.packetErrors; r.failed=true; return; }
        }
        ++r.completed;
        if(!r.submit(s.ticket.slot)) r.failed=true;
    }
    IOUSBDeviceInterface182** device=nullptr;
    IOUSBInterfaceInterface942** audio=nullptr;
    CFRunLoopSourceRef source=nullptr;
    bool deviceOpen=false,audioOpen=false,changed=false,alternate=false,failed=false;
    RecordingSchedule schedule;
    std::array<Slot,RecordingSchedule::slots> slots{};
    size_t submitted=0,completed=0;
    bool tone_=false;
    uint64_t packetErrors=0;
};

int main(int argc,char** argv) {
    bool tone=argc==2 && !std::strcmp(argv[1],"--tone-deck1-5s");
    if(argc!=2 || (!tone && std::strcmp(argv[1],"--silence-5s"))) {
        std::fprintf(stderr,"Usage: %s --silence-5s | --tone-deck1-5s\n"
            "ACTIVE five-second USB playback experiment; close audio apps first.\n"
            "Tone: 440 Hz at -42 dBFS, only Deck 1 pair 3/4, one second silence at each end.\n",argv[0]);return 2;
    }
    std::signal(SIGINT,signalStop);std::signal(SIGTERM,signalStop);setvbuf(stdout,nullptr,_IOLBF,0);
    auto* r=new(std::nothrow) PlaybackDiagnostic(tone);if(!r)return 1;
    bool ran=r->open() && r->run();bool restored=r->close();
    if(r->drained)delete r;
    return ran && restored?0:1;
}
