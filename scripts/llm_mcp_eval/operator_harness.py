"""ThresholdGate operator package creation, compilation, and behavior testing."""

from __future__ import annotations

import asyncio
import json
import os
import pathlib
import shutil
import tempfile
from dataclasses import dataclass

from .graders import OperatorTestResult

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent

PACKAGE_MANIFEST = {
    "name": "eval-threshold-gate",
    "version": "0.0.1",
    "description": "Eval harness: ThresholdGate operator",
    "operators": {
        "control": ["threshold_gate"]
    },
    "tests": {
        "cpp": ["tests/test_threshold_gate.cpp"]
    },
}

BEHAVIOR_TEST_SOURCE = r'''#include "operator_api/operator.h"
#include "control/threshold_gate/threshold_gate.cpp"
#include <cstdio>
#include <cmath>
#include <cstring>

static int failures = 0;

static void check(const char* label, float got, float expected) {
    if (std::fabs(got - expected) > 1e-6f) {
        std::printf("FAIL %s: got %f, expected %f\n", label, got, expected);
        failures++;
    } else {
        std::printf("PASS %s: %f\n", label, got);
    }
}

int main() {
    ThresholdGate op;

    // Discover ports and params to validate structure
    std::vector<vivid::ParamBase*> params;
    op.collect_params(params);
    if (params.size() < 1) {
        std::printf("FAIL: expected at least 1 param, got %zu\n", params.size());
        return 1;
    }

    std::vector<VividPortDescriptor> ports;
    op.collect_ports(ports);

    int input_count = 0, output_count = 0;
    for (auto& p : ports) {
        if (p.direction == VIVID_PORT_INPUT) input_count++;
        if (p.direction == VIVID_PORT_OUTPUT) output_count++;
    }
    if (input_count < 1 || output_count < 1) {
        std::printf("FAIL: expected at least 1 input and 1 output port, got %d in / %d out\n",
                     input_count, output_count);
        return 1;
    }

    // Set up context arrays
    float param_vals[8] = {};
    float input_vals[8] = {};
    float output_vals[8] = {};

    // Default threshold = 0.5
    param_vals[0] = 0.5f;

    VividFrameContext ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.param_values = param_vals;
    ctx.input_values = input_vals;
    ctx.output_values = output_vals;
    ctx.lane_count = 1;
    ctx.delta_time = 1.0 / 60.0;

    // Test: below threshold (0.3 < 0.5) -> 0.0
    input_vals[0] = 0.3f;
    output_vals[0] = -1.0f;
    op.process_frame(&ctx);
    check("below_threshold", output_vals[0], 0.0f);

    // Test: at threshold (0.5 >= 0.5) -> 1.0
    input_vals[0] = 0.5f;
    output_vals[0] = -1.0f;
    op.process_frame(&ctx);
    check("at_threshold", output_vals[0], 1.0f);

    // Test: above threshold (0.8 >= 0.5) -> 1.0
    input_vals[0] = 0.8f;
    output_vals[0] = -1.0f;
    op.process_frame(&ctx);
    check("above_threshold", output_vals[0], 1.0f);

    if (failures > 0) {
        std::printf("\n%d test(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll ThresholdGate tests passed.\n");
    return 0;
}
'''


def create_package(source_code: str, output_dir: pathlib.Path) -> pathlib.Path:
    """Create a temporary package directory with the operator source and behavior test.

    Returns the package root directory.
    """
    pkg_dir = output_dir / "threshold_gate_pkg"
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)

    op_dir = pkg_dir / "operators" / "control" / "threshold_gate"
    test_dir = pkg_dir / "tests"
    op_dir.mkdir(parents=True)
    test_dir.mkdir(parents=True)

    # Write manifest
    (pkg_dir / "vivid-package.json").write_text(
        json.dumps(PACKAGE_MANIFEST, indent=2) + "\n"
    )

    # Write operator source
    (op_dir / "threshold_gate.cpp").write_text(source_code)

    # Write behavior test
    (test_dir / "test_threshold_gate.cpp").write_text(BEHAVIOR_TEST_SOURCE)

    return pkg_dir


async def _run_vivid_cli(args: list[str], env: dict[str, str] | None = None) -> tuple[int, str]:
    """Run the vivid CLI and return (exit_code, combined_output)."""
    vivid_bin = REPO_ROOT / "build" / "vivid"
    if not vivid_bin.exists():
        return 1, f"vivid CLI not found at {vivid_bin}"

    run_env = os.environ.copy()
    if env:
        run_env.update(env)

    proc = await asyncio.create_subprocess_exec(
        str(vivid_bin), *args,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
        env=run_env,
        cwd=str(REPO_ROOT),
    )
    stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=120)
    return proc.returncode or 0, stdout.decode("utf-8", errors="replace")


def _extract_json_object(text: str) -> str | None:
    """Find the last JSON object in text that may contain leading log lines."""
    # Search backwards for the last '{' that starts a valid JSON object
    idx = text.rfind("{")
    while idx >= 0:
        candidate = text[idx:]
        try:
            json.loads(candidate)
            return candidate
        except json.JSONDecodeError:
            idx = text.rfind("{", 0, idx)
    return None


async def build_and_test(
    source_code: str,
    output_dir: pathlib.Path,
    isolated_home: pathlib.Path | None = None,
) -> OperatorTestResult:
    """Create the package, link it, run tests, unlink it, and return the result."""

    pkg_dir = create_package(source_code, output_dir)
    env_override = {"HOME": str(isolated_home)} if isolated_home else None

    # Link the package
    rc, link_out = await _run_vivid_cli(["link", str(pkg_dir), "--json"], env=env_override)
    if rc != 0:
        return OperatorTestResult(
            code_extracted=True,
            source=source_code,
            compile_ok=False,
            test_passed=False,
            output=f"link failed (rc={rc}):\n{link_out}",
        )

    # Run tests
    rc, test_out = await _run_vivid_cli(
        ["test-package", "eval-threshold-gate", "--json"],
        env=env_override,
    )

    # Always try to unlink
    await _run_vivid_cli(["unlink", "eval-threshold-gate", "--json"], env=env_override)

    # Parse results — the CLI output may have log lines before the JSON object
    compile_ok = False
    test_passed = False
    json_str = _extract_json_object(test_out)
    if json_str:
        try:
            result = json.loads(json_str)
            if result.get("ok"):
                tests = result.get("result", {}).get("tests", [])
                compile_ok = True
                for t in tests:
                    if t.get("type") == "cpp" and "threshold_gate" in t.get("name", ""):
                        test_passed = t.get("status") == "passed"
        except (json.JSONDecodeError, KeyError):
            pass

    return OperatorTestResult(
        code_extracted=True,
        source=source_code,
        compile_ok=compile_ok,
        test_passed=test_passed,
        output=test_out,
    )
