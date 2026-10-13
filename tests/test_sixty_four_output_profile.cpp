/* SPDX-License-Identifier: MIT */
#include "../src/playback/SixtyFourOutputProfile.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)) {std::fprintf(stderr,"Output profile failure line %d\n",__LINE__);std::exit(1);}} while(0)
int main() {
    using sixtyfour::playback::OutputProfile;
    auto f=OutputProfile::format();
    CHECK(f.mChannelsPerFrame==10 && f.mSampleRate==48000 && f.mBytesPerFrame==40);
    CHECK(f.mBytesPerPacket==40 && f.mFramesPerPacket==1 && f.mBitsPerChannel==32);
    CHECK(f.mFormatID==kAudioFormatLinearPCM && f.mFormatFlags==(kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked));
    CHECK(OutputProfile::direction==0 && OutputProfile::startingChannel==1);
    CHECK(OutputProfile::operation==kAudioServerPlugInIOOperationWriteMix);
    CHECK(!OutputProfile::name(0) && !OutputProfile::name(11));
    const char* names[]={"Deck 3 Playback L","Deck 3 Playback R","Deck 1 Playback L","Deck 1 Playback R",
                         "Deck 2 Playback L","Deck 2 Playback R","Deck 4 Playback L","Deck 4 Playback R",
                         "FlexFX Return L","FlexFX Return R"};
    for(unsigned i=0;i<10;++i)CHECK(!std::strcmp(OutputProfile::name(i+1),names[i]));
    std::puts("Prepared CoreAudio output profile matches documented routing; no device registered");
}
