// Unit tests for subgraph module parsing and graph flattening.
// These test the JSON parsing of .vivid-module.json definitions and
// the compile-time flattening transform that expands module nodes
// into their internal graphs.

#include "runtime/graph/subgraph_module.h"
#include "runtime/graph/graph.h"
#include "ui/graph/graph_snapshot.h"
#include <cstdio>
#include <cstring>
#include "test_helpers.h"

static ScopedTempDir* g_tmp = nullptr;

// ---------------------------------------------------------------------------
// Test JSON strings
// ---------------------------------------------------------------------------

static const char* kSimpleModule = R"({
    "schema_version": 2,
    "module": {
        "name": "TestSynth",
        "description": "A test synthesizer module",
        "category": "Synthesizer",
        "ports": [
            { "name": "freq_in",  "type": "signal", "direction": "input",  "bind": "osc/frequency" },
            { "name": "gate_in",  "type": "signal", "direction": "input",  "bind": "osc/gate" },
            { "name": "output",   "type": "audio",  "direction": "output", "bind": "mixer/output" }
        ],
        "params": [
            { "name": "volume",    "bind": "mixer/gain" },
            { "name": "waveform",  "bind": "osc/waveform" }
        ],
        "presets": {
            "Bright": { "osc/waveform": 1, "mixer/gain": 0.8 },
            "Dark":   { "osc/waveform": 3, "mixer/gain": 0.5 }
        }
    },
    "nodes": {
        "osc":   { "type": "Oscillator", "params": { "frequency": 440, "waveform": 0 } },
        "mixer": { "type": "VoiceMixer", "params": { "gain": 0.7 } }
    },
    "connections": [
        { "from": "osc/output", "to": "mixer/input" }
    ]
})";

static const char* kEffectsModule = R"({
    "schema_version": 2,
    "module": {
        "name": "FXChain",
        "description": "Audio effects chain",
        "category": "Effects",
        "ports": [
            { "name": "input",   "type": "audio",  "direction": "input",  "bind": "filter/input" },
            { "name": "output",  "type": "audio",  "direction": "output", "bind": "reverb/output" }
        ],
        "params": [
            { "name": "cutoff", "bind": "filter/cutoff" }
        ]
    },
    "nodes": {
        "filter": { "type": "Filter", "params": { "cutoff": 1000, "resonance": 0.5 } },
        "reverb": { "type": "Reverb",  "params": { "mix": 0.3 } }
    },
    "connections": [
        { "from": "filter/output", "to": "reverb/input" }
    ]
})";

// ---------------------------------------------------------------------------
// Parse tests
// ---------------------------------------------------------------------------

