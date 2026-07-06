// Headless tests for the mapping bridge (app/src/mapping.h): the gamma shaping
// curve and the MappingRegistry value pipeline (clamp -> polarity -> curve ->
// range -> gain), plus connect/replace/disconnect semantics. mapping.h is
// header-only and stdlib-only, so this links nothing.
#include "mapping.h"
#include "test_helpers.h"

using vivid::Mapping;
using vivid::MappingRegistry;
using vivid::mapping_shape;

static void test_shape() {
    // curve 0 is the identity.
    CHECK_NEAR(mapping_shape(0.00f, 0.0f), 0.00f, 1e-6);
    CHECK_NEAR(mapping_shape(0.37f, 0.0f), 0.37f, 1e-6);
    CHECK_NEAR(mapping_shape(1.00f, 0.0f), 1.00f, 1e-6);

    // Endpoints are fixed for any curve.
    for (float c : {-1.0f, -0.5f, 0.5f, 1.0f}) {
        CHECK_NEAR(mapping_shape(0.0f, c), 0.0f, 1e-6);
        CHECK_NEAR(mapping_shape(1.0f, c), 1.0f, 1e-6);
    }

    // curve > 0 eases in (output below linear at the midpoint); < 0 eases out.
    CHECK(mapping_shape(0.5f, 1.0f) < 0.5f);
    CHECK(mapping_shape(0.5f, -1.0f) > 0.5f);

    // Monotonic increasing.
    CHECK(mapping_shape(0.3f, 1.0f) < mapping_shape(0.6f, 1.0f));

    // Negative input is floored to 0 (no NaN from pow of a negative base).
    CHECK_NEAR(mapping_shape(-0.5f, 1.0f), 0.0f, 1e-6);
}

static void test_sources() {
    MappingRegistry r;
    CHECK_NEAR(r.source_value("nope"), 0.0f, 1e-6);   // unknown -> 0
    r.set_source("a", 0.5f);
    CHECK_NEAR(r.source_value("a"), 0.5f, 1e-6);
    r.set_source("a", 0.8f);                            // overwrite
    CHECK_NEAR(r.source_value("a"), 0.8f, 1e-6);
}

static void test_connect_disconnect() {
    MappingRegistry r;
    CHECK(r.source_of("d") == nullptr);                // unmapped
    r.connect("a", "d");
    CHECK(r.mappings().size() == 1);
    CHECK(r.source_of("d") != nullptr && *r.source_of("d") == "a");

    // Connecting the same dest replaces (no duplicate), updating source + amount.
    r.connect("b", "d", 0.5f);
    CHECK(r.mappings().size() == 1);
    CHECK(*r.source_of("d") == "b");
    CHECK_NEAR(r.find("d")->amount, 0.5f, 1e-6);

    r.disconnect("d");
    CHECK(r.mappings().empty());
    CHECK(r.source_of("d") == nullptr);
    CHECK_NEAR(r.dest_value("d"), 0.0f, 1e-6);         // unmapped dest -> 0
}

static void test_dest_pipeline() {
    // Linear pass-through.
    {
        MappingRegistry r;
        r.connect("a", "d");
        r.set_source("a", 0.4f);
        CHECK_NEAR(r.dest_value("d"), 0.4f, 1e-6);
    }
    // amount applies as output gain.
    {
        MappingRegistry r;
        r.connect("a", "d", 2.0f);
        r.set_source("a", 0.3f);
        CHECK_NEAR(r.dest_value("d"), 0.6f, 1e-6);
    }
    // Source clamps to [0,1] before shaping.
    {
        MappingRegistry r;
        r.connect("a", "d");
        r.set_source("a", 1.5f);
        CHECK_NEAR(r.dest_value("d"), 1.0f, 1e-6);
        r.set_source("a", -0.5f);
        CHECK_NEAR(r.dest_value("d"), 0.0f, 1e-6);
    }
    // Polarity inversion: (1 - s).
    {
        MappingRegistry r;
        r.connect("a", "d");
        r.find("d")->invert = true;
        r.set_source("a", 0.25f);
        CHECK_NEAR(r.dest_value("d"), 0.75f, 1e-6);
    }
    // Output range remap: shaped 0..1 -> [out_lo, out_hi].
    {
        MappingRegistry r;
        r.connect("a", "d");
        Mapping* m = r.find("d");
        m->out_lo = 0.2f; m->out_hi = 0.6f;
        r.set_source("a", 0.5f);                        // linear -> 0.2 + 0.4*0.5
        CHECK_NEAR(r.dest_value("d"), 0.4f, 1e-6);
    }
    // Curve shapes the value (ease-in pulls the midpoint below linear).
    {
        MappingRegistry r;
        r.connect("a", "d");
        r.find("d")->curve = 1.0f;
        r.set_source("a", 0.5f);
        float v = r.dest_value("d");
        CHECK(v > 0.0f && v < 0.5f);
    }
}

int main() {
    test_shape();
    test_sources();
    test_connect_disconnect();
    test_dest_pipeline();
    return vivid::test::summary("test_mapping");
}
