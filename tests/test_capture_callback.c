/* Exercise the actual raw callback without entering hardware discovery. */
#include <IOKit/hid/IOHIDManager.h>
#include <string.h>
static unsigned outputCalls;
static uint8_t sentReport[64];
static IOReturn mockResult;
static IOReturn mockSetReport(IOHIDDeviceRef device, IOHIDReportType type,
                             CFIndex reportID, const uint8_t *report, CFIndex length) {
    (void)device;
    ++outputCalls;
    if (type != kIOHIDReportTypeOutput || reportID != 0 || length != 64)
        return kIOReturnBadArgument;
    memcpy(sentReport, report, 64);
    return mockResult;
}
#define IOHIDDeviceSetReport mockSetReport
#define main hardware_capture_main
#include "../tools/debug/capture_sixty_four_controls.c"
#undef main
#undef IOHIDDeviceSetReport
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); exit(1); } } while (0)
int main(void) {
    char *preview[] = {"capture", "--describe-init36"};
    CHECK(hardware_capture_main(2, preview) == 0 && outputCalls == 0);
    CHECK(send_startup_candidate(NULL) == kIOReturnSuccess && outputCalls == 1);
    const uint8_t prefix[] = {0x36, 0xff, 0xff, 0xff, 0xff, 1};
    CHECK(!memcmp(sentReport, prefix, sizeof(prefix)));
    for (unsigned i = sizeof(prefix); i < 64; ++i) CHECK(sentReport[i] == 0);
    mockResult = kIOReturnError;
    CHECK(send_startup_candidate(NULL) == kIOReturnError && outputCalls == 2);
    Capture c = {0};
    uint8_t bytes[64];
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)(i + 128);
    received(&c, kIOReturnSuccess, NULL, kIOHIDReportTypeInput, 0, bytes, 64);
    CHECK(c.reports == 1 && !c.failed);
    received(&c, kIOReturnSuccess, NULL, kIOHIDReportTypeInput, 7, bytes, 3);
    CHECK(c.reports == 2 && !c.failed);
    c.stopping = true;
    received(&c, kIOReturnSuccess, NULL, kIOHIDReportTypeInput, 0, bytes, 64);
    CHECK(c.reports == 2 && !c.failed);
    c.stopping = false;
    received(&c, kIOReturnSuccess, NULL, kIOHIDReportTypeInput, 0, bytes, 65);
    CHECK(c.failed && c.reports == 2);
    c.failed = false;
    received(&c, kIOReturnSuccess, NULL, kIOHIDReportTypeInput, 0, NULL, 1);
    CHECK(c.failed && c.reports == 2);
    c.failed = false;
    received(&c, kIOReturnError, NULL, kIOHIDReportTypeInput, 0, bytes, 64);
    CHECK(c.failed && c.reports == 2);
    c.failed = false;
    received(&c, kIOReturnSuccess, NULL, kIOHIDReportTypeOutput, 0, bytes, 64);
    CHECK(c.failed && c.reports == 2);
    puts("Callback tests passed (synthetic data; no hardware access)");
    return 0;
}