static void test_parse_simple_module() {
    std::fprintf(stderr, "\n--- parse: simple synth module ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_synth.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }

    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("TestSynth");
    check(mod != nullptr, "module found by name");
    if (!mod) return;

    check(mod->name == "TestSynth", "name matches");
    check(mod->description == "A test synthesizer module", "description matches");
    check(mod->category == "Synthesizer", "category matches");

    // Ports
    check(mod->ports.size() == 3, "3 ports defined");
    check(mod->ports[0].name == "freq_in", "port 0 name");
    check(mod->ports[0].type == VIVID_PORT_SCALAR, "port 0 type = signal");
    check(mod->ports[0].direction == VIVID_PORT_INPUT, "port 0 direction = input");
    check(mod->ports[0].internal_node == "osc", "port 0 internal node");
    check(mod->ports[0].internal_port == "frequency", "port 0 internal port");
    check(mod->ports[2].name == "output", "port 2 name");
    check(mod->ports[2].type == VIVID_PORT_AUDIO_BUFFER, "port 2 type = audio");
    check(mod->ports[2].direction == VIVID_PORT_OUTPUT, "port 2 direction = output");

    // Params
    check(mod->params.size() == 2, "2 params defined");
    check(mod->params[0].name == "volume", "param 0 name");
    check(mod->params[0].internal_node == "mixer", "param 0 internal node");
    check(mod->params[0].internal_param == "gain", "param 0 internal param");

    // Presets
    check(mod->presets.size() == 2, "2 presets defined");

    // Internal graph
    check(mod->internal_graph.nodes().size() == 2, "2 internal nodes");
    check(mod->internal_graph.connections().size() == 1, "1 internal connection");

    // Find helpers
    check(mod->find_port("freq_in") != nullptr, "find_port works");
   check(mod->find_port("nonexistent") == nullptr, "find_port returns null for missing");
    check(mod->find_param("volume") != nullptr, "find_param works");
    check(mod->find_preset("Bright") != nullptr, "find_preset works");

}

static void test_registry_scan_directory() {
    std::fprintf(stderr, "\n--- registry: scan directory ---\n");

    // Use a fresh subdirectory so files from earlier tests don't interfere.
    auto scan_dir = g_tmp->path / "scan_only";
    std::filesystem::create_directories(scan_dir);

    vivid::SubgraphModuleRegistry registry;
    std::string module_path = (scan_dir / "scan_test.vivid-module.json").string();
    {
        FILE* f = std::fopen(module_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    {
        FILE* f = std::fopen((scan_dir / "ignore.json").string().c_str(), "w");
        std::fputs("{}", f);
        std::fclose(f);
    }

    int count = registry.scan(scan_dir.string());
    check(count == 1, "scan loads one module file");
    check(registry.find("TestSynth") != nullptr, "scanned module registered");
}

static void test_parse_effects_module() {
    std::fprintf(stderr, "\n--- parse: effects chain module ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_fx.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kEffectsModule, f);
        std::fclose(f);
    }

    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("FXChain");
    check(mod != nullptr, "module found");
    if (!mod) return;

    check(mod->ports.size() == 2, "2 ports (audio in + audio out)");
    check(mod->ports[0].type == VIVID_PORT_AUDIO_BUFFER, "input port is audio");
    check(mod->ports[0].direction == VIVID_PORT_INPUT, "input port direction");
    check(mod->ports[1].type == VIVID_PORT_AUDIO_BUFFER, "output port is audio");
    check(mod->ports[1].direction == VIVID_PORT_OUTPUT, "output port direction");

}

// ---------------------------------------------------------------------------
// Flatten tests
// ---------------------------------------------------------------------------

static void test_flatten_single_instance() {
    std::fprintf(stderr, "\n--- flatten: single module instance ---\n");

    // Register the module
    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_flatten.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    // Build a parent graph with one module instance + external nodes
    vivid::Graph parent;
    parent.add_node("midi", "MidiInput", {});
    parent.add_node("synth1", "TestSynth", {{"volume", 0.9f}, {"waveform", 2.0f}});
    parent.add_node("out", "audio_out", {});
    parent.add_connection("midi", "note", "synth1", "freq_in");
    parent.add_connection("midi", "gate", "synth1", "gate_in");
    parent.add_connection("synth1", "output", "out", "input");

    // Flatten
    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry).graph;

    // Verify: module node "synth1" should be gone, replaced by prefixed internal nodes
    check(flat.find_node("synth1") == nullptr, "module placeholder removed");
    check(flat.find_node("midi") != nullptr, "external node 'midi' preserved");
    check(flat.find_node("out") != nullptr, "external node 'out' preserved");

    // Internal nodes should exist with prefixed IDs
    auto* osc_node = flat.find_node("synth1.__osc");
    auto* mixer_node = flat.find_node("synth1.__mixer");
    check(osc_node != nullptr, "internal node 'synth1.__osc' exists");
    check(mixer_node != nullptr, "internal node 'synth1.__mixer' exists");

    if (osc_node) {
        check(osc_node->type == "Oscillator", "osc type preserved");
        check(osc_node->subgraph_owner == "synth1", "osc subgraph_owner set");
        check(osc_node->subgraph_type == "TestSynth", "osc subgraph_type set");

        // Param override: waveform should be 2.0 (from module node params) instead of 0
        auto wf_it = osc_node->params.find("waveform");
        check(wf_it != osc_node->params.end() && wf_it->second == 2.0f,
              "waveform param overridden to 2.0");
    }

    if (mixer_node) {
        // Param override: gain should be 0.9 (from module node "volume" param -> mixer/gain)
        auto gain_it = mixer_node->params.find("gain");
        check(gain_it != mixer_node->params.end() && gain_it->second == 0.9f,
              "gain param overridden to 0.9 (via volume binding)");
    }

    // Verify connections:
    // 1. Internal: synth1.__osc/output -> synth1.__mixer/input
    // 2. Boundary input: midi/note -> synth1.__osc/frequency
    // 3. Boundary input: midi/gate -> synth1.__osc/gate
    // 4. Boundary output: synth1.__mixer/output -> out/input
    const auto& conns = flat.connections();
    check(conns.size() == 4, "4 connections in flattened graph");

    auto has_conn = [&](const std::string& fn, const std::string& fp,
                        const std::string& tn, const std::string& tp) {
        for (const auto& c : conns)
            if (c.from_node == fn && c.from_port == fp && c.to_node == tn && c.to_port == tp)
                return true;
        return false;
    };

    check(has_conn("synth1.__osc", "output", "synth1.__mixer", "input"),
          "internal connection preserved (osc->mixer)");
    check(has_conn("midi", "note", "synth1.__osc", "frequency"),
          "boundary input rewritten (midi/note -> osc/frequency)");
    check(has_conn("midi", "gate", "synth1.__osc", "gate"),
          "boundary input rewritten (midi/gate -> osc/gate)");
    check(has_conn("synth1.__mixer", "output", "out", "input"),
          "boundary output rewritten (mixer/output -> out/input)");

}

static void test_flatten_multiple_instances() {
    std::fprintf(stderr, "\n--- flatten: multiple instances of same module ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_multi.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    // Two instances of the same module
    vivid::Graph parent;
    parent.add_node("synth_a", "TestSynth", {{"volume", 0.5f}});
    parent.add_node("synth_b", "TestSynth", {{"volume", 0.8f}});
    parent.add_node("out", "audio_out", {});
    parent.add_connection("synth_a", "output", "out", "input");

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry).graph;

    // Both instances should have their own prefixed nodes
    check(flat.find_node("synth_a.__osc") != nullptr, "instance A osc exists");
    check(flat.find_node("synth_a.__mixer") != nullptr, "instance A mixer exists");
    check(flat.find_node("synth_b.__osc") != nullptr, "instance B osc exists");
    check(flat.find_node("synth_b.__mixer") != nullptr, "instance B mixer exists");

    // No collisions — IDs are different
    check(flat.find_node("synth_a.__osc")->subgraph_owner == "synth_a", "A osc owner");
    check(flat.find_node("synth_b.__osc")->subgraph_owner == "synth_b", "B osc owner");

    // Independent param overrides
    auto* a_mixer = flat.find_node("synth_a.__mixer");
    auto* b_mixer = flat.find_node("synth_b.__mixer");
    if (a_mixer && b_mixer) {
        check(a_mixer->params.at("gain") == 0.5f, "instance A gain = 0.5");
        check(b_mixer->params.at("gain") == 0.8f, "instance B gain = 0.8");
    }

    // Total nodes: 2 instances * 2 internal nodes + 1 external = 5
    check(flat.nodes().size() == 5, "5 total nodes after flattening");

}

static void test_flatten_no_modules() {
    std::fprintf(stderr, "\n--- flatten: graph with no modules (passthrough) ---\n");

    vivid::SubgraphModuleRegistry registry;  // empty

    vivid::Graph parent;
    parent.add_node("a", "TypeA", {{"x", 1.0f}});
    parent.add_node("b", "TypeB", {});
    parent.add_connection("a", "out", "b", "in");

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry).graph;

    check(flat.nodes().size() == 2, "nodes preserved");
    check(flat.connections().size() == 1, "connections preserved");
    check(flat.find_node("a") != nullptr, "node a preserved");
    check(flat.find_node("b") != nullptr, "node b preserved");
}

static void test_flatten_effects_chain() {
    std::fprintf(stderr, "\n--- flatten: effects chain (audio in -> audio out) ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_fx_flatten.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kEffectsModule, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("osc", "Oscillator", {});
    parent.add_node("fx", "FXChain", {{"cutoff", 5000.0f}});
    parent.add_node("out", "audio_out", {});
    parent.add_connection("osc", "output", "fx", "input");
    parent.add_connection("fx", "output", "out", "input");

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry).graph;

    check(flat.find_node("fx") == nullptr, "module placeholder removed");
    check(flat.find_node("fx.__filter") != nullptr, "internal filter node exists");
    check(flat.find_node("fx.__reverb") != nullptr, "internal reverb node exists");

    // Cutoff override applied
    auto* filter = flat.find_node("fx.__filter");
    if (filter) {
        auto cutoff_it = filter->params.find("cutoff");
        check(cutoff_it != filter->params.end() && cutoff_it->second == 5000.0f,
              "cutoff overridden to 5000");
    }

    // Boundary connections rewritten
    const auto& conns = flat.connections();
    auto has_conn = [&](const std::string& fn, const std::string& fp,
                        const std::string& tn, const std::string& tp) {
        for (const auto& c : conns)
            if (c.from_node == fn && c.from_port == fp && c.to_node == tn && c.to_port == tp)
                return true;
        return false;
    };

    check(has_conn("osc", "output", "fx.__filter", "input"),
          "audio input rewritten to internal filter");
    check(has_conn("fx.__reverb", "output", "out", "input"),
          "audio output rewritten from internal reverb");

}

static void test_registry_type_names() {
    std::fprintf(stderr, "\n--- registry: type_names ---\n");

    vivid::SubgraphModuleRegistry registry;

    std::string tmp1 = (g_tmp->path / "test_type1.vivid-module.json").string();
    std::string tmp2 = (g_tmp->path / "test_type2.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp1.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    {
        FILE* f = std::fopen(tmp2.c_str(), "w");
        std::fputs(kEffectsModule, f);
        std::fclose(f);
    }

    registry.load(tmp1);
    registry.load(tmp2);

    auto names = registry.type_names();
    check(names.size() == 2, "2 type names");
    // Should be sorted
    check(names[0] == "FXChain", "first type alphabetically");
    check(names[1] == "TestSynth", "second type alphabetically");

}

static void test_flatten_midi_mapping_remap() {
    std::fprintf(stderr, "\n--- flatten: MIDI mapping remapped through param binding ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_midi.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "TestSynth", {});
    parent.add_node("out", "audio_out", {});
    parent.add_connection("synth1", "output", "out", "input");
    // MIDI mapping on the module's exposed "volume" param (binds to mixer/gain)
    parent.add_midi_mapping("synth1", "volume", 7, 0, 0.0f, 1.0f);
    // MIDI mapping on an external node (should be unaffected)
    parent.add_midi_mapping("out", "gain", 11, 0, 0.0f, 1.0f);

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry).graph;

    const auto& mappings = flat.midi_mappings();
    check(mappings.size() == 2, "2 MIDI mappings preserved");

    // The module mapping should be remapped to synth1.__mixer/gain
    bool found_remapped = false;
    bool found_external = false;
    for (const auto& m : mappings) {
        if (m.node_id == "synth1.__mixer" && m.param_name == "gain" && m.cc_number == 7)
            found_remapped = true;
        if (m.node_id == "out" && m.param_name == "gain" && m.cc_number == 11)
            found_external = true;
    }
    check(found_remapped, "module MIDI mapping remapped to synth1.__mixer/gain");
    check(found_external, "external MIDI mapping preserved unchanged");

}

static void test_flatten_variation_remap() {
    std::fprintf(stderr, "\n--- flatten: variation params remapped through param binding ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_var.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "TestSynth", {});
    parent.add_node("out", "audio_out", {});
    parent.add_connection("synth1", "output", "out", "input");

    // Add a variation that sets module params + external node params
    vivid::VariationDef var;
    var.name = "Loud";
    var.params["synth1"]["volume"] = 0.95f;
    var.params["synth1"]["waveform"] = 3.0f;
    var.params["out"]["master_vol"] = 0.8f;
    parent.add_variation(var);

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry).graph;

    check(flat.variations().size() == 1, "1 variation preserved");
    const auto& v = flat.variations()[0];
    check(v.name == "Loud", "variation name preserved");

    // Module params should be remapped
    check(v.params.count("synth1") == 0, "module node ID removed from variation");
    check(v.params.count("synth1.__mixer") == 1, "mixer params added");
    check(v.params.at("synth1.__mixer").at("gain") == 0.95f, "volume -> gain remapped");
    check(v.params.count("synth1.__osc") == 1, "osc params added");
    check(v.params.at("synth1.__osc").at("waveform") == 3.0f, "waveform remapped");

    // External node params preserved
    check(v.params.count("out") == 1, "external node preserved");
    check(v.params.at("out").at("master_vol") == 0.8f, "external param unchanged");

}

static void test_flatten_cross_instance_connection() {
    std::fprintf(stderr, "\n--- flatten: connection between two module instances ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp1 = (g_tmp->path / "test_cross1.vivid-module.json").string();
    std::string tmp2 = (g_tmp->path / "test_cross2.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp1.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    {
        FILE* f = std::fopen(tmp2.c_str(), "w");
        std::fputs(kEffectsModule, f);
        std::fclose(f);
    }
    registry.load(tmp1);
    registry.load(tmp2);

    // synth -> fx -> out
    vivid::Graph parent;
    parent.add_node("synth1", "TestSynth", {});
    parent.add_node("fx1", "FXChain", {});
    parent.add_node("out", "audio_out", {});
    parent.add_connection("synth1", "output", "fx1", "input");
    parent.add_connection("fx1", "output", "out", "input");

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry).graph;

    // synth1 internal: osc + mixer = 2 nodes
    // fx1 internal: filter + reverb = 2 nodes
    // external: out = 1 node
    check(flat.nodes().size() == 5, "5 total nodes");

    auto has_conn = [&](const std::string& fn, const std::string& fp,
                        const std::string& tn, const std::string& tp) {
        for (const auto& c : flat.connections())
            if (c.from_node == fn && c.from_port == fp && c.to_node == tn && c.to_port == tp)
                return true;
        return false;
    };

    // synth1 output (mixer/output) -> fx1 input (filter/input)
    check(has_conn("synth1.__mixer", "output", "fx1.__filter", "input"),
          "cross-module connection rewritten correctly");
    // fx1 output (reverb/output) -> out/input
    check(has_conn("fx1.__reverb", "output", "out", "input"),
          "fx output -> audio_out rewritten");

}

// ---------------------------------------------------------------------------
// Param metadata parsing tests
// ---------------------------------------------------------------------------

static const char* kModuleWithParamMetadata = R"({
    "schema_version": 2,
    "module": {
        "name": "RichSynth",
        "description": "A synth with rich param metadata",
        "category": "Synthesizer",
        "ports": [
            { "name": "output", "type": "audio", "direction": "output", "bind": "mixer/output" }
        ],
        "params": [
            {
                "name": "cutoff", "bind": "filter/cutoff",
                "type": "float",
                "group": "Filter",
                "description": "Lowpass filter cutoff frequency",
                "min": 20.0, "max": 20000.0, "default": 1000.0,
                "display_hint": "knob",
                "semantic_tag": "frequency_hz",
                "semantic_shape": "scalar",
                "semantic_unit": "Hz",
                "semantic_intent": "filter_cutoff",
                "layout_columns": 2,
                "layout_column_index": 0
            },
            {
                "name": "mode", "bind": "filter/mode",
                "type": "int",
                "group": "Filter",
                "description": "Filter mode",
                "choices": ["Lowpass", "Highpass", "Bandpass"],
                "default": 0, "min": 0, "max": 2
            },
            {
                "name": "bypass", "bind": "filter/bypass",
                "type": "bool",
                "display_hint": "hidden"
            },
            {
                "name": "volume", "bind": "mixer/gain"
            }
        ]
    },
    "nodes": {
        "filter": { "type": "Filter", "params": { "cutoff": 500, "mode": 0, "bypass": 0 } },
        "mixer":  { "type": "VoiceMixer", "params": { "gain": 0.7 } }
    },
    "connections": [
        { "from": "filter/output", "to": "mixer/input" }
    ]
})";

