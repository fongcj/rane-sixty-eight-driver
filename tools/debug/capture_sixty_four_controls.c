/* SPDX-License-Identifier: MIT */
/* Bounded HID input diagnostics. Activation modes temporarily select configuration
 * 1 and restore it. Existing-session mode only observes an already configured
 * mixer; it never opens the USB device or changes its configuration.
 * The explicit init36 experiment sends one model-derived startup output report.
 * Other modes are IN-only. No audio alternate selection or seize fallback.
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

#include "../../src/protocol/sixty_four_descriptors.h"
static volatile sig_atomic_t interrupted = 0;
static void stop_signal(int sig) { (void)sig; interrupted = 1; }

typedef struct {
    uint8_t buffer[64];
    bool stopping, failed;
    unsigned reports;
} Capture;
static void received(void *context, IOReturn result, void *sender,
                     IOHIDReportType type, uint32_t reportID, uint8_t *report, CFIndex length) {
    (void)sender;
    Capture *c = context;
    if (c->stopping) return;
    if (result != kIOReturnSuccess || type != kIOHIDReportTypeInput || length < 0 || length > 64 || (length > 0 && !report)) {
        fprintf(stderr, "HID input result=0x%x type=%d length=%ld\n", result, type, length);
        c->failed = true; return;
    }
    printf("ticks=%llu reportID=%u length=%ld bytes=", (unsigned long long)mach_absolute_time(), reportID, length);
    for (CFIndex i = 0; i < length; ++i) printf("%02x%s", report[i], i+1==length ? "" : " ");
    putchar('\n'); fflush(stdout);
    ++c->reports;
}
static void match_number(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, n); CFRelease(n);
}
/* Candidate from SixtyFour.10.9 InitHid, x86_64 0x1292/0x1298.
 * Zero padding is deliberate; firmware semantics and reset behavior are unverified.
 * Never add a guessed inverse command or the Sixty-Eight SysEx sequence here.
 */
