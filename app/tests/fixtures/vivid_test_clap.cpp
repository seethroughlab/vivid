// A minimal, self-contained CLAP plugin built from source as a CI fixture (ADR-0030 Phase 3
// follow-up). It exists ONLY so the test suite can exercise the REAL plugin path end-to-end —
// load -> author a param -> non-destructive bridge deliver -> save/load -> reconcile — without
// depending on any third-party plugin being installed (CI has none; Surge stalls). CLAP is chosen
// because its ABI is MIT-licensed and header-only (already vendored), so a whole plugin is one file.
//
// The plugin is a stereo GAIN effect with a single continuous param "gain" (0..1). It implements the
// three extensions Vivid's loader queries and the test needs: params (get/set via events + value),
// audio-ports (so it binds as an effect), and state (save/load of the gain). Everything is
// single-threaded and allocation-free in process(); it is a fixture, not a shipping op.
#include <clap/clap.h>

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

namespace {

constexpr clap_id kGainId = 0;

// ADR-0032 E1.3: an optional reported+real processing latency, gated on VIVID_TEST_CLAP_LATENCY (samples,
// read once; unset/0 => the plugin advertises NO latency extension exactly as before, so the Phase B
// "unknown" path and every other consumer are unchanged). When >0 the plugin BOTH advertises
// CLAP_EXT_LATENCY returning N AND actually delays its audio by N samples — a faithful latency plugin, so
// the PDC alignment test can prove reported latency == real delay and that PDC realigns it.
int test_clap_latency() {
    static const int n = [] {
        const char* e = std::getenv("VIVID_TEST_CLAP_LATENCY");
        const int v = e ? std::atoi(e) : 0;
        return v > 0 ? v : 0;
    }();
    return n;
}

struct TestClap {
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    double gain = 0.5;   // authoritative param value (plain == normalized here, 0..1)
    // E1.3 delay line (only used when test_clap_latency() > 0): per-channel ring of N samples + a shared
    // write cursor, giving an exact N-sample delay so the reported latency matches the real one.
    float    dl[2][1 << 16] = {};   // >= max tested latency (kPdcMaxComp = 61440 < 65536)
    uint32_t dpos = 0;
};

// --- params extension ---------------------------------------------------------------------------
uint32_t params_count(const clap_plugin_t*) { return 1; }

bool params_get_info(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kGainId;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0; info->max_value = 1.0; info->default_value = 0.5;
    std::snprintf(info->name, sizeof(info->name), "%s", "gain");
    info->module[0] = '\0';
    return true;
}
bool params_get_value(const clap_plugin_t* p, clap_id id, double* out) {
    if (id != kGainId) return false;
    *out = static_cast<TestClap*>(p->plugin_data)->gain;
    return true;
}
bool params_value_to_text(const clap_plugin_t*, clap_id id, double v, char* buf, uint32_t cap) {
    if (id != kGainId) return false;
    std::snprintf(buf, cap, "%.3f", v);
    return true;
}
bool params_text_to_value(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    if (id != kGainId) return false;
    *out = std::atof(text);
    return true;
}
// Apply any queued param-value events (the host delivers knob edits / bridge deliveries here).
void params_flush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
    auto* self = static_cast<TestClap*>(p->plugin_data);
    const uint32_t n = in ? in->size(in) : 0;
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (h->type == CLAP_EVENT_PARAM_VALUE && h->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
            if (e->param_id == kGainId) self->gain = e->value;
        }
    }
}
const clap_plugin_params_t s_params = {
    params_count, params_get_info, params_get_value, params_value_to_text, params_text_to_value, params_flush
};

