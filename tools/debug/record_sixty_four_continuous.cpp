/* SPDX-License-Identifier: MIT */
// Explicit active diagnostic. No HAL installation, playback, MIDI or vendor writes.
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
#include "../../src/transport/RecordingBuffer.h"

static volatile sig_atomic_t interrupted;
static void signalStop(int) { interrupted=1; }
static void number(CFMutableDictionaryRef d, CFStringRef key, int value) {
    auto n=CFNumberCreate(nullptr,kCFNumberIntType,&value);
    CFDictionarySetValue(d,key,n); CFRelease(n);
}
class Recorder {
public:
    static constexpr size_t batchCount=30000; // 30 seconds in USB bus time
    Recorder() : buffer(batchCount) {}
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
    bool run() {
        UInt64 now; AbsoluteTime at;
        if(!ok((*audio)->GetBusFrameNumber(audio,&now,&at),"start clock") || !schedule.begin(now)) return false;
        startBus=now; startHost=mach_absolute_time();
        for(unsigned i=0;i<slots.size();++i) {
            slots[i].owner=this;
            if(!submit(i)) {failed=true; break;}
        }
        std::puts("RECORDING: continuous 30-second input-only capture; no playback");
        auto deadline=CFAbsoluteTimeGetCurrent()+35;
        while(schedule.pending() && !failed && !interrupted && CFAbsoluteTimeGetCurrent()<deadline)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);
        if(schedule.pending() || interrupted) failed=true;
        schedule.stop();
        endHost=mach_absolute_time();
        std::printf("Batches=%zu/%zu, short packets=%llu, packet errors=%llu, scheduling faults=%llu\n",
            buffer.completed(),batchCount,(unsigned long long)shortPackets,(unsigned long long)packetErrors,
            (unsigned long long)schedule.faults());
        return !failed && buffer.complete();
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
    bool save(const char* path) const;
    // Exercised explicitly offline: no open(), run() or hardware API access.
    bool fixture(const char* path) {
        buffer.setFirstBusFrame(100);
        std::array<uint8_t,8*336> data{};
        std::array<uint16_t,8> lengths; lengths.fill(288);
        for(unsigned m=0;m<8;++m) {
            for(unsigned f=0;f<6;++f) {
                unsigned offset=m*336+f*48;
                data[offset+2]=0x80; data[offset+3]=0x3e; // channel 1 = .25
                data[offset+6]=0x00; data[offset+7]=0xbf; // channel 2 = -.5
            }
        }
        // Reverse arrival order exercises scheduled-order storage and WAV output.
        for(size_t i=batchCount;i>0;--i)
            if(!buffer.store(100+i-1,i,data.data(),lengths)) return false;
        startHost=endHost=mach_absolute_time();
        return save(path);
    }
    bool drained=true;
private:
    struct Slot {
        Recorder* owner=nullptr;
        RecordingSchedule::Ticket ticket{};
        std::array<uint8_t,8*336> data{};
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
        if(!submitted) { firstBus=s.ticket.frame; buffer.setFirstBusFrame(firstBus); }
        s.frames={};
        for(auto& f:s.frames) f.frReqCount=336;
        auto result=(*audio)->ReadIsochPipeAsync(audio,1,s.data.data(),s.ticket.frame,8,s.frames.data(),completion,&s);
        if(!ok(result,"submit input")) { schedule.failed(s.ticket); return false; }
        ++submitted; return true;
    }
    static void completion(void* context,IOReturn result,void*) {
        auto& s=*static_cast<Slot*>(context); auto& r=*s.owner;
        if(!r.schedule.completed(s.ticket)) { r.failed=true; return; }
        if(!r.schedule.running() || r.failed) return;
        if(result!=kIOReturnSuccess && result!=kIOReturnUnderrun) { ++r.packetErrors; r.failed=true; return; }
        std::array<uint16_t,8> lengths{};
        for(unsigned i=0;i<8;++i) {
            auto& f=s.frames[i];
            if(f.frStatus!=kIOReturnSuccess && f.frStatus!=kIOReturnUnderrun) { ++r.packetErrors; r.failed=true; return; }
            if(f.frStatus==kIOReturnUnderrun) ++r.shortPackets;
            lengths[i]=f.frActCount;
        }
        if(!r.buffer.store(s.ticket.frame,mach_absolute_time(),s.data.data(),lengths)) { ++r.packetErrors; r.failed=true; return; }
        if(!r.submit(s.ticket.slot)) r.failed=true;
    }
    IOUSBDeviceInterface182** device=nullptr;
    IOUSBInterfaceInterface942** audio=nullptr;
    CFRunLoopSourceRef source=nullptr;
    bool deviceOpen=false,audioOpen=false,changed=false,alternate=false,failed=false;
    RecordingSchedule schedule;
    RecordingBuffer buffer;
    std::array<Slot,RecordingSchedule::slots> slots{};
    size_t submitted=0;
    uint64_t shortPackets=0,packetErrors=0,firstBus=0,startBus=0,startHost=0,endHost=0;
};

