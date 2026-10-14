/* SPDX-License-Identifier: MIT */
#include "../src/protocol/sixty_four_midi.h"
#include <cstdio>
#include <cstdlib>

static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    // Meaningful prefixes observed on hardware after the approved startup test.
    // Padding is deliberately synthetic: bytes outside the declared length varied.
    for (uint8_t status : {uint8_t(0x90), uint8_t(0x80), uint8_t(0xf8)}) {
        std::array<uint8_t, 64> livePrefix{};
        livePrefix.fill(0xcd);
        livePrefix[0] = 0x40;
        livePrefix[1] = 0x1c; livePrefix[2] = 0xc5;
        livePrefix[3] = 0; livePrefix[4] = 0x17;
        livePrefix[5] = status == 0xf8 ? 1 : 3;
        livePrefix[6] = status;
        livePrefix[7] = 0x68; livePrefix[8] = 0x40;
        sixty_four::MidiEnvelope observed;
        check(sixty_four::decodeMidiEnvelope(livePrefix.data(), 64, observed));
        check(observed.transaction == 0x1700c51c && observed.bytes[0] == status);
        check(observed.length == livePrefix[5]);
        if (status != 0xf8) check(observed.bytes[1] == 104 && observed.bytes[2] == 64);
        for (size_t i = observed.length; i < 58; ++i) check(observed.bytes[i] == 0);
    }
    // Synthetic fixtures, not captured controls. Distinct ID bytes check endian.
    std::array<uint8_t, 65> report{};
    report.fill(0xee);
    report[0] = 0x40;
    report[1] = 0x12; report[2] = 0x34;
    report[3] = 0x56; report[4] = 0x78;
    report[5] = 3;
    report[6] = 0x90; report[7] = 0x25; report[8] = 0x7f;
    sixty_four::MidiEnvelope out;
    check(sixty_four::decodeMidiEnvelope(report.data(), 64, out));
    check(out.transaction == 0x78563412 && out.length == 3);
    check(out.bytes[0] == 0x90 && out.bytes[1] == 0x25 && out.bytes[2] == 0x7f);
    for (size_t i = 3; i < out.bytes.size(); ++i) check(out.bytes[i] == 0);
    for (unsigned n = 0; n <= 255; ++n) {
        report[5] = uint8_t(n);
        const auto before = out;
        check(sixty_four::decodeMidiEnvelope(report.data(), 64, out) == (n <= 58));
        if (n <= 58) {
            check(out.length == n);
            for (size_t i = 0; i < 58; ++i)
                check(out.bytes[i] == (i < n ? report[6 + i] : 0));
        } else check(out.transaction == before.transaction &&
                     out.length == before.length && out.bytes == before.bytes);
    }
    report[5] = 3;
    for (size_t n = 0; n <= 65; ++n)
        if (n != 64) check(!sixty_four::decodeMidiEnvelope(report.data(), n, out));
    check(!sixty_four::decodeMidiEnvelope(nullptr, 64, out));
    for (unsigned opcode = 0; opcode <= 255; ++opcode) {
        report[0] = uint8_t(opcode);
        check(sixty_four::decodeMidiEnvelope(report.data(), 64, out) == (opcode == 0x40));
    }
    std::puts("Sixty-Four candidate MIDI envelope: synthetic bounds tests passed");
}
