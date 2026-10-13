/* SPDX-License-Identifier: MIT */
/* Explicit active experiment, NOT passive discovery.
 * Temporarily selects configuration 1 and recording alt 1, restores both.
 * No playback, HID output, vendor commands, or seize fallback.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <mach/mach_time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

static const uint8_t expectedConfiguration[] = {
    0x09, 0x02, 0xeb, 0x00, 0x04, 0x01, 0x00, 0xc0, 0x32, 0x09, 0x04, 0x00, 0x00, 0x00, 0xff, 0x01,
    0x20, 0x04, 0x09, 0x24, 0x01, 0x00, 0x02, 0x0a, 0x4b, 0x00, 0x00, 0x08, 0x24, 0x0a, 0x05, 0x01,
    0x01, 0x00, 0x00, 0x11, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00, 0x05, 0x0a, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x0c, 0x24, 0x03, 0x02, 0x02, 0x06, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00,
    0x11, 0x24, 0x02, 0x03, 0x02, 0x06, 0x00, 0x05, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
    0x00, 0x0c, 0x24, 0x03, 0x04, 0x01, 0x01, 0x00, 0x03, 0x05, 0x00, 0x00, 0x00, 0x09, 0x04, 0x01,
    0x00, 0x00, 0xff, 0x02, 0x20, 0x00, 0x09, 0x04, 0x01, 0x01, 0x01, 0xff, 0x02, 0x20, 0x00, 0x10,
    0x24, 0x01, 0x01, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x05, 0x06,
    0x24, 0x02, 0x01, 0x04, 0x20, 0x07, 0x05, 0x06, 0x05, 0x18, 0x01, 0x01, 0x08, 0x25, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x04, 0x02, 0x00, 0x00, 0xff, 0x02, 0x20, 0x00, 0x09, 0x04, 0x02,
    0x01, 0x01, 0xff, 0x02, 0x20, 0x00, 0x10, 0x24, 0x01, 0x03, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x06, 0x24, 0x02, 0x01, 0x04, 0x20, 0x07, 0x05, 0x82, 0x25,
    0x50, 0x01, 0x01, 0x08, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x04, 0x03, 0x00, 0x02,
    0x03, 0x00, 0x00, 0x00, 0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0x1c, 0x00, 0x07, 0x05, 0x81,
    0x03, 0x40, 0x00, 0x01, 0x07, 0x05, 0x01, 0x03, 0x40, 0x00, 0x01
};
static volatile sig_atomic_t interrupted = 0;
static void stop_signal(int sig) { (void)sig; interrupted = 1; }


static uint8_t samples[80 * 336];
static IOUSBIsocFrame frames[80];
static bool pending;
static IOReturn completionResult;
static void completed(void *context, IOReturn result, void *arg) {
    (void)context; (void)arg; completionResult = result; pending = false;
}
static void match_number(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, n); CFRelease(n);
}
int main(int argc, char **argv) {
    bool faderTest = argc == 3 && !strcmp(argv[1], "--fader-test-20s");
    if (argc != 3 || (!faderTest && strcmp(argv[1], "--record-packets-10s"))) {
        fprintf(stderr, "Usage: %s --record-packets-10s NEW_RAW_FILE (or --fader-test-20s NEW_RAW_FILE)\n", argv[0]); return 2;
    }
    FILE *raw = fopen(argv[2], "wbx");
    if (!raw) { perror("New capture file"); return 1; }
    signal(SIGINT, stop_signal); signal(SIGTERM, stop_signal);
    setvbuf(stdout, NULL, _IOLBF, 0);
    CFMutableDictionaryRef matching = IOServiceMatching("IOUSBHostDevice");
    if (!matching) return 1;
    match_number(matching, CFSTR("idVendor"), 0x1cc5);
    match_number(matching, CFSTR("idProduct"), 0x0017);
    io_iterator_t iter = 0;
    IOReturn kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
    if (kr != kIOReturnSuccess) return 1;
    io_service_t service = IOIteratorNext(iter), extra = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (!service || extra) {
        if (service) IOObjectRelease(service);
        if (extra) IOObjectRelease(extra);
        fprintf(stderr, "Expected exactly one Sixty-Four; aborting\n"); return 1;
    }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kr = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID,
                                          kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (kr != kIOReturnSuccess || !plugin) return 1;
    IOUSBDeviceInterface182 **dev = NULL;
    HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID182), (LPVOID*)&dev);
    (*plugin)->Release(plugin);
    if (hr != S_OK || !dev) return 1;
    int status = 1;
    UInt8 original = 0;
    bool deviceOpen = false, changed = false;
    IOUSBInterfaceInterface942 **audio = NULL;
    bool opened = false, alternate = false;
    CFRunLoopSourceRef source = NULL;
    UInt8 pipe = 0;
    IOUSBConfigurationDescriptorPtr config = NULL;
    kr = (*dev)->GetConfigurationDescriptorPtr(dev, 0, &config);
    if (kr != kIOReturnSuccess || !config) goto cleanup;
    const uint8_t *bytes = (const uint8_t*)config;
    if ((bytes[2] | ((unsigned)bytes[3]<<8)) != sizeof(expectedConfiguration) ||
        memcmp(bytes, expectedConfiguration, sizeof(expectedConfiguration))) {
        fprintf(stderr, "Descriptor fingerprint mismatch; refusing activation\n"); goto cleanup;
    }
    kr = (*dev)->GetConfiguration(dev, &original);
    if (kr != kIOReturnSuccess) goto cleanup;
    if (original != 0) {
        fprintf(stderr, "Device already configured (%u); refusing to disturb an existing session\n", original);
        goto cleanup;
    }
    kr = (*dev)->USBDeviceOpen(dev); /* No seize fallback. */
    if (kr != kIOReturnSuccess) { fprintf(stderr, "Non-seizing device open: 0x%x\n", kr); goto cleanup; }
    deviceOpen = true;
    /* Recheck after acquiring access to avoid a stale configuration snapshot. */
    UInt8 rechecked = 0;
    kr = (*dev)->GetConfiguration(dev, &rechecked);
    if (kr != kIOReturnSuccess || rechecked != original) goto cleanup;
    changed = true; /* Attempt restoration even if the configuration request fails. */
    kr = (*dev)->SetConfiguration(dev, 1);
    if (kr != kIOReturnSuccess) { fprintf(stderr, "SetConfiguration(1): 0x%x\n", kr); goto cleanup; }
    printf("Activated configuration 1; preparing recording interface only\n");

    IOUSBFindInterfaceRequest request = {kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
                                       kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare};
    io_iterator_t interfaces = 0;
    kr = (*dev)->CreateInterfaceIterator(dev, &request, &interfaces);
    if (kr != kIOReturnSuccess) goto cleanup;
    io_service_t entry;
    while ((entry = IOIteratorNext(interfaces))) {
        IOCFPlugInInterface **p = NULL;
        kr = IOCreatePlugInInterfaceForService(entry, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &p, &score);
        IOObjectRelease(entry);
        if (kr != kIOReturnSuccess || !p) continue;
        IOUSBInterfaceInterface942 **candidate = NULL;
        hr = (*p)->QueryInterface(p, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID942), (LPVOID*)&candidate);
        (*p)->Release(p);
        if (hr != S_OK || !candidate) continue;
        UInt8 number = 255;
        kr = (*candidate)->GetInterfaceNumber(candidate, &number);
        if (kr == kIOReturnSuccess && number == 2 && !audio) audio = candidate;
        else (*candidate)->Release(candidate);
    }
    IOObjectRelease(interfaces);
    if (!audio) { fprintf(stderr, "No recording interface 2\n"); goto cleanup; }
    kr = (*audio)->USBInterfaceOpen(audio);
    if (kr != kIOReturnSuccess) goto cleanup;
    opened = true;
    UInt8 alt = 255;
    kr = (*audio)->GetAlternateSetting(audio, &alt);
    if (kr != kIOReturnSuccess || alt != 0) goto cleanup;
    alternate = true;
    kr = (*audio)->SetAlternateInterface(audio, 1);
    if (kr != kIOReturnSuccess) goto cleanup;
    UInt8 count = 0;
    kr = (*audio)->GetNumEndpoints(audio, &count);
    if (kr != kIOReturnSuccess || count != 1) goto cleanup;
    UInt8 direction, number, type, interval;
    UInt16 packetSize;
    kr = (*audio)->GetPipeProperties(audio, 1, &direction, &number, &type, &packetSize, &interval);
    if (kr != kIOReturnSuccess || direction != kUSBIn || number != 2 || type != kUSBIsoc || packetSize != 336 || interval != 1) goto cleanup;
    pipe = 1;
    kr = (*audio)->CreateInterfaceAsyncEventSource(audio, &source);
    if (kr != kIOReturnSuccess || !source) goto cleanup;
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    printf("READY: %d-second recording-packet probe, interface 2 alt 1 IN 0x82; no playback\n", faderTest ? 20 : 10);
    CFAbsoluteTime started = CFAbsoluteTimeGetCurrent();
    CFAbsoluteTime deadline = started + (faderTest ? 20 : 10);
    unsigned transfers = 0, good = 0, bad = 0;
    unsigned long long total = 0;
    while (!interrupted && CFAbsoluteTimeGetCurrent() < deadline) {
        memset(samples, 0, sizeof(samples));
        memset(frames, 0, sizeof(frames));
        for (unsigned i = 0; i < 80; ++i) frames[i].frReqCount = 336;
        UInt64 busFrame; AbsoluteTime at;
        kr = (*audio)->GetBusFrameNumber(audio, &busFrame, &at);
        if (kr != kIOReturnSuccess) goto cleanup;
        pending = true;
        kr = (*audio)->ReadIsochPipeAsync(audio, pipe, samples, busFrame + 10, 80, frames, completed, NULL);
        if (kr != kIOReturnSuccess) { pending = false; goto cleanup; }
        CFAbsoluteTime timeout = CFAbsoluteTimeGetCurrent() + 1;
        while (pending && !interrupted && CFAbsoluteTimeGetCurrent() < timeout)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, .01, true);
        if (pending) { fprintf(stderr, "Read timed out or interrupted\n"); goto cleanup; }
        ++transfers;
        printf("BATCH %u elapsed=%.3f\n", transfers, CFAbsoluteTimeGetCurrent() - started);
        for (unsigned i = 0; i < 80; ++i) {
            if ((frames[i].frStatus == kIOReturnSuccess || frames[i].frStatus == kIOReturnUnderrun) && frames[i].frActCount > 0 && frames[i].frActCount <= 336) {
                ++good; total += frames[i].frActCount;
            } else ++bad;
        }
        if (transfers <= 3) {
            printf("transfer=%u completion=0x%x first-status=0x%x first-bytes=%u\n", transfers, completionResult, frames[0].frStatus, frames[0].frActCount);
        }
        /* Save native IOUSBIsocFrame metadata and fixed requested-size data slots.
           Diagnostic container only, not a WAV or proven sample representation. */
        if (fwrite(frames, sizeof(frames), 1, raw) != 1 || fwrite(samples, sizeof(samples), 1, raw) != 1) goto cleanup;
    }
    printf("RESULT transfers=%u nonempty-success-or-short-microframes=%u other-microframes=%u bytes=%llu\n", transfers, good, bad, total);
    status = 0;
