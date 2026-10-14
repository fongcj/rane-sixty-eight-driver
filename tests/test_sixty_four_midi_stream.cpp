/* SPDX-License-Identifier: MIT */
#include "../src/midi64/MidiStream.h"
#include <vector>
#include <cstdlib>
#include <cstdio>
using Bytes = std::vector<uint8_t>;
using Messages = std::vector<Bytes>;
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    sixty_four::MidiStream parser;
    Messages result;
    auto emit = [&](const uint8_t* p, size_t n) { result.emplace_back(p,p+n); };
    // Clock can interrupt a message; running status crosses HID reports.
    const Bytes input{0x90,104,0xf8,64,105,0,0x80,104,64,0xb2,10,65,
                      0xc3,7,8,0xd4,20,0xe5,0,64,0xf2,1,2,3};
    for (uint8_t b : input) parser.feed(&b,1,emit);
    check(result == Messages{{0xf8},{0x90,104,64},{0x90,105,0},{0x80,104,64},
          {0xb2,10,65},{0xc3,7},{0xc3,8},{0xd4,20},{0xe5,0,64},{0xf2,1,2}});
    parser.reset(); result.clear();
    const Bytes noisy{64,0x90,1,0xf0,0x17,0xf8,4,0xf7,64,0xf9,0xfd,0x91,2,3};
    parser.feed(noisy.data(),noisy.size(),emit);
    check(result == Messages{{0xf8},{0x91,2,3}});
    // Reset discards partial messages and running status at a device boundary.
    const uint8_t partial[]{0x90,104}; parser.feed(partial,2,emit);
    parser.reset(); result.clear(); const uint8_t value = 64; parser.feed(&value,1,emit);
    check(result.empty());
    // All channel statuses, data extrema, and every possible split position.
    for (unsigned s=0x80; s<0xf0; ++s) {
        Bytes msg{uint8_t(s),0}; if ((s & 0xe0)!=0xc0) msg.push_back(127);
        for (size_t split=0;split<=msg.size();++split) {
            parser.reset(); result.clear();
            parser.feed(msg.data(),split,emit);
            parser.feed(msg.data()+split,msg.size()-split,emit);
            check(result == Messages{msg});
        }
    }
    puts("MIDI stream boundary, running-status, realtime and recovery tests passed");
}
