#pragma once
// Shared VST3 host infrastructure used by Vst3Instrument and Vst3Effect.
// Header-only. Mirrors clap_host_common.h for VST3 — like it, the host TYPES + helpers live in
// namespace vivid::session with external linkage (types) / `inline` (free functions + globals), so
// cohesive groups can be split into their own TUs (ADR-0025). It used to be an anonymous namespace,
// which gave everything internal linkage and is exactly what kept vst3_host.cpp a single TU.

#include "vivid_audio_context.h"
#include "audio/plugin_watchdog.h"   // ADR-0045 Tier 2a: PluginFaultState
#include "base64.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"

#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>
#include <zlib.h>
#include "audio/authored_base.h"   // Ph5 P2-05: authored base survives a param re-cache (by id)
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace vivid::session {

using namespace Steinberg;
using namespace Steinberg::Vst;

// Reference count per bundle path — prevents CFBundleUnloadExecutable from
// unmapping a dylib that a second handle (e.g. from reload_for_rate_change)
// still depends on. Only call bundle_exit/UnloadExecutable when count → 0.
// All accesses are main-thread-only (load and destroy both happen on main).
inline std::unordered_map<std::string, int> g_vst3_bundle_refs;

// ---------------------------------------------------------------------------
// Base64 encode/decode (RFC 4648, standard alphabet)
// ---------------------------------------------------------------------------

// Thin wrappers over the shared canonical base64 (operators/shared/plugin_common/
// base64.h) — keep the host-local names so call sites are unchanged. (audit 09-F1)
inline std::string vst3_b64_encode(const uint8_t* data, size_t len) {
    return vivid::plugin_common::base64_encode(data, len);
}
inline std::vector<uint8_t> vst3_b64_decode(const std::string& s) {
    return vivid::plugin_common::base64_decode(s);
}

// ---------------------------------------------------------------------------
// IBStream backed by a memory buffer — used for plugin state save/load
// ---------------------------------------------------------------------------

struct MemIBStream : IBStream {
    std::vector<uint8_t> buf;
    int64                pos = 0;

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0 ||
            std::memcmp(_iid, IBStream::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IBStream*>(this); return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
        int32 avail = static_cast<int32>(static_cast<int64>(buf.size()) - pos);
        int32 n = (numBytes < avail) ? numBytes : avail;
        if (n > 0) { std::memcpy(buffer, buf.data() + pos, static_cast<size_t>(n)); pos += n; }
        if (numBytesRead) *numBytesRead = n;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
        const auto* p = static_cast<const uint8_t*>(buffer);
        buf.insert(buf.end(), p, p + static_cast<size_t>(numBytes));
        pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos_, int32 mode, int64* result) override {
        int64 sz = static_cast<int64>(buf.size());
        if      (mode == kIBSeekSet) pos = pos_;
        else if (mode == kIBSeekCur) pos += pos_;
        else if (mode == kIBSeekEnd) pos = sz + pos_;
        if (pos < 0)  pos = 0;
        if (pos > sz) pos = sz;
        if (result) *result = pos;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos_out) override {
        if (pos_out) *pos_out = pos;
        return kResultOk;
    }
};

// ---------------------------------------------------------------------------
// IEventList — fixed-capacity event queue for note events
// ---------------------------------------------------------------------------

struct Vst3EventList : IEventList {
    static constexpr int kMaxEvents = 256;
    Event events_[kMaxEvents];
    int   count_ = 0;

    void clear() { count_ = 0; }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0 ||
            std::memcmp(_iid, IEventList::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IEventList*>(this); return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override { return count_; }

    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= count_) return kResultFalse;
        e = events_[index];
        return kResultOk;
    }

    tresult PLUGIN_API addEvent(Event& e) override {
        if (count_ >= kMaxEvents) return kResultFalse;
        events_[count_++] = e;
        return kResultOk;
    }
};

// ---------------------------------------------------------------------------
// IParameterChanges / IParamValueQueue — for macro parameter automation
// ---------------------------------------------------------------------------

