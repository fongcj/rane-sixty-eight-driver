/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace sixtyfour::playback {
// Five-second diagnostic only: silence in seconds 0..1 and 4..5,
// 50 ms fades, 440 Hz at -42 dBFS in Deck 1 lanes 3/4 only.
inline std::array<float,10> probeFrame(uint64_t sample,bool tone) {
    std::array<float,10> frame{};
    if(!tone || sample<=48000 || sample>=192000) return frame;
    double t=sample/48000.0;
    double envelope=std::fmax(0.0,std::fmin(1.0,std::fmin((t-1.0)/.05,(4.0-t)/.05)));
    frame[2]=frame[3]=float(.007943282347*envelope*std::sin(2*3.14159265358979323846*440*t));
    return frame;
}
}
