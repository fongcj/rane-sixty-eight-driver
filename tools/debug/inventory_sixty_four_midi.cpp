/* SPDX-License-Identifier: MIT */
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>

static void properties(MIDIObjectRef object, const char* kind) {
    std::printf("%s ref=%u", kind, unsigned(object));
    const CFStringRef strings[]{kMIDIPropertyName, kMIDIPropertyManufacturer,
                               kMIDIPropertyModel};
    const char* labels[]{"name", "manufacturer", "model"};
    for (unsigned i=0;i<3;++i) {
        CFStringRef value=nullptr;
        if (!MIDIObjectGetStringProperty(object, strings[i], &value) && value) {
            char text[512]{};
            if (CFStringGetCString(value,text,sizeof(text),kCFStringEncodingUTF8))
                std::printf(" %s=\"%s\"",labels[i],text);
            CFRelease(value);
        }
    }
    const CFStringRef numbers[]{kMIDIPropertyUniqueID, CFSTR("USBVendorProduct"),
                               CFSTR("USBLocationID"), kMIDIPropertyOffline};
    const char* numberLabels[]{"uniqueID", "USBVendorProduct", "USBLocationID", "offline"};
    for (unsigned i=0;i<4;++i) {
        SInt32 value=0;
        if (!MIDIObjectGetIntegerProperty(object,numbers[i],&value))
            std::printf(" %s=0x%08x",numberLabels[i],unsigned(value));
    }
    std::putchar('\n');
}
int main(int argc,char** argv) {
    if (argc!=2 || std::strcmp(argv[1],"--list")) {
        std::puts("Usage: inventory_sixty_four_midi --list\nRead-only CoreMIDI inventory; no endpoints created, messages sent, or USB opened.");
        return argc==1?0:2;
    }
    MIDIClientRef client=0;
    auto status=MIDIClientCreate(CFSTR("Sixty-Four MIDI Inventory"),nullptr,nullptr,&client);
    if (status) { std::fprintf(stderr,"MIDIClientCreate failed: %d\n",int(status)); return 1; }
    for (ItemCount i=0;i<MIDIGetNumberOfDevices();++i) {
        auto device=MIDIGetDevice(i); properties(device,"device");
        for (ItemCount j=0;j<MIDIDeviceGetNumberOfEntities(device);++j) {
            auto entity=MIDIDeviceGetEntity(device,j); properties(entity,"  entity");
            std::printf("    sources=%lu destinations=%lu\n",
                (unsigned long)MIDIEntityGetNumberOfSources(entity),
                (unsigned long)MIDIEntityGetNumberOfDestinations(entity));
        }
    }
    for (unsigned direction=0;direction<2;++direction) {
        auto count=direction?MIDIGetNumberOfDestinations():MIDIGetNumberOfSources();
        for (ItemCount i=0;i<count;++i) {
            auto endpoint=direction?MIDIGetDestination(i):MIDIGetSource(i);
            properties(endpoint,direction?"destination":"source");
            MIDIEntityRef entity=0; MIDIDeviceRef device=0;
            auto error=MIDIEndpointGetEntity(endpoint,&entity);
            if (!error && entity) MIDIEntityGetDevice(entity,&device);
            std::printf("  parent_entity=%u parent_device=%u entity_status=%d\n",
                        unsigned(entity),unsigned(device),int(error));
        }
    }
    MIDIClientDispose(client);
    return 0;
}
