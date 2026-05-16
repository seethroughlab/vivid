#include "runtime/core/file_drop_registry.h"
#include "runtime/operators/operator_registry.h"

#include <cstdio>
#include <filesystem>
#include "test_helpers.h"

int main() {
    namespace fs = std::filesystem;

    ScopedTempDir sandbox("file_drop_registry");
    fs::path staging = sandbox / "staging";
    fs::create_directories(staging);
    fs::copy_file("file_drop_test_op.dylib", staging / "file_drop_test_op.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file("file_drop_test_op_alt.dylib", staging / "file_drop_test_op_alt.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file("file_drop_bad_param_op.dylib", staging / "file_drop_bad_param_op.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file("midi_clip.dylib", staging / "midi_clip.dylib",
                  fs::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan_deferred(staging.c_str()), "scan_deferred succeeds");

    vivid::FileDropRegistry drops;
    drops.refresh(registry);

    auto handlers = drops.all_registered_handlers();
    check(handlers.size() == 3, "invalid file-drop registration filtered out");

    auto matches = drops.matches_for_path((sandbox / "example.DROPX").string());
    check(matches.size() == 2, "extension match is case-insensitive");
    if (matches.size() == 2) {
        check(matches[0].type_name == "FileDropTestOp", "higher priority handler ordered first");
        check(matches[1].type_name == "FileDropTestOpAlt", "second handler preserved");
    }

    auto no_matches = drops.matches_for_path((sandbox / "example.dropbad").string());
    check(no_matches.empty(), "invalid file_param handler does not match");

    auto midi_matches = drops.matches_for_path((sandbox / "example.MID").string());
    check(midi_matches.size() >= 1, "midi extension resolves to at least one handler");
    if (!midi_matches.empty()) {
        check(midi_matches[0].type_name == "MidiClip", "midi handler prefers MidiClip");
    }

    auto midi_matches_long = drops.matches_for_path((sandbox / "example.midi").string());
    check(!midi_matches_long.empty(), ".midi extension also resolves");

    std::fprintf(stderr, "\n%d failed\n", failures);
    return failures ? 1 : 0;
}
