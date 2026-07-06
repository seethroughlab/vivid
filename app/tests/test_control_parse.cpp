// Headless tests for the control server's pure parse/validation helpers
// (app/src/cli/control_parse.h): index range checks, the characteristic-kind
// table, and the mapping-source -> char_id encoding (master = kind; track t =
// 100 + t*8 + kind) — a wire format where an off-by-one silently mis-routes.
#include "cli/control_parse.h"
#include "test_helpers.h"

using namespace vivid::control;

static void test_in_range() {
    CHECK(in_range(0, 1));
    CHECK(in_range(2, 5));
    CHECK(!in_range(-1, 5));
    CHECK(!in_range(5, 5));
    CHECK(!in_range(0, 0));
}

static void test_kind_index() {
    CHECK(kind_index("level") == 0);
    CHECK(kind_index("transient") == 1);
    CHECK(kind_index("low") == 2);
    CHECK(kind_index("mid") == 3);
    CHECK(kind_index("high") == 4);
    CHECK(kind_index("bogus") == -1);
    CHECK(kind_index("") == -1);
}

static void test_char_id() {
    // master.<kind> == kind index.
    CHECK(char_id_from_source("master.level") == 0);
    CHECK(char_id_from_source("master.high") == 4);
    // track_<n>.<kind> == 100 + n*8 + kind.
    CHECK(char_id_from_source("track_0.level") == 100);
    CHECK(char_id_from_source("track_1.transient") == 109);   // 100 + 8 + 1
    CHECK(char_id_from_source("track_2.low") == 118);          // 100 + 16 + 2
    CHECK(char_id_from_source("track_3.mid") == 127);          // 100 + 24 + 3
    // Malformed -> -1.
    CHECK(char_id_from_source("master") == -1);                // no dot
    CHECK(char_id_from_source("master.bogus") == -1);          // bad kind
    CHECK(char_id_from_source("nope.level") == -1);            // bad head
    CHECK(char_id_from_source("") == -1);
}

int main() {
    test_in_range();
    test_kind_index();
    test_char_id();
    return vivid::test::summary("test_control_parse");
}
