"""Tests for the Vivid operator development MCP server resource tools."""

import json
import pytest
from pathlib import Path

# Import the module under test
import sys
sys.path.insert(0, str(Path(__file__).parent))
import vivid_opdev_mcp as opdev


# ---------------------------------------------------------------------------
# get_operator_api_docs
# ---------------------------------------------------------------------------

class TestGetOperatorApiDocs:
    @pytest.mark.anyio
    async def test_all_topics_exist(self):
        for topic in opdev.DOC_TOPICS:
            result = json.loads(await opdev.get_operator_api_docs(topic))
            assert result["ok"], f"Topic '{topic}' failed: {result}"
            assert result["topic"] == topic
            assert len(result["content"]) > 100, f"Topic '{topic}' content too short"

    @pytest.mark.anyio
    async def test_unknown_topic(self):
        result = json.loads(await opdev.get_operator_api_docs("nonexistent"))
        assert not result["ok"]
        assert "Unknown topic" in result["error"]

    @pytest.mark.anyio
    async def test_case_insensitive(self):
        result = json.loads(await opdev.get_operator_api_docs("CORE"))
        assert result["ok"]
        assert result["topic"] == "core"

    @pytest.mark.anyio
    async def test_core_has_key_content(self):
        result = json.loads(await opdev.get_operator_api_docs("core"))
        content = result["content"]
        assert "VIVID_REGISTER" in content
        assert "Param<float>" in content
        assert "collect_params" in content

    @pytest.mark.anyio
    async def test_gpu_has_wgsl_filter(self):
        result = json.loads(await opdev.get_operator_api_docs("gpu"))
        content = result["content"]
        assert "WgslFilterBase" in content
        assert "process_gpu" in content

    @pytest.mark.anyio
    async def test_dsp_has_utilities(self):
        result = json.loads(await opdev.get_operator_api_docs("dsp"))
        content = result["content"]
        assert "WhiteNoise" in content
        assert "SVF" in content
        assert "ADSR" in content


# ---------------------------------------------------------------------------
# get_api_header
# ---------------------------------------------------------------------------

class TestGetApiHeader:
    @pytest.mark.anyio
    async def test_valid_header(self):
        result = json.loads(await opdev.get_api_header("operator.h"))
        assert result["ok"]
        assert result["header"] == "operator.h"
        assert "VIVID_REGISTER" in result["content"]

    @pytest.mark.anyio
    async def test_types_header(self):
        result = json.loads(await opdev.get_api_header("types.h"))
        assert result["ok"]
        assert "VividProcessContext" in result["content"]

    @pytest.mark.anyio
    async def test_invalid_header(self):
        result = json.loads(await opdev.get_api_header("not_real.h"))
        assert not result["ok"]
        assert "not in allowlist" in result["error"]

    @pytest.mark.anyio
    async def test_path_traversal_blocked(self):
        result = json.loads(await opdev.get_api_header("../../etc/passwd"))
        assert not result["ok"]

    @pytest.mark.anyio
    async def test_all_allowed_headers_exist(self):
        for header in opdev.ALLOWED_HEADERS:
            result = json.loads(await opdev.get_api_header(header))
            assert result["ok"], f"Header '{header}' failed: {result}"
            assert len(result["content"]) > 10


# ---------------------------------------------------------------------------
# list_example_operators
# ---------------------------------------------------------------------------

class TestListExampleOperators:
    @pytest.mark.anyio
    async def test_list_all(self):
        result = json.loads(await opdev.list_example_operators())
        assert result["ok"]
        assert result["count"] >= 40  # we know there are ~49+
        domains_found = {op["domain"] for op in result["operators"]}
        assert "control" in domains_found
        assert "audio" in domains_found
        assert "gpu" in domains_found

    @pytest.mark.anyio
    async def test_filter_by_domain(self):
        result = json.loads(await opdev.list_example_operators("audio"))
        assert result["ok"]
        assert result["count"] >= 5
        for op in result["operators"]:
            assert op["domain"] == "audio"

    @pytest.mark.anyio
    async def test_known_operators_present(self):
        result = json.loads(await opdev.list_example_operators())
        names = {(op["domain"], op["name"]) for op in result["operators"]}
        assert ("control", "lfo") in names
        assert ("audio", "gain") in names
        assert ("gpu", "noise") in names

    @pytest.mark.anyio
    async def test_invalid_domain(self):
        result = json.loads(await opdev.list_example_operators("invalid"))
        assert not result["ok"]


# ---------------------------------------------------------------------------
# get_example_operator
# ---------------------------------------------------------------------------

class TestGetExampleOperator:
    @pytest.mark.anyio
    async def test_control_lfo(self):
        result = json.loads(await opdev.get_example_operator("control", "lfo"))
        assert result["ok"]
        assert result["domain"] == "control"
        assert result["name"] == "lfo"
        assert "lfo.h" in result["files"]
        assert "kName" in result["files"]["lfo.h"]

    @pytest.mark.anyio
    async def test_audio_gain(self):
        result = json.loads(await opdev.get_example_operator("audio", "gain"))
        assert result["ok"]
        assert "gain.cpp" in result["files"]

    @pytest.mark.anyio
    async def test_gpu_noise(self):
        result = json.loads(await opdev.get_example_operator("gpu", "noise"))
        assert result["ok"]
        assert "noise.cpp" in result["files"]

    @pytest.mark.anyio
    async def test_nonexistent_operator(self):
        result = json.loads(await opdev.get_example_operator("control", "nonexistent_op"))
        assert not result["ok"]

    @pytest.mark.anyio
    async def test_path_traversal_blocked(self):
        result = json.loads(await opdev.get_example_operator("control", "../../../etc"))
        assert not result["ok"]

    @pytest.mark.anyio
    async def test_invalid_domain(self):
        result = json.loads(await opdev.get_example_operator("invalid", "lfo"))
        assert not result["ok"]


# ---------------------------------------------------------------------------
# Structural checks
# ---------------------------------------------------------------------------

class TestStructure:
    def test_doc_files_exist(self):
        for topic, filename in opdev.DOC_TOPICS.items():
            path = opdev.OPDEV_DOCS_DIR / filename
            assert path.is_file(), f"Missing doc file for topic '{topic}': {path}"

    def test_operator_api_dir_exists(self):
        assert opdev.OPERATOR_API_DIR.is_dir()

    def test_operators_dir_exists(self):
        assert opdev.OPERATORS_DIR.is_dir()

    def test_allowed_headers_all_exist(self):
        for header in opdev.ALLOWED_HEADERS:
            path = opdev.OPERATOR_API_DIR / header
            assert path.is_file(), f"Allowlisted header missing: {header}"
