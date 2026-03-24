#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <algorithm>

namespace tracker {

// Effect type IDs (MOD-style)
static constexpr uint8_t FX_NONE         = 0x00;
static constexpr uint8_t FX_ARPEGGIO     = 0x00;  // disambiguated: only when param != 0
static constexpr uint8_t FX_PORTA_UP     = 0x01;
static constexpr uint8_t FX_PORTA_DOWN   = 0x02;
static constexpr uint8_t FX_TONE_PORTA   = 0x03;
static constexpr uint8_t FX_VIBRATO      = 0x04;
static constexpr uint8_t FX_VOL_SLIDE    = 0x0A;
static constexpr uint8_t FX_SET_VOLUME   = 0x0C;
static constexpr uint8_t FX_PATTERN_BREAK= 0x0D;
static constexpr uint8_t FX_EXTENDED     = 0x0E;  // sub-commands in high nibble of param
static constexpr uint8_t FX_SET_SPEED    = 0x0F;

// Extended effect sub-commands (stored as effect_type = 0xE0 | sub)
static constexpr uint8_t FX_EXT_RETRIGGER = 0x09;
static constexpr uint8_t FX_EXT_NOTE_CUT  = 0x0C;
static constexpr uint8_t FX_EXT_NOTE_DELAY= 0x0D;

static constexpr uint8_t NOTE_EMPTY   = 0;
static constexpr uint8_t NOTE_OFF     = 255;
static constexpr int     MAX_CHANNELS = 8;
static constexpr int     MAX_ROWS     = 64;
static constexpr int     MAX_PATTERNS = 64;
static constexpr int     MAX_ARRANGEMENT = 128;

struct TrackerCell {
    uint8_t note;         // 0=empty, 1-127=MIDI note, 255=note-off
    uint8_t velocity;     // 0-127 (0 = use previous)
    uint8_t effect_type;  // see FX_ constants
    uint8_t effect_param; // XY nibbles
};

struct TrackerPattern {
    uint8_t num_rows;
    TrackerCell cells[MAX_CHANNELS][MAX_ROWS];
};

struct TrackerSong {
    uint8_t num_patterns;
    TrackerPattern patterns[MAX_PATTERNS];
    uint8_t arrangement[MAX_ARRANGEMENT];
    uint8_t arrangement_length;

