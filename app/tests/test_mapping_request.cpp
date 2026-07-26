#include "cli/mapping_request.h"
#include "test_helpers.h"

#include <string>

int main() {
    using namespace vivid::control_mapping;

    CHECK(lower_copy("Gate") == "gate");
    CHECK(lower_copy("TRANSIENT") == "transient");

    CHECK(valid_audio_characteristic("level", true));
    CHECK(valid_audio_characteristic("transient", true));
    CHECK(valid_audio_characteristic("high", true));
    CHECK(!valid_audio_characteristic("gate", true));
    CHECK(!valid_audio_characteristic("note", true));

    CHECK(valid_audio_characteristic("level", false));
    CHECK(valid_audio_characteristic("gate", false));
    CHECK(valid_audio_characteristic("note", false));
    CHECK(valid_audio_characteristic("velocity", false));
    CHECK(!valid_audio_characteristic("pitch", false));

    CHECK(master_source("level") == "master.level");
    CHECK(master_source("transient") == "master.transient");
    CHECK(track_source(9, "gate") == "track_9.gate");
    CHECK(track_source(42, "note") == "track_42.note");

    return vivid::test::summary("test_mapping_request");
}
