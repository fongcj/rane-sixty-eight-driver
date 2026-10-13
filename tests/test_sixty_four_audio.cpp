/* SPDX-License-Identifier: MIT */
#include "sixty_four_audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <array>
static void check(bool ok) { if (!ok) { std::fputs("Sixty-Four audio check failed\n", stderr); std::exit(1); } }
static void put(uint8_t* dst, uint32_t bits) { for (unsigned i=0;i<4;++i) dst[i]=bits>>(8*i); }
int main() {
    const unsigned decks[] = {3,1,2,4};
    for (unsigned pair=0;pair<4;++pair) {
        check(sixty_four_deck_left_channel(decks[pair]) == pair*2+1);
        for (unsigned side=0;side<2;++side) {
            char expected[64];
            std::snprintf(expected,sizeof(expected),"Deck %u Record %c",decks[pair],side?'R':'L');
            check(!std::strcmp(sixty_four_channel_name(true,pair*2+side+1),expected));
            std::snprintf(expected,sizeof(expected),"Deck %u Playback %c",decks[pair],side?'R':'L');
            check(!std::strcmp(sixty_four_channel_name(false,pair*2+side+1),expected));
        }
    }
    check(!std::strcmp(sixty_four_channel_name(true,9),"FlexFX Send L"));
    check(!std::strcmp(sixty_four_channel_name(true,12),"Main / Mic Record R"));
    check(!std::strcmp(sixty_four_channel_name(false,10),"FlexFX Return R"));
    check(!sixty_four_channel_name(true,0) && !sixty_four_channel_name(true,13));
    check(!sixty_four_channel_name(false,11) && !sixty_four_channel_category(false,UINT32_MAX));
    check(!sixty_four_deck_left_channel(0) && !sixty_four_deck_left_channel(5));
    std::array<uint8_t,337> bytes{};
    std::array<float,84> output;
    size_t frames=99;
    // Distinct lane values across seven frames catch swaps and bad frame strides.
    for (unsigned i=0;i<84;++i) {
        float v=float(int(i)-40)/16; uint32_t bits; std::memcpy(&bits,&v,4); put(bytes.data()+i*4,bits);
    }
    for (size_t length : {size_t(240),size_t(288),size_t(336)}) {
        output.fill(99);
        check(sixty_four_decode_input(bytes.data(),length,output.data(),output.size(),&frames));
        check(frames==length/48);
        for (unsigned i=0;i<length/4;++i) check(output[i]==float(int(i)-40)/16);
        if (length<336) check(output[length/4]==99); // Padding never decoded.
    }
    for (size_t length : {size_t(1),size_t(47),size_t(289),size_t(337)}) {
        output.fill(99);
        check(!sixty_four_decode_input(bytes.data(),length,output.data(),output.size(),&frames));
        check(frames==0 && output[0]==99);
    }
    check(!sixty_four_decode_input(bytes.data(),336,output.data(),83,&frames));
    check(!sixty_four_decode_input(nullptr,48,output.data(),84,&frames));
    check(!sixty_four_decode_input(bytes.data(),48,nullptr,84,&frames));
    check(!sixty_four_decode_input(bytes.data(),48,output.data(),84,nullptr));
    for (uint32_t invalid : {0x7fc00001u,0x7f800000u,0xff800000u}) {
        put(bytes.data()+332,invalid); output.fill(99);
        check(!sixty_four_decode_input(bytes.data(),336,output.data(),84,&frames));
        for(float v:output) check(v==99); // All-or-nothing even with corrupt last sample.
    }
    check(sixty_four_decode_input(nullptr,0,nullptr,0,&frames) && frames==0);
    std::puts("Sixty-Four mapping and Float32 decoder tests passed");
}
