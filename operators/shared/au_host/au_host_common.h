#pragma once
// au_host_common.h — Shared AU v2 host infrastructure used by AUInstrument and AUEffect.
// Header-only, anonymous namespace. macOS only.
//
// Thread model:
//   main thread  : au_load_plugin, AudioUnitInitialize, AudioUnitUninitialize,
//                  AudioComponentInstanceDispose, au_save_state, au_load_state,
//                  au_cache_params, AudioUnitSetProperty (stream format, host callbacks)
//   audio thread : AudioUnitRender, MusicDeviceMIDIEvent, AudioUnitSetParameter

#ifdef __APPLE__
#include "operator_api/types.h"
#include "shared/plugin_common/base64.h"
#include "shared/au_host/au_scanner.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {

// ---------------------------------------------------------------------------
// Base64 encode/decode (RFC 4648, standard alphabet) — same as clap_host_common.h
// ---------------------------------------------------------------------------

// Thin wrappers over the shared canonical base64 (operators/shared/plugin_common/
// base64.h) — keep the host-local names so call sites are unchanged. (audit 09-F1)
static std::string au_b64_encode(const uint8_t* data, size_t len) {
    return vivid::plugin_common::base64_encode(data, len);
}
static std::vector<uint8_t> au_b64_decode(const std::string& s) {
    return vivid::plugin_common::base64_decode(s);
}

// ---------------------------------------------------------------------------
// Transport state — written on audio thread before each render, read by
// HostCallbackInfo callbacks (which are invoked during AudioUnitRender on
// the same audio thread).
// ---------------------------------------------------------------------------

struct AUTransportState {
    double   bpm  = 120.0;
    double   beat = 0.0;
    uint32_t bpb  = 4;
};

// ---------------------------------------------------------------------------
// HostCallbackInfo static callbacks
// ---------------------------------------------------------------------------

static OSStatus au_beat_and_tempo_proc(void*    data,
                                        Float64* outCurrentBeat,
                                        Float64* outCurrentTempo) {
    const auto* ts = static_cast<const AUTransportState*>(data);
    if (outCurrentBeat)  *outCurrentBeat  = ts->beat;
    if (outCurrentTempo) *outCurrentTempo = ts->bpm;
    return noErr;
}

static OSStatus au_musical_time_location_proc(void*    data,
                                               UInt32*  outDeltaSampleOffsetToNextBeat,
                                               Float32* outTimeSig_Numerator,
                                               UInt32*  outTimeSig_Denominator,
                                               Float64* outCurrentMeasureDownBeat) {
    const auto* ts = static_cast<const AUTransportState*>(data);
    if (outDeltaSampleOffsetToNextBeat) *outDeltaSampleOffsetToNextBeat = 0;
    if (outTimeSig_Numerator)           *outTimeSig_Numerator  = static_cast<Float32>(ts->bpb);
    if (outTimeSig_Denominator)         *outTimeSig_Denominator = 4;
    if (outCurrentMeasureDownBeat) {
        double bpb = ts->bpb > 0 ? static_cast<double>(ts->bpb) : 4.0;
        *outCurrentMeasureDownBeat = std::floor(ts->beat / bpb) * bpb;
    }
    return noErr;
}

// ---------------------------------------------------------------------------
// AUHandle — owns one initialized AudioUnit instance.
// Constructed and destroyed on the main thread only.
// ---------------------------------------------------------------------------

static constexpr AudioUnitParameterID kAUInvalidParamID = static_cast<AudioUnitParameterID>(-1);

struct AUHandle {
    AudioUnit   au        = nullptr;
    std::string comp_name;

    struct ParamEntry {
        AudioUnitParameterID id          = kAUInvalidParamID;
        float                min_val     = 0.f;
        float                max_val     = 1.f;
        float                default_val = 0.f;
        int                  step_count  = 0;    // 0=continuous, >0=discrete
        char                 name[64]    = {};
        char                 units[32]   = {};
    };
    std::vector<ParamEntry> params;