cleanup:
    printf("Last API result: 0x%x\n", kr);
    if (pending && audio && pipe) {
        IOReturn aborted = (*audio)->AbortPipe(audio, pipe);
        printf("Abort input: 0x%x\n", aborted);
        CFAbsoluteTime drain = CFAbsoluteTimeGetCurrent() + 2;
        while (pending && CFAbsoluteTimeGetCurrent() < drain) CFRunLoopRunInMode(kCFRunLoopDefaultMode, .01, true);
        if (pending) { fprintf(stderr, "Callback drain not verified; static buffers retained until process exit\n"); status = 1; }
    }
    if (alternate) {
        IOReturn reset = (*audio)->SetAlternateInterface(audio, 0);
        UInt8 altCheck = 255;
        IOReturn check = (*audio)->GetAlternateSetting(audio, &altCheck);
        printf("Restore recording alt 0: 0x%x verified=%d\n", reset, check == 0 && altCheck == 0);
        if (reset || check || altCheck != 0) status = 1;
    }
    if (source) { CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode); CFRelease(source); }
    if (opened) (*audio)->USBInterfaceClose(audio);
    if (audio) (*audio)->Release(audio);
    if (changed) {
        IOReturn reset = (*dev)->SetConfiguration(dev, original);
        UInt8 restored = 255;
        IOReturn check = (*dev)->GetConfiguration(dev, &restored);
        printf("Restore configuration %u: 0x%x verified=%d\n", original, reset, check == 0 && restored == original);
        if (reset || check || restored != original) status = 1;
    }
    if (deviceOpen) (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    if (fclose(raw)) status = 1;
    return status;
}
