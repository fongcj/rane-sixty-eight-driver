/* SPDX-License-Identifier: MIT */
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <atomic>
#include <mutex>
#include <set>
#include <cstring>
#include <cmath>
#include "../protocol/sixty_four_audio.h"
#include "../transport/SixtyFourDuplex.h"

namespace {
constexpr AudioObjectID plugin=1,device=2,stream=3,outStream=4;
constexpr UInt32 period=SixtyFourDuplex::clockPeriod;
AudioServerPlugInHostRef host=nullptr;
std::atomic<UInt32> refs{1},bufferSize{512};
std::atomic<uint64_t> seed{1};
std::mutex lifecycle;
std::set<UInt32> clients;
SixtyFourDuplex capture;
std::atomic<bool> active{false};

bool present() {
    auto match=IOServiceMatching("IOUSBHostDevice");if(!match) return false;
    int vid=0x1cc5,pid=0x17;
    auto v=CFNumberCreate(nullptr,kCFNumberIntType,&vid),p=CFNumberCreate(nullptr,kCFNumberIntType,&pid);
    CFDictionarySetValue(match,CFSTR("idVendor"),v);CFDictionarySetValue(match,CFSTR("idProduct"),p);CFRelease(v);CFRelease(p);
    io_iterator_t it=0;if(IOServiceGetMatchingServices(kIOMainPortDefault,match,&it)) return false;
    auto a=IOIteratorNext(it),b=IOIteratorNext(it);bool one=a && !b;
    if(a) IOObjectRelease(a);if(b) IOObjectRelease(b);IOObjectRelease(it);return one;
}
AudioStreamBasicDescription format(bool output=false) {
    UInt32 bytes=output?40:48,channels=output?10:12;
    return {48000,kAudioFormatLinearPCM,kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,bytes,1,bytes,channels,32,0};
}
// One property implementation supplies both sizing and data, with exact bounds.
OSStatus property(AudioObjectID object,const AudioObjectPropertyAddress* a,UInt32 qsize,const void* qualifier,
                  UInt32 capacity,UInt32* size,void* data) {
    if(!a || !size) return kAudioHardwareIllegalOperationError;
    *size=0;
    if(object!=plugin && object!=device && object!=stream && object!=outStream) return kAudioHardwareBadObjectError;
    bool output=object==outStream || a->mScope==kAudioObjectPropertyScopeOutput;
    auto bytes=[&](const void* p,UInt32 n)->OSStatus {
        *size=n;if(!data) return noErr;
        if(capacity<n) return kAudioHardwareBadPropertySizeError;
        if(n) std::memcpy(data,p,n);return noErr;
    };
    auto scalar=[&](auto value)->OSStatus {return bytes(&value,sizeof(value));};
    auto string=[&](CFStringRef text)->OSStatus {
        auto result=scalar(text);if(!result && data) CFRetain(text);return result;
    };
    auto objects=[&](AudioObjectID id,bool include)->OSStatus {return bytes(&id,include?sizeof(id):0);};
    switch(a->mSelector) {
    case kAudioObjectPropertyBaseClass:return scalar(AudioClassID(kAudioObjectClassID));
    case kAudioObjectPropertyClass:return scalar(AudioClassID(object==plugin?kAudioPlugInClassID:object==device?kAudioDeviceClassID:kAudioStreamClassID));
    case kAudioObjectPropertyOwner:return scalar(AudioObjectID(object==plugin?kAudioObjectUnknown:object==device?plugin:device));
    case kAudioObjectPropertyManufacturer:return string(CFSTR("Rane Corporation"));
    case kAudioObjectPropertyOwnedObjects:
        if(object==plugin) return objects(device,true);
        if(object!=device) return bytes(nullptr,0);
        {AudioObjectID ids[]={stream,outStream};
         if(a->mScope==kAudioObjectPropertyScopeGlobal) return bytes(ids,sizeof(ids));
         return objects(output?outStream:stream,true);}
    case kAudioObjectPropertyName:
    case kAudioObjectPropertyElementName:
        if(object==plugin) return string(CFSTR("Sixty-Four Duplex Driver"));
        if(!a->mElement) return string(object==device?CFSTR("Rane Sixty-Four"):(output?CFSTR("Sixty-Four 10 Outputs"):CFSTR("Sixty-Four 12 Inputs")));
        if(a->mElement>(output?10:12)) return kAudioHardwareUnknownPropertyError;
        {auto text=CFStringCreateWithCString(nullptr,sixty_four_channel_name(!output,a->mElement),kCFStringEncodingUTF8);
         auto r=string(text);CFRelease(text);return r;}
    }
    if(object==plugin) switch(a->mSelector) {
    case kAudioPlugInPropertyDeviceList:return objects(device,true);
    case kAudioPlugInPropertyResourceBundle:return string(CFSTR(""));
    case kAudioPlugInPropertyTranslateUIDToDevice:
        if(qsize!=sizeof(CFStringRef) || !qualifier) return kAudioHardwareBadPropertySizeError;
        {auto uid=*static_cast<const CFStringRef*>(qualifier);
         if(!uid || CFGetTypeID(uid)!=CFStringGetTypeID()) return kAudioHardwareIllegalOperationError;
         return scalar(AudioObjectID(CFEqual(uid,CFSTR("org.openrane.sixtyfour.duplex"))?device:kAudioObjectUnknown));}
    }
    if(object==device) switch(a->mSelector) {
    case kAudioObjectPropertyModelName:return string(CFSTR("Rane Sixty-Four"));
    case kAudioDevicePropertyDeviceUID:return string(CFSTR("org.openrane.sixtyfour.duplex"));
    case kAudioDevicePropertyModelUID:return string(CFSTR("RaneSixtyFour_Duplex_Model"));
    case kAudioDevicePropertyTransportType:return scalar(UInt32(kAudioDeviceTransportTypeUSB));
    case kAudioDevicePropertyClockDomain:return scalar(UInt32(0));
    case kAudioDevicePropertyDeviceIsAlive:return scalar(UInt32(active.load()?capture.healthy():present()));
    case kAudioDevicePropertyDeviceIsRunning:return scalar(UInt32(active.load() && capture.healthy()));
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:return scalar(UInt32(0));
    case kAudioDevicePropertyIsHidden:return scalar(UInt32(0));
    case kAudioDevicePropertyNominalSampleRate:return scalar(Float64(48000));
    case kAudioDevicePropertyAvailableNominalSampleRates:{AudioValueRange v{48000,48000};return scalar(v);}
    case kAudioDevicePropertyBufferFrameSize:return scalar(bufferSize.load());
    case kAudioDevicePropertyBufferFrameSizeRange:{AudioValueRange v{64,1024};return scalar(v);}
    case kAudioDevicePropertyZeroTimeStampPeriod:return scalar(period);
    case kAudioDevicePropertyLatency:return scalar(UInt32(0));
    case kAudioDevicePropertySafetyOffset:return scalar(UInt32(output?4096:2048));
    case kAudioDevicePropertyStreams:{AudioObjectID ids[]={stream,outStream};
        if(a->mScope==kAudioObjectPropertyScopeGlobal) return bytes(ids,sizeof(ids));
        return objects(output?outStream:stream,true);}
    case kAudioDevicePropertyRelatedDevices:return objects(device,true);
    case kAudioObjectPropertyControlList:return bytes(nullptr,0);
    case kAudioDevicePropertyPreferredChannelsForStereo:{UInt32 pair[]={output?3u:11u,output?4u:12u};return bytes(pair,sizeof(pair));}
    }
    if(object==stream || object==outStream) switch(a->mSelector) {
    case kAudioStreamPropertyIsActive:return scalar(UInt32(1));
    case kAudioStreamPropertyDirection:return scalar(UInt32(output?0:1));
    case kAudioStreamPropertyTerminalType:return scalar(UInt32(kAudioStreamTerminalTypeLine));
    case kAudioStreamPropertyStartingChannel:return scalar(UInt32(1));
    case kAudioStreamPropertyLatency:return scalar(UInt32(0));
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat:return scalar(format(output));
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats:{AudioStreamRangedDescription d{format(output),{48000,48000}};return scalar(d);}
    }
    return kAudioHardwareUnknownPropertyError;
}
HRESULT query(void* self,REFIID iid,LPVOID* out) {
    if(!out) return E_POINTER;*out=nullptr;auto id=CFUUIDCreateFromUUIDBytes(nullptr,iid);
    bool valid=CFEqual(id,IUnknownUUID) || CFEqual(id,kAudioServerPlugInDriverInterfaceUUID);CFRelease(id);
    if(!valid) return E_NOINTERFACE;refs.fetch_add(1);*out=self;return S_OK;
}
ULONG add(void*) {return refs.fetch_add(1)+1;}
ULONG release(void*) {return refs.fetch_sub(1)-1;}
OSStatus initialize(AudioServerPlugInDriverRef,AudioServerPlugInHostRef h) {host=h;return noErr;}
OSStatus create(AudioServerPlugInDriverRef,CFDictionaryRef,const AudioServerPlugInClientInfo*,AudioObjectID*) {return kAudioHardwareUnsupportedOperationError;}
OSStatus destroy(AudioServerPlugInDriverRef,AudioObjectID) {return kAudioHardwareUnsupportedOperationError;}
OSStatus clientAdd(AudioServerPlugInDriverRef,AudioObjectID id,const AudioServerPlugInClientInfo*) {return id==device?noErr:kAudioHardwareBadObjectError;}
OSStatus stop(AudioServerPlugInDriverRef,AudioObjectID,UInt32);
OSStatus clientRemove(AudioServerPlugInDriverRef d,AudioObjectID id,const AudioServerPlugInClientInfo* c) {return c?stop(d,id,c->mClientID):kAudioHardwareIllegalOperationError;}
OSStatus configure(AudioServerPlugInDriverRef,AudioObjectID id,UInt64 action,void*) {
    if(id!=device || action<64 || action>1024) return kAudioHardwareIllegalOperationError;
    bufferSize.store(UInt32(action));return noErr;
}
OSStatus abortConfigure(AudioServerPlugInDriverRef,AudioObjectID,UInt64,void*) {return noErr;}
Boolean has(AudioServerPlugInDriverRef,AudioObjectID id,pid_t,const AudioObjectPropertyAddress* a) {
    UInt32 size=0;CFStringRef uid=CFSTR("org.openrane.sixtyfour.duplex");return property(id,a,sizeof(uid),&uid,0,&size,nullptr)==noErr;
}
OSStatus settable(AudioServerPlugInDriverRef,AudioObjectID id,pid_t,const AudioObjectPropertyAddress* a,Boolean* value) {
    if(!a || !value) return kAudioHardwareIllegalOperationError;
    UInt32 size=0;CFStringRef uid=CFSTR("org.openrane.sixtyfour.duplex");auto r=property(id,a,sizeof(uid),&uid,0,&size,nullptr);if(r) return r;
    *value=(id==device && (a->mSelector==kAudioDevicePropertyBufferFrameSize || a->mSelector==kAudioDevicePropertyNominalSampleRate)) ||
           ((id==stream || id==outStream) && (a->mSelector==kAudioStreamPropertyVirtualFormat || a->mSelector==kAudioStreamPropertyPhysicalFormat));return noErr;
}
OSStatus sizeOf(AudioServerPlugInDriverRef,AudioObjectID id,pid_t,const AudioObjectPropertyAddress* a,UInt32 qs,const void* q,UInt32* size) {return property(id,a,qs,q,0,size,nullptr);}
OSStatus get(AudioServerPlugInDriverRef,AudioObjectID id,pid_t,const AudioObjectPropertyAddress* a,UInt32 qs,const void* q,UInt32 cap,UInt32* size,void* data) {
    if(!data) {
        auto r=property(id,a,qs,q,0,size,nullptr);
        return r?r:(*size?kAudioHardwareBadPropertySizeError:noErr);
    }
    return property(id,a,qs,q,cap,size,data);
}
OSStatus set(AudioServerPlugInDriverRef,AudioObjectID id,pid_t,const AudioObjectPropertyAddress* a,UInt32,const void*,UInt32 size,const void* data) {
    if(!a || !data) return kAudioHardwareIllegalOperationError;
    if(id==device && a->mSelector==kAudioDevicePropertyNominalSampleRate) {
        if(size!=sizeof(Float64)) return kAudioHardwareBadPropertySizeError;
        return *static_cast<const Float64*>(data)==48000?noErr:kAudioHardwareIllegalOperationError;
    }
    if(id==device && a->mSelector==kAudioDevicePropertyBufferFrameSize) {
        if(size!=sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;auto n=*static_cast<const UInt32*>(data);
        if(n<64 || n>1024) return kAudioHardwareIllegalOperationError;
        if(n==bufferSize.load()) return noErr;
        return host?host->RequestDeviceConfigurationChange(host,device,n,nullptr):kAudioHardwareIllegalOperationError;
    }
    if((id==stream || id==outStream) && (a->mSelector==kAudioStreamPropertyVirtualFormat || a->mSelector==kAudioStreamPropertyPhysicalFormat)) {
        if(size!=sizeof(AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
        auto expected=format(id==outStream);return !std::memcmp(data,&expected,sizeof(expected))?noErr:kAudioHardwareUnsupportedOperationError;
    }
    return kAudioHardwareUnknownPropertyError;
}
OSStatus start(AudioServerPlugInDriverRef,AudioObjectID id,UInt32 client) {
    if(id!=device) return kAudioHardwareBadObjectError;
    std::lock_guard<std::mutex> guard(lifecycle);
    if(clients.count(client)) return noErr;
    if(clients.empty()) {
        if(!capture.start()) return kAudioHardwareUnspecifiedError;
        seed.fetch_add(1);active.store(true);
    } else if(!capture.healthy()) return kAudioHardwareUnspecifiedError;
    try { clients.insert(client); }
    catch(...) {if(clients.empty()) {active.store(false);capture.stop();}return kAudioHardwareUnspecifiedError;}
    return noErr;
}
OSStatus stop(AudioServerPlugInDriverRef,AudioObjectID id,UInt32 client) {
    if(id!=device) return kAudioHardwareBadObjectError;
    std::lock_guard<std::mutex> guard(lifecycle);
    if(clients.erase(client) && clients.empty()) {active.store(false);capture.stop();if(!capture.cleanlyStopped())return kAudioHardwareUnspecifiedError;}
    return noErr;
}
OSStatus timestamp(AudioServerPlugInDriverRef,AudioObjectID id,UInt32,Float64* sample,UInt64* ticks,UInt64* generation) {
    if(id!=device || !sample || !ticks || !generation) return kAudioHardwareIllegalOperationError;
    uint64_t h=0;
    if(!active.load() || !capture.healthy() || !capture.timestamp(sample,&h)) return kAudioHardwareNotRunningError;
    *ticks=h;*generation=seed.load();return noErr;
}
OSStatus will(AudioServerPlugInDriverRef,AudioObjectID id,UInt32,UInt32 op,Boolean* yes,Boolean* inplace) {
    if(id!=device || !yes || !inplace) return kAudioHardwareIllegalOperationError;
    *yes=op==kAudioServerPlugInIOOperationReadInput || op==kAudioServerPlugInIOOperationWriteMix;*inplace=true;return noErr;
}
OSStatus begin(AudioServerPlugInDriverRef,AudioObjectID,UInt32,UInt32,UInt32,const AudioServerPlugInIOCycleInfo*) {return noErr;}
OSStatus end(AudioServerPlugInDriverRef,AudioObjectID,UInt32,UInt32,UInt32,const AudioServerPlugInIOCycleInfo*) {return noErr;}
OSStatus io(AudioServerPlugInDriverRef,AudioObjectID id,AudioObjectID sid,UInt32,UInt32 op,UInt32 frames,const AudioServerPlugInIOCycleInfo* cycle,void* data,void*) {
    if(id!=device || (sid!=stream && sid!=outStream)) return kAudioHardwareBadObjectError;
    bool output=sid==outStream;
    if(op!=(output?kAudioServerPlugInIOOperationWriteMix:kAudioServerPlugInIOOperationReadInput)) return kAudioHardwareUnsupportedOperationError;
    if(!cycle || !data || frames>1024) return kAudioHardwareIllegalOperationError;
    if(!active.load() || !capture.healthy()) {
        if(!output) std::memset(data,0,frames*48);
        return kAudioHardwareNotRunningError;
    }
    if(output) return capture.write(cycle->mOutputTime.mSampleTime,frames,static_cast<const float*>(data))?noErr:kAudioHardwareUnspecifiedError;
    capture.read(cycle->mInputTime.mSampleTime,frames,static_cast<float*>(data));return noErr;
}
AudioServerPlugInDriverInterface table={nullptr,query,add,release,initialize,create,destroy,clientAdd,clientRemove,configure,abortConfigure,has,settable,sizeOf,get,set,start,stop,timestamp,will,begin,io,end};
AudioServerPlugInDriverInterface* pointer=&table;
}
extern "C" __attribute__((visibility("default"))) void* SixtyFourDuplexFactory(CFAllocatorRef,CFUUIDRef type) {
    return type && CFEqual(type,kAudioServerPlugInTypeUUID)?&pointer:nullptr;
}