    TrackerSong() {
        num_patterns = 1;
        arrangement_length = 1;
        arrangement[0] = 0;
        std::memset(arrangement + 1, 0, MAX_ARRANGEMENT - 1);
        patterns[0].num_rows = 16;
        std::memset(patterns[0].cells, 0, sizeof(patterns[0].cells));
        for (int i = 1; i < MAX_PATTERNS; ++i) {
            patterns[i].num_rows = 16;
            std::memset(patterns[i].cells, 0, sizeof(patterns[i].cells));
        }
    }
};

struct ChannelState {
    int8_t  prev_midi_note = -1;
    float   current_pitch = 0;
    float   target_pitch = 0;
    float   porta_speed = 0;
    float   vibrato_phase = 0;
    float   vibrato_depth = 0;
    float   vibrato_speed = 0;
    float   volume = 1.0f;
    float   volume_slide_delta = 0;
    int     retrigger_period = 0;
    int     retrigger_counter = 0;
    int     arpeggio_x = 0;
    int     arpeggio_y = 0;
    int     arpeggio_tick = 0;
    int     note_delay_ticks = 0;
    int     note_cut_ticks = 0;
    bool    gate_active = false;
    uint8_t current_velocity = 100;
    uint8_t last_note = 0;
};

// Text serialization format:
//   arrangement: 0 1 2 ...
//   pattern N rows M
//    R: NNN VV EEE | NNN VV EEE | ...   (8 channels)
//
// NNN = note name: C-4, C#4, ---, ===
// VV  = velocity in hex (00-7F) or .. for "use previous" (velocity=0)
// EEE = effect (type nibble + param byte) or ... for none

static constexpr char kHexChars[] = "0123456789ABCDEF";

inline void format_cell_note(uint8_t note, char out[4]) {
    static const char* names[] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    if (note == NOTE_EMPTY) { out[0]='-'; out[1]='-'; out[2]='-'; out[3]=0; return; }
    if (note == NOTE_OFF)   { out[0]='='; out[1]='='; out[2]='='; out[3]=0; return; }
    const char* prefix = names[note % 12];
    out[0] = prefix[0]; out[1] = prefix[1];
    int oct = (note / 12) - 1;
    out[2] = (oct >= 0 && oct <= 9) ? static_cast<char>('0' + oct) : '?';
    out[3] = 0;
}

inline void format_cell_vel(uint8_t vel, char out[3]) {
    if (vel == 0) { out[0]='.'; out[1]='.'; out[2]=0; return; }
    out[0] = kHexChars[(vel >> 4) & 0xF];
    out[1] = kHexChars[vel & 0xF];
    out[2] = 0;
}

inline void format_cell_fx(uint8_t type, uint8_t param, char out[4]) {
    if (type == 0 && param == 0) { out[0]='.'; out[1]='.'; out[2]='.'; out[3]=0; return; }
    out[0] = kHexChars[type & 0xF];
    out[1] = kHexChars[(param >> 4) & 0xF];
    out[2] = kHexChars[param & 0xF];
    out[3] = 0;
}

inline int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

inline uint8_t parse_cell_note(const char* s) {
    if (!s || s[0] == '-') return NOTE_EMPTY;
    if (s[0] == '=') return NOTE_OFF;
    int semi = -1;
    switch (s[0]) {
        case 'C': case 'c': semi = 0; break;
        case 'D': case 'd': semi = 2; break;
        case 'E': case 'e': semi = 4; break;
        case 'F': case 'f': semi = 5; break;
        case 'G': case 'g': semi = 7; break;
        case 'A': case 'a': semi = 9; break;
        case 'B': case 'b': semi = 11; break;
        default: return NOTE_EMPTY;
    }
    int idx = 1;
    if (s[1] == '#') { semi++; idx = 2; }
    else if (s[1] == '-' || s[1] == ' ') { idx = 2; }
    if (!s[idx] || s[idx] < '0' || s[idx] > '9') return NOTE_EMPTY;
    int oct = s[idx] - '0';
    return static_cast<uint8_t>(std::clamp((oct + 1) * 12 + semi, 0, 127));
}

inline uint8_t parse_cell_vel(const char* s) {
    if (!s || s[0] == '.') return 0;
    int hi = hex_char_val(s[0]), lo = hex_char_val(s[1]);
    if (hi < 0 || lo < 0) return 0;
    return static_cast<uint8_t>((hi << 4) | lo);
}

inline void parse_cell_fx(const char* s, uint8_t& type, uint8_t& param) {
    if (!s || s[0] == '.') { type = 0; param = 0; return; }
    int t = hex_char_val(s[0]), ph = hex_char_val(s[1]), pl = hex_char_val(s[2]);
    type  = (t  >= 0) ? static_cast<uint8_t>(t) : 0;
    param = (ph >= 0 && pl >= 0) ? static_cast<uint8_t>((ph << 4) | pl) : 0;
}

inline std::string serialize_song(const TrackerSong& song) {
    char buf[64];
    std::string out;

    // Arrangement line
    out += "arrangement:";
    for (int i = 0; i < song.arrangement_length; ++i) {
        out += ' ';
        out += std::to_string(song.arrangement[i]);
    }
    out += '\n';

    // Patterns
    char note_buf[4], vel_buf[3], fx_buf[4];
    for (int p = 0; p < song.num_patterns; ++p) {
        const auto& pat = song.patterns[p];
        std::snprintf(buf, sizeof(buf), "pattern %d rows %d\n", p, pat.num_rows);
        out += buf;
        for (int r = 0; r < pat.num_rows; ++r) {
            std::snprintf(buf, sizeof(buf), "%2d:", r);
            out += buf;
            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                const auto& cell = pat.cells[ch][r];
                format_cell_note(cell.note, note_buf);
                format_cell_vel(cell.velocity, vel_buf);
                format_cell_fx(cell.effect_type, cell.effect_param, fx_buf);
                std::snprintf(buf, sizeof(buf), " %s %s %s", note_buf, vel_buf, fx_buf);
                out += buf;
                if (ch < MAX_CHANNELS - 1) out += " |";
            }
            out += '\n';
        }
    }

