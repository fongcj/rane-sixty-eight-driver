/* SPDX-License-Identifier: MIT */
#include "sixty_four_audio.h"
#include <string.h>
#include <math.h>
_Static_assert(sizeof(float) == 4, "32-bit float required");

static const char *const inputNames[] = {
    "Deck 3 Record L", "Deck 3 Record R", "Deck 1 Record L", "Deck 1 Record R",
    "Deck 2 Record L", "Deck 2 Record R", "Deck 4 Record L", "Deck 4 Record R",
    "FlexFX Send L", "FlexFX Send R", "Main / Mic Record L", "Main / Mic Record R"
};
static const char *const outputNames[] = {
    "Deck 3 Playback L", "Deck 3 Playback R", "Deck 1 Playback L", "Deck 1 Playback R",
    "Deck 2 Playback L", "Deck 2 Playback R", "Deck 4 Playback L", "Deck 4 Playback R",
    "FlexFX Return L", "FlexFX Return R"
};
static const char *const categories[] = {"Deck 3", "Deck 1", "Deck 2", "Deck 4", "FlexFX", "Main / Mic"};
const char *sixty_four_channel_name(bool input, uint32_t element) {
    uint32_t count = input ? SIXTY_FOUR_INPUT_CHANNELS : SIXTY_FOUR_OUTPUT_CHANNELS;
    if (!element || element > count) return NULL;
    return input ? inputNames[element-1] : outputNames[element-1];
}
const char *sixty_four_channel_category(bool input, uint32_t element) {
    uint32_t count = input ? SIXTY_FOUR_INPUT_CHANNELS : SIXTY_FOUR_OUTPUT_CHANNELS;
    return element && element <= count ? categories[(element-1)/2] : NULL;
}
uint32_t sixty_four_deck_left_channel(uint32_t deck) {
    static const uint32_t left[] = {3, 5, 1, 7};
    return deck >= 1 && deck <= 4 ? left[deck-1] : 0;
}
static float read_float(const uint8_t *p) {
    uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
                    ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}
bool sixty_four_decode_input(const uint8_t *bytes, size_t length,
                            float *samples, size_t sampleCapacity,
                            size_t *framesWritten) {
    if (!framesWritten) return false;
    *framesWritten = 0;
    if (length > SIXTY_FOUR_INPUT_MAX_PACKET_BYTES || length % SIXTY_FOUR_INPUT_FRAME_BYTES) return false;
    if (!length) return true;
    if (!bytes || !samples || sampleCapacity < length / 4) return false;
    /* Validate into a bounded local buffer before publishing a whole packet. */
    float decoded[SIXTY_FOUR_INPUT_MAX_PACKET_BYTES / 4];
    for (size_t i = 0; i < length / 4; ++i) {
        decoded[i] = read_float(bytes + i*4);
        if (!isfinite(decoded[i])) return false;
    }
    memcpy(samples, decoded, length);
    *framesWritten = length / SIXTY_FOUR_INPUT_FRAME_BYTES;
    return true;
}