static void test_parse_param_metadata() {
    std::fprintf(stderr, "\n--- parse: param metadata ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "rich_synth.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithParamMetadata, f);
        std::fclose(f);
    }
    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("RichSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;
    check(mod->params.size() == 4, "4 params defined");

    // cutoff — fully specified
    const auto& cutoff = mod->params[0];
    check(cutoff.name == "cutoff", "cutoff name");
    check(cutoff.type.has_value() && *cutoff.type == VIVID_PARAM_FLOAT, "cutoff type = float");
    check(cutoff.group == "Filter", "cutoff group");
    check(cutoff.description == "Lowpass filter cutoff frequency", "cutoff description");
    check(cutoff.min_value.has_value() && *cutoff.min_value == 20.0f, "cutoff min");
    check(cutoff.max_value.has_value() && *cutoff.max_value == 20000.0f, "cutoff max");
    check(cutoff.default_value.has_value() && *cutoff.default_value == 1000.0f, "cutoff default");
    check(cutoff.display_hint.has_value() && *cutoff.display_hint == VIVID_DISPLAY_KNOB, "cutoff display_hint = knob");
    check(cutoff.semantic_tag == "frequency_hz", "cutoff semantic_tag");
    check(cutoff.semantic_shape == "scalar", "cutoff semantic_shape");
    check(cutoff.semantic_unit == "Hz", "cutoff semantic_unit");
    check(cutoff.semantic_intent == "filter_cutoff", "cutoff semantic_intent");
    check(cutoff.layout_columns == 2, "cutoff layout_columns");
    check(cutoff.layout_column_index == 0, "cutoff layout_column_index");

    // mode — int with choices
    const auto& mode = mod->params[1];
    check(mode.type.has_value() && *mode.type == VIVID_PARAM_INT, "mode type = int");
    check(mode.group == "Filter", "mode group");
    check(mode.choice_labels.size() == 3, "mode has 3 choices");
    check(mode.choice_labels[0] == "Lowpass", "mode choice 0");
    check(mode.choice_labels[2] == "Bandpass", "mode choice 2");

    // bypass — bool, hidden
    const auto& bypass = mod->params[2];
    check(bypass.type.has_value() && *bypass.type == VIVID_PARAM_BOOL, "bypass type = bool");
    check(bypass.display_hint.has_value() && *bypass.display_hint == VIVID_DISPLAY_HIDDEN, "bypass display_hint = hidden");

    // volume — no metadata (backward compat)
    const auto& volume = mod->params[3];
    check(!volume.type.has_value(), "volume type not specified");
    check(!volume.default_value.has_value(), "volume default not specified");
    check(!volume.min_value.has_value(), "volume min not specified");
    check(!volume.max_value.has_value(), "volume max not specified");
    check(volume.group.empty(), "volume group empty");
    check(volume.description.empty(), "volume description empty");
    check(!volume.display_hint.has_value(), "volume display_hint not specified");
    check(volume.choice_labels.empty(), "volume no choices");
}

