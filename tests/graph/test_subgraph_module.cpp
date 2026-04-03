// Unit tests for subgraph module parsing and graph flattening.
// These test the JSON parsing of .vivid-module.json definitions and
// the compile-time flattening transform that expands module nodes
// into their internal graphs.

#include "runtime/graph/subgraph_module.h"
#include "runtime/graph/graph.h"
#include <cstdio>
#include <cstring>
#include "test_helpers.h"

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
    std::string tmp_path = "/tmp/test_synth.vivid-module.json";
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

    std::remove(tmp_path.c_str());
}

static void test_parse_effects_module() {
    std::fprintf(stderr, "\n--- parse: effects chain module ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = "/tmp/test_fx.vivid-module.json";
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

    std::remove(tmp_path.c_str());
}

// ---------------------------------------------------------------------------
// Flatten tests
// ---------------------------------------------------------------------------

static void test_flatten_single_instance() {
    std::fprintf(stderr, "\n--- flatten: single module instance ---\n");

    // Register the module
    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = "/tmp/test_flatten.vivid-module.json";
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
    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry);

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

    std::remove(tmp_path.c_str());
}

static void test_flatten_multiple_instances() {
    std::fprintf(stderr, "\n--- flatten: multiple instances of same module ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = "/tmp/test_multi.vivid-module.json";
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

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry);

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

    std::remove(tmp_path.c_str());
}

static void test_flatten_no_modules() {
    std::fprintf(stderr, "\n--- flatten: graph with no modules (passthrough) ---\n");

    vivid::SubgraphModuleRegistry registry;  // empty

    vivid::Graph parent;
    parent.add_node("a", "TypeA", {{"x", 1.0f}});
    parent.add_node("b", "TypeB", {});
    parent.add_connection("a", "out", "b", "in");

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry);

    check(flat.nodes().size() == 2, "nodes preserved");
    check(flat.connections().size() == 1, "connections preserved");
    check(flat.find_node("a") != nullptr, "node a preserved");
    check(flat.find_node("b") != nullptr, "node b preserved");
}

static void test_flatten_effects_chain() {
    std::fprintf(stderr, "\n--- flatten: effects chain (audio in -> audio out) ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = "/tmp/test_fx_flatten.vivid-module.json";
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

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry);

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

    std::remove(tmp_path.c_str());
}

static void test_registry_type_names() {
    std::fprintf(stderr, "\n--- registry: type_names ---\n");

    vivid::SubgraphModuleRegistry registry;

    std::string tmp1 = "/tmp/test_type1.vivid-module.json";
    std::string tmp2 = "/tmp/test_type2.vivid-module.json";
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

    std::remove(tmp1.c_str());
    std::remove(tmp2.c_str());
}

static void test_flatten_midi_mapping_remap() {
    std::fprintf(stderr, "\n--- flatten: MIDI mapping remapped through param binding ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = "/tmp/test_midi.vivid-module.json";
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

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry);

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

    std::remove(tmp_path.c_str());
}

static void test_flatten_variation_remap() {
    std::fprintf(stderr, "\n--- flatten: variation params remapped through param binding ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp_path = "/tmp/test_var.vivid-module.json";
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

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry);

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

    std::remove(tmp_path.c_str());
}

static void test_flatten_cross_instance_connection() {
    std::fprintf(stderr, "\n--- flatten: connection between two module instances ---\n");

    vivid::SubgraphModuleRegistry registry;
    std::string tmp1 = "/tmp/test_cross1.vivid-module.json";
    std::string tmp2 = "/tmp/test_cross2.vivid-module.json";
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

    vivid::Graph flat = vivid::flatten_subgraphs(parent, registry);

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

    std::remove(tmp1.c_str());
    std::remove(tmp2.c_str());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::fprintf(stderr, "=== test_subgraph_module ===\n");

    test_parse_simple_module();
    test_parse_effects_module();
    test_flatten_single_instance();
    test_flatten_multiple_instances();
    test_flatten_no_modules();
    test_flatten_effects_chain();
    test_registry_type_names();
    test_flatten_midi_mapping_remap();
    test_flatten_variation_remap();
    test_flatten_cross_instance_connection();

    std::fprintf(stderr, "\n=== %s (%d failure%s) ===\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