struct SinglePointQueue : IParamValueQueue {
    ParamID    id    = 0;
    ParamValue value = 0.0;

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0 ||
            std::memcmp(_iid, IParamValueQueue::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IParamValueQueue*>(this); return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    ParamID PLUGIN_API getParameterId()  override { return id; }
    int32   PLUGIN_API getPointCount()   override { return 1;  }

    tresult PLUGIN_API getPoint(int32 /*index*/, int32& sampleOffset, ParamValue& val) override {
        sampleOffset = 0; val = value; return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
};

struct Vst3ParamChanges : IParameterChanges {
    // ADR-0034: was 8; raised so per-block control-edge modulation (up to kMaxControlInputs params)
    // can stack on top of the drained UI param edits without overflowing the queue.
    static constexpr int kMaxParams = 64;
    SinglePointQueue queues_[kMaxParams];
    int count_ = 0;

    void clear() { count_ = 0; }

    void add(ParamID id, ParamValue value) {
        if (count_ >= kMaxParams) return;
        queues_[count_].id    = id;
        queues_[count_].value = value;
        ++count_;
    }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0 ||
            std::memcmp(_iid, IParameterChanges::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IParameterChanges*>(this); return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32            PLUGIN_API getParameterCount() override { return count_; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        if (index < 0 || index >= count_) return nullptr;
        return &queues_[index];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& pid, int32& index) override {
        index = count_;
        if (count_ >= kMaxParams) return nullptr;
        queues_[count_].id = pid;
        return &queues_[count_++];
    }
};

// ---------------------------------------------------------------------------
// IHostApplication — minimal host context passed to IPluginBase::initialize()
// ---------------------------------------------------------------------------

struct Vst3AttributeList final : IAttributeList {
    std::atomic<uint32> ref_count{1};
    std::unordered_map<std::string, int64> int_values;
    std::unordered_map<std::string, double> float_values;
    std::unordered_map<std::string, std::u16string> string_values;
    std::unordered_map<std::string, std::vector<uint8_t>> binary_values;

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0 ||
            std::memcmp(_iid, IAttributeList::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IAttributeList*>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++ref_count; }
    uint32 PLUGIN_API release() override {
        uint32 n = --ref_count;
        if (n == 0) delete this;
        return n;
    }

    tresult PLUGIN_API setInt(AttrID id, int64 value) override {
        if (!id) return kInvalidArgument;
        int_values[id] = value;
        return kResultOk;
    }
    tresult PLUGIN_API getInt(AttrID id, int64& value) override {
        if (!id) return kInvalidArgument;
        auto it = int_values.find(id);
        if (it == int_values.end()) return kResultFalse;
        value = it->second;
        return kResultTrue;
    }
    tresult PLUGIN_API setFloat(AttrID id, double value) override {
        if (!id) return kInvalidArgument;
        float_values[id] = value;
        return kResultOk;
    }
    tresult PLUGIN_API getFloat(AttrID id, double& value) override {
        if (!id) return kInvalidArgument;
        auto it = float_values.find(id);
        if (it == float_values.end()) return kResultFalse;
        value = it->second;
        return kResultTrue;
    }
    tresult PLUGIN_API setString(AttrID id, const TChar* string) override {
        if (!id) return kInvalidArgument;
        std::u16string value;
        if (string) {
            for (const TChar* p = string; *p; ++p)
                value.push_back(static_cast<char16_t>(*p));
        }
        string_values[id] = std::move(value);
        return kResultOk;
    }
    tresult PLUGIN_API getString(AttrID id, TChar* string, uint32 sizeInBytes) override {
        if (!id || !string || sizeInBytes < sizeof(TChar)) return kInvalidArgument;
        auto it = string_values.find(id);
        if (it == string_values.end()) return kResultFalse;
        uint32 max_chars = sizeInBytes / sizeof(TChar);
        uint32 n = static_cast<uint32>(std::min<size_t>(it->second.size(), max_chars - 1));
        for (uint32 i = 0; i < n; ++i)
            string[i] = static_cast<TChar>(it->second[i]);
        string[n] = 0;
        return kResultTrue;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void* data, uint32 sizeInBytes) override {
        if (!id || (!data && sizeInBytes > 0)) return kInvalidArgument;
        const auto* p = static_cast<const uint8_t*>(data);
        binary_values[id] = std::vector<uint8_t>(p, p + sizeInBytes);
        return kResultOk;
    }
    tresult PLUGIN_API getBinary(AttrID id, const void*& data, uint32& sizeInBytes) override {
        if (!id) return kInvalidArgument;
        auto it = binary_values.find(id);
        if (it == binary_values.end()) {
            data = nullptr;
            sizeInBytes = 0;
            return kResultFalse;
        }
        data = it->second.data();
        sizeInBytes = static_cast<uint32>(it->second.size());
        return kResultTrue;
    }
};

struct Vst3Message final : IMessage {
    std::atomic<uint32> ref_count{1};
    std::string message_id;
    Vst3AttributeList* attrs = new Vst3AttributeList();

    ~Vst3Message() {
        if (attrs) {
            attrs->release();
            attrs = nullptr;
        }
    }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0 ||
            std::memcmp(_iid, IMessage::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IMessage*>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++ref_count; }
    uint32 PLUGIN_API release() override {
        uint32 n = --ref_count;
        if (n == 0) delete this;
        return n;
    }

    FIDString PLUGIN_API getMessageID() override {
        return message_id.empty() ? nullptr : message_id.c_str();
    }
    void PLUGIN_API setMessageID(FIDString id) override {
        message_id = id ? id : "";
    }
    IAttributeList* PLUGIN_API getAttributes() override {
        attrs->addRef();
        return attrs;
    }
};

// ---------------------------------------------------------------------------
// IComponentHandler + IComponentHandler2 stub.
// Plugins query for IComponentHandler2 (requestOpenEditor, setDirty, etc.)
// from the handler during createView; crashing if they don't check the result.
// ---------------------------------------------------------------------------

// IComponentHandler2 is a sibling of IComponentHandler (both extend FUnknown),
// so we need multiple inheritance and explicit casts in queryInterface.
struct Vst3ComponentHandler : IComponentHandler, IComponentHandler2 {
    // Set by Vst3Instrument after vst3_load_plugin() returns. Signals the operator that
    // plugin state has changed so save_state() can persist it without polling every frame.
    // Only accessed from the UI/main thread (VST3 spec requirement for performEdit).
    bool* state_dirty = nullptr;
    // ADR-0030: a plugin-GUI param edit authors the host-owned base. vst3_cache_params installs
    // this after the param table exists; null before then (and for the probe, which has no base
    // cache). Main/UI thread only, exactly like performEdit. (id, normalized) -> update base cache.
    std::function<void(ParamID, ParamValue)> on_authored_edit;

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IComponentHandler*>(this); return kResultOk;
        }
        if (std::memcmp(_iid, IComponentHandler::iid,  sizeof(TUID)) == 0) {
            *obj = static_cast<IComponentHandler*>(this); return kResultOk;
        }
        if (std::memcmp(_iid, IComponentHandler2::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IComponentHandler2*>(this); return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    // IComponentHandler
    tresult PLUGIN_API beginEdit(ParamID)                         override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v)      override {
        if (state_dirty) *state_dirty = true;
        if (on_authored_edit) on_authored_edit(id, v);   // ADR-0030: GUI edit -> authored base
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID)                           override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32)                    override { return kResultOk; }

    // IComponentHandler2
    tresult PLUGIN_API setDirty(TBool state)                      override {
        if (state && state_dirty) *state_dirty = true;
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString)               override { return kResultOk; }
    tresult PLUGIN_API startGroupEdit()                           override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit()                          override { return kResultOk; }
};

// ---------------------------------------------------------------------------

// IPlugInterfaceSupport: tells plugins which host interfaces we implement.
struct Vst3PlugInterfaceSupport : IPlugInterfaceSupport {
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IPlugInterfaceSupport*>(this); return kResultOk;
        }
        if (std::memcmp(_iid, IPlugInterfaceSupport::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IPlugInterfaceSupport*>(this); return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API isPlugInterfaceSupported(const TUID _iid) override {
        if (std::memcmp(_iid, IComponentHandler::iid,  sizeof(TUID)) == 0) return kResultTrue;
        if (std::memcmp(_iid, IComponentHandler2::iid, sizeof(TUID)) == 0) return kResultTrue;
        if (std::memcmp(_iid, IHostApplication::iid,   sizeof(TUID)) == 0) return kResultTrue;
        return kResultFalse;
    }
};

struct Vst3HostApp : IHostApplication {
    Vst3PlugInterfaceSupport plug_iface_support;

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IHostApplication*>(this); return kResultOk;
        }
        if (std::memcmp(_iid, IHostApplication::iid, sizeof(TUID)) == 0) {
            *obj = static_cast<IHostApplication*>(this); return kResultOk;
        }
        if (std::memcmp(_iid, IPlugInterfaceSupport::iid, sizeof(TUID)) == 0) {
            *obj = &plug_iface_support; return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API getName(String128 name) override {
        const char* src = "Vivid";
        for (int i = 0; src[i]; ++i) name[i] = static_cast<TChar>(src[i]);
        name[5] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (std::memcmp(cid, IMessage::iid, sizeof(TUID)) == 0 &&
            (std::memcmp(_iid, IMessage::iid, sizeof(TUID)) == 0 ||
             std::memcmp(_iid, FUnknown::iid, sizeof(TUID)) == 0)) {
            auto* message = new Vst3Message();
            *obj = static_cast<IMessage*>(message);
            return kResultOk;
        }
        return kNotImplemented;
    }
};

// ---------------------------------------------------------------------------
// Lock-free SPSC queue carrying UI->audio parameter changes (id, normalized).
// ---------------------------------------------------------------------------
struct ParamMsg { uint32_t id; float value; };
struct ParamQueue {
    static constexpr int N = 256;
    ParamMsg buf[N];
    std::atomic<int> head{0}, tail{0};
    bool push(uint32_t id, float v) {           // UI thread
        const int t = tail.load(std::memory_order_relaxed);
        const int nt = (t + 1) % N;
        if (nt == head.load(std::memory_order_acquire)) return false;  // full
        buf[t] = { id, v };
        tail.store(nt, std::memory_order_release);
        return true;
    }
    bool pop(ParamMsg& m) {                      // audio thread
        const int h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;   // empty
        m = buf[h];
        head.store((h + 1) % N, std::memory_order_release);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Vst3Handle — owns one loaded+initialized VST3 plugin instance
// ---------------------------------------------------------------------------

struct Vst3Handle {
#ifdef __APPLE__
    CFBundleRef        bundle                = nullptr;
    using BundleExitFn = bool (*)();
    BundleExitFn       bundle_exit           = nullptr;
    std::string        bundle_path_;         // key into g_vst3_bundle_refs
#else
    void*              library               = nullptr;
#endif
    IPluginFactory*    factory               = nullptr;
    IComponent*        component             = nullptr;
    IAudioProcessor*   processor             = nullptr;
    // ADR-0015 (M3): the plugin has an event OUTPUT bus, i.e. it can GENERATE notes (a chord
    // generator / arpeggiator). Set at load; drives whether the host drains its output events.
    bool               has_note_out          = false;
    Vst3EventList      out_events;           // the host-owned list the plugin writes notes into
    // Plugin identity, captured at load — used to resolve the standard preset
    // directory (<root>/<vendor>/<plugin_name>) and match preset adapters.
    std::string        vendor;               // PFactoryInfo.vendor
    std::string        plugin_name;          // PClassInfo.name of the loaded Audio Module class
    IEditController*   controller            = nullptr;
    bool               controller_is_owned   = false; // created separately; we must terminate+release
    bool               processing            = false;  // setProcessing(true) called
    vivid::audio::PluginFaultState watchdog;           // ADR-0045 Tier 2a: over-budget strikes + faulted latch
    Vst3ComponentHandler component_handler;           // stub for setComponentHandler
    ParamQueue         param_q;                        // UI->audio parameter changes

    // Param info cache for macro mapping
    struct ParamEntry {
        ParamID     id;
        double      default_plain;       // plain value default
        double      min_plain;           // plain value at normalized=0
        double      max_plain;           // plain value at normalized=1
        int32       step_count;          // 0=continuous, >0=discrete steps
        std::string name;                // UTF-8 display name
        std::string units;               // UTF-8 units string (may be empty)
    };
    std::vector<ParamEntry> params;

    // ADR-0030: host-owned AUTHORED base for each param, index-aligned with `params`. `has_base[i]`
    // == 0 means "not authored yet" → the base reader falls back to the plugin's live value; it
    // flips to 1 the moment Vivid authors the param (a knob edit, a project restore, or the plugin's
    // own GUI via performEdit). This is what lets save/undo record what the user authored separately
    // from what the plugin currently hears, and is the P2 prerequisite ADR-0022 named for safe
    // plugin modulation. Normalized 0..1, matching the param setter. Main/UI thread only.
    std::vector<float>   host_base;
    std::vector<uint8_t> has_base;
    // Ph5 P2-05: the authored base keyed by STABLE param id. host_base/has_base are index-aligned and
    // rebuilt on every re-cache (params are compacted + reordered), so this durable map is the source
    // of truth that re-applies authored values by id after a rescan — otherwise they'd be lost.
    std::unordered_map<ParamID, float> authored_by_id;
    // ADR-0034: an audio-thread-readable MIRROR of the authored base (normalized), so the render
    // thread can resolve control-edge modulation against a plugin param's base. Fixed-size, allocated
    // once with the param table (never resized while the audio thread runs — mirrors native `pvals`).
    // Written on the main thread alongside host_base; read on the audio thread via `abase_load`.
    std::unique_ptr<std::atomic<float>[]>   abase;
    std::unique_ptr<std::atomic<uint8_t>[]> ahas;
    // ADR-0034 Phase 3: the frame-bridge's delivered value per param (mirror of native `fovr`), so a
    // param driven by BOTH a bridge mapping and a control edge composes — the bridge value is the
    // effective base modulation swings around. Written by the bridge deliver path, cleared by clear.
    std::unique_ptr<std::atomic<float>[]>   abridge;
    std::unique_ptr<std::atomic<uint8_t>[]> abr_on;
    int abase_n = 0;
    void base_size_to_params() {
        // Ph5 P2-05: re-apply authored values BY ID for the new (possibly reordered/compacted) param
        // table, so a rescan/restartComponent preserves what the user authored instead of resetting it.
        std::vector<ParamID> ids; ids.reserve(params.size());
        for (const auto& p : params) ids.push_back(p.id);
        reapply_authored_base(authored_by_id, ids, host_base, has_base);
        abase_n = static_cast<int>(params.size());
        abase.reset(new std::atomic<float>[params.size()]);
        ahas.reset(new std::atomic<uint8_t>[params.size()]);
        abridge.reset(new std::atomic<float>[params.size()]);
        abr_on.reset(new std::atomic<uint8_t>[params.size()]);
        for (int i = 0; i < abase_n; ++i) { abase[i].store(has_base[i] ? host_base[i] : 0.f, std::memory_order_relaxed);
                                            ahas[i].store(has_base[i], std::memory_order_relaxed);
                                            abridge[i].store(0.f, std::memory_order_relaxed); abr_on[i].store(0u, std::memory_order_relaxed); }
    }
    int  base_index_of(ParamID pid) const {
        for (size_t i = 0; i < params.size(); ++i) if (params[i].id == pid) return static_cast<int>(i);
        return -1;
    }
    void base_author(int i, float norm) {
        if (i < 0 || i >= static_cast<int>(params.size())) return;
        if (host_base.size() != params.size()) base_size_to_params();
        host_base[static_cast<size_t>(i)] = norm; has_base[static_cast<size_t>(i)] = 1u;
        authored_by_id[params[static_cast<size_t>(i)].id] = norm;   // Ph5 P2-05: durable, survives re-cache
        if (i < abase_n) { abase[i].store(norm, std::memory_order_relaxed); ahas[i].store(1u, std::memory_order_release); }
    }
    // A preset/state load replaces the authored patch wholesale: forget every cached base so the
    // reader falls back to the plugin's (newly loaded) values until the user authors again.
    void base_forget_all() {
        has_base.assign(params.size(), 0u);
        authored_by_id.clear();   // Ph5 P2-05: a wholesale preset replace forgets the id-keyed store too
        for (int i = 0; i < abase_n; ++i) ahas[i].store(0u, std::memory_order_relaxed);
    }
    // ADR-0034 Phase 3: bridge delivery sets/clears the effective base modulation resolves against.
    void bridge_set(int i, float norm) { if (i >= 0 && i < abase_n) { abridge[i].store(norm, std::memory_order_relaxed); abr_on[i].store(1u, std::memory_order_release); } }
    void bridge_clear(int i)           { if (i >= 0 && i < abase_n) abr_on[i].store(0u, std::memory_order_relaxed); }
    // Audio thread: EFFECTIVE base (bridge value if delivering, else authored base), or false if neither.
    bool aeff_load(int i, float& out) const {
        if (i < 0 || i >= abase_n) return false;
        if (abr_on[i].load(std::memory_order_acquire)) { out = abridge[i].load(std::memory_order_relaxed); return true; }
        if (ahas[i].load(std::memory_order_acquire))   { out = abase[i].load(std::memory_order_relaxed);   return true; }
        return false;
    }
    // Audio thread: the authored base (normalized) for param `i`, or false if none authored.
    bool abase_load(int i, float& out) const {
        if (i < 0 || i >= abase_n || !ahas[i].load(std::memory_order_acquire)) return false;
        out = abase[i].load(std::memory_order_relaxed);
        return true;
    }

    void destroy() {
        if (component && component->setActive(false) != kResultOk)
            fprintf(stderr, "[Vst3] setActive(false) failed during teardown\n");

        if (controller_is_owned && controller) controller->terminate();
        if (controller)  { controller->release();  controller  = nullptr; }
        if (processor)   { processor->release();   processor   = nullptr; }
        if (component)   { component->terminate(); component->release();  component  = nullptr; }
        if (factory)     { factory->release();     factory     = nullptr; }
#ifdef __APPLE__
        if (!bundle_path_.empty()) {
            auto it = g_vst3_bundle_refs.find(bundle_path_);
            if (it != g_vst3_bundle_refs.end() && --it->second <= 0) {
                g_vst3_bundle_refs.erase(it);
                if (bundle_exit) { bundle_exit(); bundle_exit = nullptr; }
                if (bundle)      { CFBundleUnloadExecutable(bundle); }
            }
        }
        if (bundle) { CFRelease(bundle); bundle = nullptr; }
#else
        if (library)     { dlclose(library);        library     = nullptr; }
#endif
    }

    ~Vst3Handle() { destroy(); }
};

// ---------------------------------------------------------------------------
// UTF-16 → UTF-8 (ASCII fast-path — covers 99% of plugin names and param names)
// ---------------------------------------------------------------------------

inline std::string vst3_tchar_to_utf8(const TChar* src) {
    std::string out;
    for (int i = 0; src[i]; ++i) {
        TChar c = src[i];
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Cache parameter info from IEditController into Vst3Handle::params
// ---------------------------------------------------------------------------

inline void vst3_cache_params(Vst3Handle* h) {
    if (!h->controller) return;
    int32 n = h->controller->getParameterCount();
    h->params.clear();
    h->params.reserve(static_cast<size_t>(n));
    for (int32 i = 0; i < n; ++i) {
        ParameterInfo info{};
        if (h->controller->getParameterInfo(i, info) != kResultOk) continue;
        if (info.flags & ParameterInfo::kIsHidden) continue;

        Vst3Handle::ParamEntry e;
        e.id          = info.id;
        e.min_plain   = h->controller->normalizedParamToPlain(info.id, 0.0);
        e.max_plain   = h->controller->normalizedParamToPlain(info.id, 1.0);
        e.default_plain = h->controller->normalizedParamToPlain(info.id, info.defaultNormalizedValue);
        e.step_count  = info.stepCount;
        e.name        = vst3_tchar_to_utf8(info.title);
        e.units       = vst3_tchar_to_utf8(info.units);
        h->params.push_back(std::move(e));
    }
    // ADR-0030: the base cache is index-aligned with `params`, so (re)size it here and route the
    // plugin's own GUI edits into it. Re-caching params (e.g. after restartComponent) resets the
    // cache to "not authored"; that is correct — the host hasn't authored the new table yet.
    h->base_size_to_params();
    h->component_handler.on_authored_edit = [h](ParamID pid, ParamValue v) {
        h->base_author(h->base_index_of(pid), static_cast<float>(v));
    };
}

// ---------------------------------------------------------------------------
// Build JSON array from cached params (for _vst3_params hidden param)
// ---------------------------------------------------------------------------

inline std::string vst3_params_to_json(const Vst3Handle* h) {
    if (!h || h->params.empty()) return "[]";
    std::string json = "[";
    bool first = true;
    for (const auto& p : h->params) {
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"";
        for (char c : p.name) {
            if (c == '"')       json += "\\\"";
            else if (c == '\\') json += "\\\\";
            else                json += c;
        }
        json += "\",\"id\":";
        json += std::to_string(p.id);
        json += ",\"min\":";
        json += std::to_string(p.min_plain);
        json += ",\"max\":";
        json += std::to_string(p.max_plain);
        json += ",\"default\":";
        json += std::to_string(p.default_plain);
        json += ",\"step_count\":";
        json += std::to_string(p.step_count);
        if (!p.units.empty()) {
            json += ",\"units\":\"";
            for (char c : p.units) {
                if (c == '"')       json += "\\\"";
                else if (c == '\\') json += "\\\\";
                else                json += c;
            }
            json += "\"";
        }
        if (h->controller) {
            double val = h->controller->normalizedParamToPlain(
                p.id, h->controller->getParamNormalized(p.id));
            json += ",\"value\":";
            json += std::to_string(val);
        }
        json += "}";
    }
    json += "]";
    return json;
}

// ---------------------------------------------------------------------------
// State save/load via IComponent::getState / setState
//
// Stored as "z:<base64>" where the base64 payload is zlib-compressed bytes.
// Legacy blobs without the "z:" prefix are raw base64 (backward-compatible).
// ---------------------------------------------------------------------------

inline std::string vst3_compress_b64(const uint8_t* data, size_t len) {
    uLongf bound = compressBound(static_cast<uLong>(len));
    std::vector<uint8_t> buf(bound);
    if (compress2(buf.data(), &bound, data, static_cast<uLong>(len), Z_BEST_COMPRESSION) != Z_OK)
        return {};
    return "z:" + vst3_b64_encode(buf.data(), static_cast<size_t>(bound));
}

inline std::vector<uint8_t> vst3_decompress_b64(const std::string& s) {
    // s starts after the "z:" prefix
    std::vector<uint8_t> compressed = vst3_b64_decode(s);
    if (compressed.empty()) return {};

    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return {};
    zs.next_in  = compressed.data();
    zs.avail_in = static_cast<uInt>(compressed.size());

    std::vector<uint8_t> out;
    out.reserve(compressed.size() * 8);
    uint8_t chunk[65536];
    int r;
    do {
        zs.next_out  = chunk;
        zs.avail_out = sizeof(chunk);
        r = inflate(&zs, Z_NO_FLUSH);
        if (r == Z_STREAM_ERROR || r == Z_DATA_ERROR || r == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return {};
        }
        out.insert(out.end(), chunk, chunk + (sizeof(chunk) - zs.avail_out));
    } while (r != Z_STREAM_END);
    inflateEnd(&zs);
    return out;
}

inline std::string vst3_save_state(Vst3Handle* h) {
    if (!h || !h->component) return {};
    MemIBStream stream;
    if (h->component->getState(&stream) != kResultOk) return {};
    if (stream.buf.empty()) return {};
    std::string compressed = vst3_compress_b64(stream.buf.data(), stream.buf.size());
    if (!compressed.empty()) return compressed;
    // Fall back to uncompressed if zlib fails (shouldn't happen, but be safe)
    return vst3_b64_encode(stream.buf.data(), stream.buf.size());
}

inline void vst3_load_state(Vst3Handle* h, const std::string& b64) {
    if (b64.empty() || !h || !h->component) return;
    std::vector<uint8_t> raw;
    if (b64.size() >= 2 && b64[0] == 'z' && b64[1] == ':')
        raw = vst3_decompress_b64(b64.substr(2));
    else
        raw = vst3_b64_decode(b64);
    if (raw.empty()) return;
    MemIBStream stream;
    stream.buf = std::move(raw);
    if (h->component->setState(&stream) != kResultOk)
        fprintf(stderr, "[Vst3] setState failed (state may not have been applied)\n");
    // Sync controller state from component if they're separate objects
    if (h->controller_is_owned && h->controller) {
        stream.pos = 0;
        h->controller->setComponentState(&stream);
    }
}

// VST3 preset discovery + loading (`.vstpreset` files + native-format adapters for
// Serum/Pigments) lives in vst3_presets.h, included by vst3_host.cpp after this header.

// ---------------------------------------------------------------------------
// Build ProcessContext from VividAudioContext
// ---------------------------------------------------------------------------

inline ProcessContext vst3_build_process_context(const VividAudioContext* ctx,
                                                   uint64_t steady_sample) {
    ProcessContext pc{};
    pc.sampleRate            = static_cast<SampleRate>(ctx->sample_rate);
    pc.projectTimeSamples    = static_cast<int64>(steady_sample);
    pc.continousTimeSamples  = static_cast<int64>(steady_sample);
    pc.systemTime            = 0;

    const double bpm  = ctx->metronome_bpm > 0.f ? static_cast<double>(ctx->metronome_bpm) : 120.0;
    const double beat = ctx->metronome_beats_elapsed;
    const uint32_t bpb = ctx->metronome_beats_per_bar > 0
                        ? static_cast<uint32_t>(ctx->metronome_beats_per_bar) : 4u;

    pc.tempo             = bpm;
    pc.projectTimeMusic  = beat;
    pc.barPositionMusic  = beat - static_cast<double>(static_cast<int64>(beat / bpb)) * bpb;
    pc.timeSigNumerator  = static_cast<int32>(bpb);
    pc.timeSigDenominator = 4;

    pc.state = ProcessContext::kPlaying
             | ProcessContext::kTempoValid
             | ProcessContext::kTimeSigValid
             | ProcessContext::kProjectTimeMusicValid
             | ProcessContext::kBarPositionValid
             | ProcessContext::kContTimeValid;

    return pc;
}

// ---------------------------------------------------------------------------
// Load a VST3 plugin from a bundle path + FUID hex string.
// Returns null on failure. Caller takes ownership.
// ---------------------------------------------------------------------------

typedef IPluginFactory* (*GetPluginFactoryFunc)();

inline bool vst3_has_subcategory(const char* subcategories, const char* wanted) {
    if (!subcategories || !wanted || !wanted[0]) return false;
    std::string_view cats{subcategories};
    std::string_view needle{wanted};
    size_t start = 0;
    while (start <= cats.size()) {
        size_t end = cats.find('|', start);
        if (end == std::string_view::npos) end = cats.size();
        if (cats.substr(start, end - start) == needle) return true;
        if (end == cats.size()) break;
        start = end + 1;
    }
    return false;
}

inline Vst3Handle* vst3_load_plugin(const char* bundle_path,
                                     const char* uid_hex,
                                     uint32_t sample_rate,
                                     const std::string& saved_state,
                                     Vst3HostApp* host_app,
                                     bool as_effect = false) {
    std::string b(bundle_path);
    while (!b.empty() && b.back() == '/') b.pop_back();

#ifdef __APPLE__
    // macOS: load via CFBundle so bundleEntry() is called and the plugin bundle
    // is registered in CoreFoundation's registry. VSTGUI-based plugins (including
    // Serum2) require this — they call CFBundleGetBundleWithIdentifier() during
    // font initialization, which only succeeds if the bundle was created via CFBundle.
    using BundleEntryFn = bool (*)(CFBundleRef);
    using BundleExitFn  = bool (*)();

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(b.c_str()),
        static_cast<CFIndex>(b.size()),
        true /* isDirectory — pass the .vst3 bundle dir */);
    if (!url) {
        fprintf(stderr, "[Vst3] CFURLCreate failed for: %s\n", b.c_str());
        return nullptr;
    }

    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!bundle) {
        fprintf(stderr, "[Vst3] CFBundleCreate failed for: %s\n", b.c_str());
        return nullptr;
    }

    CFErrorRef load_err = nullptr;
    if (!CFBundleLoadExecutableAndReturnError(bundle, &load_err)) {
        if (load_err) {
            CFStringRef desc = CFErrorCopyDescription(load_err);
            char buf[512]{};
            CFStringGetCString(desc, buf, sizeof(buf), kCFStringEncodingUTF8);
            fprintf(stderr, "[Vst3] CFBundleLoad failed: %s\n", buf);
            CFRelease(desc); CFRelease(load_err);
        } else {
            fprintf(stderr, "[Vst3] CFBundleLoad failed (no error info)\n");
        }
        CFRelease(bundle); return nullptr;
    }

    auto bundle_entry = reinterpret_cast<BundleEntryFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
    auto bundle_exit = reinterpret_cast<BundleExitFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
    auto* fn = reinterpret_cast<GetPluginFactoryFunc>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));

    if (!fn) {
        fprintf(stderr, "[Vst3] no GetPluginFactory symbol\n");
        CFBundleUnloadExecutable(bundle); CFRelease(bundle); return nullptr;
    }

    // bundleEntry MUST be called before GetPluginFactory — it stores the CFBundleRef
    // inside the plugin (ghInst), which VSTGUI uses to find font/resource files.
    if (bundle_entry && !bundle_entry(bundle)) {
        fprintf(stderr, "[Vst3] bundleEntry() failed\n");
        CFBundleUnloadExecutable(bundle); CFRelease(bundle); return nullptr;
    }

#define VST3_RELEASE_BUNDLE do { \
        if (bundle_exit) bundle_exit(); \
        CFBundleUnloadExecutable(bundle); CFRelease(bundle); } while(0)

#else
    // Non-macOS: fall back to dlopen
    size_t slash = b.rfind('/');
    std::string bname = (slash == std::string::npos) ? b : b.substr(slash + 1);
    std::string stem = bname;
    const std::string vext = ".vst3";
    if (stem.size() > vext.size() &&
        stem.compare(stem.size() - vext.size(), vext.size(), vext) == 0)
        stem.resize(stem.size() - vext.size());
    std::string binary = b + "/Contents/MacOS/" + stem;

    void* lib = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "[Vst3] dlopen failed: %s\n", dlerror());
        return nullptr;
    }