static void test_parse_param_metadata_absent() {
    std::fprintf(stderr, "\n--- parse: existing module unchanged ---\n");

    // Load the original kSimpleModule and verify params have no metadata
    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "simple_compat.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("TestSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;

    for (const auto& pb : mod->params) {
        check(!pb.type.has_value(), (pb.name + " type not specified").c_str());
        check(!pb.default_value.has_value(), (pb.name + " default not specified").c_str());
        check(!pb.min_value.has_value(), (pb.name + " min not specified").c_str());
        check(!pb.max_value.has_value(), (pb.name + " max not specified").c_str());
        check(pb.group.empty(), (pb.name + " group empty").c_str());
        check(pb.description.empty(), (pb.name + " description empty").c_str());
        check(!pb.display_hint.has_value(), (pb.name + " display_hint not specified").c_str());
    }
}

// ---------------------------------------------------------------------------
// make_operator_info tests
// ---------------------------------------------------------------------------

static void test_make_operator_info_with_metadata() {
    std::fprintf(stderr, "\n--- make_operator_info: rich metadata ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "rich_opinfo.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithParamMetadata, f);
        std::fclose(f);
    }
    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("RichSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;

    auto info = vivid::make_operator_info(*mod);
    check(info != nullptr, "operator info created");
    check(info->is_module, "is_module = true");
    check(info->name == "RichSynth", "info name");
    check(info->params.size() == 4, "4 params in info");

    // cutoff — full metadata propagated
    const auto& cutoff = info->params[0];
    check(cutoff.name == "cutoff", "cutoff name");
    check(cutoff.type == VIVID_PARAM_FLOAT, "cutoff type");
    check(cutoff.min_value == 20.0f, "cutoff min");
    check(cutoff.max_value == 20000.0f, "cutoff max");
    check(cutoff.default_value == 1000.0f, "cutoff default (explicit override)");
    check(cutoff.group == "Filter", "cutoff group");
    check(cutoff.description == "Lowpass filter cutoff frequency", "cutoff description");
    check(cutoff.display_hint == VIVID_DISPLAY_KNOB, "cutoff display_hint");
    check(cutoff.semantic_tag == "frequency_hz", "cutoff semantic_tag");
    check(cutoff.semantic_unit == "Hz", "cutoff semantic_unit");
    check(cutoff.layout_columns == 2, "cutoff layout_columns");

    // mode — int with choices
    const auto& mode = info->params[1];
    check(mode.type == VIVID_PARAM_INT, "mode type = int");
    check(mode.choice_count == 3, "mode choice_count");
    check(mode.choice_labels.size() == 3, "mode choice_labels size");
    check(mode.choice_labels[1] == "Highpass", "mode choice 1");
    check(mode.group == "Filter", "mode group");

    // volume — no metadata, inherits internal default
    const auto& volume = info->params[3];
    check(volume.type == VIVID_PARAM_FLOAT, "volume type defaults to float");
    check(volume.default_value == 0.7f, "volume default inherited from internal node");
    check(volume.min_value == 0.0f, "volume min defaults to 0");
    check(volume.max_value == 1.0f, "volume max defaults to 1");
    check(volume.group.empty(), "volume group empty");
}

static void test_make_operator_info_backward_compat() {
    std::fprintf(stderr, "\n--- make_operator_info: backward compat ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "compat_opinfo.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("TestSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;

    auto info = vivid::make_operator_info(*mod);
    check(info->is_module, "is_module = true");
    check(info->params.size() == 2, "2 params");

    // volume — should inherit default 0.7 from mixer/gain internal node
    const auto& volume = info->params[0];
    check(volume.name == "volume", "volume name");
    check(volume.type == VIVID_PARAM_FLOAT, "volume type = float");
    check(volume.default_value == 0.7f, "volume default from internal node");
    check(volume.min_value == 0.0f, "volume min = 0");
    check(volume.max_value == 1.0f, "volume max = 1");
    check(volume.group.empty(), "volume no group");
    check(volume.description.empty(), "volume no description");
    check(volume.display_hint == VIVID_DISPLAY_DEFAULT, "volume display_hint = default");
    check(volume.choice_labels.empty(), "volume no choices");
}

// ---------------------------------------------------------------------------
// to_operator_preset tests
// ---------------------------------------------------------------------------

static void test_to_operator_preset_basic() {
    std::fprintf(stderr, "\n--- to_operator_preset: basic translation ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "preset_test.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("TestSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;

    // "Bright" preset: osc/waveform -> 1, mixer/gain -> 0.8
    const auto* sp = mod->find_preset("Bright");
    check(sp != nullptr, "Bright preset found");
    if (!sp) return;

    auto op = vivid::to_operator_preset(*sp, *mod);
    check(op.name == "Bright", "preset name preserved");
    check(op.params.size() == 2, "2 params translated");

    // "osc/waveform" -> exposed name "waveform"
    auto wf_it = op.params.find("waveform");
    check(wf_it != op.params.end(), "waveform param mapped");
    if (wf_it != op.params.end())
        check(wf_it->second == 1.0f, "waveform value = 1");

    // "mixer/gain" -> exposed name "volume"
    auto vol_it = op.params.find("volume");
    check(vol_it != op.params.end(), "volume param mapped");
    if (vol_it != op.params.end())
        check(vol_it->second == 0.8f, "volume value = 0.8");
}

static void test_to_operator_preset_unmapped() {
    std::fprintf(stderr, "\n--- to_operator_preset: unmapped keys dropped ---\n");

    // Create a module def with a preset that has an internal-only override
    vivid::SubgraphModuleDef def;
    def.name = "TestDrop";
    vivid::SubgraphParamBinding pb;
    pb.name = "volume";
    pb.internal_node = "mixer";
    pb.internal_param = "gain";
    def.params.push_back(pb);

    vivid::SubgraphPreset sp;
    sp.name = "Mixed";
    sp.param_overrides["mixer/gain"] = 0.5f;       // mapped
    sp.param_overrides["osc/detune"] = 0.1f;        // unmapped — should be dropped

    auto op = vivid::to_operator_preset(sp, def);
    check(op.params.size() == 1, "only mapped param kept");
    check(op.params.count("volume") == 1, "volume param present");
    check(op.params.count("detune") == 0, "unmapped detune dropped");
}

// ---------------------------------------------------------------------------
// Validation tests
// ---------------------------------------------------------------------------

static const char* kModuleBadParamBinding = R"({
    "schema_version": 2,
    "module": {
        "name": "BadParamBind",
        "ports": [
            { "name": "output", "type": "audio", "direction": "output", "bind": "mixer/output" }
        ],
        "params": [
            { "name": "volume", "bind": "nonexistent_node/gain" }
        ]
    },
    "nodes": {
        "mixer": { "type": "VoiceMixer", "params": { "gain": 0.7 } }
    },
    "connections": []
})";

static const char* kModuleBadPortBinding = R"({
    "schema_version": 2,
    "module": {
        "name": "BadPortBind",
        "ports": [
            { "name": "output", "type": "audio", "direction": "output", "bind": "nonexistent/output" }
        ],
        "params": [
            { "name": "volume", "bind": "mixer/gain" }
        ]
    },
    "nodes": {
        "mixer": { "type": "VoiceMixer", "params": { "gain": 0.7 } }
    },
    "connections": []
})";

