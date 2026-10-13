/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Manual p23; verified hardware ID and IN format in docs/hardware/.
 * Recording taps are mixer settings, not a software channel permutation.
 * These labels deliberately do not assert pre/post-fader or physical A/B.
 */
#define SIXTY_FOUR_INPUT_CHANNELS 12u
#define SIXTY_FOUR_OUTPUT_CHANNELS 10u
#define SIXTY_FOUR_SAMPLE_RATE 48000u
#define SIXTY_FOUR_INPUT_FRAME_BYTES 48u
#define SIXTY_FOUR_INPUT_MAX_PACKET_BYTES 336u

/* CoreAudio elements are one-based; element 0 and out-of-range return NULL. */
const char *sixty_four_channel_name(bool input, uint32_t element);
const char *sixty_four_channel_category(bool input, uint32_t element);
/* Deck numbers 1..4; returns one-based left channel, or 0 for invalid deck. */
uint32_t sixty_four_deck_left_channel(uint32_t deck);

/* Decode one actual IN microframe payload, not its requested-size padding.
 * Returns false on malformed length, nonfinite payload, insufficient capacity
 * or invalid pointers. Failure writes no samples and sets framesWritten to 0.
 * Zero-byte payload is accepted. No clipping or gain change is performed.
 */
bool sixty_four_decode_input(const uint8_t *bytes, size_t length,
                            float *samples, size_t sampleCapacity,
                            size_t *framesWritten);
#ifdef __cplusplus
}
#endif
