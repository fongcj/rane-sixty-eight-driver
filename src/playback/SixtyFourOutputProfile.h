/* SPDX-License-Identifier: MIT */
#pragma once
#include <CoreAudio/AudioServerPlugIn.h>
#include "SixtyFourPlayback.h"
#include "../protocol/sixty_four_audio.h"

namespace sixtyfour::playback {
// Prepared metadata only. No factory, device registration, USB operations or
// enable flag: the recording HAL continues to expose no output streams.
struct OutputProfile {
    static AudioStreamBasicDescription format() {
        return {SIXTY_FOUR_SAMPLE_RATE,kAudioFormatLinearPCM,
                kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,
                frameBytes,1,frameBytes,channels,32,0};
    }
    static constexpr UInt32 direction=0,startingChannel=1;
    static constexpr UInt32 operation=kAudioServerPlugInIOOperationWriteMix;
    static const char* name(UInt32 element) {return sixty_four_channel_name(false,element);}
};
}