static const char* kModuleBadPresetRef = R"({
    "schema_version": 2,
    "module": {
        "name": "BadPresetRef",
        "ports": [
            { "name": "output", "type": "audio", "direction": "output", "bind": "mixer/output" }
        ],
        "params": [
            { "name": "volume", "bind": "mixer/gain" }
        ],
        "presets": {
            "Test": { "also_missing/param": 0.5 }
        }
    },
    "nodes": {
        "mixer": { "type": "VoiceMixer", "params": { "gain": 0.7 } }
    },
    "connections": []
})";

static void test_validation_bad_param_binding_fails() {
    std::fprintf(stderr, "\n--- validation: bad param binding fails to load ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "bad_param.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleBadParamBinding, f);
        std::fclose(f);
    }
    check(!registry.load(tmp_path), "load fails for bad param binding");
    check(registry.find("BadParamBind") == nullptr, "module not registered");
}

static void test_validation_bad_port_binding_fails() {
    std::fprintf(stderr, "\n--- validation: bad port binding fails to load ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "bad_port.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleBadPortBinding, f);
        std::fclose(f);
    }
    check(!registry.load(tmp_path), "load fails for bad port binding");
    check(registry.find("BadPortBind") == nullptr, "module not registered");
}

static void test_validation_bad_preset_ref_warns() {
    std::fprintf(stderr, "\n--- validation: bad preset ref warns but loads ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "bad_preset.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleBadPresetRef, f);
        std::fclose(f);
    }
    check(registry.load(tmp_path), "load succeeds despite bad preset ref");
    const auto* mod = registry.find("BadPresetRef");
    check(mod != nullptr, "module found");
    if (!mod) return;
    check(mod->params.size() == 1, "param preserved");
    check(mod->presets.size() == 1, "preset preserved");
}

// ---------------------------------------------------------------------------
// Modulation source/destination parsing tests
// ---------------------------------------------------------------------------

static const char* kModuleWithModulation = R"({
    "schema_version": 3,
    "module": {
        "name": "ModSynth",
        "description": "Synth with mod sources and destinations",
        "category": "Synthesizer",
        "ports": [
            { "name": "vel_in",  "type": "signal", "direction": "input",  "bind": "osc/gate" },
            { "name": "output",  "type": "audio",  "direction": "output", "bind": "mixer/output" }
        ],
        "params": [
            { "name": "cutoff",   "bind": "filter/cutoff" },
            { "name": "position", "bind": "osc/position" }
        ],
        "mod_sources": [
            { "name": "lfo1",     "bind": "lfo1/value",  "description": "LFO 1", "polarity": "bipolar", "shape": "scalar" },
            { "name": "env2",     "bind": "env2/value",   "description": "Envelope 2" },
            { "name": "velocity", "bind": "vel_in",       "kind": "port", "description": "Note velocity" }
        ],
        "mod_destinations": [
            { "name": "filter_cutoff", "bind": "filter/cutoff", "description": "Filter cutoff", "group": "Filter" },
            { "name": "wt_position",   "bind": "osc/position",  "description": "WT position" }
        ]
    },
    "nodes": {
        "osc":    { "type": "Oscillator", "params": { "frequency": 440, "position": 0.5 } },
        "filter": { "type": "Filter",     "params": { "cutoff": 1000, "resonance": 0.5 } },
        "lfo1":   { "type": "LFO",        "params": { "rate": 2 } },
        "env2":   { "type": "Envelope",   "params": { "attack": 0.1 } },
        "mixer":  { "type": "VoiceMixer", "params": { "gain": 0.7 } }
    },
    "connections": [
        { "from": "osc/output", "to": "filter/input" },
        { "from": "filter/output", "to": "mixer/input" }
    ]
})";

static void test_parse_mod_sources_destinations() {
    std::fprintf(stderr, "\n--- parse: mod_sources and mod_destinations ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "mod_synth.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithModulation, f);
        std::fclose(f);
    }

    check(registry.load(tmp_path), "load succeeds");
    const auto* mod = registry.find("ModSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;

    // Sources
    check(mod->mod_sources.size() == 3, "3 mod_sources");
    check(mod->mod_sources[0].name == "lfo1", "source 0 name");
    check(mod->mod_sources[0].polarity == "bipolar", "source 0 polarity");
    check(mod->mod_sources[0].shape == "scalar", "source 0 shape");
    check(mod->mod_sources[0].internal_node == "lfo1", "source 0 internal_node");
    check(mod->mod_sources[0].internal_port == "value", "source 0 internal_port");
    check(mod->mod_sources[0].kind == "internal", "source 0 kind");
    check(mod->mod_sources[1].name == "env2", "source 1 name");
    check(mod->mod_sources[1].polarity == "unipolar", "source 1 polarity default");
    check(mod->mod_sources[1].shape == "scalar", "source 1 shape default");
    check(mod->mod_sources[2].name == "velocity", "source 2 name");
    check(mod->mod_sources[2].kind == "port", "source 2 kind");
    check(mod->mod_sources[2].internal_port == "vel_in", "source 2 port name");
    check(mod->mod_sources[2].internal_node.empty(), "source 2 no internal_node for port kind");

    // Destinations
    check(mod->mod_destinations.size() == 2, "2 mod_destinations");
    check(mod->mod_destinations[0].name == "filter_cutoff", "dest 0 name");
    check(mod->mod_destinations[0].internal_node == "filter", "dest 0 internal_node");
    check(mod->mod_destinations[0].internal_param == "cutoff", "dest 0 internal_param");
    check(mod->mod_destinations[0].group == "Filter", "dest 0 group");
    check(mod->mod_destinations[1].name == "wt_position", "dest 1 name");

    // Lookup helpers
    check(mod->find_mod_source("lfo1") != nullptr, "find_mod_source lfo1");
    check(mod->find_mod_source("nonexistent") == nullptr, "find_mod_source nonexistent");
    check(mod->find_mod_destination("filter_cutoff") != nullptr, "find_mod_destination");
    check(mod->find_mod_destination("nonexistent") == nullptr, "find_mod_destination nonexistent");
}

static const char* kModuleBadModSourceBind = R"({
    "schema_version": 3,
    "module": {
        "name": "BadModSource",
        "category": "Test",
        "ports": [
            { "name": "output", "type": "audio", "direction": "output", "bind": "mixer/output" }
        ],
        "mod_sources": [
            { "name": "ghost", "bind": "nonexistent/value" }
        ]
    },
    "nodes": {
        "mixer": { "type": "VoiceMixer", "params": { "gain": 0.7 } }
    },
    "connections": []
})";

