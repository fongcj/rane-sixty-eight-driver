/* Exercise the actual raw callback without entering hardware discovery. */
#define main hardware_capture_main
#include "../tools/debug/capture_sixty_four_controls.c"
#undef main
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); exit(1); } } while (0)
int main(void) {
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
