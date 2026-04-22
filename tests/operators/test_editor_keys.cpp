// Lock down the GLFW keycode values and the small set of helpers in
// operator_api/editor_keys.h. If the header ever drifts from GLFW's
// canonical values, every editor adopter's keyboard handling breaks
// silently — this test catches that drift early.

#include "operator_api/editor_keys.h"

#include <cstdio>

#include "test_helpers.h"

namespace ek = ::vivid::editor_keys;

int main() {
    std::fprintf(stderr, "=== Test: editor_keys constants ===\n\n");

    // --- Action values ---
    check(ek::kRelease == 0, "kRelease == 0");
    check(ek::kPress   == 1, "kPress   == 1");
    check(ek::kRepeat  == 2, "kRepeat  == 2");

    // --- Modifier bits ---
    check(ek::kModShift   == 0x0001, "kModShift   == 0x0001");
    check(ek::kModControl == 0x0002, "kModControl == 0x0002");
    check(ek::kModAlt     == 0x0004, "kModAlt     == 0x0004");
    check(ek::kModSuper   == 0x0008, "kModSuper   == 0x0008");

    // --- Printable keys: digits, letters, symbols ---
    check(ek::kSpace == 32,  "kSpace ==  32");
    check(ek::k0     == 48,  "k0     ==  48");
    check(ek::k9     == 57,  "k9     ==  57");
    check(ek::kA     == 65,  "kA     ==  65");
    check(ek::kZ     == 90,  "kZ     ==  90");
    check(ek::kC     == 67,  "kC     ==  67");
    check(ek::kV     == 86,  "kV     ==  86");

    // --- Navigation keys ---
    check(ek::kEscape    == 256, "kEscape    == 256");
    check(ek::kEnter     == 257, "kEnter     == 257");
    check(ek::kTab       == 258, "kTab       == 258");
    check(ek::kBackspace == 259, "kBackspace == 259");
    check(ek::kDelete    == 261, "kDelete    == 261");
    check(ek::kRight     == 262, "kRight     == 262");
    check(ek::kLeft      == 263, "kLeft      == 263");
    check(ek::kDown      == 264, "kDown      == 264");
    check(ek::kUp        == 265, "kUp        == 265");
    check(ek::kHome      == 268, "kHome      == 268");
    check(ek::kEnd       == 269, "kEnd       == 269");

    // --- is_cmd_or_ctrl: Cmd or Ctrl counts; plain Shift does not ---
    check(!ek::is_cmd_or_ctrl(0),                     "no mods → not cmd/ctrl");
    check( ek::is_cmd_or_ctrl(ek::kModSuper),         "Super → cmd/ctrl");
    check( ek::is_cmd_or_ctrl(ek::kModControl),       "Control → cmd/ctrl");
    check(!ek::is_cmd_or_ctrl(ek::kModShift),         "Shift alone → not cmd/ctrl");
    check(!ek::is_cmd_or_ctrl(ek::kModAlt),           "Alt alone → not cmd/ctrl");
    check( ek::is_cmd_or_ctrl(ek::kModShift | ek::kModSuper),
          "Shift+Super → cmd/ctrl (carries Super)");

    // --- is_digit_key / digit_value ---
    check( ek::is_digit_key(ek::k0), "k0 is a digit key");
    check( ek::is_digit_key(ek::k9), "k9 is a digit key");
    check(!ek::is_digit_key(ek::kA), "kA is not a digit key");
    check(!ek::is_digit_key(ek::kSpace), "kSpace is not a digit key");
    check(ek::digit_value(ek::k0) == 0, "digit_value(k0) == 0");
    check(ek::digit_value(ek::k9) == 9, "digit_value(k9) == 9");
    check(ek::digit_value(ek::k0 + 5) == 5, "digit_value(k5) == 5");

    // --- is_letter_key ---
    check( ek::is_letter_key(ek::kA), "kA is a letter");
    check( ek::is_letter_key(ek::kZ), "kZ is a letter");
    check(!ek::is_letter_key(ek::k0), "k0 is not a letter");
    check(!ek::is_letter_key(ek::kSpace), "kSpace is not a letter");

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
