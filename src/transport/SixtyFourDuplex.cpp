/* SPDX-License-Identifier: MIT */
#include "SixtyFourDuplex.h"
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
using D=sixtyfour::DuplexDiagnostics;
namespace {
class Session {
public:
    explicit Session(D& telemetry):diag(telemetry) {}
    bool drained=true;
    std::function<bool(uint64_t,const std::array<unsigned,8>&,uint8_t*,std::array<IOUSBIsocFrame,8>&)> produce;
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
            else if(!result && index==1 && !playback) playback=candidate;
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
        if(!playback || !ok((*playback)->USBInterfaceOpen(playback),"playback open")) return false;
        playbackOpen=true;
        alt=255;
        if(!ok((*playback)->GetAlternateSetting(playback,&alt),"playback alternate") || alt!=0) return false;
        playbackAlternate=true;
        if(!ok((*playback)->SetAlternateInterface(playback,1),"playback alt 1")) return false;
        if(!ok((*playback)->GetNumEndpoints(playback,&count),"playback endpoint count") || count!=1) return false;
        if(!ok((*playback)->GetPipeProperties(playback,1,&dir,&num,&type,&size,&interval),"playback endpoint") ||
           dir!=kUSBOut || num!=6 || type!=kUSBIsoc || size!=280 || interval!=1) return false;
        if(!ok((*playback)->CreateInterfaceAsyncEventSource(playback,&playbackSource),"playback event source") || !playbackSource) return false;
        CFRunLoopAddSource(CFRunLoopGetCurrent(),playbackSource,kCFRunLoopDefaultMode);
        return true;
    }
    bool close() {
        schedule.stop();
        bool restored=true;
        if(schedule.pending() && audio) restored=ok((*audio)->AbortPipe(audio,1),"abort input") && restored;
        if(outputPending && playback) restored=ok((*playback)->AbortPipe(playback,1),"abort output") && restored;
        auto deadline=CFAbsoluteTimeGetCurrent()+2;
        while((schedule.pending() || outputPending) && CFAbsoluteTimeGetCurrent()<deadline)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);
        if(playbackAlternate) {
            UInt8 alt=255;
            bool reset=ok((*playback)->SetAlternateInterface(playback,0),"restore playback alt");
            reset=ok((*playback)->GetAlternateSetting(playback,&alt),"verify playback alt") && alt==0 && reset;
            std::printf("Playback alternate restored: %s\n",reset?"yes":"NO");restored=reset && restored;
        }
        if(alternate) {
            UInt8 alt=255;
            bool reset=ok((*audio)->SetAlternateInterface(audio,0),"restore alt 0");
            reset=ok((*audio)->GetAlternateSetting(audio,&alt),"verify alternate") && alt==0 && reset;
            std::printf("Recording alternate restored: %s\n",reset?"yes":"NO"); restored=reset && restored;
        }
        // If callbacks fail to drain, keep owner, DMA buffers and interfaces alive
        // until this process exits. Never free memory still owned by USB.
        drained=!schedule.pending() && !outputPending;
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
        if(playbackSource) {CFRunLoopRemoveSource(CFRunLoopGetCurrent(),playbackSource,kCFRunLoopDefaultMode);CFRelease(playbackSource);playbackSource=nullptr;}
        if(playbackOpen) restored=ok((*playback)->USBInterfaceClose(playback),"close playback") && restored;
        if(playback) {(*playback)->Release(playback);playback=nullptr;}
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
            if(CFAbsoluteTimeGetCurrent()-lastProgress>1) {diag.add(D::workerStalls);diag.failure(6);failed=true;}
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
    struct OutputSlot {
        Session* owner=nullptr;
        bool busy=false;
        std::array<uint8_t,8*280> data{};
        std::array<IOUSBIsocFrame,8> frames{};
    };
    bool submitOutput(uint64_t bus,uint64_t sample,const std::array<unsigned,8>& counts) {
        if(bus>UINT64_MAX-SixtyFourDuplex::outputLeadBusFrames) return false;
        uint64_t target=bus+SixtyFourDuplex::outputLeadBusFrames;
        UInt64 now;AbsoluteTime at;
        if(!ok((*playback)->GetBusFrameNumber(playback,&now,&at),"output bus clock") || target<=now) {diag.add(D::outputScheduleFaults);diag.failure(4);return false;}
        auto& s=outSlots[bus%outSlots.size()];
        if(s.busy || sample>UINT64_MAX-SixtyFourDuplex::outputLeadSamples) {diag.add(D::outputSlotBusy);diag.failure(4);return false;}
        s.owner=this;s.frames={};s.data={};
        if(!produce(sample+SixtyFourDuplex::outputLeadSamples,counts,s.data.data(),s.frames)) {diag.add(D::outputScheduleFaults);diag.failure(4);return false;}
        s.busy=true;++outputPending;
        auto result=(*playback)->WriteIsochPipeAsync(playback,1,s.data.data(),target,8,s.frames.data(),outputCompletion,&s);
        if(!ok(result,"submit output")) {diag.add(D::outputTransferErrors);diag.failure(5,uint32_t(result));s.busy=false;--outputPending;return false;}
        return true;
    }
    static void outputCompletion(void* context,IOReturn result,void*) {
        auto& s=*static_cast<OutputSlot*>(context);auto& r=*s.owner;
        if(!s.busy) {r.diag.add(D::outputTransferErrors);r.diag.failure(5);r.failed=true;return;}s.busy=false;--r.outputPending;
        if(!r.schedule.running() || r.failed) return;
        r.gap(D::maxOutputCallbackGapNs,r.previousOutputTicks);
        if(result) {r.diag.add(D::outputTransferErrors);r.diag.failure(5,uint32_t(result));r.failed=true;return;}
        for(auto& f:s.frames) if(f.frStatus) {r.diag.add(D::outputTransferErrors);r.diag.failure(5,uint32_t(f.frStatus));r.failed=true;return;}
        r.diag.add(D::outputBatches);
        uint64_t frames=0;for(auto& f:s.frames)frames+=f.frReqCount/40;
        r.diag.add(D::outputFrames,frames);
    }
    static bool ok(IOReturn code,const char* step) {
        if(code) std::fprintf(stderr,"SixtyFourDuplex %s failed: 0x%x\n",step,code);
        return code==kIOReturnSuccess;
    }
    bool submit(unsigned index) {
        UInt64 now; AbsoluteTime at;
        if(!ok((*audio)->GetBusFrameNumber(audio,&now,&at),"clock")) {diag.add(D::inputScheduleFaults);diag.failure(2);return false;}
        auto& s=slots[index];
        if(!schedule.reserve(index,now,s.ticket)) {diag.add(D::inputScheduleFaults);diag.failure(2);return false;}
        if(!expected) expected=s.ticket.frame;
        s.ready=false; s.frames={}; for(auto& f:s.frames) f.frReqCount=336;
        auto r=(*audio)->ReadIsochPipeAsync(audio,1,s.data.data(),s.ticket.frame,8,s.frames.data(),completion,&s);
        if(r) {diag.add(D::inputTransferErrors);diag.failure(3,uint32_t(r));schedule.failed(s.ticket); return false;} return true;
    }
    static void completion(void* context,IOReturn result,void*) {
        auto& s=*static_cast<Slot*>(context);auto& r=*s.owner;
        if(!r.schedule.completed(s.ticket)) {r.diag.add(D::inputScheduleFaults);r.diag.failure(2);r.failed=true;return;}
        if(!r.schedule.running() || r.failed) return;
        r.gap(D::maxInputCallbackGapNs,r.previousInputTicks);
        if(result && result!=kIOReturnUnderrun) {r.diag.add(D::inputTransferErrors);r.diag.failure(3,uint32_t(result));r.failed=true;return;}
        s.ready=true;
        // Retain completed slots until all earlier bus frames can be delivered.
        for(;;) {
            Slot* next=nullptr;
            for(auto& candidate:r.slots) if(candidate.ready && candidate.ticket.frame==r.expected) {next=&candidate;break;}
            if(!next) break;
            std::array<float,8*7*12> samples{}; size_t count=0;
            std::array<unsigned,8> counts{};
            for(unsigned i=0;i<8;++i) {
                auto& f=next->frames[i];size_t n=0;
                if((f.frStatus && f.frStatus!=kIOReturnUnderrun) || !f.frActCount ||
                   !sixty_four_decode_input(next->data.data()+i*336,f.frActCount,samples.data()+count*12,samples.size()-count*12,&n)) {r.diag.add(D::decodeErrors);r.diag.failure(3,uint32_t(f.frStatus));r.failed=true;return;}
                counts[i]=unsigned(n);count+=n;
            }
            uint64_t endHost=r.baseHost+uint64_t((next->ticket.frame+1-r.baseBus)*r.ticksPerMs);
            if(!r.submitOutput(next->ticket.frame,r.inputSamples,counts)) {r.failed=true;return;}
            r.diag.add(D::inputBatches);r.diag.add(D::inputFrames,count);
            r.inputSamples+=count;
            r.receive(samples.data(),count,endHost);
            ++r.expected;++r.delivered;
            if(!r.submit(next->ticket.slot)) {r.failed=true;return;}
        }
    }
    void gap(D::Metric metric,uint64_t& previous) {
        uint64_t now=mach_absolute_time();
        if(previous && now>=previous)diag.maximum(metric,uint64_t((now-previous)*1e6/ticksPerMs));
        previous=now;
    }
    D& diag;
    uint64_t previousInputTicks=0,previousOutputTicks=0;
    IOUSBDeviceInterface182** device=nullptr;
    IOUSBInterfaceInterface942** audio=nullptr;
    IOUSBInterfaceInterface942** playback=nullptr;
    CFRunLoopSourceRef playbackSource=nullptr;
    bool playbackOpen=false,playbackAlternate=false;
    std::array<OutputSlot,128> outSlots{};
    uint64_t outputPending=0,inputSamples=0;
    CFRunLoopSourceRef source=nullptr;
    bool deviceOpen=false,audioOpen=false,changed=false,alternate=false,failed=false;
    RecordingSchedule schedule;
    std::array<Slot,RecordingSchedule::slots> slots{};
    uint64_t expected=0,delivered=0,baseBus=0,baseHost=0;
    double ticksPerMs=0;
};
}
bool SixtyFourDuplex::start() {
    if(thread.joinable() || poisoned.load()) return false;
    diagnostics.reset();sessionNumber.fetch_add(1);previousWriteTicks=0;
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);nanosPerTick=double(tb.numer)/tb.denom;
    stopping.store(false);state.store(0);clockSeq.store(0);missing.store(0);
    try { thread=std::thread(&SixtyFourDuplex::worker,this); }
    catch(...) {diagnostics.add(D::startFailures);diagnostics.failure(1);state.store(-1);return false;}
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(state.load()==0 && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(state.load()!=1) {stop();return false;} return true;
}
void SixtyFourDuplex::stop() {
    stopping.store(true);if(thread.joinable()) thread.join();
}
void SixtyFourDuplex::worker() {
    // On an undrained cancellation, retain the Session/DMA storage for process lifetime.
    timeline.clear();output.reset();
    auto* session=new(std::nothrow) Session(diagnostics);
    if(!session) {diagnostics.add(D::startFailures);diagnostics.failure(1);state.store(-1);return;}
    uint64_t total=0,nextAnchor=0;
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    double ticksPerSample=1e9*tb.denom/(tb.numer*48000.0);
    try {
    session->produce=[&](uint64_t first,const std::array<unsigned,8>& counts,uint8_t* packed,std::array<IOUSBIsocFrame,8>& frames) {
        sixtyfour::playback::Batch batch;
        if(!sixtyfour::playback::prepare(output,first,counts,batch)) return false;
        size_t offset=0;
        for(unsigned i=0;i<8;++i) {
            frames[i].frReqCount=batch.lengths[i];
            std::memcpy(packed+offset,batch.packets[i].data(),batch.lengths[i]);offset+=batch.lengths[i];
        }
        return true;
    };
    session->receive=[&](const float* data,size_t count,uint64_t endHost) {
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
    } catch(...) {delete session;diagnostics.add(D::startFailures);diagnostics.failure(1);state.store(-1);return;}
    bool good=session->open() && session->run(stopping);
    if(!good) {if(diagnostics.snapshot()[D::lastFailureStage]==0) {diagnostics.add(D::startFailures);diagnostics.failure(1);}state.store(-1);}
    bool restored=session->close();
    if(!restored) {diagnostics.add(D::cleanupFailures);diagnostics.failure(7);poisoned.store(true);}
    if(session->drained) delete session;
    state.store(good && restored ? 2 : -1);
}
bool SixtyFourDuplex::timestamp(double* sample,uint64_t* host) const {
    for(unsigned tries=0;tries<8;++tries) {
        auto a=clockSeq.load(std::memory_order_seq_cst);if(!a || (a&1)) continue;
        auto s=clockSample.load(std::memory_order_seq_cst),h=clockHost.load(std::memory_order_seq_cst);
        if(clockSeq.load(std::memory_order_seq_cst)==a) {*sample=double(s);*host=h;return true;}
    }
    return false;
}

bool SixtyFourDuplex::write(double sample,unsigned frames,const float* data) {
    diagnostics.add(D::writeCalls);
    uint64_t now=mach_absolute_time();
    if(previousWriteTicks && now>=previousWriteTicks)diagnostics.maximum(D::maxWriteGapNs,uint64_t((now-previousWriteTicks)*nanosPerTick));
    previousWriteTicks=now;
    if(!healthy()) {diagnostics.add(D::writeFailures);return false;}
    if(!std::isfinite(sample) || sample<0 || sample>=std::ldexp(1.0,64) || std::floor(sample)!=sample) {
        diagnostics.add(D::invalidWriteTimes);diagnostics.add(D::writeFailures);return false;
    }
    bool good=output.push(uint64_t(sample),data,frames);
    if(!good)diagnostics.add(D::writeFailures);
    return good;
}