    auto* fn = reinterpret_cast<GetPluginFactoryFunc>(dlsym(lib, "GetPluginFactory"));
    if (!fn) {
        fprintf(stderr, "[Vst3] no GetPluginFactory symbol\n");
        dlclose(lib); return nullptr;
    }

#define VST3_RELEASE_BUNDLE do { dlclose(lib); } while(0)
#endif

    IPluginFactory* factory = fn();
    if (!factory) {
        fprintf(stderr, "[Vst3] GetPluginFactory returned null\n");
        VST3_RELEASE_BUNDLE; return nullptr;
    }

    IPluginFactory3* factory3 = nullptr;
    if (factory->queryInterface(IPluginFactory3::iid, (void**)&factory3) == kResultOk && factory3) {
        factory3->setHostContext(host_app);
        factory3->release();
    }

    // Find the matching class by FUID hex
    IPluginFactory2* factory2 = nullptr;
    if (factory->queryInterface(IPluginFactory2::iid, (void**)&factory2) != kResultOk)
        factory2 = nullptr;

    int32 count = factory->countClasses();
    int   target_idx = -1;
    int   fallback_idx = -1;
    for (int32 i = 0; i < count; ++i) {
        PClassInfo info{};
        if (factory->getClassInfo(i, &info) != kResultOk) continue;
        if (std::strcmp(info.category, "Audio Module Class") != 0) continue;

        // Convert cid to hex for comparison
        char hex[33] = {};
        static const char H[] = "0123456789ABCDEF";
        for (int j = 0; j < 16; ++j) {
            hex[j*2]   = H[(static_cast<uint8_t>(info.cid[j]) >> 4) & 0xF];
            hex[j*2+1] = H[ static_cast<uint8_t>(info.cid[j])       & 0xF];
        }
        if (std::strcmp(hex, uid_hex) == 0) { target_idx = i; break; }

        if (!uid_hex || uid_hex[0] == '\0') {
            if (fallback_idx < 0) fallback_idx = i;
            if (factory2) {
                PClassInfo2 info2{};
                if (factory2->getClassInfo2(i, &info2) == kResultOk &&
                    vst3_has_subcategory(info2.subCategories, PlugType::kInstrument)) {
                    target_idx = i;
                    break;
                }
            }
        }
    }
    if (factory2) factory2->release();
    if (target_idx < 0) target_idx = fallback_idx;

