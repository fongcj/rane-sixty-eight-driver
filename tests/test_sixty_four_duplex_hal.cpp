/* SPDX-License-Identifier: MIT */
// Compile the HAL contract into a normal executable and replace the USB engine.
// This test never loads a driver bundle, opens USB, or calls Initialize.
#include "../src/hal64/SixtyFourDuplex.cpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do {if(!(x)) {std::cerr << "Failed line " << __LINE__ << ": " #x "\n";std::exit(1);}} while(0)
static unsigned starts=0,stops=0;
static bool failStart=false;
static double writtenAt=-1;
static unsigned writtenFrames=0;
bool SixtyFourDuplex::start() {++starts;state.store(failStart?-1:1);return !failStart;}
void SixtyFourDuplex::stop() {++stops;state.store(2);}
bool SixtyFourDuplex::timestamp(double* s,uint64_t* h) const {*s=4096;*h=123456;return true;}
bool SixtyFourDuplex::write(double sample,unsigned frames,const float*) {writtenAt=sample;writtenFrames=frames;return true;}
int main() {
    AudioObjectPropertyAddress a{kAudioPlugInPropertyDeviceList,kAudioObjectPropertyScopeGlobal,0};
    UInt32 size=0,value=0;
    CHECK(get(nullptr,plugin,0,&a,0,nullptr,4,&size,&value)==noErr && value==device && size==4);
    a={kAudioDevicePropertyStreams,kAudioObjectPropertyScopeInput,0};
    CHECK(get(nullptr,device,0,&a,0,nullptr,4,&size,&value)==noErr && value==stream);
    a.mScope=kAudioObjectPropertyScopeOutput;
    CHECK(get(nullptr,device,0,&a,0,nullptr,4,&size,&value)==noErr && value==outStream);
    a={kAudioDevicePropertyZeroTimeStampPeriod,kAudioObjectPropertyScopeGlobal,0};
    CHECK(get(nullptr,device,0,&a,0,nullptr,4,&size,&value)==noErr && value>=10923 && value==SixtyFourDuplex::clockPeriod);
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
    CHECK(will(nullptr,device,0,kAudioServerPlugInIOOperationWriteMix,&yes,&inplace)==noErr && yes);
    a={kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,0};
    CHECK(get(nullptr,outStream,0,&a,0,nullptr,sizeof(f),&size,&f)==noErr);
    CHECK(f.mChannelsPerFrame==10 && f.mBytesPerFrame==40 && f.mSampleRate==48000);
    a={kAudioDevicePropertyStreams,kAudioObjectPropertyScopeGlobal,0};AudioObjectID both[2];
    CHECK(get(nullptr,device,0,&a,0,nullptr,sizeof(both),&size,both)==noErr && size==sizeof(both) && both[0]==stream && both[1]==outStream);
    for(UInt32 c=1;c<=10;++c) {
        a={kAudioObjectPropertyElementName,kAudioObjectPropertyScopeOutput,c};CFStringRef label=nullptr;
        CHECK(get(nullptr,device,0,&a,0,nullptr,sizeof(label),&size,&label)==noErr);
        auto expected=CFStringCreateWithCString(nullptr,sixty_four_channel_name(false,c),kCFStringEncodingUTF8);
        CHECK(CFEqual(label,expected));CFRelease(label);CFRelease(expected);
    }
    CHECK(starts==0 && stops==0);
    CHECK(start(nullptr,device,1)==noErr && start(nullptr,device,1)==noErr && starts==1);
    CHECK(start(nullptr,device,2)==noErr && starts==1);
    AudioServerPlugInIOCycleInfo outputCycle{};outputCycle.mOutputTime.mSampleTime=8192;
    float outputData[256*10]={};
    CHECK(io(nullptr,device,outStream,1,kAudioServerPlugInIOOperationWriteMix,256,&outputCycle,outputData,nullptr)==noErr);
    CHECK(writtenAt==8192 && writtenFrames==256);
    CHECK(io(nullptr,device,stream,1,kAudioServerPlugInIOOperationWriteMix,256,&outputCycle,outputData,nullptr)!=noErr);
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
