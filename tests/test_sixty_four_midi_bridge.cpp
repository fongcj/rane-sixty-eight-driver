/* SPDX-License-Identifier: MIT */
#include <CoreMIDI/CoreMIDI.h>
#include <vector>
#include <cstdlib>
static std::vector<std::vector<uint8_t>> delivered;
static OSStatus deliveryResult = 0;
static OSStatus mockReceived(MIDIEndpointRef, const MIDIPacketList* list) {
    auto p=&list->packet[0];
    for (UInt32 i=0;i<list->numPackets;++i) {
        delivered.emplace_back(p->data,p->data+p->length); p=MIDIPacketNext(p);
    }
    return deliveryResult;
}
#define MIDIReceived mockReceived
#define main bridge_main
#include "../src/midi64/main.cpp"
#undef main
#undef MIDIReceived
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    Bridge b; b.ready=true; b.source=42;
    b.hid=reinterpret_cast<IOHIDDeviceRef>(uintptr_t(1));
    uint8_t report[64]{0x40,0x1c,0xc5,0,0x17,3,0x90,104,64};
    auto receive=[&]{received(&b,0,b.hid,kIOHIDReportTypeInput,0,report,64);};
    receive(); check(delivered.size()==1 && delivered[0]==std::vector<uint8_t>({0x90,104,64}));
    check(b.notes[0][104]);
    report[5]=1; report[6]=0xf8; receive();
    check(delivered.size()==1 && b.clocks==1);
    b.clock=true; receive(); check(delivered.back()==std::vector<uint8_t>({0xf8}));
    report[0]=0x36; receive(); check(delivered.size()==2);
    report[0]=0x40; report[4]=0; receive(); check(delivered.size()==2);
    report[4]=0x17;
    received(&b,0,nullptr,kIOHIDReportTypeInput,0,report,64); check(delivered.size()==2);
    // Bad report cancels a pending message, so later data cannot complete it.
    report[5]=2; report[6]=0x90; report[7]=105; receive();
    received(&b,kIOReturnError,b.hid,kIOHIDReportTypeInput,0,report,64);
    report[5]=1; report[6]=64; receive(); check(delivered.size()==2);
    b.releaseNotes(); check(delivered.back()==std::vector<uint8_t>({0x80,104,0}));
    check(!b.notes[0][104]);
    removed(&b,0,nullptr,b.hid); check(stopping && !b.ready);
    stopping=0; b.ready=true; deliveryResult=-1;
    report[5]=3; report[6]=0x90; report[7]=104; report[8]=64; receive();
    check(b.failed && stopping);
    puts("Bridge filtering, note cleanup, removal, and CoreMIDI failure tests passed (mock delivery)");
}
