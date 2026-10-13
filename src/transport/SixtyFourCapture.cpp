/* SPDX-License-Identifier: MIT */
#include "SixtyFourCapture.h"
#include "RecordingSchedule.h"
#include "../protocol/sixty_four_descriptors.h"
#include "../protocol/sixty_four_audio.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach_time.h>
#include <cstdio>
#include <chrono>
#include <array>
#include <functional>
#include <new>

static void number(CFMutableDictionaryRef d,CFStringRef key,int value) {
    auto n=CFNumberCreate(nullptr,kCFNumberIntType,&value); CFDictionarySetValue(d,key,n); CFRelease(n);
}
namespace {
class Session {
public:
    bool drained=true;
    std::function<void(const float*,size_t,uint64_t)> receive;
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
            if(!result && index==2 && !audio) audio=candidate;
            else (*candidate)->Release(candidate);
        }
        IOObjectRelease(it);
        if (!audio || !ok((*audio)->USBInterfaceOpen(audio),"recording open")) return false;
        audioOpen=true;
        UInt8 alt=255;
        if(!ok((*audio)->GetAlternateSetting(audio,&alt),"alternate") || alt!=0) return false;
        alternate=true;
        if(!ok((*audio)->SetAlternateInterface(audio,1),"recording alt 1")) return false;
        UInt8 count=0,dir=0,num=0,type=0,interval=0; UInt16 size=0;
        if(!ok((*audio)->GetNumEndpoints(audio,&count),"endpoint count") || count!=1) return false;
        if(!ok((*audio)->GetPipeProperties(audio,1,&dir,&num,&type,&size,&interval),"endpoint") ||
           dir!=kUSBIn || num!=2 || type!=kUSBIsoc || size!=336 || interval!=1) return false;
        if(!ok((*audio)->CreateInterfaceAsyncEventSource(audio,&source),"event source") || !source) return false;
        CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode);
        return true;
    }
    bool close() {
        schedule.stop();
        bool restored=true;
        if(schedule.pending() && audio) {
            restored=ok((*audio)->AbortPipe(audio,1),"abort input") && restored;
            auto deadline=CFAbsoluteTimeGetCurrent()+2;
            while(schedule.pending() && CFAbsoluteTimeGetCurrent()<deadline)
                CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);
        }
        if(alternate) {
            UInt8 alt=255;
            bool reset=ok((*audio)->SetAlternateInterface(audio,0),"restore alt 0");
            reset=ok((*audio)->GetAlternateSetting(audio,&alt),"verify alternate") && alt==0 && reset;
            std::printf("Recording alternate restored: %s\n",reset?"yes":"NO"); restored=reset && restored;
        }
        // If callbacks fail to drain, keep owner, DMA buffers and interfaces alive
        // until this process exits. Never free memory still owned by USB.
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

    bool run(const std::atomic<bool>& stop) {
        UInt64 now; AbsoluteTime at;
        if(!ok((*audio)->GetBusFrameNumber(audio,&now,&at),"start clock") || !schedule.begin(now)) return false;
        mach_timebase_info_data_t tb; mach_timebase_info(&tb);
        ticksPerMs=1e6*tb.denom/tb.numer;
        // GetBusFrameNumber returns the host AbsoluteTime associated with its bus frame.
        uint64_t host=0; static_assert(sizeof(at)==sizeof(host)); std::memcpy(&host,&at,sizeof(host));
        baseBus=now; baseHost=host;
        for(unsigned i=0;i<slots.size();++i) { slots[i].owner=this; if(!submit(i)) {failed=true; break;} }
        auto lastProgress=CFAbsoluteTimeGetCurrent(); uint64_t previous=0;
        while(!stop.load() && !failed) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);
            if(delivered!=previous) {previous=delivered;lastProgress=CFAbsoluteTimeGetCurrent();}
            if(CFAbsoluteTimeGetCurrent()-lastProgress>1) failed=true;
        }
        schedule.stop(); return !failed;
    }
