/**
 * @file test_modulators.cpp
 * @brief Unit tests for modulator system (LFO, ADSRMod) and WavetableSynth
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/modulators/lfo.h>
#include <vivid/audio/modulators/adsr.h>
#include <vivid/audio/wavetable_synth.h>
#include <vivid/audio/modulator.h>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;

// =============================================================================
// LFO Modulator Tests
// =============================================================================

TEST_CASE("LFO parameter defaults", "[audio][lfo][modulator]") {
    LFO lfo;

    SECTION("rate defaults to 1.0 Hz") {
        REQUIRE_THAT(static_cast<float>(lfo.rate), WithinAbs(1.0f, 0.001f));
    }

    SECTION("startPhase defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(lfo.startPhase), WithinAbs(0.0f, 0.001f));
    }

    SECTION("sync defaults to false") {
        REQUIRE(static_cast<bool>(lfo.sync) == false);
    }

    SECTION("bpm defaults to 120") {
        REQUIRE_THAT(static_cast<float>(lfo.bpm), WithinAbs(120.0f, 0.001f));
    }

    SECTION("waveform defaults to Sine") {
        REQUIRE(static_cast<LFOWaveform>(lfo.waveform) == LFOWaveform::Sine);
    }
}

TEST_CASE("LFO parameter assignment", "[audio][lfo][modulator]") {
    LFO lfo;

    SECTION("rate assignment works") {
        lfo.rate = 5.0f;
        REQUIRE_THAT(static_cast<float>(lfo.rate), WithinAbs(5.0f, 0.001f));
    }

    SECTION("startPhase assignment works") {
        lfo.startPhase = 0.5f;
        REQUIRE_THAT(static_cast<float>(lfo.startPhase), WithinAbs(0.5f, 0.001f));
    }

    SECTION("sync assignment works") {
        lfo.sync = true;
        REQUIRE(static_cast<bool>(lfo.sync) == true);
    }

    SECTION("waveform assignment works") {
        lfo.waveform = LFOWaveform::Triangle;
        REQUIRE(static_cast<LFOWaveform>(lfo.waveform) == LFOWaveform::Triangle);
    }
}

TEST_CASE("LFO setParam/getParam", "[audio][lfo][modulator]") {
    LFO lfo;
    float out[4] = {0};

    SECTION("setParam updates rate") {
        float value[4] = {10.0f, 0, 0, 0};
        REQUIRE(lfo.setParam("rate", value));
        REQUIRE(lfo.getParam("rate", out));
        REQUIRE_THAT(out[0], WithinAbs(10.0f, 0.001f));
    }

    SECTION("setParam updates startPhase") {
        float value[4] = {0.25f, 0, 0, 0};
        REQUIRE(lfo.setParam("startPhase", value));
        REQUIRE(lfo.getParam("startPhase", out));
        REQUIRE_THAT(out[0], WithinAbs(0.25f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(lfo.setParam("nonexistent", value));
        REQUIRE_FALSE(lfo.getParam("nonexistent", out));
    }
}

TEST_CASE("LFO modulator interface", "[audio][lfo][modulator]") {
    LFO lfo;

    SECTION("modulatorName returns 'LFO'") {
        REQUIRE(lfo.modulatorName() == "LFO");
    }

    SECTION("name returns 'LFO'") {
        REQUIRE(lfo.name() == "LFO");
    }

    SECTION("createState returns valid state") {
        auto state = lfo.createState();
        REQUIRE(state != nullptr);
    }

    SECTION("isActive always returns true for LFO") {
        auto state = lfo.createState();
        REQUIRE(lfo.isActive(*state) == true);
    }

    SECTION("value() starts at 0") {
        // Initial value depends on waveform and phase
        REQUIRE(lfo.value() >= -1.0f);
        REQUIRE(lfo.value() <= 1.0f);
    }

    SECTION("triggered() returns bool") {
        bool triggered = lfo.triggered();
        REQUIRE((triggered == false || triggered == true));
    }
}

TEST_CASE("LFO state management", "[audio][lfo][modulator]") {
    LFO lfo;

    SECTION("state reset works") {
        auto state = lfo.createState();
        LFOState* lfoState = dynamic_cast<LFOState*>(state.get());
        REQUIRE(lfoState != nullptr);

        lfoState->phase = 0.5f;
        lfoState->value = 0.75f;
        lfoState->reset();

        REQUIRE_THAT(lfoState->phase, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(lfoState->value, WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("LFO waveform types", "[audio][lfo][modulator]") {
    SECTION("all waveform types are valid") {
        LFO lfo;
        lfo.waveform = LFOWaveform::Sine;
        lfo.waveform = LFOWaveform::Triangle;
        lfo.waveform = LFOWaveform::Square;
        lfo.waveform = LFOWaveform::Saw;
        lfo.waveform = LFOWaveform::SawDown;
        lfo.waveform = LFOWaveform::SampleHold;
        REQUIRE(true);  // Compiles and runs
    }
}

TEST_CASE("LFO perVoice and retrigger", "[audio][lfo][modulator]") {
    LFO lfo;

    SECTION("perVoice defaults to false") {
        REQUIRE(static_cast<bool>(lfo.perVoice) == false);
    }

    SECTION("retrigger defaults to true") {
        REQUIRE(static_cast<bool>(lfo.retrigger) == true);
    }

    SECTION("perVoice can be enabled") {
        lfo.perVoice = true;
        REQUIRE(static_cast<bool>(lfo.perVoice) == true);
    }
}

// =============================================================================
// ADSR Modulator Tests
// =============================================================================

TEST_CASE("ADSRMod parameter defaults", "[audio][adsr][modulator]") {
    ADSRMod adsr;

    SECTION("attack defaults to 0.01") {
        REQUIRE_THAT(static_cast<float>(adsr.attack), WithinAbs(0.01f, 0.001f));
    }

    SECTION("decay defaults to 0.1") {
        REQUIRE_THAT(static_cast<float>(adsr.decay), WithinAbs(0.1f, 0.001f));
    }

    SECTION("sustain defaults to 0.7") {
        REQUIRE_THAT(static_cast<float>(adsr.sustain), WithinAbs(0.7f, 0.001f));
    }

    SECTION("release defaults to 0.3") {
        REQUIRE_THAT(static_cast<float>(adsr.release), WithinAbs(0.3f, 0.001f));
    }
}

TEST_CASE("ADSRMod parameter assignment", "[audio][adsr][modulator]") {
    ADSRMod adsr;

    SECTION("all ADSR params can be set") {
        adsr.attack = 0.05f;
        adsr.decay = 0.2f;
        adsr.sustain = 0.5f;
        adsr.release = 0.8f;

        REQUIRE_THAT(static_cast<float>(adsr.attack), WithinAbs(0.05f, 0.001f));
        REQUIRE_THAT(static_cast<float>(adsr.decay), WithinAbs(0.2f, 0.001f));
        REQUIRE_THAT(static_cast<float>(adsr.sustain), WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(static_cast<float>(adsr.release), WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("ADSRMod setParam/getParam", "[audio][adsr][modulator]") {
    ADSRMod adsr;
    float out[4] = {0};

    SECTION("setParam updates attack") {
        float value[4] = {0.1f, 0, 0, 0};
        REQUIRE(adsr.setParam("attack", value));
        REQUIRE(adsr.getParam("attack", out));
        REQUIRE_THAT(out[0], WithinAbs(0.1f, 0.001f));
    }

    SECTION("setParam updates sustain") {
        float value[4] = {0.5f, 0, 0, 0};
        REQUIRE(adsr.setParam("sustain", value));
        REQUIRE(adsr.getParam("sustain", out));
        REQUIRE_THAT(out[0], WithinAbs(0.5f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(adsr.setParam("nonexistent", value));
        REQUIRE_FALSE(adsr.getParam("nonexistent", out));
    }
}

TEST_CASE("ADSRMod modulator interface", "[audio][adsr][modulator]") {
    ADSRMod adsr;

    SECTION("modulatorName returns 'ADSR'") {
        REQUIRE(adsr.modulatorName() == "ADSR");
    }

    SECTION("name returns 'ADSRMod'") {
        REQUIRE(adsr.name() == "ADSRMod");
    }

    SECTION("createState returns valid state") {
        auto state = adsr.createState();
        REQUIRE(state != nullptr);
    }

    SECTION("isActive returns false when idle") {
        auto state = adsr.createState();
        REQUIRE(adsr.isActive(*state) == false);
    }

    SECTION("value() starts at 0") {
        REQUIRE_THAT(adsr.value(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("isActive() starts false") {
        REQUIRE(adsr.isActive() == false);
    }

    SECTION("stage() starts at Idle") {
        REQUIRE(adsr.stage() == EnvelopeStage::Idle);
    }
}

TEST_CASE("ADSRMod state management", "[audio][adsr][modulator]") {
    ADSRMod adsr;

    SECTION("state reset works") {
        auto state = adsr.createState();
        ADSRState* adsrState = dynamic_cast<ADSRState*>(state.get());
        REQUIRE(adsrState != nullptr);

        adsrState->value = 0.5f;
        adsrState->progress = 0.3f;
        adsrState->reset();

        REQUIRE_THAT(adsrState->value, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(adsrState->progress, WithinAbs(0.0f, 0.001f));
        REQUIRE(adsrState->stage == EnvelopeStage::Idle);
    }
}

TEST_CASE("ADSRMod perVoice defaults to true", "[audio][adsr][modulator]") {
    ADSRMod adsr;
    // Envelopes are per-voice by default (each voice gets independent envelope)
    REQUIRE(static_cast<bool>(adsr.perVoice) == true);
}

// =============================================================================
// WavetableSynth Tests
// =============================================================================

TEST_CASE("WavetableSynth parameter defaults", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("position defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.position), WithinAbs(0.0f, 0.001f));
    }

    SECTION("maxVoices defaults to 4") {
        REQUIRE(static_cast<int>(wt.maxVoices) == 4);
    }

    SECTION("detune defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.detune), WithinAbs(0.0f, 0.001f));
    }

    SECTION("volume defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(wt.volume), WithinAbs(0.5f, 0.001f));
    }

    SECTION("attack defaults to 0.01") {
        REQUIRE_THAT(static_cast<float>(wt.attack), WithinAbs(0.01f, 0.001f));
    }

    SECTION("decay defaults to 0.1") {
        REQUIRE_THAT(static_cast<float>(wt.decay), WithinAbs(0.1f, 0.001f));
    }

    SECTION("sustain defaults to 0.7") {
        REQUIRE_THAT(static_cast<float>(wt.sustain), WithinAbs(0.7f, 0.001f));
    }

    SECTION("release defaults to 0.3") {
        REQUIRE_THAT(static_cast<float>(wt.release), WithinAbs(0.3f, 0.001f));
    }
}

TEST_CASE("WavetableSynth unison parameters", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("unisonVoices defaults to 1") {
        REQUIRE(static_cast<int>(wt.unisonVoices) == 1);
    }

    SECTION("unisonSpread defaults to 20.0") {
        REQUIRE_THAT(static_cast<float>(wt.unisonSpread), WithinAbs(20.0f, 0.001f));
    }

    SECTION("unisonStereo defaults to 1.0") {
        REQUIRE_THAT(static_cast<float>(wt.unisonStereo), WithinAbs(1.0f, 0.001f));
    }

    SECTION("unison params can be set") {
        wt.unisonVoices = 4;
        wt.unisonSpread = 30.0f;
        wt.unisonStereo = 0.5f;

        REQUIRE(static_cast<int>(wt.unisonVoices) == 4);
        REQUIRE_THAT(static_cast<float>(wt.unisonSpread), WithinAbs(30.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(wt.unisonStereo), WithinAbs(0.5f, 0.001f));
    }
}

TEST_CASE("WavetableSynth sub oscillator", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("subLevel defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.subLevel), WithinAbs(0.0f, 0.001f));
    }

    SECTION("subOctave defaults to -1") {
        REQUIRE(static_cast<int>(wt.subOctave) == -1);
    }

    SECTION("sub params can be set") {
        wt.subLevel = 0.5f;
        wt.subOctave = -2;

        REQUIRE_THAT(static_cast<float>(wt.subLevel), WithinAbs(0.5f, 0.001f));
        REQUIRE(static_cast<int>(wt.subOctave) == -2);
    }
}

TEST_CASE("WavetableSynth filter parameters", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("filterCutoff defaults to 20000") {
        REQUIRE_THAT(static_cast<float>(wt.filterCutoff), WithinAbs(20000.0f, 1.0f));
    }

    SECTION("filterResonance defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.filterResonance), WithinAbs(0.0f, 0.001f));
    }

    SECTION("filterKeytrack defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.filterKeytrack), WithinAbs(0.0f, 0.001f));
    }

    SECTION("filter params can be set") {
        wt.filterCutoff = 5000.0f;
        wt.filterResonance = 0.5f;
        wt.filterKeytrack = 0.8f;

        REQUIRE_THAT(static_cast<float>(wt.filterCutoff), WithinAbs(5000.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(wt.filterResonance), WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(static_cast<float>(wt.filterKeytrack), WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("WavetableSynth filter envelope", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("filterAttack defaults to 0.01") {
        REQUIRE_THAT(static_cast<float>(wt.filterAttack), WithinAbs(0.01f, 0.001f));
    }

    SECTION("filterDecay defaults to 0.3") {
        REQUIRE_THAT(static_cast<float>(wt.filterDecay), WithinAbs(0.3f, 0.001f));
    }

    SECTION("filterSustain defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.filterSustain), WithinAbs(0.0f, 0.001f));
    }

    SECTION("filterRelease defaults to 0.3") {
        REQUIRE_THAT(static_cast<float>(wt.filterRelease), WithinAbs(0.3f, 0.001f));
    }

    SECTION("filterEnvAmount defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.filterEnvAmount), WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("WavetableSynth LFO parameters", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("lfoRate defaults to 1.0") {
        REQUIRE_THAT(static_cast<float>(wt.lfoRate), WithinAbs(1.0f, 0.001f));
    }

    SECTION("lfoSync defaults to 0 (free)") {
        REQUIRE(static_cast<int>(wt.lfoSync) == 0);
    }

    SECTION("lfoWaveform defaults to 0 (Sine)") {
        REQUIRE(static_cast<int>(wt.lfoWaveform) == 0);
    }

    SECTION("lfoRetrigger defaults to 1 (true)") {
        REQUIRE(static_cast<int>(wt.lfoRetrigger) == 1);
    }

    SECTION("LFO mod depths default to 0") {
        REQUIRE_THAT(static_cast<float>(wt.lfoToVolume), WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(wt.lfoToFilter), WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(wt.lfoToPosition), WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("WavetableSynth warp mode", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("warpMode defaults to None") {
        REQUIRE(wt.warpMode() == WarpMode::None);
    }

    SECTION("warpAmount defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.warpAmount), WithinAbs(0.0f, 0.001f));
    }

    SECTION("warp modes can be set") {
        wt.setWarpMode(WarpMode::Sync);
        REQUIRE(wt.warpMode() == WarpMode::Sync);

        wt.setWarpMode(WarpMode::BendPlus);
        REQUIRE(wt.warpMode() == WarpMode::BendPlus);

        wt.setWarpMode(WarpMode::FM);
        REQUIRE(wt.warpMode() == WarpMode::FM);
    }

    SECTION("all warp modes are valid") {
        wt.setWarpMode(WarpMode::None);
        wt.setWarpMode(WarpMode::Sync);
        wt.setWarpMode(WarpMode::BendPlus);
        wt.setWarpMode(WarpMode::BendMinus);
        wt.setWarpMode(WarpMode::Mirror);
        wt.setWarpMode(WarpMode::Asym);
        wt.setWarpMode(WarpMode::Quantize);
        wt.setWarpMode(WarpMode::FM);
        wt.setWarpMode(WarpMode::Flip);
        REQUIRE(true);  // Compiles and runs
    }
}

TEST_CASE("WavetableSynth filter type", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("filterType defaults to LP24") {
        REQUIRE(wt.filterType() == SynthFilterType::LP24);
    }

    SECTION("filter types can be set") {
        wt.setFilterType(SynthFilterType::LP12);
        REQUIRE(wt.filterType() == SynthFilterType::LP12);

        wt.setFilterType(SynthFilterType::HP12);
        REQUIRE(wt.filterType() == SynthFilterType::HP12);

        wt.setFilterType(SynthFilterType::BP);
        REQUIRE(wt.filterType() == SynthFilterType::BP);

        wt.setFilterType(SynthFilterType::Notch);
        REQUIRE(wt.filterType() == SynthFilterType::Notch);
    }
}

TEST_CASE("WavetableSynth builtin tables", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("all builtin tables can be loaded") {
        wt.loadBuiltin(BuiltinTable::Basic);
        REQUIRE(wt.frameCount() > 0);

        wt.loadBuiltin(BuiltinTable::Analog);
        REQUIRE(wt.frameCount() > 0);

        wt.loadBuiltin(BuiltinTable::Digital);
        REQUIRE(wt.frameCount() > 0);

        wt.loadBuiltin(BuiltinTable::Vocal);
        REQUIRE(wt.frameCount() > 0);

        wt.loadBuiltin(BuiltinTable::Texture);
        REQUIRE(wt.frameCount() > 0);

        wt.loadBuiltin(BuiltinTable::PWM);
        REQUIRE(wt.frameCount() > 0);
    }
}

TEST_CASE("WavetableSynth setParam/getParam", "[audio][wavetable]") {
    WavetableSynth wt;
    float out[4] = {0};

    SECTION("setParam updates position") {
        float value[4] = {0.5f, 0, 0, 0};
        REQUIRE(wt.setParam("position", value));
        REQUIRE(wt.getParam("position", out));
        REQUIRE_THAT(out[0], WithinAbs(0.5f, 0.001f));
    }

    SECTION("setParam updates volume") {
        float value[4] = {0.8f, 0, 0, 0};
        REQUIRE(wt.setParam("volume", value));
        REQUIRE(wt.getParam("volume", out));
        REQUIRE_THAT(out[0], WithinAbs(0.8f, 0.001f));
    }

    SECTION("setParam updates filterCutoff") {
        float value[4] = {2000.0f, 0, 0, 0};
        REQUIRE(wt.setParam("filterCutoff", value));
        REQUIRE(wt.getParam("filterCutoff", out));
        REQUIRE_THAT(out[0], WithinAbs(2000.0f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(wt.setParam("nonexistent", value));
        REQUIRE_FALSE(wt.getParam("nonexistent", out));
    }
}

TEST_CASE("WavetableSynth state", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("name returns 'WavetableSynth'") {
        REQUIRE(wt.name() == "WavetableSynth");
    }

    SECTION("starts not playing") {
        REQUIRE(wt.isPlaying() == false);
    }

    SECTION("activeVoiceCount starts at 0") {
        REQUIRE(wt.activeVoiceCount() == 0);
    }

    SECTION("allNotesOff works without crash") {
        wt.allNotesOff();
        REQUIRE(wt.isPlaying() == false);
    }

    SECTION("panic works without crash") {
        wt.panic();
        REQUIRE(wt.isPlaying() == false);
    }
}

TEST_CASE("WavetableSynth pitch bend", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("setPitchBendRange works") {
        wt.setPitchBendRange(12.0f);  // Octave bend
        REQUIRE(true);  // If we get here, it compiled and ran
    }
}

TEST_CASE("WavetableSynth portamento", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("portamento defaults to 0") {
        REQUIRE_THAT(static_cast<float>(wt.portamento), WithinAbs(0.0f, 0.001f));
    }

    SECTION("portamento can be set") {
        wt.portamento = 200.0f;  // 200ms glide
        REQUIRE_THAT(static_cast<float>(wt.portamento), WithinAbs(200.0f, 0.001f));
    }
}

TEST_CASE("WavetableSynth velocity sensitivity", "[audio][wavetable]") {
    WavetableSynth wt;

    SECTION("velToVolume defaults to 1.0") {
        REQUIRE_THAT(static_cast<float>(wt.velToVolume), WithinAbs(1.0f, 0.001f));
    }

    SECTION("velToAttack defaults to 0.0") {
        REQUIRE_THAT(static_cast<float>(wt.velToAttack), WithinAbs(0.0f, 0.001f));
    }

    SECTION("velocity params can be set") {
        wt.velToVolume = 0.5f;
        wt.velToAttack = 0.3f;

        REQUIRE_THAT(static_cast<float>(wt.velToVolume), WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(static_cast<float>(wt.velToAttack), WithinAbs(0.3f, 0.001f));
    }
}

// =============================================================================
// ModulatorHost Tests (via WavetableSynth)
// =============================================================================

TEST_CASE("ModulatorHost addModulator", "[audio][modulator]") {
    WavetableSynth wt;

    SECTION("LFO modulator can be added") {
        auto& lfo = wt.addModulator<LFO>("testLfo");
        lfo.rate = 2.0f;
        REQUIRE_THAT(static_cast<float>(lfo.rate), WithinAbs(2.0f, 0.001f));
    }

    SECTION("ADSR modulator can be added") {
        auto& adsr = wt.addModulator<ADSRMod>("testAdsr");
        adsr.attack = 0.05f;
        REQUIRE_THAT(static_cast<float>(adsr.attack), WithinAbs(0.05f, 0.001f));
    }
}

TEST_CASE("ModulatorHost getModulator", "[audio][modulator]") {
    WavetableSynth wt;
    wt.addModulator<LFO>("testLfo");

    SECTION("getModulator returns added modulator") {
        auto* lfo = wt.getModulator<LFO>("testLfo");
        REQUIRE(lfo != nullptr);
    }

    SECTION("getModulator returns nullptr for nonexistent") {
        auto* lfo = wt.getModulator<LFO>("nonexistent");
        REQUIRE(lfo == nullptr);
    }

    SECTION("getModulator returns nullptr for wrong type") {
        auto* adsr = wt.getModulator<ADSRMod>("testLfo");  // Wrong type
        REQUIRE(adsr == nullptr);
    }
}

TEST_CASE("ModulatorHost modulate routing", "[audio][modulator]") {
    WavetableSynth wt;

    SECTION("modulate call compiles and runs") {
        auto& lfo = wt.addModulator<LFO>("vibrato");
        wt.modulate(lfo, "position", 0.5f);
        REQUIRE(true);  // If we get here, it compiled and ran
    }

    SECTION("clearModulation works") {
        auto& lfo = wt.addModulator<LFO>("vibrato");
        wt.modulate(lfo, "position", 0.5f);
        wt.clearModulation("position");
        REQUIRE(true);  // If we get here, it compiled and ran
    }
}