    if (target_idx < 0) {
        fprintf(stderr, "[Vst3] no matching class for uid '%s'\n", uid_hex ? uid_hex : "");
        factory->release(); VST3_RELEASE_BUNDLE; return nullptr;
    }

    PClassInfo target_info{};
    if (factory->getClassInfo(target_idx, &target_info) != kResultOk) {
        fprintf(stderr, "[Vst3] getClassInfo(%d) failed\n", target_idx);
        factory->release(); VST3_RELEASE_BUNDLE; return nullptr;
    }

    IComponent* component = nullptr;
    if (factory->createInstance(target_info.cid, IComponent::iid,
                                (void**)&component) != kResultOk || !component) {
        fprintf(stderr, "[Vst3] createInstance failed\n");
        factory->release(); VST3_RELEASE_BUNDLE; return nullptr;
    }

    if (component->initialize(host_app) != kResultOk) {
        fprintf(stderr, "[Vst3] component->initialize failed\n");
        component->release(); factory->release(); VST3_RELEASE_BUNDLE; return nullptr;
    }

    // Activate the audio output bus
    if (component->activateBus(kAudio, kOutput, 0, true) != kResultOk)
        fprintf(stderr, "[Vst3] activateBus(kAudio, kOutput, 0) failed\n");

    // Activate the event (MIDI/note) input bus so instruments actually receive
    // note events. Without this, hosted synths produce silence. Only instruments
    // expose an event input bus; skip silently for effects that have none.
    if (component->getBusCount(kEvent, kInput) > 0 &&
        component->activateBus(kEvent, kInput, 0, true) != kResultOk)
        fprintf(stderr, "[Vst3] activateBus(kEvent, kInput, 0) failed\n");