private:
    struct Slot {
        Session* owner=nullptr;
        RecordingSchedule::Ticket ticket{};
        std::array<uint8_t,8*336> data{};
        std::array<IOUSBIsocFrame,8> frames{};
        bool ready=false;
    };
    static bool ok(IOReturn code,const char* step) {
        if(code) std::fprintf(stderr,"SixtyFourInput %s failed: 0x%x\n",step,code);
        return code==kIOReturnSuccess;
    }
    bool submit(unsigned index) {
        UInt64 now; AbsoluteTime at;
        if(!ok((*audio)->GetBusFrameNumber(audio,&now,&at),"clock")) return false;
        auto& s=slots[index];
        if(!schedule.reserve(index,now,s.ticket)) return false;
        if(!expected) expected=s.ticket.frame;
        s.ready=false; s.frames={}; for(auto& f:s.frames) f.frReqCount=336;
        auto r=(*audio)->ReadIsochPipeAsync(audio,1,s.data.data(),s.ticket.frame,8,s.frames.data(),completion,&s);
        if(r) {schedule.failed(s.ticket); return false;} return true;
    }
    static void completion(void* context,IOReturn result,void*) {
        auto& s=*static_cast<Slot*>(context);auto& r=*s.owner;
        if(!r.schedule.completed(s.ticket)) {r.failed=true;return;}
        if(!r.schedule.running() || r.failed) return;
        if(result && result!=kIOReturnUnderrun) {r.failed=true;return;}
        s.ready=true;
        // Retain completed slots until all earlier bus frames can be delivered.
        for(;;) {
            Slot* next=nullptr;
            for(auto& candidate:r.slots) if(candidate.ready && candidate.ticket.frame==r.expected) {next=&candidate;break;}
            if(!next) break;
            std::array<float,8*7*12> samples{}; size_t count=0;
            for(unsigned i=0;i<8;++i) {
                auto& f=next->frames[i];size_t n=0;
                if((f.frStatus && f.frStatus!=kIOReturnUnderrun) || !f.frActCount ||
                   !sixty_four_decode_input(next->data.data()+i*336,f.frActCount,samples.data()+count*12,samples.size()-count*12,&n)) {r.failed=true;return;}
                count+=n;
            }
            uint64_t endHost=r.baseHost+uint64_t((next->ticket.frame+1-r.baseBus)*r.ticksPerMs);
            r.receive(samples.data(),count,endHost);
            ++r.expected;++r.delivered;
            if(!r.submit(next->ticket.slot)) {r.failed=true;return;}
        }
    }
    IOUSBDeviceInterface182** device=nullptr;
    IOUSBInterfaceInterface942** audio=nullptr;
    CFRunLoopSourceRef source=nullptr;
    bool deviceOpen=false,audioOpen=false,changed=false,alternate=false,failed=false;
    RecordingSchedule schedule;
    std::array<Slot,RecordingSchedule::slots> slots{};
    uint64_t expected=0,delivered=0,baseBus=0,baseHost=0;
    double ticksPerMs=0;
};
}
bool SixtyFourCapture::start() {
    if(thread.joinable() || poisoned.load()) return false;
    stopping.store(false);state.store(0);clockSeq.store(0);missing.store(0);
    try { thread=std::thread(&SixtyFourCapture::worker,this); }
    catch(...) {state.store(-1);return false;}
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(state.load()==0 && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(state.load()!=1) {stop();return false;} return true;
}
void SixtyFourCapture::stop() {
    stopping.store(true);if(thread.joinable()) thread.join();
}
void SixtyFourCapture::worker() {
    // On an undrained cancellation, retain the Session/DMA storage for process lifetime.
    timeline.clear();
    auto* session=new(std::nothrow) Session;
    if(!session) {state.store(-1);return;}
    uint64_t total=0,nextAnchor=0;
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    double ticksPerSample=1e9*tb.denom/(tb.numer*48000.0);
    try { session->receive=[&](const float* data,size_t count,uint64_t endHost) {
        timeline.write(total,data,count);total+=count;
        if(total>=nextAnchor) {
            uint64_t anchor=(total/clockPeriod)*clockPeriod;
            clockSeq.fetch_add(1,std::memory_order_seq_cst);
            clockSample.store(anchor,std::memory_order_seq_cst);
            clockHost.store(endHost-uint64_t((total-anchor)*ticksPerSample),std::memory_order_seq_cst);
            clockSeq.fetch_add(1,std::memory_order_seq_cst);
            nextAnchor=anchor+clockPeriod;
        }
        if(total>=2048 && state.load()==0) state.store(1);
    };
    } catch(...) {delete session;state.store(-1);return;}
    bool good=session->open() && session->run(stopping);
    if(!good) state.store(-1);
    bool restored=session->close();
    if(!restored) poisoned.store(true);
    if(session->drained) delete session;
    state.store(good && restored ? 2 : -1);
}
bool SixtyFourCapture::timestamp(double* sample,uint64_t* host) const {
    for(unsigned tries=0;tries<8;++tries) {
        auto a=clockSeq.load(std::memory_order_seq_cst);if(!a || (a&1)) continue;
        auto s=clockSample.load(std::memory_order_seq_cst),h=clockHost.load(std::memory_order_seq_cst);
        if(clockSeq.load(std::memory_order_seq_cst)==a) {*sample=double(s);*host=h;return true;}
    }
    return false;
}