// --- audio-ports extension (stereo in + stereo out => an effect) ---------------------------------
uint32_t aports_count(const clap_plugin_t*, bool) { return 1; }
bool aports_get(const clap_plugin_t*, uint32_t index, bool, clap_audio_port_info_t* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", "main");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t s_aports = { aports_count, aports_get };

// --- state extension (save/load the gain) --------------------------------------------------------
bool state_save(const clap_plugin_t* p, const clap_ostream_t* os) {
    const double g = static_cast<TestClap*>(p->plugin_data)->gain;
    return os->write(os, &g, sizeof(g)) == static_cast<int64_t>(sizeof(g));
}
bool state_load(const clap_plugin_t* p, const clap_istream_t* is) {
    double g = 0.5;
    if (is->read(is, &g, sizeof(g)) != static_cast<int64_t>(sizeof(g))) return false;
    static_cast<TestClap*>(p->plugin_data)->gain = g;
    return true;
}
const clap_plugin_state_t s_state = { state_save, state_load };

// --- plugin vtable -------------------------------------------------------------------------------
bool plug_init(const clap_plugin_t*) { return true; }
void plug_destroy(const clap_plugin_t* p) { delete static_cast<TestClap*>(p->plugin_data); }
bool plug_activate(const clap_plugin_t*, double, uint32_t, uint32_t) { return true; }
void plug_deactivate(const clap_plugin_t*) {}
bool plug_start(const clap_plugin_t*) { return true; }
void plug_stop(const clap_plugin_t*) {}
void plug_reset(const clap_plugin_t*) {}

clap_process_status plug_process(const clap_plugin_t* p, const clap_process_t* proc) {
    auto* self = static_cast<TestClap*>(p->plugin_data);
    // ADR-0045 Tier 2a test hook: simulate an over-budget / hung plugin by busy-waiting VIVID_TEST_CLAP_SLOW_MS
    // (read once) inside process(). Off by default, so every other consumer of this fixture is unaffected.
    static const int slow_ms = [] {
        const char* e = std::getenv("VIVID_TEST_CLAP_SLOW_MS");
        return e ? std::atoi(e) : 0;
    }();
    if (slow_ms > 0) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(slow_ms);
        while (std::chrono::steady_clock::now() < deadline) { /* burn CPU like a real over-budget plugin */ }
    }
    // Apply param events (sample-accurate enough for a fixture: honor them at block start).
    if (proc->in_events) params_flush(p, proc->in_events, proc->out_events);
    const float g = static_cast<float>(self->gain);
    const int lat = test_clap_latency();
    if (proc->audio_inputs_count && proc->audio_outputs_count &&
        proc->audio_inputs[0].data32 && proc->audio_outputs[0].data32) {
        const uint32_t ch = proc->audio_outputs[0].channel_count;
        if (lat <= 0) {
            for (uint32_t c = 0; c < ch; ++c) {
                const float* in = proc->audio_inputs[0].data32[c];
                float* out = proc->audio_outputs[0].data32[c];
                for (uint32_t i = 0; i < proc->frames_count; ++i) out[i] = in[i] * g;
            }
        } else {
            // E1.3: exact N-sample delay per channel (out[i] = the sample written N frames ago), so the
            // reported latency (N) is the real latency. Shared write cursor across channels.
            const uint32_t N = static_cast<uint32_t>(lat);
            for (uint32_t c = 0; c < ch && c < 2; ++c) {
                const float* in = proc->audio_inputs[0].data32[c];
                float* out = proc->audio_outputs[0].data32[c];
                uint32_t p = self->dpos;
                for (uint32_t i = 0; i < proc->frames_count; ++i) {
                    const float x = in[i] * g;
                    out[i] = self->dl[c][p];
                    self->dl[c][p] = x;
                    p = (p + 1) % N;
                }
                if (c + 1 == ch || c == 1) self->dpos = p;   // advance the shared cursor once (last channel)
            }
        }
    }
    return CLAP_PROCESS_CONTINUE;
}
// ADR-0032 E1.3: the latency extension (only advertised when VIVID_TEST_CLAP_LATENCY > 0).
uint32_t latency_get(const clap_plugin_t*) { return static_cast<uint32_t>(test_clap_latency()); }
const clap_plugin_latency_t s_latency = { latency_get };

const void* plug_get_extension(const clap_plugin_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_PARAMS))      return &s_params;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &s_aports;
    if (!std::strcmp(id, CLAP_EXT_STATE))       return &s_state;
    // Advertise latency ONLY when configured — unset => no ext => "unknown" latency, unchanged.
    if (test_clap_latency() > 0 && !std::strcmp(id, CLAP_EXT_LATENCY)) return &s_latency;
    return nullptr;
}
void plug_on_main_thread(const clap_plugin_t*) {}

const char* const s_features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr };
const clap_plugin_descriptor_t s_desc = {
    CLAP_VERSION_INIT,
    "app.vivid.test.clapgain",
    "Vivid Test Gain",
    "Vivid",
    "", "", "", "1.0",
    "A minimal gain effect used only by Vivid's CI tests.",
    s_features
};

// --- factory -------------------------------------------------------------------------------------
uint32_t factory_count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factory_get_desc(const clap_plugin_factory_t*, uint32_t i) {
    return i == 0 ? &s_desc : nullptr;
}
const clap_plugin_t* factory_create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!id || std::strcmp(id, s_desc.id) != 0) return nullptr;
    auto* self = new TestClap();
    self->host = host;
    self->plugin.desc = &s_desc;
    self->plugin.plugin_data = self;
    self->plugin.init = plug_init;
    self->plugin.destroy = plug_destroy;
    self->plugin.activate = plug_activate;
    self->plugin.deactivate = plug_deactivate;
    self->plugin.start_processing = plug_start;
    self->plugin.stop_processing = plug_stop;
    self->plugin.reset = plug_reset;
    self->plugin.process = plug_process;
    self->plugin.get_extension = plug_get_extension;
    self->plugin.on_main_thread = plug_on_main_thread;
    return &self->plugin;
}
const clap_plugin_factory_t s_factory = { factory_count, factory_get_desc, factory_create };

// --- entry ---------------------------------------------------------------------------------------
bool entry_init(const char*) { return true; }
void entry_deinit() {}
const void* entry_get_factory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &s_factory : nullptr;
}

}  // namespace

// The symbol CFBundleGetDataPointerForName("clap_entry") resolves (see clap_host.h).
extern "C" const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, entry_init, entry_deinit, entry_get_factory
};