    // ADR-0015 (M3): activate the event OUTPUT bus. A plugin that GENERATES notes (a chord
    // generator, an arpeggiator, the Captain suite) writes them to this bus — and the host never
    // even asked for it before, so every note such a plugin produced was thrown away.
    // Only plugins that have one expose it; skip silently otherwise.
    bool has_note_out_bus = false;
    if (component->getBusCount(kEvent, kOutput) > 0) {
        if (component->activateBus(kEvent, kOutput, 0, true) != kResultOk)
            fprintf(stderr, "[Vst3] activateBus(kEvent, kOutput, 0) failed\n");
        else
            has_note_out_bus = true;
    }

    // Effects process audio in -> out, so activate their audio input bus too.
    if (as_effect && component->getBusCount(kAudio, kInput) > 0 &&
        component->activateBus(kAudio, kInput, 0, true) != kResultOk)
        fprintf(stderr, "[Vst3] activateBus(kAudio, kInput, 0) failed\n");

    IAudioProcessor* processor = nullptr;
    if (component->queryInterface(IAudioProcessor::iid, (void**)&processor) != kResultOk
        || !processor) {
        fprintf(stderr, "[Vst3] no IAudioProcessor interface\n");
        component->terminate(); component->release();
        factory->release(); VST3_RELEASE_BUNDLE; return nullptr;
    }

