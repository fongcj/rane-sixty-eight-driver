/* SPDX-License-Identifier: MIT */
#pragma once
#include <cstddef>
#include <cstdint>

namespace sixty_four {
// Allocation-free MIDI 1.0 stream parser. SysEx is deliberately discarded in
// this input-controls milestone. Running status survives realtime bytes and
// report boundaries; system-common and SysEx cancel it.
class MidiStream {
    uint8_t running = 0, message[3]{}, used = 0, needed = 0;
    bool sysex = false;
public:
    void reset() { running = used = needed = 0; sysex = false; }
    template<class Emit> void feed(const uint8_t* bytes, size_t length, Emit emit) {
        for (size_t i = 0; i < length; ++i) {
            const uint8_t b = bytes[i];
            if (b >= 0xf8) {
                // Reserved F9/FD are ignored. Clock forwarding is a bridge policy.
                if (b != 0xf9 && b != 0xfd) emit(&bytes[i], size_t(1));
                continue;
            }
            if (b & 0x80) {
                used = needed = 0;
                if (b >= 0xf0) {
                    running = 0;
                    sysex = b == 0xf0;
                    if (b == 0xf6) emit(&bytes[i], size_t(1));
                    if (b != 0xf1 && b != 0xf2 && b != 0xf3) continue;
                    needed = b == 0xf2 ? 3 : 2;
                } else {
                    sysex = false;
                    running = b;
                    needed = (b & 0xe0) == 0xc0 ? 2 : 3;
                }
                message[0] = b; used = 1;
            } else if (!sysex) {
                if (!used && running) {
                    message[0] = running; used = 1;
                    needed = (running & 0xe0) == 0xc0 ? 2 : 3;
                }
                if (!used || !needed) continue;
                message[used++] = b;
                if (used == needed) {
                    emit(message, size_t(used)); used = needed = 0;
                }
            }
        }
    }
};
}
