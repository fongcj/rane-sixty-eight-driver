/* SPDX-License-Identifier: MIT */
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <cstdio>
#include <cstring>
static std::atomic<unsigned> messages{0};
static void readPackets(const MIDIPacketList* list, void*, void*) {
    auto p=&list->packet[0];
    for (UInt32 i=0;i<list->numPackets;++i) {
        std::printf("MIDI timestamp=%llu bytes=",(unsigned long long)p->timeStamp);
        for (UInt16 j=0;j<p->length;++j) std::printf("%02x%s",p->data[j],j+1==p->length?"":" ");
        std::putchar('\n'); ++messages; p=MIDIPacketNext(p);
    }
    std::fflush(stdout);
}
int main(int argc,char** argv) {
    if (argc!=2 || std::strcmp(argv[1],"--listen-60s")) {
        std::puts("Usage: monitor_sixty_four_midi --listen-60s\nRead-only subscriber to 'Rane Sixty-Four Controls'; sends no MIDI or USB messages.");
        return argc==1?0:2;
    }
    MIDIClientRef client=0; MIDIPortRef port=0; MIDIEndpointRef source=0;
    if (MIDIClientCreate(CFSTR("Sixty-Four Diagnostic Listener"),nullptr,nullptr,&client)) return 1;
    unsigned matches=0;
    for (ItemCount i=0;i<MIDIGetNumberOfSources();++i) {
        auto candidate=MIDIGetSource(i); CFStringRef name=nullptr;
        if (!MIDIObjectGetStringProperty(candidate,kMIDIPropertyName,&name) && name) {
            if (CFEqual(name,CFSTR("Rane Sixty-Four Controls"))) {source=candidate; ++matches;}
            CFRelease(name);
        }
    }
    int result=1;
    if (matches==1 && !MIDIInputPortCreate(client,CFSTR("Input"),readPackets,nullptr,&port) &&
        !MIDIPortConnectSource(port,source,nullptr)) {
        std::puts("READY: CoreMIDI subscriber connected for 60 seconds"); std::fflush(stdout);
        const auto end=CFAbsoluteTimeGetCurrent()+60;
        while (CFAbsoluteTimeGetCurrent()<end) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
        MIDIPortDisconnectSource(port,source);
        result=0;
    } else std::fputs("Require exactly one bridge source; source lookup or connection failed\n",stderr);
    if (port) MIDIPortDispose(port);
    MIDIClientDispose(client);
    std::printf("CoreMIDI messages received: %u\n",messages.load());
    return result;
}