    // Negotiate the output bus arrangement (stereo, no audio inputs). Many
    // instruments output silence until setBusArrangements tells them their
    // output speaker layout — this must happen while the component is inactive.
    {
        SpeakerArrangement in_arr = SpeakerArr::kStereo, out_arr = SpeakerArr::kStereo;
        const bool ok = as_effect
            ? (processor->setBusArrangements(&in_arr, 1, &out_arr, 1) == kResultOk)
            : (processor->setBusArrangements(nullptr, 0, &out_arr, 1) == kResultOk);
        if (!ok)
            fprintf(stderr, "[Vst3] setBusArrangements(%s) not accepted; "
                            "plugin may use its default layout\n", as_effect ? "stereo in/out" : "stereo out");
    }

    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 4096;
    setup.sampleRate         = static_cast<SampleRate>(sample_rate > 0 ? sample_rate : 48000);

    if (processor->setupProcessing(setup) != kResultOk) {
        fprintf(stderr, "[Vst3] setupProcessing failed\n");
        processor->release(); component->terminate(); component->release();
        factory->release(); VST3_RELEASE_BUNDLE; return nullptr;
    }

    if (component->setActive(true) != kResultOk) {
        fprintf(stderr, "[Vst3] setActive failed\n");
        processor->release(); component->terminate(); component->release();
        factory->release(); VST3_RELEASE_BUNDLE; return nullptr;
    }

