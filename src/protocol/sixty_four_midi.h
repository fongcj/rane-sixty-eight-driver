/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sixty_four {
// Derived from SixtyFourApi 2.0.0b3; clock and Deck 1 Trigger 1 prefixes
// validated on hardware. Other controls still need live coverage.
// No USB/CoreMIDI access, output encoding, or initialization commands.
struct MidiEnvelope {
    uint32_t transaction = 0;
    uint8_t length = 0;
    std::array<uint8_t, 58> bytes{};
};

// Extracts the 0x40 envelope only. This does not validate MIDI grammar or
// distinguish an unsolicited event from a response by transaction ID.
// Failure leaves the caller's output unchanged.
inline bool decodeMidiEnvelope(const uint8_t* report, size_t size,
                               MidiEnvelope& output) {
    if (!report || size != 64 || report[0] != 0x40 || report[5] > 58)
        return false;
    MidiEnvelope candidate;
    for (unsigned i = 0; i < 4; ++i)
        candidate.transaction |= uint32_t(report[1 + i]) << (8 * i);
    candidate.length = report[5];
    for (size_t i = 0; i < candidate.length; ++i)
        candidate.bytes[i] = report[6 + i];
    output = candidate;
    return true;
}
} // namespace sixty_four