static bool u16(FILE* f,uint16_t v) { uint8_t p[]={uint8_t(v),uint8_t(v>>8)}; return fwrite(p,1,2,f)==2; }
static bool u32(FILE* f,uint32_t v) { uint8_t p[]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)}; return fwrite(p,1,4,f)==4; }
bool Recorder::save(const char* path) const {
    if(!buffer.complete()) return false;
    uint32_t count=0; for(auto& b:buffer.batches()) count+=b.frames;
    // A complete timeline with the wrong sample count is not a valid 48 kHz recording.
    constexpr uint32_t nominal=30*48000;
    if(count < nominal-nominal/200 || count > nominal+nominal/200) {
        std::fprintf(stderr,"Sample count %u outside 48 kHz +/-0.5%% diagnostic gate\n",count); return false;
    }
    uint32_t bytes=count*48;
    FILE* f=fopen(path,"wbx"); if(!f) { perror("New WAV file"); return false; }
    bool good=fwrite("RIFF",1,4,f)==4 && u32(f,48+bytes) && fwrite("WAVEfmt ",1,8,f)==8 &&
        u32(f,16) && u16(f,3) && u16(f,12) && u32(f,48000) && u32(f,48000*48) && u16(f,48) && u16(f,32) &&
        fwrite("fact",1,4,f)==4 && u32(f,4) && u32(f,count) && fwrite("data",1,4,f)==4 && u32(f,bytes);
    for(auto& b:buffer.batches()) {
        if(!good) break;
        good=fwrite(b.samples.data(),48,b.frames,f)==b.frames; // macOS arm64/x86 are little-endian
    }
    if(fclose(f)) good=false;
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    double wall=double(endHost-startHost)*tb.numer/tb.denom/1e9;
    std::printf("Frames=%u nominal audio seconds=%.6f bus seconds=30 callback-wall seconds=%.6f first-bus=%llu start-bus=%llu\n",
        count,count/48000.0,wall,(unsigned long long)firstBus,(unsigned long long)startBus);
    if(!good) std::fprintf(stderr,"WAV write failed; output file is incomplete\n");
    return good;
}
int main(int argc,char** argv) {
    bool offline=argc==3 && !std::strcmp(argv[1],"--offline-fixture");
    if(argc!=3 || (!offline && std::strcmp(argv[1],"--record-30s"))) {
        std::fprintf(stderr,"Usage: %s --record-30s NEW_12_CHANNEL_WAV\nActive recording-only test; restores USB settings; never overwrites files.\n",argv[0]); return 2;
    }
    std::signal(SIGINT,signalStop); std::signal(SIGTERM,signalStop); setvbuf(stdout,nullptr,_IOLBF,0);
    Recorder* r=nullptr;
    try { r=new Recorder; } catch(const std::bad_alloc&) { std::fputs("Capture allocation failed\n",stderr); return 1; }
    if(offline) { bool good=r->fixture(argv[2]); delete r; return good?0:1; }
    bool captured=r->open() && r->run();
    bool restored=r->close();
    bool saved=captured && restored && r->save(argv[2]);
    if(r->drained) delete r;
    return saved?0:1;
}