static void test_parse_bad_mod_source_bind() {
    std::fprintf(stderr, "\n--- parse: bad mod_source bind rejected ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "bad_mod_source.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleBadModSourceBind, f);
        std::fclose(f);
    }

    check(!registry.load(tmp_path), "load fails for bad mod_source bind");
}

static const char* kModuleDuplicateModSourceName = R"({
    "schema_version": 3,
    "module": {
        "name": "DupModSource",
        "category": "Test",
        "ports": [
            { "name": "output", "type": "audio", "direction": "output", "bind": "mixer/output" }
        ],
        "mod_sources": [
            { "name": "lfo1", "bind": "lfo/value" },
            { "name": "lfo1", "bind": "lfo/value" }
        ]
    },
    "nodes": {
        "mixer": { "type": "VoiceMixer", "params": {} },
        "lfo": { "type": "LFO", "params": {} }
    },
    "connections": []
})";

static void test_parse_duplicate_mod_source_name() {
    std::fprintf(stderr, "\n--- parse: duplicate mod_source name rejected ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "dup_mod_source.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleDuplicateModSourceName, f);
        std::fclose(f);
    }

    check(!registry.load(tmp_path), "load fails for duplicate mod_source name");
}

static const char* kModuleBadPortKindSource = R"({
    "schema_version": 3,
    "module": {
        "name": "BadPortSource",
        "category": "Test",
        "ports": [
            { "name": "output", "type": "audio", "direction": "output", "bind": "mixer/output" }
        ],
        "mod_sources": [
            { "name": "ghost_port", "bind": "nonexistent_port", "kind": "port" }
        ]
    },
    "nodes": {
        "mixer": { "type": "VoiceMixer", "params": {} }
    },
    "connections": []
})";

static void test_parse_bad_port_kind_source() {
    std::fprintf(stderr, "\n--- parse: bad port-kind mod_source rejected ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "bad_port_source.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleBadPortKindSource, f);
        std::fclose(f);
    }

    check(!registry.load(tmp_path), "load fails for bad port-kind mod_source");
}

// ---------------------------------------------------------------------------
// Modulation assignment serialization tests
// ---------------------------------------------------------------------------

static void test_mod_assignment_graph_crud() {
    std::fprintf(stderr, "\n--- mod_assignment: graph CRUD ---\n");

    vivid::Graph g;
    g.add_node("synth1", "ModSynth", {});

    vivid::ModAssignmentDef a1;
    a1.source = "lfo1";
    a1.destination = "filter_cutoff";
    a1.amount = 0.7f;
    a1.polarity = "bipolar";

    check(g.add_mod_assignment("synth1", a1), "add assignment succeeds");

    // Reject duplicate
    check(!g.add_mod_assignment("synth1", a1), "duplicate rejected");

    // Find
    auto* assignments = g.find_mod_assignments("synth1");
    check(assignments != nullptr, "assignments found");
    check(assignments->size() == 1, "1 assignment");
    check((*assignments)[0].source == "lfo1", "source matches");
    check((*assignments)[0].amount == 0.7f, "amount matches");
    check((*assignments)[0].polarity == "bipolar", "polarity matches");
    check((*assignments)[0].curve == "linear", "curve defaults to linear");

    // Update
    check(g.update_mod_assignment("synth1", "lfo1", "filter_cutoff", 0.3f, "unipolar", "linear"),
          "update succeeds");
    check((*g.find_mod_assignments("synth1"))[0].amount == 0.3f, "amount updated");
    check((*g.find_mod_assignments("synth1"))[0].polarity == "unipolar", "polarity updated");

    // Add a second
    vivid::ModAssignmentDef a2;
    a2.source = "env2";
    a2.destination = "wt_position";
    a2.amount = 0.5f;
    check(g.add_mod_assignment("synth1", a2), "add second assignment");
    check(g.find_mod_assignments("synth1")->size() == 2, "2 assignments");

    // Remove first
    check(g.remove_mod_assignment("synth1", "lfo1", "filter_cutoff"), "remove succeeds");
    check(g.find_mod_assignments("synth1")->size() == 1, "1 assignment after remove");

    // Remove second
    check(g.remove_mod_assignment("synth1", "env2", "wt_position"), "remove last succeeds");
    check(g.find_mod_assignments("synth1") == nullptr, "empty map cleaned up");
}

static void test_mod_assignment_serialization() {
    std::fprintf(stderr, "\n--- mod_assignment: round-trip serialization ---\n");

    vivid::Graph g;
    g.add_node("synth1", "ModSynth", {});

    vivid::ModAssignmentDef a1;
    a1.source = "lfo1";
    a1.destination = "filter_cutoff";
    a1.amount = 0.7f;
    a1.polarity = "bipolar";
    g.add_mod_assignment("synth1", a1);

    vivid::ModAssignmentDef a2;
    a2.source = "env2";
    a2.destination = "wt_position";
    a2.amount = 0.3f;
    g.add_mod_assignment("synth1", a2);

    // Save to string
    std::string json;
    check(g.save_to_string(json), "save_to_string succeeds");

    // Verify JSON contains mod_assignments
    check(json.find("\"mod_assignments\"") != std::string::npos, "JSON contains mod_assignments");

    // Reload
    vivid::Graph g2;
    check(g2.load_from_string(json.c_str(), json.size()), "reload succeeds");

    auto* loaded = g2.find_mod_assignments("synth1");
    check(loaded != nullptr, "assignments loaded");
    check(loaded->size() == 2, "2 assignments loaded");

    // Check first assignment
    check((*loaded)[0].source == "lfo1", "loaded source 0");
    check((*loaded)[0].destination == "filter_cutoff", "loaded dest 0");
    check((*loaded)[0].amount == 0.7f, "loaded amount 0");
    check((*loaded)[0].polarity == "bipolar", "loaded polarity 0");
    check((*loaded)[0].curve == "linear", "loaded curve 0");

    // Check second assignment
    check((*loaded)[1].source == "env2", "loaded source 1");
    check((*loaded)[1].destination == "wt_position", "loaded dest 1");
    check((*loaded)[1].amount == 0.3f, "loaded amount 1");
    check((*loaded)[1].polarity == "unipolar", "loaded polarity 1 default");
}

// ---------------------------------------------------------------------------
// Modulation lowering tests
// ---------------------------------------------------------------------------

// Helper: find a connection in a graph
static const vivid::ConnectionDef* find_conn(const vivid::Graph& g,
                                              const std::string& fn, const std::string& fp,
                                              const std::string& tn, const std::string& tp) {
    for (const auto& c : g.connections())
        if (c.from_node == fn && c.from_port == fp && c.to_node == tn && c.to_port == tp)
            return &c;
    return nullptr;
}

