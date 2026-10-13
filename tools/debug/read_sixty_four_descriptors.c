/* SPDX-License-Identifier: MIT */
/* Read cached descriptors for the Sixty-Four identity observed on both USB ports.
 * Default: cached metadata only. --hid-report adds one standard IN
 * GET_DESCRIPTOR request. No open, configuration changes or output reports.
 * Standalone: never link this reader to rane_engine.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned le16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void match_number(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, n);
    CFRelease(n);
}
static int dump(const uint8_t *p, size_t total) {
    if (total < 9 || p[0] < 9 || p[1] != 2) return 1;
    for (size_t off = 0; off < total;) {
        if (total - off < 2 || p[off] < 2 || p[off] > total - off) {
            fprintf(stderr, "Malformed descriptor at %zu\n", off);
            return 1;
        }
        const uint8_t *d = p + off;
        unsigned len = d[0], type = d[1];
        printf("offset=%zu type=0x%02x bytes=", off, type);
        for (unsigned i = 0; i < len; ++i) printf("%02x%s", d[i], i + 1 == len ? "" : " ");
        putchar('\n');
        if (type == 2 && len >= 9)
            printf("  CONFIG value=%u interfaces=%u total=%u\n", d[5], d[4], le16(d+2));
        if (type == 4 && len >= 9)
            printf("  INTERFACE number=%u alt=%u endpoints=%u class=0x%02x subclass=0x%02x protocol=0x%02x\n",
                   d[2], d[3], d[4], d[5], d[6], d[7]);
        if (type == 5 && len >= 7)
            printf("  ENDPOINT address=0x%02x attributes=0x%02x maxPacketRaw=%u interval=%u\n",
                   d[2], d[3], le16(d+4), d[6]);
        off += len;
    }
    return 0;
}
int main(int argc, char **argv) {
    int readReport = argc == 2 && strcmp(argv[1], "--hid-report") == 0;
    if (argc != 1 && !readReport) {
        fprintf(stderr, "Usage: %s [--hid-report]\n", argv[0]);
        return 2;
    }
    CFMutableDictionaryRef matching = IOServiceMatching("IOUSBHostDevice");
    if (!matching) return 1;
    match_number(matching, CFSTR("idVendor"), 0x1cc5);
    match_number(matching, CFSTR("idProduct"), 0x0017);
    io_iterator_t iter = IO_OBJECT_NULL;
    IOReturn kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
    if (kr != kIOReturnSuccess) { fprintf(stderr, "Enumeration: 0x%x\n", kr); return 1; }
    io_service_t service = IOIteratorNext(iter);
    io_service_t extra = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (!service || extra) {
        fprintf(stderr, "Expected exactly one Sixty-Four device; refusing ambiguous/absent match\n");
        if (service) IOObjectRelease(service);
        if (extra) IOObjectRelease(extra);
        return 1;
    }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kr = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID,
                                          kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (kr != kIOReturnSuccess || !plugin) {
        fprintf(stderr, "Cached descriptor access unavailable: 0x%x; no open/seize fallback\n", kr);
        return 1;
    }
    IOUSBDeviceInterface182 **dev = NULL;
    HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID182), (LPVOID*)&dev);
    (*plugin)->Release(plugin);
    if (hr != S_OK || !dev) { fprintf(stderr, "QueryInterface failed\n"); return 1; }
    UInt16 vid = 0, pid = 0;
    UInt8 count = 0;
    int status = 1;
    if ((*dev)->GetDeviceVendor(dev, &vid) != kIOReturnSuccess ||
        (*dev)->GetDeviceProduct(dev, &pid) != kIOReturnSuccess || vid != 0x1cc5 || pid != 0x0017 ||
        (*dev)->GetNumberOfConfigurations(dev, &count) != kIOReturnSuccess) goto done;
    printf("Cached descriptors VID=0x%04x PID=0x%04x configurations=%u\n", vid, pid, count);
    UInt8 activeConfig = 0;
    kr = (*dev)->GetConfiguration(dev, &activeConfig);
    if (kr != kIOReturnSuccess) { fprintf(stderr, "GetConfiguration failed: 0x%x\n", kr); goto done; }
    printf("Current configuration=%u (unchanged)\n", activeConfig);
    status = count ? 0 : 1;
    for (unsigned i = 0; i < count; ++i) {
        IOUSBConfigurationDescriptorPtr conf = NULL;
        kr = (*dev)->GetConfigurationDescriptorPtr(dev, (UInt8)i, &conf);
        if (kr != kIOReturnSuccess || !conf) {
            fprintf(stderr, "Configuration %u cache read failed: 0x%x\n", i, kr);
            status = 1; break;
        }
        const uint8_t *bytes = (const uint8_t *)conf;
        if (dump(bytes, le16(bytes + 2))) { status = 1; break; }
    }
    if (status == 0 && readReport) {
        /* Fixed standard device-to-host request for the observed interface 3.
         * No SET_CONFIGURATION fallback, even if unconfigured or stalled.
         */
        uint8_t report[28] = {0};
        IOUSBDevRequestTO req = {0};
        req.bmRequestType = 0x81; /* IN, standard, interface */
        req.bRequest = 0x06;      /* GET_DESCRIPTOR */
        req.wValue = 0x2200;      /* HID report descriptor, index 0 */
        req.wIndex = 3;
        req.wLength = sizeof(report);
        req.pData = report;
        req.noDataTimeout = 1000;
        req.completionTimeout = 2000;
        printf("Request: IN GET_DESCRIPTOR type=22 interface=3 length=28; no state writes\n");
        kr = (*dev)->DeviceRequestTO(dev, &req);
        printf("GET_DESCRIPTOR result=0x%x actual=%u\n", kr, (unsigned)req.wLenDone);
        if (kr != kIOReturnSuccess || req.wLenDone != sizeof(report)) {
            fprintf(stderr, "Report descriptor unavailable; no open/configure fallback\n");
            status = 1;
        } else {
            printf("HID_REPORT bytes=");
            for (size_t i = 0; i < sizeof(report); ++i)
                printf("%02x%s", report[i], i + 1 == sizeof(report) ? "\n" : " ");
        }
    }
done:
    (*dev)->Release(dev);
    return status;
}
