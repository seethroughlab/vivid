/**
 * @file test_snapshot.cpp
 * @brief Unit tests for SnapshotStore (capture, recall, crossfade, persistence)
 *
 * Uses real Chain + operators (Noise, Blur, SolidColor) which work without GPU context,
 * as proven in tests/integration/test_chain.cpp.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/snapshot.h>
#include <vivid/chain.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/blur.h>
#include <vivid/effects/solid_color.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <memory>

using namespace vivid;
using namespace vivid::effects;
using Catch::Matchers::WithinAbs;
using json = nlohmann::json;

// Helper: build a chain with Noise + Blur + SolidColor
static std::unique_ptr<Chain> makeTestChain() {
    auto chain = std::make_unique<Chain>();
    auto& noise = chain->add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.5f;
    noise.octaves = 4;
    noise.colorNoise = false;

    auto& blur = chain->add<Blur>("blur");
    blur.radius = 5.0f;

    auto& solid = chain->add<SolidColor>("solid");
    solid.color.set(0.0f, 0.0f, 0.0f, 1.0f);

    return chain;
}

// Helper: cleanup a temp file
static void removeFile(const std::string& path) {
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

TEST_CASE("SnapshotStore capture", "[unit][snapshot]") {
    auto chain = makeTestChain();
    SnapshotStore store;

    SECTION("returns sequential indices") {
        REQUIRE(store.capture("A", *chain) == 0);
        REQUIRE(store.capture("B", *chain) == 1);
        REQUIRE(store.capture("C", *chain) == 2);
    }

    SECTION("stores correct name") {
        store.capture("My Snapshot", *chain);
        auto* snap = store.get(0);
        REQUIRE(snap != nullptr);
        REQUIRE(snap->name == "My Snapshot");
    }

    SECTION("captures operator params") {
        store.capture("test", *chain);
        auto* snap = store.get(0);
        REQUIRE(snap != nullptr);

        // Noise scale should be 4.0
        auto noiseIt = snap->values.find("noise");
        REQUIRE(noiseIt != snap->values.end());
        auto scaleIt = noiseIt->second.find("scale");
        REQUIRE(scaleIt != noiseIt->second.end());
        REQUIRE(scaleIt->second[0] == 4.0f);

        // Blur radius should be 5.0
        auto blurIt = snap->values.find("blur");
        REQUIRE(blurIt != snap->values.end());
        auto radiusIt = blurIt->second.find("radius");
        REQUIRE(radiusIt != blurIt->second.end());
        REQUIRE(radiusIt->second[0] == 5.0f);
    }

    SECTION("size reflects capture count") {
        REQUIRE(store.size() == 0);
        store.capture("A", *chain);
        REQUIRE(store.size() == 1);
        store.capture("B", *chain);
        REQUIRE(store.size() == 2);
    }
}

// ---------------------------------------------------------------------------
// Recall (hard cut)
// ---------------------------------------------------------------------------

TEST_CASE("SnapshotStore recall hard cut", "[unit][snapshot]") {
    auto chain = makeTestChain();
    SnapshotStore store;

    // Capture initial state (scale=4)
    store.capture("initial", *chain);

    // Change params
    auto& noise = chain->get<Noise>("noise");
    noise.scale = 12.0f;

    // Capture altered state
    store.capture("altered", *chain);

    SECTION("applies values immediately") {
        // Currently at scale=12, recall snapshot 0 (scale=4)
        store.recall(0, *chain);

        float out[4] = {};
        noise.getParam("scale", out);
        REQUIRE(out[0] == 4.0f);
    }

    SECTION("sets activeIndex") {
        REQUIRE(store.activeIndex() == -1);
        store.recall(0, *chain);
        REQUIRE(store.activeIndex() == 0);
        store.recall(1, *chain);
        REQUIRE(store.activeIndex() == 1);
    }

    SECTION("out-of-range index is a no-op") {
        store.recall(0, *chain);
        REQUIRE(store.activeIndex() == 0);

        store.recall(99, *chain);
        REQUIRE(store.activeIndex() == 0);  // unchanged

        store.recall(-1, *chain);
        REQUIRE(store.activeIndex() == 0);  // unchanged
    }

    SECTION("does not start crossfade") {
        store.recall(0, *chain);
        REQUIRE_FALSE(store.isCrossfading());
    }
}

// ---------------------------------------------------------------------------
// Recall (crossfade)
// ---------------------------------------------------------------------------

TEST_CASE("SnapshotStore crossfade", "[unit][snapshot]") {
    auto chain = makeTestChain();
    SnapshotStore store;

    auto& noise = chain->get<Noise>("noise");

    // Snapshot 0: scale=4
    noise.scale = 4.0f;
    store.capture("A", *chain);

    // Snapshot 1: scale=12
    noise.scale = 12.0f;
    store.capture("B", *chain);

    // Set current state to snapshot 0
    store.recall(0, *chain);

    SECTION("isCrossfading becomes true") {
        store.recall(1, *chain, 2.0f);
        REQUIRE(store.isCrossfading());
    }

    SECTION("update advances crossfadeProgress") {
        store.recall(1, *chain, 2.0f);  // 2 second crossfade
        REQUIRE(store.crossfadeProgress() == 0.0f);

        store.update(1.0f, *chain);  // 1s into 2s = 50%
        REQUIRE_THAT(store.crossfadeProgress(), WithinAbs(0.5, 0.001));
    }

    SECTION("float params interpolate linearly") {
        store.recall(1, *chain, 2.0f);
        store.update(1.0f, *chain);  // t=0.5 → lerp(4, 12, 0.5) = 8

        float out[4] = {};
        noise.getParam("scale", out);
        REQUIRE_THAT(static_cast<double>(out[0]), WithinAbs(8.0, 0.01));
    }

    SECTION("crossfade completes") {
        store.recall(1, *chain, 1.0f);
        store.update(1.0f, *chain);  // exactly 1s into 1s

        REQUIRE_FALSE(store.isCrossfading());
        REQUIRE(store.activeIndex() == 1);

        float out[4] = {};
        noise.getParam("scale", out);
        REQUIRE(out[0] == 12.0f);
    }

    SECTION("overshoot clamps to target") {
        store.recall(1, *chain, 1.0f);
        store.update(5.0f, *chain);  // way past duration

        REQUIRE_FALSE(store.isCrossfading());
        REQUIRE(store.activeIndex() == 1);

        float out[4] = {};
        noise.getParam("scale", out);
        REQUIRE(out[0] == 12.0f);
    }

    SECTION("update is no-op when not crossfading") {
        // Not crossfading, update should do nothing
        store.update(1.0f, *chain);
        REQUIRE_FALSE(store.isCrossfading());

        // Params unchanged
        float out[4] = {};
        noise.getParam("scale", out);
        REQUIRE(out[0] == 4.0f);
    }
}

// ---------------------------------------------------------------------------
// Crossfade with easing
// ---------------------------------------------------------------------------

TEST_CASE("SnapshotStore crossfade with easing", "[unit][snapshot]") {
    auto chain = makeTestChain();
    SnapshotStore store;

    auto& noise = chain->get<Noise>("noise");

    // Snapshot 0: scale=0
    noise.scale = 0.0f;
    store.capture("A", *chain);

    // Snapshot 1: scale=100
    noise.scale = 100.0f;
    store.capture("B", *chain);

    // Reset to snapshot 0
    store.recall(0, *chain);

    SECTION("EaseIn produces slower-than-linear start") {
        store.recall(1, *chain, 2.0f, EasingCurve::easeIn());
        store.update(1.0f, *chain);  // t=0.5, eased=0.25

        float out[4] = {};
        noise.getParam("scale", out);
        // lerp(0, 100, 0.25) = 25
        REQUIRE_THAT(static_cast<double>(out[0]), WithinAbs(25.0, 0.1));
    }

    SECTION("EaseOut produces faster-than-linear start") {
        store.recall(1, *chain, 2.0f, EasingCurve::easeOut());
        store.update(1.0f, *chain);  // t=0.5, eased=0.75

        float out[4] = {};
        noise.getParam("scale", out);
        // lerp(0, 100, 0.75) = 75
        REQUIRE_THAT(static_cast<double>(out[0]), WithinAbs(75.0, 0.1));
    }

    SECTION("eased crossfade still completes at exact target") {
        store.recall(1, *chain, 1.0f, EasingCurve::easeInOut());
        store.update(1.0f, *chain);

        REQUIRE_FALSE(store.isCrossfading());
        float out[4] = {};
        noise.getParam("scale", out);
        REQUIRE(out[0] == 100.0f);
    }
}

// ---------------------------------------------------------------------------
// Interpolation types
// ---------------------------------------------------------------------------

TEST_CASE("SnapshotStore interpolation types", "[unit][snapshot]") {
    auto chain = makeTestChain();
    SnapshotStore store;

    SECTION("bool params snap at midpoint") {
        auto& noise = chain->get<Noise>("noise");

        noise.colorNoise = false;
        store.capture("A", *chain);

        noise.colorNoise = true;
        store.capture("B", *chain);

        store.recall(0, *chain);  // colorNoise=false
        store.recall(1, *chain, 2.0f);

        // Before midpoint (t=0.25): should still be false (0)
        store.update(0.5f, *chain);
        float out[4] = {};
        noise.getParam("colorNoise", out);
        REQUIRE(out[0] == 0.0f);

        // After midpoint (t=0.75): should be true (1)
        store.update(1.0f, *chain);  // now at t=0.75
        noise.getParam("colorNoise", out);
        REQUIRE(out[0] == 1.0f);
    }

    SECTION("int params lerp and round") {
        auto& noise = chain->get<Noise>("noise");

        noise.octaves = 2;
        store.capture("A", *chain);

        noise.octaves = 8;
        store.capture("B", *chain);

        store.recall(0, *chain);
        store.recall(1, *chain, 2.0f);
        store.update(1.0f, *chain);  // t=0.5 → lerp(2,8,0.5) = 5

        float out[4] = {};
        noise.getParam("octaves", out);
        REQUIRE(out[0] == 5.0f);
    }

    SECTION("color params lerp 4 components") {
        auto& solid = chain->get<SolidColor>("solid");

        solid.color.set(0.0f, 0.0f, 0.0f, 0.0f);
        store.capture("black", *chain);

        solid.color.set(1.0f, 1.0f, 1.0f, 1.0f);
        store.capture("white", *chain);

        store.recall(0, *chain);
        store.recall(1, *chain, 2.0f);
        store.update(1.0f, *chain);  // t=0.5 → gray

        float out[4] = {};
        chain->getByName("solid")->getParam("color", out);
        REQUIRE_THAT(static_cast<double>(out[0]), WithinAbs(0.5, 0.01));
        REQUIRE_THAT(static_cast<double>(out[1]), WithinAbs(0.5, 0.01));
        REQUIRE_THAT(static_cast<double>(out[2]), WithinAbs(0.5, 0.01));
        REQUIRE_THAT(static_cast<double>(out[3]), WithinAbs(0.5, 0.01));
    }
}

// ---------------------------------------------------------------------------
// Management
// ---------------------------------------------------------------------------

TEST_CASE("SnapshotStore management", "[unit][snapshot]") {
    auto chain = makeTestChain();
    SnapshotStore store;

    store.capture("A", *chain);
    store.capture("B", *chain);
    store.capture("C", *chain);

    SECTION("get returns nullptr for out-of-range") {
        REQUIRE(store.get(-1) == nullptr);
        REQUIRE(store.get(3) == nullptr);
        REQUIRE(store.get(99) == nullptr);
    }

    SECTION("list returns all snapshots") {
        auto& list = store.list();
        REQUIRE(list.size() == 3);
        REQUIRE(list[0].name == "A");
        REQUIRE(list[1].name == "B");
        REQUIRE(list[2].name == "C");
    }

    SECTION("remove shifts indices") {
        store.recall(2, *chain);  // active = 2
        REQUIRE(store.activeIndex() == 2);

        store.remove(0);  // remove A, B→0, C→1
        REQUIRE(store.size() == 2);
        REQUIRE(store.get(0)->name == "B");
        REQUIRE(store.get(1)->name == "C");
        REQUIRE(store.activeIndex() == 1);  // was 2, shifted down
    }

    SECTION("removing active sets activeIndex to -1") {
        store.recall(1, *chain);
        REQUIRE(store.activeIndex() == 1);

        store.remove(1);
        REQUIRE(store.activeIndex() == -1);
    }

    SECTION("rename updates name") {
        store.rename(1, "Renamed");
        REQUIRE(store.get(1)->name == "Renamed");
    }

    SECTION("move reorders and adjusts activeIndex") {
        store.recall(0, *chain);  // active = 0 ("A")
        REQUIRE(store.activeIndex() == 0);

        store.move(0, 2);  // A moves to index 2: B, C, A
        REQUIRE(store.get(0)->name == "B");
        REQUIRE(store.get(1)->name == "C");
        REQUIRE(store.get(2)->name == "A");
        REQUIRE(store.activeIndex() == 2);  // follows the moved snapshot
    }
}

// ---------------------------------------------------------------------------
// JSON persistence
// ---------------------------------------------------------------------------

TEST_CASE("SnapshotStore JSON persistence", "[unit][snapshot]") {
    const std::string path = "/tmp/vivid_test_snapshots.json";

    SECTION("save + load round-trip preserves names and values") {
        auto chain = makeTestChain();
        SnapshotStore store;

        auto& noise = chain->get<Noise>("noise");
        noise.scale = 7.5f;
        store.capture("Look A", *chain);

        noise.scale = 15.0f;
        store.capture("Look B", *chain);

        REQUIRE(store.save(path));

        // Load into a fresh store
        SnapshotStore loaded;
        REQUIRE(loaded.load(path));

        REQUIRE(loaded.size() == 2);
        REQUIRE(loaded.get(0)->name == "Look A");
        REQUIRE(loaded.get(1)->name == "Look B");

        // Check param values round-tripped
        auto scaleA = loaded.get(0)->values.at("noise").at("scale");
        REQUIRE(scaleA[0] == 7.5f);

        auto scaleB = loaded.get(1)->values.at("noise").at("scale");
        REQUIRE(scaleB[0] == 15.0f);

        removeFile(path);
    }

    SECTION("load resets activeIndex and crossfading state") {
        auto chain = makeTestChain();
        SnapshotStore store;
        store.capture("A", *chain);
        store.recall(0, *chain);
        REQUIRE(store.activeIndex() == 0);

        REQUIRE(store.save(path));

        SnapshotStore loaded;
        REQUIRE(loaded.load(path));
        REQUIRE(loaded.activeIndex() == -1);
        REQUIRE_FALSE(loaded.isCrossfading());

        removeFile(path);
    }

    SECTION("load returns false for nonexistent file") {
        SnapshotStore store;
        REQUIRE_FALSE(store.load("/tmp/vivid_nonexistent_file.json"));
    }

    SECTION("load returns false for invalid JSON") {
        // Write garbage to file
        {
            std::ofstream f(path);
            f << "not valid json {{{";
        }

        SnapshotStore store;
        REQUIRE_FALSE(store.load(path));

        removeFile(path);
    }

    SECTION("load returns false for missing snapshots key") {
        {
            std::ofstream f(path);
            f << R"({"version": 1})";
        }

        SnapshotStore store;
        REQUIRE_FALSE(store.load(path));

        removeFile(path);
    }

    SECTION("save returns false for invalid path") {
        auto chain = makeTestChain();
        SnapshotStore store;
        store.capture("test", *chain);

        REQUIRE_FALSE(store.save("/nonexistent/deeply/nested/dir/file.json"));
    }

    SECTION("save produces valid JSON structure") {
        auto chain = makeTestChain();
        SnapshotStore store;
        store.capture("Test", *chain);

        REQUIRE(store.save(path));

        std::ifstream f(path);
        auto j = json::parse(f);
        REQUIRE(j.contains("snapshots"));
        REQUIRE(j["snapshots"].is_array());
        REQUIRE(j["snapshots"].size() == 1);
        REQUIRE(j["snapshots"][0]["name"] == "Test");
        REQUIRE(j["snapshots"][0]["values"].is_object());

        removeFile(path);
    }

    SECTION("load clears existing snapshots") {
        auto chain = makeTestChain();
        SnapshotStore store;
        store.capture("A", *chain);
        store.capture("B", *chain);
        store.capture("C", *chain);
        REQUIRE(store.size() == 3);

        // Save just one snapshot from another store
        SnapshotStore other;
        other.capture("Only", *chain);
        REQUIRE(other.save(path));

        // Load into the store that has 3 — should now have 1
        REQUIRE(store.load(path));
        REQUIRE(store.size() == 1);
        REQUIRE(store.get(0)->name == "Only");

        removeFile(path);
    }
}