    return out;
}

inline bool deserialize_song(const std::string& text, TrackerSong& song) {
    if (text.empty()) return false;
    // Detect old Base64 format (doesn't start with "arrangement:")
    if (text.size() < 12 || text.substr(0, 12) != "arrangement:") return false;

    song = TrackerSong();

    auto next_line = [](const char* s) -> const char* {
        while (*s && *s != '\n') ++s;
        if (*s == '\n') ++s;
        return s;
    };

    const char* p = text.c_str() + 12;  // skip "arrangement:"

    // Parse arrangement indices
    song.arrangement_length = 0;
    while (*p && *p != '\n') {
        while (*p == ' ') ++p;
        if (*p == '\n' || !*p) break;
        char* end;
        int idx = static_cast<int>(std::strtol(p, &end, 10));
        if (end == p) break;
        if (song.arrangement_length < MAX_ARRANGEMENT)
            song.arrangement[song.arrangement_length++] =
                static_cast<uint8_t>(std::clamp(idx, 0, MAX_PATTERNS - 1));
        p = end;
    }
    p = next_line(p);

    song.num_patterns = 0;

    while (*p) {
        if (std::strncmp(p, "pattern ", 8) == 0) {
            char* end;
            int pat_idx = static_cast<int>(std::strtol(p + 8, &end, 10));
            if (end == p + 8 || pat_idx < 0 || pat_idx >= MAX_PATTERNS) { p = next_line(p); continue; }
            const char* q = end;
            while (*q == ' ') ++q;
            if (std::strncmp(q, "rows ", 5) != 0) { p = next_line(p); continue; }
            int num_rows = static_cast<int>(std::strtol(q + 5, &end, 10));
            num_rows = std::clamp(num_rows, 1, MAX_ROWS);
            song.patterns[pat_idx].num_rows = static_cast<uint8_t>(num_rows);
            song.num_patterns = std::max(song.num_patterns, static_cast<uint8_t>(pat_idx + 1));
            p = next_line(p);

            for (int r = 0; r < num_rows && *p; ++r) {
                // Skip row number and ":"
                const char* rp = p;
                while (*rp && *rp != ':') ++rp;
                if (*rp == ':') ++rp;

                for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                    while (*rp == ' ') ++rp;
                    song.patterns[pat_idx].cells[ch][r].note     = parse_cell_note(rp);
                    if (*rp) rp += 3;
                    while (*rp == ' ') ++rp;
                    song.patterns[pat_idx].cells[ch][r].velocity = parse_cell_vel(rp);
                    if (*rp) rp += 2;
                    while (*rp == ' ') ++rp;
                    parse_cell_fx(rp,
                        song.patterns[pat_idx].cells[ch][r].effect_type,
                        song.patterns[pat_idx].cells[ch][r].effect_param);
                    if (*rp) rp += 3;
                    while (*rp == ' ' || *rp == '|') ++rp;
                }
                p = next_line(p);
            }
        } else {
            p = next_line(p);
        }
    }

    if (song.num_patterns == 0) {
        song.num_patterns = 1;
        song.arrangement_length = 1;
        song.arrangement[0] = 0;
    }

    return true;
}

// Note name helpers
inline const char* note_name(uint8_t note) {
    static const char* names[] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    if (note == NOTE_EMPTY) return "---";
    if (note == NOTE_OFF) return "===";
    if (note > 127) return "???";
    return names[note % 12];
}

inline int note_octave(uint8_t note) {
    if (note == NOTE_EMPTY || note == NOTE_OFF || note > 127) return -1;
    return (note / 12) - 1;
}

} // namespace tracker