static void test_flatten_single_mod_assignment() {
    std::fprintf(stderr, "\n--- flatten: single modulation assignment ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "mod_synth_flatten.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithModulation, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "ModSynth", {{"cutoff", 880.0f}});

    // Add a single assignment: lfo1 -> filter_cutoff, amount 0.7, bipolar
    vivid::ModAssignmentDef a;
    a.source = "lfo1";
    a.destination = "filter_cutoff";
    a.amount = 0.7f;
    a.polarity = "bipolar";
    parent.add_mod_assignment("synth1", a);

    auto result = vivid::flatten_subgraphs(parent, registry);
    const auto& flat = result.graph;

    // No Math(add) nodes for single assignment
    bool has_math = false;
    for (const auto& n : flat.nodes())
        if (n.type == "Math" && n.subgraph_owner == "synth1") has_math = true;
    check(!has_math, "no synthesized Math node for single assignment");

    // Should have a connection: synth1.__lfo1/value -> synth1.__filter/cutoff
    auto* conn = find_conn(flat, "synth1.__lfo1", "value", "synth1.__filter", "cutoff");
    check(conn != nullptr, "modulation connection exists");
    if (conn) {
        check(conn->has_remap(), "connection has remap");
        // Base=880, amount=0.7, bipolar: to_min=880-0.7=879.3, to_max=880+0.7=880.7
        check_float(conn->to_min, 879.3f, "remap to_min = base - amount");
        check_float(conn->to_max, 880.7f, "remap to_max = base + amount");
        check_float(conn->from_min, 0.0f, "remap from_min = 0");
        check_float(conn->from_max, 1.0f, "remap from_max = 1");
    }

    // Check lowering record
    check(result.modulation_records.size() == 1, "1 lowering record");
    if (!result.modulation_records.empty()) {
        const auto& rec = result.modulation_records[0];
        check(rec.instance_id == "synth1", "record instance_id");
        check(rec.exposed_param == "cutoff", "record exposed_param");
        check(rec.bipolar == true, "record bipolar");
        check_float(rec.amount, 0.7f, "record amount");
    }
}

static void test_flatten_two_mod_assignments_same_dest() {
    std::fprintf(stderr, "\n--- flatten: two mod assignments to same destination ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "mod_synth_multi.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithModulation, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "ModSynth", {{"cutoff", 500.0f}});

    vivid::ModAssignmentDef a1;
    a1.source = "lfo1";
    a1.destination = "filter_cutoff";
    a1.amount = 100.0f;
    a1.polarity = "unipolar";
    parent.add_mod_assignment("synth1", a1);

    vivid::ModAssignmentDef a2;
    a2.source = "env2";
    a2.destination = "filter_cutoff";
    a2.amount = 200.0f;
    a2.polarity = "bipolar";
    parent.add_mod_assignment("synth1", a2);

    auto result = vivid::flatten_subgraphs(parent, registry);
    const auto& flat = result.graph;

    // Should have one Math(add) node
    int math_count = 0;
    std::string math_id;
    for (const auto& n : flat.nodes()) {
        if (n.type == "Math" && n.subgraph_owner == "synth1") {
            math_count++;
            math_id = n.id;
        }
    }
    check(math_count == 1, "1 synthesized Math(add) node");

    if (!math_id.empty()) {
        // Check Math node is hidden (subgraph_owner set)
        auto* mn = flat.find_node(math_id);
        check(mn != nullptr, "Math node exists");
        check(mn->subgraph_owner == "synth1", "Math subgraph_owner");
        check(mn->subgraph_type == "ModSynth", "Math subgraph_type");

        // Check connections:
        // lfo1/value -> math/a with remap (base=500, amount=100, unipolar)
        auto* conn_a = find_conn(flat, "synth1.__lfo1", "value", math_id, "a");
        check(conn_a != nullptr, "source 1 -> math/a connection");
        if (conn_a) {
            check_float(conn_a->to_min, 500.0f, "conn_a to_min = base");
            check_float(conn_a->to_max, 600.0f, "conn_a to_max = base + amount");
        }

        // env2/value -> math/b with remap (amount=200, bipolar)
        auto* conn_b = find_conn(flat, "synth1.__env2", "value", math_id, "b");
        check(conn_b != nullptr, "source 2 -> math/b connection");
        if (conn_b) {
            check_float(conn_b->to_min, -200.0f, "conn_b to_min = -amount (bipolar)");
            check_float(conn_b->to_max, 200.0f, "conn_b to_max = amount");
        }

        // math/result -> filter/cutoff
        auto* conn_out = find_conn(flat, math_id, "result",
                                    "synth1.__filter", "cutoff");
        check(conn_out != nullptr, "math/result -> dest connection");
    }

    // Should have 1 lowering record (for the base-carrying connection)
    check(result.modulation_records.size() == 1, "1 lowering record for multi-assignment");
}

static void test_flatten_unipolar_remap() {
    std::fprintf(stderr, "\n--- flatten: unipolar remap values ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "mod_synth_unipolar.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithModulation, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "ModSynth", {{"position", 0.5f}});

    vivid::ModAssignmentDef a;
    a.source = "env2";
    a.destination = "wt_position";
    a.amount = 0.3f;
    a.polarity = "unipolar";
    parent.add_mod_assignment("synth1", a);

    auto result = vivid::flatten_subgraphs(parent, registry);
    const auto& flat = result.graph;

    auto* conn = find_conn(flat, "synth1.__env2", "value", "synth1.__osc", "position");
    check(conn != nullptr, "unipolar connection exists");
    if (conn) {
        check_float(conn->to_min, 0.5f, "unipolar to_min = base");
        check_float(conn->to_max, 0.8f, "unipolar to_max = base + amount");
    }
}

static void test_flatten_zero_amount() {
    std::fprintf(stderr, "\n--- flatten: zero amount ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "mod_synth_zero.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithModulation, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "ModSynth", {{"cutoff", 440.0f}});

    vivid::ModAssignmentDef a;
    a.source = "lfo1";
    a.destination = "filter_cutoff";
    a.amount = 0.0f;
    parent.add_mod_assignment("synth1", a);

    auto result = vivid::flatten_subgraphs(parent, registry);
    const auto& flat = result.graph;

    auto* conn = find_conn(flat, "synth1.__lfo1", "value", "synth1.__filter", "cutoff");
    check(conn != nullptr, "zero-amount connection exists");
    if (conn) {
        check_float(conn->to_min, 440.0f, "zero amount: to_min = base");
        check_float(conn->to_max, 440.0f, "zero amount: to_max = base (no modulation)");
    }
}

static void test_flatten_no_assignments_unchanged() {
    std::fprintf(stderr, "\n--- flatten: no assignments leaves graph unchanged ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "mod_synth_nomod.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kModuleWithModulation, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "ModSynth", {{"cutoff", 1000.0f}});
    // No mod_assignments

    auto result = vivid::flatten_subgraphs(parent, registry);
    const auto& flat = result.graph;

    // Should have no Math nodes
    for (const auto& n : flat.nodes())
        check(n.type != "Math", "no Math nodes without assignments");

    // No lowering records
    check(result.modulation_records.empty(), "no lowering records");
}

static void test_flatten_existing_tests_still_work() {
    std::fprintf(stderr, "\n--- flatten: existing MIDI/variation remap unaffected by mod feature ---\n");

    // This test uses the TestSynth module (no mod_sources) with MIDI mappings
    // to verify the new code doesn't break existing behavior
    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = (g_tmp->path / "test_synth_compat.vivid-module.json").string();
    {
        FILE* f = std::fopen(tmp_path.c_str(), "w");
        std::fputs(kSimpleModule, f);
        std::fclose(f);
    }
    registry.load(tmp_path);

    vivid::Graph parent;
    parent.add_node("synth1", "TestSynth", {{"volume", 0.9f}});
    parent.add_midi_mapping("synth1", "volume", 7, 0, 0.0f, 1.0f);

    auto result = vivid::flatten_subgraphs(parent, registry);
    const auto& flat = result.graph;

    // MIDI mapping should be remapped to internal node
    check(flat.midi_mappings().size() == 1, "1 MIDI mapping preserved");
    if (!flat.midi_mappings().empty()) {
        check(flat.midi_mappings()[0].node_id == "synth1.__mixer", "MIDI mapping remapped");
        check(flat.midi_mappings()[0].param_name == "gain", "MIDI param remapped");
    }

    check(result.modulation_records.empty(), "no modulation records for module without mod_sources");
}

