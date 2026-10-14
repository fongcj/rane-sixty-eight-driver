/* SPDX-License-Identifier: MIT */
#include "MidiStream.h"
#include "../protocol/sixty_four_midi.h"
#include "../protocol/sixty_four_descriptors.h"
#include <CoreMIDI/CoreMIDI.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <mach/mach_time.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
void number(CFMutableDictionaryRef d, CFStringRef key, int v) {
    auto n = CFNumberCreate(nullptr,kCFNumberIntType,&v);
    CFDictionarySetValue(d,key,n); CFRelease(n);
}
// Observe the audio owner's configuration. Never open or configure USB here.
bool verifyAudioSession(uint32_t& location) {
    auto match = IOServiceMatching("IOUSBHostDevice");
    if (!match) return false;
    number(match,CFSTR("idVendor"),0x1cc5); number(match,CFSTR("idProduct"),0x17);
    io_iterator_t it = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault,match,&it)) return false;
    auto service = IOIteratorNext(it), extra = IOIteratorNext(it); IOObjectRelease(it);
    if (!service || extra) {
        if (service) IOObjectRelease(service);
        if (extra) IOObjectRelease(extra);
        return false;
    }
    IOCFPlugInInterface** plugin = nullptr; SInt32 score = 0;
    auto r = IOCreatePlugInInterfaceForService(service,kIOUSBDeviceUserClientTypeID,
                                                kIOCFPlugInInterfaceID,&plugin,&score);
    IOObjectRelease(service);
    if (r || !plugin) return false;
    IOUSBDeviceInterface182** dev = nullptr;
    auto hr = (*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID182),(LPVOID*)&dev);
    (*plugin)->Release(plugin);
    if (hr != S_OK || !dev) return false;
    IOUSBConfigurationDescriptorPtr descriptor = nullptr;
    UInt8 configuration = 255;
    bool ok = !(*dev)->GetConfigurationDescriptorPtr(dev,0,&descriptor) && descriptor;
    if (ok) {
        auto p = reinterpret_cast<const uint8_t*>(descriptor);
        ok = (p[2] | (unsigned(p[3]) << 8)) == sizeof(expectedConfiguration) &&
             !std::memcmp(p,expectedConfiguration,sizeof(expectedConfiguration));
    }
    ok = ok && !(*dev)->GetConfiguration(dev,&configuration) && configuration == 1 &&
         !(*dev)->GetLocationID(dev,&location);
    (*dev)->Release(dev);
    return ok;
}
bool verifyHid(IOHIDDeviceRef device, uint32_t location) {
    static const uint8_t expected[]{0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0x19,0x01,0x29,0x02,0x15,0x00,0x25,
        0x01,0x75,0x08,0x95,0x40,0x81,0x02,0x19,0x01,0x29,0x02,0x91,0x02,0xc0};
    auto d = IOHIDDeviceGetProperty(device,CFSTR(kIOHIDReportDescriptorKey));
    auto l = IOHIDDeviceGetProperty(device,CFSTR(kIOHIDLocationIDKey));
    int64_t id = -1;
    return d && CFGetTypeID(d)==CFDataGetTypeID() &&
        CFDataGetLength(static_cast<CFDataRef>(d))==sizeof(expected) &&
        !std::memcmp(CFDataGetBytePtr(static_cast<CFDataRef>(d)),expected,sizeof(expected)) &&
        l && CFGetTypeID(l)==CFNumberGetTypeID() &&
        CFNumberGetValue(static_cast<CFNumberRef>(l),kCFNumberSInt64Type,&id) && uint64_t(id)==location;
}
struct Bridge {
    MIDIClientRef client = 0;
    MIDIEndpointRef source = 0;
    IOHIDDeviceRef hid = nullptr;
    sixty_four::MidiStream parser;
    bool ready = false, failed = false, clock = false, disconnected = false;
    bool notes[16][128]{};
    uint64_t reports = 0, messages = 0, clocks = 0, ignored = 0;
    void send(const uint8_t* data, size_t size) {
        if (size==1 && data[0]==0xf8 && !clock) { ++clocks; return; }
        MIDIPacketList packets{};
        auto packet = MIDIPacketListInit(&packets);
        if (!MIDIPacketListAdd(&packets,sizeof(packets),packet,mach_absolute_time(),size,data) ||
            MIDIReceived(source,&packets)) { failed = true; stopping = 1; return; }
        ++messages;
        if (size==3 && (data[0]&0xe0)==0x80)
            notes[data[0]&15][data[1]] = (data[0]&0xf0)==0x90 && data[2]!=0;
        if (size==3 && (data[0]&0xf0)==0xb0 && (data[1]==120 || data[1]==123))
            std::memset(notes[data[0]&15],0,sizeof(notes[0]));
    }
    void releaseNotes() {
        for (unsigned channel=0;channel<16;++channel)
            for (unsigned note=0;note<128;++note)
                if (notes[channel][note]) {
                    const uint8_t off[]{uint8_t(0x80|channel),uint8_t(note),0};
                    send(off,sizeof(off));
                }
        parser.reset();
    }
};
void removed(void* context, IOReturn, void*, IOHIDDeviceRef device) {
    auto& b = *static_cast<Bridge*>(context);
    if (device == b.hid) { b.ready = false; b.disconnected = true; stopping = 1; }
}
void received(void* context, IOReturn result, void* sender, IOHIDReportType type,
              uint32_t reportID, uint8_t* bytes, CFIndex length) {
    auto& b = *static_cast<Bridge*>(context);
    if (!b.ready || sender != b.hid || stopping) return;
    ++b.reports;
    if (result || type!=kIOHIDReportTypeInput || reportID || length!=64 || !bytes) {
        b.parser.reset(); ++b.ignored; return;
    }
    sixty_four::MidiEnvelope envelope;
    // Only the unsolicited transaction observed and verified for this mixer.
    if (!sixty_four::decodeMidiEnvelope(bytes,size_t(length),envelope)) {
        if (bytes[0]==0x40) b.parser.reset();
        ++b.ignored; return;
    }
    if (envelope.transaction != 0x1700c51c) { ++b.ignored; return; }
    b.parser.feed(envelope.bytes.data(),envelope.length,
        [&](const uint8_t* p,size_t n){ b.send(p,n); });
}
}
int main(int argc, char** argv) {
    const bool run = argc>=2 && !std::strcmp(argv[1],"--run-existing-audio");
    const bool clock = argc==3 && !std::strcmp(argv[2],"--include-clock");
    if (!run || argc>3 || (argc==3 && !clock)) {
        std::puts("Rane Sixty-Four input-only CoreMIDI bridge (foreground, no installation)\n"
            "Usage: sixty_four_midi_bridge --run-existing-audio [--include-clock]\n"
            "Start audio through the Sixty-Four first. Sends one tested HID startup packet.\n"
            "Creates MIDI source 'Rane Sixty-Four Controls'; no MIDI destination or LED output.\n"
            "Exits when audio's HID session ends, on error, or Ctrl-C. Never changes USB configuration.");
        return argc==1 ? 0 : 2;
    }
    char lockPath[128];
    std::snprintf(lockPath,sizeof(lockPath),"/tmp/org.openrane.sixtyfour.midi.%u.lock",unsigned(getuid()));
    const int lockFd=open(lockPath,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat st{};
    if (lockFd<0 || fstat(lockFd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=getuid() ||
        st.st_nlink!=1 || flock(lockFd,LOCK_EX|LOCK_NB)) {
        std::fputs("Another bridge is running, or its per-user lock is unavailable.\n",stderr);
        if (lockFd>=0) close(lockFd);
        return 1;
    }
    // Retain this lock until process exit, including all early-return cleanup.
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    uint32_t location=0;
    if (!verifyAudioSession(location)) {
        std::fputs("Require one exact Sixty-Four with audio already active (configuration 1). No changes made.\n",stderr);
        return 1;
    }
    Bridge b; b.clock=clock;
    auto manager=IOHIDManagerCreate(nullptr,kIOHIDOptionsTypeNone);
    if (!manager) return 1;
    auto match=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    number(match,CFSTR(kIOHIDVendorIDKey),0x1cc5); number(match,CFSTR(kIOHIDProductIDKey),0x17);
    IOHIDManagerSetDeviceMatching(manager,match); CFRelease(match);
    IOHIDManagerRegisterInputReportCallback(manager,received,&b);
    IOHIDManagerRegisterDeviceRemovalCallback(manager,removed,&b);
    IOHIDManagerScheduleWithRunLoop(manager,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
    int status=1;
    auto open=IOHIDManagerOpen(manager,kIOHIDOptionsTypeNone);
    if (!open) {
        auto deadline=CFAbsoluteTimeGetCurrent()+3;
        while (!b.hid && !stopping && CFAbsoluteTimeGetCurrent()<deadline) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,true);
            auto devices=IOHIDManagerCopyDevices(manager);
            if (devices) {
                if (CFSetGetCount(devices)==1) {
                    const void* value=nullptr; CFSetGetValues(devices,&value);
                    auto device=static_cast<IOHIDDeviceRef>(const_cast<void*>(value));
                    if (verifyHid(device,location)) { CFRetain(device); b.hid=device; }
                }
                CFRelease(devices);
            }
        }
        uint32_t rechecked=0;
        if (b.hid && !stopping && verifyAudioSession(rechecked) && rechecked==location &&
            !MIDIClientCreate(CFSTR("Rane Sixty-Four Controls Bridge"),nullptr,nullptr,&b.client) &&
            !MIDISourceCreate(b.client,CFSTR("Rane Sixty-Four Controls"),&b.source) &&
            !MIDIObjectSetIntegerProperty(b.source,kMIDIPropertyUniqueID,0x52363443)) {
            MIDIObjectSetStringProperty(b.source,kMIDIPropertyManufacturer,CFSTR("OpenRane"));
            MIDIObjectSetStringProperty(b.source,kMIDIPropertyModel,CFSTR("Sixty-Four Controls"));
            const uint8_t startup[64]{0x36,0xff,0xff,0xff,0xff,1};
            auto result=IOHIDDeviceSetReport(b.hid,kIOHIDReportTypeOutput,0,startup,sizeof(startup));
            if (!result) {
                b.ready=true;
                std::puts("READY: Rane Sixty-Four Controls MIDI source. Ctrl-C to stop."); std::fflush(stdout);
                while (!stopping && !b.failed) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,true);
                status=b.failed?1:0;
            } else std::fprintf(stderr,"Startup report failed: 0x%x; no retry\n",result);
        } else std::fputs("HID identity, active audio session, or CoreMIDI setup failed\n",stderr);
    } else std::fprintf(stderr,"Non-seizing HID open failed: 0x%x\n",open);
    b.ready=false;
    if (b.disconnected) std::puts("HID session ended; restart audio and the bridge to reconnect.");
    IOHIDManagerUnscheduleFromRunLoop(manager,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
    IOHIDManagerClose(manager,kIOHIDOptionsTypeNone); CFRelease(manager);
    if (b.hid) CFRelease(b.hid);
    if (b.source) { b.releaseNotes(); MIDIEndpointDispose(b.source); }
    if (b.client) MIDIClientDispose(b.client);
    std::printf("Stopped: reports=%llu messages=%llu filtered_clock=%llu ignored=%llu. USB configuration untouched.\n",
        (unsigned long long)b.reports,(unsigned long long)b.messages,
        (unsigned long long)b.clocks,(unsigned long long)b.ignored);
    return b.failed?1:status;
}