    ~AUHandle() {
        if (au) {
            AudioUnitUninitialize(au);
            AudioComponentInstanceDispose(au);
            au = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Map AudioUnitParameterUnit enum to a short display string.
// Returns "" for generic/unknown units (caller should omit the field).
// ---------------------------------------------------------------------------

static const char* au_unit_to_string(AudioUnitParameterUnit u) {
    switch (u) {
        case kAudioUnitParameterUnit_Percent:             return "%";
        case kAudioUnitParameterUnit_Seconds:             return "s";
        case kAudioUnitParameterUnit_Milliseconds:        return "ms";
        case kAudioUnitParameterUnit_SampleFrames:        return "frames";
        case kAudioUnitParameterUnit_Phase:               return "deg";
        case kAudioUnitParameterUnit_Rate:                return "x";
        case kAudioUnitParameterUnit_Hertz:               return "Hz";
        case kAudioUnitParameterUnit_Cents:               return "cents";
        case kAudioUnitParameterUnit_RelativeSemiTones:   return "semitones";
        case kAudioUnitParameterUnit_MIDINoteNumber:      return "MIDI note";
        case kAudioUnitParameterUnit_MIDIController:      return "MIDI CC";
        case kAudioUnitParameterUnit_Decibels:            return "dB";
        case kAudioUnitParameterUnit_LinearGain:          return "gain";
        case kAudioUnitParameterUnit_Degrees:             return "deg";
        case kAudioUnitParameterUnit_Pan:                 return "pan";
        case kAudioUnitParameterUnit_Meters:              return "m";
        case kAudioUnitParameterUnit_AbsoluteCents:       return "cents";
        case kAudioUnitParameterUnit_Octaves:             return "oct";
        case kAudioUnitParameterUnit_BPM:                 return "BPM";
        case kAudioUnitParameterUnit_Beats:               return "beats";
        case kAudioUnitParameterUnit_Ratio:               return "ratio";
        default:                                          return "";
    }
}

// ---------------------------------------------------------------------------
// Cache AU parameters into handle (main thread, after AudioUnitInitialize)
// ---------------------------------------------------------------------------

static void au_cache_params(AUHandle* h) {
    UInt32 sz = 0;
    if (AudioUnitGetPropertyInfo(h->au, kAudioUnitProperty_ParameterList,
                                  kAudioUnitScope_Global, 0, &sz, nullptr) != noErr || sz == 0)
        return;

    uint32_t count = sz / sizeof(AudioUnitParameterID);
    std::vector<AudioUnitParameterID> ids(count);
    if (AudioUnitGetProperty(h->au, kAudioUnitProperty_ParameterList,
                              kAudioUnitScope_Global, 0, ids.data(), &sz) != noErr)
        return;

    h->params.reserve(count);
    for (auto pid : ids) {
        AudioUnitParameterInfo info = {};
        UInt32 isz = sizeof(info);
        if (AudioUnitGetProperty(h->au, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Global, pid, &info, &isz) != noErr)
            continue;

        AUHandle::ParamEntry e{};
        e.id          = pid;
        e.min_val     = info.minValue;
        e.max_val     = info.maxValue;
        e.default_val = info.defaultValue;

        if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) && info.cfNameString) {
            CFStringGetCString(info.cfNameString, e.name, sizeof(e.name), kCFStringEncodingUTF8);
            CFRelease(info.cfNameString);
        } else {
            std::strncpy(e.name, info.name, sizeof(e.name) - 1);
        }

        // Units
        if (info.unit == kAudioUnitParameterUnit_CustomUnit && info.unitName) {
            CFStringGetCString(info.unitName, e.units, sizeof(e.units), kCFStringEncodingUTF8);
            CFRelease(info.unitName);
        } else {
            const char* u = au_unit_to_string(info.unit);
            std::strncpy(e.units, u, sizeof(e.units) - 1);
        }

        // Stepped detection
        if (info.unit == kAudioUnitParameterUnit_Boolean) {
            e.step_count = 1;
        } else if (info.unit == kAudioUnitParameterUnit_Indexed) {
            e.step_count = static_cast<int>(info.maxValue - info.minValue);
        }

        h->params.push_back(e);
    }
}

// ---------------------------------------------------------------------------
// State persistence via kAudioUnitProperty_ClassInfo + CFPropertyList XML
// ---------------------------------------------------------------------------

static std::string au_save_state(const AUHandle* h) {
    if (!h || !h->au) return {};
    CFPropertyListRef plist = nullptr;
    UInt32 sz = sizeof(plist);
    if (AudioUnitGetProperty(h->au, kAudioUnitProperty_ClassInfo,
                              kAudioUnitScope_Global, 0, &plist, &sz) != noErr || !plist)
        return {};
    CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, plist,
                                               kCFPropertyListXMLFormat_v1_0, 0, nullptr);
    CFRelease(plist);
    if (!data) return {};
    std::string b64 = au_b64_encode(CFDataGetBytePtr(data),
                                     static_cast<size_t>(CFDataGetLength(data)));
    CFRelease(data);
    return b64;
}

