import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import audit_mcp_operator_coverage as audit  # noqa: E402


POLICY = {
    "legacy_operators": ["MidiFilePlayer"],
    "preferred_replacements": {"MidiFilePlayer": "MidiClip"},
    "allowed_missing_docs": ["AllowedCore"],
    "capability_expectations": {
        "midi_file_playback": {
            "preferred": "MidiClip",
            "legacy": ["MidiFilePlayer"],
            "mcp_terms": ["MidiClip", "midi_clip"],
        }
    },
}


def op(name, kind="audio", **extra):
    base = {
        "name": name,
        "kind": kind,
        "display_name": name,
        "has_docs": True,
        "brief": f"{name} docs",
    }
    base.update(extra)
    return base


def test_fully_covered_core_operator_passes():
    result = audit.audit_catalog(
        [op("MidiClip", kind="control"), op("Lfo", kind="control")],
        POLICY,
        mcp_text={"mcp/vivid_opdev_mcp.py": "Prefer MidiClip for MIDI file playback."},
    )
    assert result.ok
    assert result.summary["errors"] == 0


def test_core_operator_missing_docs_fails():
    result = audit.audit_catalog(
        [
            op("MidiClip", kind="control"),
            op("Lfo", kind="control", has_docs=False, brief="", summary=""),
        ],
        POLICY,
        mcp_text={"mcp/vivid_opdev_mcp.py": "Prefer MidiClip for MIDI file playback."},
    )
    assert not result.ok
    assert any(f.code == "missing_discovery_docs" and f.operator == "Lfo"
               for f in result.findings)


def test_allowed_missing_docs_suppresses_core_failure():
    result = audit.audit_catalog(
        [
            op("MidiClip", kind="control"),
            op("AllowedCore", kind="control", has_docs=False, brief="", summary=""),
        ],
        POLICY,
        mcp_text={"mcp/vivid_opdev_mcp.py": "Prefer MidiClip for MIDI file playback."},
    )
    assert result.ok


def test_package_operator_missing_docs_is_advisory_only():
    result = audit.audit_catalog(
        [
            op("MidiClip", kind="control"),
            op("PackageOnly", kind="audio", has_docs=False, brief="", summary=""),
        ],
        POLICY,
        mcp_text={"mcp/vivid_opdev_mcp.py": "Prefer MidiClip for MIDI file playback."},
    )
    assert result.ok
    assert any(f.severity == "warning" and f.operator == "PackageOnly"
               for f in result.findings)


def test_legacy_operator_without_context_fails():
    result = audit.audit_catalog(
        [op("MidiClip", kind="control"), op("MidiFilePlayer")],
        POLICY,
        mcp_text={"mcp/vivid_opdev_mcp.py": "Use MidiFilePlayer for MIDI files."},
    )
    assert not result.ok
    assert any(f.code == "legacy_reference_without_context"
               for f in result.findings)


def test_legacy_operator_with_replacement_context_passes():
    result = audit.audit_catalog(
        [op("MidiClip", kind="control"), op("MidiFilePlayer")],
        POLICY,
        mcp_text={
            "mcp/vivid_opdev_mcp.py":
                "Prefer MidiClip for MIDI file playback; MidiFilePlayer is legacy."
        },
    )
    assert result.ok


def test_cli_reads_catalog_fixture(tmp_path, capsys):
    catalog = tmp_path / "catalog.json"
    policy = tmp_path / "policy.json"
    catalog.write_text(json.dumps({
        "ok": True,
        "result": {
            "types": [op("MidiClip", kind="control"), op("Lfo", kind="control")]
        },
    }))
    policy.write_text(json.dumps(POLICY))

    rc = audit.main([
        "--catalog-json", str(catalog),
        "--policy", str(policy),
        "--strict", "none",
        "--format", "json",
    ])
    out = json.loads(capsys.readouterr().out)
    assert rc == 0
    assert out["ok"] is True
    assert out["summary"]["errors"] == 0