// ---------------------------------------------------------------------------
// Performance surface metadata tests (Step 5)
// ---------------------------------------------------------------------------

static const char* kModuleWithPerformance = R"({
    "schema_version": 3,
    "module": {
        "name": "PerfSynth",
        "description": "Test module with performance-tagged params",
        "category": "Synthesizer",
        "ports": [
            { "name": "output", "type": "signal", "direction": "output", "bind": "osc/value" }
        ],
        "params": [
            { "name": "cutoff", "bind": "filter/cutoff", "type": "float", "min": 20, "max": 20000, "default": 1000 },
            { "name": "macro1", "bind": "macro/a", "type": "float", "min": 0, "max": 1, "default": 0.5,
              "performance_page": "Performance", "performance_order": 0, "performance_role": "macro" },
            { "name": "macro2", "bind": "macro/b", "type": "float", "min": 0, "max": 1, "default": 0.5,
              "performance_page": "Performance", "performance_order": 1, "performance_role": "macro" },
            { "name": "brightness", "bind": "timbre/bright", "type": "float", "min": 0, "max": 1, "default": 0.3,
              "performance_page": "Timbre", "performance_order": 0, "performance_role": "expression" }
        ]
    },
    "nodes": {
        "osc": { "type": "LFO" },
        "filter": { "type": "Math" },
        "macro": { "type": "Math" },
        "timbre": { "type": "Math" }
    },
    "connections": []
})";

static void test_parse_performance_metadata() {
    std::fprintf(stderr, "\n--- parse: performance metadata ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string path = (g_tmp->path / "perf_synth.vivid-module.json").string();
    {
        FILE* f = std::fopen(path.c_str(), "w");
        std::fputs(kModuleWithPerformance, f);
        std::fclose(f);
    }

    check(registry.load(path), "load succeeds");
    const auto* mod = registry.find("PerfSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;

    check(mod->params.size() == 4, "4 params");

    // cutoff has no performance metadata
    check(mod->params[0].performance_page.empty(), "cutoff: no perf page");
    check(mod->params[0].performance_order == -1, "cutoff: perf order default");
    check(mod->params[0].performance_role.empty(), "cutoff: no perf role");

    // macro1
    check(mod->params[1].performance_page == "Performance", "macro1: perf page");
    check(mod->params[1].performance_order == 0, "macro1: perf order = 0");
    check(mod->params[1].performance_role == "macro", "macro1: perf role = macro");

    // macro2
    check(mod->params[2].performance_page == "Performance", "macro2: perf page");
    check(mod->params[2].performance_order == 1, "macro2: perf order = 1");

    // brightness
    check(mod->params[3].performance_page == "Timbre", "brightness: perf page = Timbre");
    check(mod->params[3].performance_role == "expression", "brightness: perf role = expression");
}

static void test_performance_metadata_defaults() {
    std::fprintf(stderr, "\n--- parse: performance metadata defaults (absent fields) ---\n");

    // Use an existing module JSON that doesn't have performance fields
    vivid::SubgraphModuleRegistry registry;
    std::string path = (g_tmp->path / "no_perf.vivid-module.json").string();
    {
        FILE* f = std::fopen(path.c_str(), "w");
        // Minimal module without any performance fields
        std::fputs(R"({
            "schema_version": 3,
            "module": {
                "name": "NoPerf",
                "ports": [
                    { "name": "output", "type": "signal", "direction": "output", "bind": "gain/output" }
                ],
                "params": [
                    { "name": "level", "bind": "gain/level", "type": "float", "min": 0, "max": 1, "default": 0.5 }
                ]
            },
            "nodes": { "gain": { "type": "Math" } },
            "connections": []
        })", f);
        std::fclose(f);
    }

    check(registry.load(path), "load succeeds");
    const auto* mod = registry.find("NoPerf");
    check(mod != nullptr, "module found");
    if (!mod) return;

    check(mod->params[0].performance_page.empty(), "empty perf page default");
    check(mod->params[0].performance_order == -1, "perf order default = -1");
    check(mod->params[0].performance_role.empty(), "empty perf role default");
}

static void test_performance_metadata_in_operator_info() {
    std::fprintf(stderr, "\n--- make_operator_info: performance metadata propagation ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string path = (g_tmp->path / "perf_synth_oi.vivid-module.json").string();
    {
        FILE* f = std::fopen(path.c_str(), "w");
        std::fputs(kModuleWithPerformance, f);
        std::fclose(f);
    }

    check(registry.load(path), "load succeeds");
    const auto* mod = registry.find("PerfSynth");
    check(mod != nullptr, "module found");
    if (!mod) return;

    auto info = vivid::make_operator_info(*mod);
    check(info != nullptr, "operator info created");
    if (!info) return;

    check(info->params.size() == 4, "4 params in OperatorInfo");

    // cutoff — no performance metadata
    check(info->params[0].performance_page.empty(), "OI cutoff: no perf page");
    check(info->params[0].performance_order == -1, "OI cutoff: perf order default");

    // macro1 — has performance metadata
    check(info->params[1].performance_page == "Performance", "OI macro1: perf page");
    check(info->params[1].performance_order == 0, "OI macro1: perf order");
    check(info->params[1].performance_role == "macro", "OI macro1: perf role");

    // brightness
    check(info->params[3].performance_page == "Timbre", "OI brightness: perf page");
    check(info->params[3].performance_role == "expression", "OI brightness: perf role");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    ScopedTempDir tmp("subgraph");
    g_tmp = &tmp;
    std::fprintf(stderr, "=== test_subgraph_module ===\n");

    test_parse_simple_module();
    test_registry_scan_directory();
    test_parse_effects_module();
    test_flatten_single_instance();
    test_flatten_multiple_instances();
    test_flatten_no_modules();
    test_flatten_effects_chain();
    test_registry_type_names();
    test_flatten_midi_mapping_remap();
    test_flatten_variation_remap();
    test_flatten_cross_instance_connection();
    test_parse_param_metadata();
    test_parse_param_metadata_absent();
    test_make_operator_info_with_metadata();
    test_make_operator_info_backward_compat();
    test_to_operator_preset_basic();
    test_to_operator_preset_unmapped();
    test_validation_bad_param_binding_fails();
    test_validation_bad_port_binding_fails();
    test_validation_bad_preset_ref_warns();

    // Modulation source/destination tests
    test_parse_mod_sources_destinations();
    test_parse_bad_mod_source_bind();
    test_parse_duplicate_mod_source_name();
    test_parse_bad_port_kind_source();
    test_mod_assignment_graph_crud();
    test_mod_assignment_serialization();

    // Modulation lowering tests
    test_flatten_single_mod_assignment();
    test_flatten_two_mod_assignments_same_dest();
    test_flatten_unipolar_remap();
    test_flatten_zero_amount();
    test_flatten_no_assignments_unchanged();
    test_flatten_existing_tests_still_work();

    // Performance surface metadata tests (Step 5)
    test_parse_performance_metadata();
    test_performance_metadata_defaults();
    test_performance_metadata_in_operator_info();

    std::fprintf(stderr, "\n=== %s (%d failure%s) ===\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