static IOReturn send_startup_candidate(IOHIDDeviceRef device) {
    uint8_t report[64] = {0x36, 0xff, 0xff, 0xff, 0xff, 0x01};
    return IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0, report, sizeof(report));
}
int main(int argc, char **argv) {
    bool startupExperiment = argc == 2 && !strcmp(argv[1], "--activate-init36-and-listen-60s");
    if (argc == 2 && !strcmp(argv[1], "--describe-init36")) {
        puts("OFFLINE ONLY: proposed experiment sends one 64-byte HID output report, ID 0:");
        puts("36 ff ff ff ff 01 followed by 58 zero bytes; no retries or extra commands.");
        puts("Requires idle configuration 0, selects 1, listens, then restores configuration 0.");
        puts("Configuration restoration is NOT proof of undoing firmware state. No inverse command is known.");
        puts("Obtain explicit approval and stop audio apps before running --activate-init36-and-listen-60s.");
        return 0;
    }
    bool existingSession = argc == 2 && !strcmp(argv[1], "--listen-existing-60s");
    bool managerReports = startupExperiment || existingSession || (argc == 2 && !strcmp(argv[1], "--activate-manager-listen-60s"));
    if (argc != 2 || (!managerReports && strcmp(argv[1], "--activate-and-listen-60s"))) {
        fprintf(stderr, "Requires explicit approval: %s --activate-and-listen-60s\n"
                        "Alternative: --activate-manager-listen-60s (manager-owned report delivery).\n"
                        "Existing audio session: --listen-existing-60s (no configuration changes).\n"
                        "Startup experiment (separate approval): --activate-init36-and-listen-60s; preview: --describe-init36.\n"
                        "Activation modes select configuration 1, listen on HID IN for <=60s, then restore.\n", argv[0]);
        return 2;
    }
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
    bool deviceOpen = false, changed = false, hidOpen = false;
    Capture capture = {0};
    IOHIDManagerRef manager = NULL;
    IOHIDDeviceRef hid = NULL;
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
    if (existingSession) {
        if (original != 1) {
            fprintf(stderr, "Existing-session mode requires configuration 1; start audio first. No activation attempted.\n");
            goto cleanup;
        }
        printf("Observing existing configuration 1; no USB open, configuration change or HID output\n");
    } else {
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
        printf("Activated configuration 1; no audio alternate settings or HID output\n");
    }
    /* Use the system HID service; do not compete for its raw USB interface. */
    manager = IOHIDManagerCreate(NULL, kIOHIDManagerOptionNone);
    if (!manager) goto cleanup;
    CFMutableDictionaryRef hidMatch = CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    match_number(hidMatch, CFSTR(kIOHIDVendorIDKey), 0x1cc5);
    match_number(hidMatch, CFSTR(kIOHIDProductIDKey), 0x0017);
    IOHIDManagerSetDeviceMatching(manager, hidMatch);
    CFRelease(hidMatch);
    if (managerReports) IOHIDManagerRegisterInputReportCallback(manager, received, &capture);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    kr = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (kr != kIOReturnSuccess) { fprintf(stderr, "HID manager open: 0x%x\n", kr); goto cleanup; }
    CFAbsoluteTime discoveryDeadline = CFAbsoluteTimeGetCurrent() + 3;
    while (!hid && !interrupted && CFAbsoluteTimeGetCurrent() < discoveryDeadline) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
        CFSetRef devices = IOHIDManagerCopyDevices(manager);
        if (devices) {
            CFIndex count = CFSetGetCount(devices);
            if (count == 1) {
                const void *value = NULL;
                CFSetGetValues(devices, &value);
                hid = (IOHIDDeviceRef)CFRetain(value);
            }
            CFRelease(devices);
            if (count > 1) { fprintf(stderr, "Multiple matching HID devices; aborting\n"); goto cleanup; }
        }
    }
    if (!hid) { fprintf(stderr, "No system HID device appeared\n"); goto cleanup; }
    if (startupExperiment) {
        static const uint8_t expectedReport[] = {
            0x06,0x00,0xff,0x09,0x01,0xa1,0x01,0x19,0x01,0x29,0x02,0x15,0x00,0x25,
            0x01,0x75,0x08,0x95,0x40,0x81,0x02,0x19,0x01,0x29,0x02,0x91,0x02,0xc0
        };
        CFTypeRef descriptor = IOHIDDeviceGetProperty(hid, CFSTR(kIOHIDReportDescriptorKey));
        if (!descriptor || CFGetTypeID(descriptor) != CFDataGetTypeID() ||
            CFDataGetLength((CFDataRef)descriptor) != sizeof(expectedReport) ||
            memcmp(CFDataGetBytePtr((CFDataRef)descriptor), expectedReport, sizeof(expectedReport))) {
            fprintf(stderr, "HID report descriptor mismatch; no startup output sent\n"); goto cleanup;
        }
        if (interrupted || capture.failed) goto cleanup;
        puts("Sending ONE experimental Sixty-Four startup report: 36 ff ff ff ff 01 + zero padding");
        kr = send_startup_candidate(hid);
        printf("Startup report return: 0x%x (no retry; firmware state not verified)\n", kr);
        if (kr != kIOReturnSuccess) goto cleanup;
    }
    if (!managerReports) {
    kr = IOHIDDeviceOpen(hid, kIOHIDOptionsTypeNone);
    if (kr != kIOReturnSuccess) { fprintf(stderr, "Non-seizing HID open: 0x%x\n", kr); goto cleanup; }
    hidOpen = true;
    IOHIDDeviceRegisterInputReportCallback(hid, capture.buffer, sizeof(capture.buffer), received, &capture);
    IOHIDDeviceScheduleWithRunLoop(hid, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    }
    printf("Report delivery: %s\n", managerReports ? "HID manager" : "explicit HID device");
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    printf("READY: 60-second input capture; timebase=%u/%u; press controls when prompted\n", tb.numer, tb.denom);
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 60;
    while (!interrupted && !capture.failed && capture.reports < 1000 && CFAbsoluteTimeGetCurrent() < deadline)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    printf("Capture finished: %u reports\n", capture.reports);
    if (existingSession) {
        UInt8 current = 255;
        IOReturn query = (*dev)->GetConfiguration(dev, &current);
        printf("Existing session final configuration: %u (query 0x%x); no restoration performed\n", current, query);
        if (query != kIOReturnSuccess || current != 1) capture.failed = true;
    }
    status = capture.failed ? 1 : 0;
cleanup:
    if (startupExperiment) puts("No inverse startup command sent. Configuration cleanup does not verify firmware reset.");
    capture.stopping = true;
    if (hid) {
        if (!managerReports) IOHIDDeviceUnscheduleFromRunLoop(hid, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        if (hidOpen) IOHIDDeviceClose(hid, kIOHIDOptionsTypeNone);
        CFRelease(hid);
    }
    if (manager) {
        IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
    }
    if (changed) {
        kr = (*dev)->SetConfiguration(dev, original);
        printf("Restore configuration %u: 0x%x\n", original, kr);
        UInt8 restored = 255;
        IOReturn check = (*dev)->GetConfiguration(dev, &restored);
        if (kr != kIOReturnSuccess || check != kIOReturnSuccess || restored != original) {
            fprintf(stderr, "RESTORE NOT VERIFIED: stop and reconnect USB during idle session\n"); status = 1;
        } else printf("Original configuration verified\n");
    }
    if (deviceOpen) (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    return status;
}