static void au_load_state(AUHandle* h, const std::string& b64) {
    if (!h || !h->au || b64.empty()) return;
    std::vector<uint8_t> raw = au_b64_decode(b64);
    if (raw.empty()) return;
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, raw.data(),
                                   static_cast<CFIndex>(raw.size()));
    if (!data) return;
    CFErrorRef err = nullptr;
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        kCFAllocatorDefault, data, kCFPropertyListImmutable, nullptr, &err);
    CFRelease(data);
    if (!plist) { if (err) CFRelease(err); return; }
    AudioUnitSetProperty(h->au, kAudioUnitProperty_ClassInfo,
                          kAudioUnitScope_Global, 0, &plist, sizeof(plist));
    CFRelease(plist);
    if (err) CFRelease(err);
}

// ---------------------------------------------------------------------------
// Params → JSON (for _au_params hidden param consumed by list_au_params)
// ---------------------------------------------------------------------------

static std::string au_params_to_json(const AUHandle* h) {
    if (!h || h->params.empty()) return "[]";
    std::string json = "[";
    bool first = true;
    for (const auto& p : h->params) {
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"";
        for (const char* c = p.name; *c; ++c) {
            if (*c == '"')       json += "\\\"";
            else if (*c == '\\') json += "\\\\";
            else                 json += *c;
        }
        json += "\",\"id\":";
        json += std::to_string(p.id);
        json += ",\"min\":";
        json += std::to_string(p.min_val);
        json += ",\"max\":";
        json += std::to_string(p.max_val);
        json += ",\"default\":";
        json += std::to_string(p.default_val);
        json += ",\"step_count\":";
        json += std::to_string(p.step_count);
        if (p.units[0] != '\0') {
            json += ",\"units\":\"";
            for (const char* c = p.units; *c; ++c) {
                if (*c == '"')       json += "\\\"";
                else if (*c == '\\') json += "\\\\";
                else                 json += *c;
            }
            json += "\"";
        }
        if (h->au) {
            Float32 val = 0.f;
            if (AudioUnitGetParameter(h->au, p.id, kAudioUnitScope_Global, 0, &val) == noErr) {
                json += ",\"value\":";
                json += std::to_string(val);
            }
        }
        json += "}";
    }
    json += "]";
    return json;
}

// ---------------------------------------------------------------------------
// Load, configure, and initialize an AudioUnit. Returns null on failure.
// Must be called on the main thread.
// ts: pointer to the operator's AUTransportState (stable, for HostCallbackInfo)
// ---------------------------------------------------------------------------

static AUHandle* au_load_plugin(const std::string& name,
                                  uint32_t sample_rate,
                                  AUTransportState* ts,
                                  const std::string& saved_state) {
    AudioComponent comp = au_find_by_name(name);
    if (!comp) {
        fprintf(stderr, "[AUInstrument] component '%s' not found\n", name.c_str());
        return nullptr;
    }

    AudioUnit au = nullptr;
    if (AudioComponentInstanceNew(comp, &au) != noErr || !au) {
        fprintf(stderr, "[AUInstrument] AudioComponentInstanceNew failed for '%s'\n", name.c_str());
        return nullptr;
    }

    // Output stream format: 32-bit float, non-interleaved stereo
    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate       = static_cast<Float64>(sample_rate);
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat
                          | kAudioFormatFlagIsNonInterleaved
                          | kAudioFormatFlagsNativeEndian;
    fmt.mBytesPerPacket   = sizeof(float);
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerFrame    = sizeof(float);
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel   = 32;
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));

    UInt32 max_frames = 4096;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice,
                          kAudioUnitScope_Global, 0, &max_frames, sizeof(max_frames));

    // Transport host callbacks — AU copies this struct; ts pointer must remain valid
    HostCallbackInfo hci = {};
    hci.hostUserData            = ts;
    hci.beatAndTempoProc        = au_beat_and_tempo_proc;
    hci.musicalTimeLocationProc = au_musical_time_location_proc;
    AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks,
                          kAudioUnitScope_Global, 0, &hci, sizeof(hci));

    if (AudioUnitInitialize(au) != noErr) {
        fprintf(stderr, "[AUInstrument] AudioUnitInitialize failed for '%s'\n", name.c_str());
        AudioComponentInstanceDispose(au);
        return nullptr;
    }

    auto* h      = new AUHandle();
    h->au        = au;
    h->comp_name = name;

    au_cache_params(h);
    au_load_state(h, saved_state);

    return h;
}

} // namespace
#endif // __APPLE__
