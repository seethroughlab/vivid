#!/usr/bin/env bash
# ============================================================================
# Ph6 audit P1-01: re-run the release-critical test legs on the EXACT commit being released, before
# it is signed and published.
#
# The production gate (core / HEADLESS_SMOKE) alone does not exercise the realtime-safety paths — the
# ASan/UBSan, ThreadSanitizer, and audio-engine legs do, and those run only as PR checks. A tag pushed
# directly (or a squash-merge that changed the SHA so the PR checks no longer attach to it) could
# therefore ship unverified for exactly the highest-risk audio paths. This script closes that gap by
# running the same legs the PR gate runs, failing the release on any red.
#
# Mirrors: .github/workflows/headless-tests.yml (sanitize=ON + thread-sanitizer) and
#          .github/workflows/production-gate-pr.yml (audio-engine-tests + audio-thread-sanitizer).
# Env: VIVID_VERIFY_JOBS (build parallelism, default 4).
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root
J="${VIVID_VERIFY_JOBS:-4}"

step() { echo; echo "======== $* ========"; }

# 1. Headless ASan/UBSan (portable, app OFF) — headless-tests.yml sanitize=ON.
step "1/4 headless ASan/UBSan"
cmake -S app -B app/build-verify-asan -DVIVID_BUILD_APP=OFF -DVIVID_BUILD_TESTS=ON \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVIVID_SANITIZE=ON
cmake --build app/build-verify-asan -j "$J"
ctest --test-dir app/build-verify-asan --output-on-failure

# 2. Headless ThreadSanitizer — the THREAD-labelled race tests.
step "2/4 headless ThreadSanitizer (ctest -L THREAD)"
cmake -S app -B app/build-verify-tsan -DVIVID_BUILD_APP=OFF -DVIVID_BUILD_TESTS=ON \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVIVID_SANITIZE_THREAD=ON
cmake --build app/build-verify-tsan -j "$J"
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir app/build-verify-tsan -L THREAD --output-on-failure

# 3. Audio-engine tests (app ON, no sanitizer) — minus the TSan-only concurrency harness.
step "3/4 audio-engine tests (ctest -L AUDIO_ENGINE)"
cmake -S app -B app/build-verify-audio -DVIVID_BUILD_APP=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build app/build-verify-audio -j "$J"
ctest --test-dir app/build-verify-audio -L AUDIO_ENGINE -E test_session_concurrency --output-on-failure

# 4. Audio ThreadSanitizer — the render-thread vs UI-mutator race harness.
step "4/4 audio ThreadSanitizer (ctest -L AUDIO_THREAD)"
cmake -S app -B app/build-verify-audio-tsan -DVIVID_BUILD_APP=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DVIVID_SANITIZE_THREAD=ON
cmake --build app/build-verify-audio-tsan -j "$J" --target test_session_concurrency
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir app/build-verify-audio-tsan -L AUDIO_THREAD --output-on-failure

echo
echo "release verification: all legs passed"
