/* SPDX-License-Identifier: MIT */
// Compile the HAL contract into a normal executable and replace the USB engine.
// This test never loads a driver bundle, opens USB, or calls Initialize.
#include "../src/hal64/SixtyFourInput.cpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do {if(!(x)) {std::cerr << "Failed line " << __LINE__ << ": " #x "\n";std::exit(1);}} while(0)
static unsigned starts=0,stops=0;
static bool failStart=false;
bool SixtyFourCapture::start() {++starts;state.store(failStart?-1:1);return !failStart;}
void SixtyFourCapture::stop() {++stops;state.store(2);}
bool SixtyFourCapture::timestamp(double* s,uint64_t* h) const {*s=4096;*h=123456;return true;}
int main() {
    AudioObjectPropertyAddress a{kAudioPlugInPropertyDeviceList,kAudioObjectPropertyScopeGlobal,0};
    UInt32 size=0,value=0;
    CHECK(get(nullptr,plugin,0,&a,0,nullptr,4,&size,&value)==noErr && value==device && size==4);
    a={kAudioDevicePropertyStreams,kAudioObjectPropertyScopeInput,0};
    CHECK(get(nullptr,device,0,&a,0,nullptr,4,&size,&value)==noErr && value==stream);
    a.mScope=kAudioObjectPropertyScopeOutput;
    CHECK(sizeOf(nullptr,device,0,&a,0,nullptr,&size)==noErr && size==0);
    a={kAudioDevicePropertyZeroTimeStampPeriod,kAudioObjectPropertyScopeGlobal,0};
    CHECK(get(nullptr,device,0,&a,0,nullptr,4,&size,&value)==noErr && value>=10923 && value==SixtyFourCapture::clockPeriod);
    a={kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,0};
    AudioStreamBasicDescription f{};
    CHECK(get(nullptr,stream,0,&a,0,nullptr,sizeof(f),&size,&f)==noErr);
    CHECK(f.mChannelsPerFrame==12 && f.mSampleRate==48000 && f.mBytesPerFrame==48 && f.mBitsPerChannel==32);
    CHECK(get(nullptr,stream,0,&a,0,nullptr,4,&size,&value)==kAudioHardwareBadPropertySizeError);
    CHECK(get(nullptr,stream,0,&a,0,nullptr,0,&size,nullptr)==kAudioHardwareBadPropertySizeError);
    f.mSampleRate=44100;CHECK(set(nullptr,stream,0,&a,0,nullptr,sizeof(f),&f)!=noErr);
    for(UInt32 channel=1;channel<=12;++channel) {
        a={kAudioObjectPropertyElementName,kAudioObjectPropertyScopeInput,channel};
        CFStringRef name=nullptr;
        CHECK(get(nullptr,device,0,&a,0,nullptr,sizeof(name),&size,&name)==noErr);
        auto expected=CFStringCreateWithCString(nullptr,sixty_four_channel_name(true,channel),kCFStringEncodingUTF8);
        CHECK(CFEqual(name,expected));CFRelease(name);CFRelease(expected);
    }
    a={kAudioDevicePropertyPreferredChannelsForStereo,kAudioObjectPropertyScopeInput,0};
    UInt32 stereo[2];CHECK(get(nullptr,device,0,&a,0,nullptr,sizeof(stereo),&size,stereo)==noErr && stereo[0]==11 && stereo[1]==12);
    a={kAudioPlugInPropertyTranslateUIDToDevice,kAudioObjectPropertyScopeGlobal,0};
    CFStringRef nullUid=nullptr;
    CHECK(get(nullptr,plugin,0,&a,sizeof(nullUid),&nullUid,4,&size,&value)!=noErr);
    Boolean yes=false,inplace=false;
    CHECK(will(nullptr,device,0,kAudioServerPlugInIOOperationReadInput,&yes,&inplace)==noErr && yes && inplace);
    CHECK(will(nullptr,device,0,kAudioServerPlugInIOOperationWriteMix,&yes,&inplace)==noErr && !yes);
    CHECK(starts==0 && stops==0);
    CHECK(start(nullptr,device,1)==noErr && start(nullptr,device,1)==noErr && starts==1);
    CHECK(start(nullptr,device,2)==noErr && starts==1);
    Float64 sample;UInt64 ticks,generation,previous;
    CHECK(timestamp(nullptr,device,1,&sample,&ticks,&generation)==noErr && sample==4096 && ticks==123456);
    previous=generation;
    CHECK(stop(nullptr,device,1)==noErr && stops==0);
    CHECK(stop(nullptr,device,2)==noErr && stops==1);
    CHECK(timestamp(nullptr,device,1,&sample,&ticks,&generation)!=noErr);
    CHECK(start(nullptr,device,1)==noErr);
    CHECK(timestamp(nullptr,device,1,&sample,&ticks,&generation)==noErr && generation>previous);
    CHECK(stop(nullptr,device,1)==noErr && stops==2);
    failStart=true;CHECK(start(nullptr,device,3)!=noErr && clients.empty() && !active.load());
    AudioServerPlugInIOCycleInfo cycle{};float silence[12];for(float& v:silence) v=1;
    CHECK(io(nullptr,device,stream,0,kAudioServerPlugInIOOperationReadInput,1,&cycle,silence,nullptr)==kAudioHardwareNotRunningError);
    for(float v:silence) CHECK(v==0);
    std::cout << "HAL discovery, 12-channel format, bounds and fake client lifecycle passed\n";
}
