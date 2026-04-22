#pragma once
//
// GLFW key / modifier constants, mirrored here so operator editors can
// reference them without linking GLFW. Values match the canonical
// GLFW3 constants; any key not listed is either unused across current
// editor adopters or can be added as new adopters arrive.
//
// Usage pattern inside a draw_editor():
//
//   namespace ek = ::vivid::editor_keys;
//   for (uint32_t i = 0; i < ctx->event_count; ++i) {
//       const auto& e = ctx->events[i];
//       if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
//       if (e.action != ek::kPress && e.action != ek::kRepeat) continue;
//       const bool shift = (e.modifiers & ek::kModShift) != 0;
//       if (e.key == ek::kEnter)  { … }
//       else if (ek::is_digit_key(e.key)) {
//           const int d = ek::digit_value(e.key);
//           …
//       }
//   }
//
// Header-only; no link dependencies.

namespace vivid::editor_keys {

// --- Event actions (VividEditorEvent::action) ---
inline constexpr int kRelease = 0;
inline constexpr int kPress   = 1;
inline constexpr int kRepeat  = 2;

// --- Modifier bits (VividEditorEvent::modifiers bitmask) ---
inline constexpr int kModShift   = 0x0001;
inline constexpr int kModControl = 0x0002;
inline constexpr int kModAlt     = 0x0004;
inline constexpr int kModSuper   = 0x0008;  // Cmd on macOS, Super on others

// --- Printable keys (subset used by current adopters) ---
inline constexpr int kSpace       = 32;
inline constexpr int kApostrophe  = 39;
inline constexpr int kComma       = 44;
inline constexpr int kMinus       = 45;
inline constexpr int kPeriod      = 46;
inline constexpr int kSlash       = 47;

inline constexpr int k0 = 48;  // '0' .. '9' are 48 .. 57
inline constexpr int k1 = 49;
inline constexpr int k2 = 50;
inline constexpr int k3 = 51;
inline constexpr int k4 = 52;
inline constexpr int k5 = 53;
inline constexpr int k6 = 54;
inline constexpr int k7 = 55;
inline constexpr int k8 = 56;
inline constexpr int k9 = 57;

inline constexpr int kSemicolon   = 59;
inline constexpr int kEqual       = 61;

inline constexpr int kA = 65;  // 'A' .. 'Z' are 65 .. 90
inline constexpr int kB = 66;
inline constexpr int kC = 67;
inline constexpr int kD = 68;
inline constexpr int kE = 69;
inline constexpr int kF = 70;
inline constexpr int kG = 71;
inline constexpr int kH = 72;
inline constexpr int kI = 73;
inline constexpr int kJ = 74;
inline constexpr int kK = 75;
inline constexpr int kL = 76;
inline constexpr int kM = 77;
inline constexpr int kN = 78;
inline constexpr int kO = 79;
inline constexpr int kP = 80;
inline constexpr int kQ = 81;
inline constexpr int kR = 82;
inline constexpr int kS = 83;
inline constexpr int kT = 84;
inline constexpr int kU = 85;
inline constexpr int kV = 86;
inline constexpr int kW = 87;
inline constexpr int kX = 88;
inline constexpr int kY = 89;
inline constexpr int kZ = 90;

inline constexpr int kLeftBracket  = 91;
inline constexpr int kBackslash    = 92;
inline constexpr int kRightBracket = 93;
inline constexpr int kGraveAccent  = 96;

// --- Navigation / editing (GLFW "function" keys start at 256) ---
inline constexpr int kEscape    = 256;
inline constexpr int kEnter     = 257;
inline constexpr int kTab       = 258;
inline constexpr int kBackspace = 259;
inline constexpr int kInsert    = 260;
inline constexpr int kDelete    = 261;
inline constexpr int kRight     = 262;
inline constexpr int kLeft      = 263;
inline constexpr int kDown      = 264;
inline constexpr int kUp        = 265;
inline constexpr int kPageUp    = 266;
inline constexpr int kPageDown  = 267;
inline constexpr int kHome      = 268;
inline constexpr int kEnd       = 269;

// --- Helpers ---

// True when the user held the platform's "command" modifier — Cmd on
// macOS via the Super bit, Ctrl elsewhere. Matches the behaviour every
// existing editor already implements locally.
inline constexpr bool is_cmd_or_ctrl(int modifiers) {
    return (modifiers & (kModSuper | kModControl)) != 0;
}

// True when the key event is a top-row digit (`0` .. `9`). Ignores the
// numpad digits — they're separate keycodes in GLFW and no current
// adopter needs them.
inline constexpr bool is_digit_key(int key) {
    return key >= k0 && key <= k9;
}

// Decode a digit-key keycode back to 0..9. Undefined for non-digit keys;
// callers should guard with `is_digit_key` first.
inline constexpr int digit_value(int key) {
    return key - k0;
}

// True when the key event is a letter A..Z (uppercase mirrors
// lowercase — GLFW reports physical-key codes, so 'a' and 'A' share
// one keycode and shift is carried in modifiers).
inline constexpr bool is_letter_key(int key) {
    return key >= kA && key <= kZ;
}

} // namespace vivid::editor_keys
