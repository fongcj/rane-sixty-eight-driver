/* SPDX-License-Identifier: MIT */
/* Offline regression test: does not link to or open any hardware API. */
#include "../src/protocol/rane_defs.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>

static void require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1); // Remains active in Release/NDEBUG builds.
    }
}

int main() {
    const uint16_t vendors[] = {0x1CC5, 0x13E5, 0x29E2};
    const uint16_t products[] = {0x0004, 0x0008, 0x0080, 0x0014, 0x0017, 0x0012, 0x0006};
    for (uint16_t vid : vendors) {
        for (uint16_t pid : products) {
            bool expected = vid == 0x1CC5 ||
                (vid == 0x13E5 && (pid == 4 || pid == 8)) ||
                (vid == 0x29E2 && pid == 4);
            auto* info = rane_lookup_device(vid, pid);
            require((info != nullptr) == expected, "reject unlisted vendor/product combinations");
            if (info) {
                require(info->vid == vid && info->pid == pid, "return exact matching entry");
                require(info->currentTransportEnabled == (pid == 4 || pid == 8 || pid == 0x80),
                        "only existing Sixty-Eight profiles enable transport");
            }
        }
    }
    // Exhaust all vendor values for the two PIDs previously treated as wildcards.
    for (unsigned vid = 0; vid <= 0xFFFF; ++vid) {
        for (uint16_t pid : {uint16_t(4), uint16_t(8)}) {
            bool expected = vid == 0x1CC5 || vid == 0x13E5 || (vid == 0x29E2 && pid == 4);
            require((rane_lookup_device(static_cast<uint16_t>(vid), pid) != nullptr) == expected,
                    "PID 4/8 must not match an unrelated vendor");
        }
    }
    require(!rane_lookup_device(0, 0x14), "zero VID is not a wildcard");
    require(!rane_lookup_device(0x1CC5, 0xFFFF), "reject unknown product");
    require(!rane_lookup_device(0x1CC5, 0x000E), "unlisted alternate PID remains disabled");
    auto* four = rane_lookup_device(0x1CC5, 0x0014);
    require(four && !std::strcmp(four->displayName, "Rane Sixty-Four"), "recognize candidate metadata");
    require(four->channelsIn == 12 && four->channelsOut == 10, "documented Sixty-Four channel counts");
    require(!four->currentTransportEnabled, "Sixty-Four must not use unverified transport");
    auto* observed = rane_lookup_device(0x1CC5, 0x0017);
    require(observed && !observed->currentTransportEnabled, "observed Sixty-Four remains disabled");
    require(observed->channelsIn == 12 && observed->channelsOut == 10, "observed Sixty-Four topology");
    std::puts("PASS: exact identity, unrelated-device rejection, channel metadata and transport guards");
}
