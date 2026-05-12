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

static const char kAUB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string au_b64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kAUB64Table[(n >> 18) & 0x3F]);
        out.push_back(kAUB64Table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kAUB64Table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kAUB64Table[ n       & 0x3F] : '=');
    }
    return out;
}

static std::vector<uint8_t> au_b64_decode(const std::string& s) {
    static const int8_t kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::vector<uint8_t> out;
    out.reserve((s.size() / 4) * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : s) {
        int v = kDec[c];
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
            acc &= (1u << bits) - 1u;
        }
    }
    return out;
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
        char                 name[64]    = {};
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