    // Get IEditController.
    // Path A: component also implements IEditController (single-object, most common).
    // Path B: separate controller object — call getControllerClassId + factory createInstance.
    IEditController* controller = nullptr;
    bool controller_is_owned = false;

    if (component->queryInterface(IEditController::iid, (void**)&controller) == kResultOk
        && controller) {
        // Check if it's actually a distinct object needing separate init/terminate.
        void* comp_raw = nullptr;
        void* ctrl_raw = nullptr;
        component->queryInterface(FUnknown::iid, &comp_raw);
        controller->queryInterface(FUnknown::iid, &ctrl_raw);
        if (comp_raw) reinterpret_cast<FUnknown*>(comp_raw)->release();
        if (ctrl_raw) reinterpret_cast<FUnknown*>(ctrl_raw)->release();

        if (comp_raw != ctrl_raw) {
            controller_is_owned = true;
            if (controller->initialize(host_app) != kResultOk) {
                fprintf(stderr, "[Vst3] distinct controller initialize failed\n");
                controller->release(); controller = nullptr; controller_is_owned = false;
            }
        }
    } else {
        // Path B: separate controller class registered in the factory.
        controller = nullptr;
        TUID ctrl_cid = {};
        if (component->getControllerClassId(ctrl_cid) == kResultOk) {
            IEditController* sep = nullptr;
            if (factory->createInstance(ctrl_cid, IEditController::iid,
                                        (void**)&sep) == kResultOk && sep) {
                if (sep->initialize(host_app) == kResultOk) {
                    controller = sep;
                    controller_is_owned = true;
                } else {
                    fprintf(stderr, "[Vst3] separate controller initialize failed\n");
                    sep->release();
                }
            } else {
                fprintf(stderr, "[Vst3] separate controller createInstance failed\n");
            }
        } else {
            fprintf(stderr, "[Vst3] no IEditController available — params will be empty\n");
        }
    }

