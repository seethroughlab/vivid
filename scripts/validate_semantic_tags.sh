#!/usr/bin/env bash
set -euo pipefail

ROOTS=("$@")
if [ ${#ROOTS[@]} -eq 0 ]; then
  ROOTS=("operators" "src" "tests")
fi

ALLOWED_TAGS="time_seconds time_milliseconds phase_01 bpm beats sample_rate_hz frequency_hz amplitude_linear gain_db pan q_factor resonance gate trigger midi_note midi_velocity color_rgba position_xy position_xyz scale_xy scale_xyz rotation_degrees rotation_radians uv resolution_px seed probability_01 count index enabled path_audio path_image path_video path_font"
ALLOWED_SHAPES="scalar vec2 vec3 vec4 color bool int enum event string path pattern"
ALLOWED_UNITS="Hz s ms dB deg rad bpm px"

errors=0

contains_word() {
  local list="$1"
  local value="$2"
  case " $list " in
    *" $value "*) return 0 ;;
    *) return 1 ;;
  esac
}

check_kind() {
  local kind="$1"
  local value="$2"
  local file_line="$3"
  if [ -z "$value" ]; then
    echo "[semantic-tags] empty $kind value at $file_line"
    errors=$((errors + 1))
    return
  fi

  case "$kind" in
    tag)
      if ! contains_word "$ALLOWED_TAGS" "$value"; then
        case "$value" in
          x_[a-z0-9_]*) ;;
          *)
            echo "[semantic-tags] invalid tag '$value' at $file_line"
            errors=$((errors + 1))
            ;;
        esac
      fi
      ;;
    shape)
      if ! contains_word "$ALLOWED_SHAPES" "$value"; then
        echo "[semantic-tags] invalid shape '$value' at $file_line"
        errors=$((errors + 1))
      fi
      ;;
    unit)
      if ! contains_word "$ALLOWED_UNITS" "$value"; then
        echo "[semantic-tags] invalid unit '$value' at $file_line"
        errors=$((errors + 1))
      fi
      ;;
  esac
}

while IFS= read -r line; do
  file="${line%%:*}"
  rest="${line#*:}"
  lineno="${rest%%:*}"
  text="${rest#*:}"
  file_line="$file:$lineno"
  value="$(echo "$text" | sed -E 's/.*semantic_tag\([^,]+,[[:space:]]*"([^"]+)".*/\1/')"
  check_kind tag "$value" "$file_line"
done < <(rg -n --no-heading 'semantic_tag\([^,]+,\s*"[^"]+"' "${ROOTS[@]}" 2>/dev/null || true)

while IFS= read -r line; do
  file="${line%%:*}"
  rest="${line#*:}"
  lineno="${rest%%:*}"
  text="${rest#*:}"
  file_line="$file:$lineno"
  value="$(echo "$text" | sed -E 's/.*semantic_shape\([^,]+,[[:space:]]*"([^"]+)".*/\1/')"
  check_kind shape "$value" "$file_line"
done < <(rg -n --no-heading 'semantic_shape\([^,]+,\s*"[^"]+"' "${ROOTS[@]}" 2>/dev/null || true)

while IFS= read -r line; do
  file="${line%%:*}"
  rest="${line#*:}"
  lineno="${rest%%:*}"
  text="${rest#*:}"
  file_line="$file:$lineno"
  value="$(echo "$text" | sed -E 's/.*semantic_unit\([^,]+,[[:space:]]*"([^"]+)".*/\1/')"
  check_kind unit "$value" "$file_line"
done < <(rg -n --no-heading 'semantic_unit\([^,]+,\s*"[^"]+"' "${ROOTS[@]}" 2>/dev/null || true)

if [ "$errors" -ne 0 ]; then
  echo "[semantic-tags] failed with $errors issue(s)"
  exit 1
fi

echo "[semantic-tags] ok"
