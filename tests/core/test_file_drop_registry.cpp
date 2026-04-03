#include "runtime/core/file_drop_registry.h"
#include "runtime/operators/operator_registry.h"

#include <cstdio>
#include <filesystem>
#include "test_helpers.h"

int main() {
    namespace fs = std::filesystem;

    fs::path staging = fs::path("./.test_file_drop_registry");
    fs::create_directories(staging);
    fs::copy_file("file_drop_test_op.dylib", staging / "file_drop_test_op.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file("file_drop_test_op_alt.dylib", staging / "file_drop_test_op_alt.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file("file_drop_bad_param_op.dylib", staging / "file_drop_bad_param_op.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file("midi_file_player.dylib", staging / "midi_file_player.dylib",
                  fs::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan_deferred(staging.c_str()), "scan_deferred succeeds");

    vivid::FileDropRegistry drops;
    drops.refresh(registry);

    auto handlers = drops.all_registered_handlers();
    check(handlers.size() == 3, "invalid file-drop registration filtered out");

    auto matches = drops.matches_for_path("/tmp/example.DROPX");
    check(matches.size() == 2, "extension match is case-insensitive");
    if (matches.size() == 2) {
        check(matches[0].type_name == "FileDropTestOp", "higher priority handler ordered first");
        check(matches[1].type_name == "FileDropTestOpAlt", "second handler preserved");
    }

    auto no_matches = drops.matches_for_path("/tmp/example.dropbad");
    check(no_matches.empty(), "invalid file_param handler does not match");

    auto midi_matches = drops.matches_for_path("/tmp/example.MID");
    check(midi_matches.size() == 1, "midi extension resolves to a single handler");
    if (midi_matches.size() == 1) {
        check(midi_matches[0].type_name == "MidiFilePlayer", "midi handler resolves to MidiFilePlayer");
    }

    auto midi_matches_long = drops.matches_for_path("/tmp/example.midi");
    check(midi_matches_long.size() == 1, ".midi extension also resolves");

    fs::remove_all(staging);

    std::fprintf(stderr, "\n%d failed\n", failures);
    return failures ? 1 : 0;
}