    auto* h = new Vst3Handle();
    h->has_note_out = has_note_out_bus;   // ADR-0015 (M3): it can generate notes
#ifdef __APPLE__
    h->bundle               = bundle;
    h->bundle_exit          = bundle_exit;
#else
    h->library              = lib;
#endif
    h->factory              = factory;
    h->component            = component;
    h->processor            = processor;
    h->controller           = controller;
    h->controller_is_owned  = controller_is_owned;

    // Capture identity for preset-directory resolution and adapter matching.
    h->plugin_name = target_info.name;
    {
        PFactoryInfo fi{};
        if (factory->getFactoryInfo(&fi) == kResultOk)
            h->vendor = fi.vendor;
    }

    if (controller) {
        // setComponentHandler: required before any UI operation (Serum2 crashes without it).
        if (controller->setComponentHandler(&h->component_handler) != kResultOk)
            fprintf(stderr, "[Vst3] setComponentHandler failed\n");

        // Connect component ↔ controller via IConnectionPoint (bidirectional).
        // Only for separate-object plugins: single-object plugins (comp == ctrl) must
        // NOT be connected to themselves — JUCE crashes on self-connect.
        if (h->controller_is_owned) {
            IConnectionPoint* comp_cp = nullptr;
            IConnectionPoint* ctrl_cp = nullptr;
            component->queryInterface(IConnectionPoint::iid, (void**)&comp_cp);
            controller->queryInterface(IConnectionPoint::iid, (void**)&ctrl_cp);
            if (comp_cp && ctrl_cp) {
                comp_cp->connect(ctrl_cp);
                ctrl_cp->connect(comp_cp);
            }
            if (comp_cp) comp_cp->release();
            if (ctrl_cp) ctrl_cp->release();
        }

        // Sync controller state from current component state.
        {
            MemIBStream state_stream;
            if (component->getState(&state_stream) == kResultOk && !state_stream.buf.empty()) {
                state_stream.pos = 0;
                controller->setComponentState(&state_stream);
            }
        }
    }

    vst3_cache_params(h);
    vst3_load_state(h, saved_state);

#ifdef __APPLE__
    h->bundle_path_ = b;
    g_vst3_bundle_refs[b]++;
#endif

#undef VST3_RELEASE_BUNDLE
    return h;
}

} // namespace vivid::session
