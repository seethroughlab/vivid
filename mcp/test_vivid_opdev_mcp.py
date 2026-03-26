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
        envs_found = {op["env"] for op in result["operators"]}
        assert "control" in envs_found
        assert "audio" in envs_found
        assert "gpu" in envs_found

    @pytest.mark.anyio
    async def test_filter_by_env(self):
        result = json.loads(await opdev.list_example_operators("audio"))
        assert result["ok"]
        assert result["count"] >= 5
        for op in result["operators"]:
            assert op["env"] == "audio"

    @pytest.mark.anyio
    async def test_known_operators_present(self):
        result = json.loads(await opdev.list_example_operators())
        names = {(op["env"], op["name"]) for op in result["operators"]}
        assert ("control", "lfo") in names
        assert ("audio", "gain") in names
        assert ("gpu", "noise") in names

    @pytest.mark.anyio
    async def test_invalid_env(self):
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
        assert result["env"] == "control"
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
    async def test_invalid_env(self):
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


# ---------------------------------------------------------------------------
# search_example_operators
# ---------------------------------------------------------------------------

class TestSearchExampleOperators:
    @pytest.mark.anyio
    async def test_search_by_keyword(self):
        result = json.loads(await opdev.search_example_operators("lfo"))
        assert result["ok"]
        assert result["count"] >= 1
        names = [m["name"] for m in result["matches"]]
        assert "lfo" in names

    @pytest.mark.anyio
    async def test_search_source_content(self):
        result = json.loads(await opdev.search_example_operators("FilePath"))
        assert result["ok"]
        assert result["count"] >= 1
        # Should find operators that use FilePath in their source
        source_matches = [m for m in result["matches"] if m["matched_in"] == "source"]
        assert len(source_matches) >= 1

    @pytest.mark.anyio
    async def test_search_with_env_filter(self):
        result = json.loads(await opdev.search_example_operators("gain", env="control"))
        assert result["ok"]
        for m in result["matches"]:
            assert m["env"] == "control"

    @pytest.mark.anyio
    async def test_search_no_matches(self):
        result = json.loads(await opdev.search_example_operators("zzz_nonexistent_keyword_zzz"))
        assert result["ok"]
        assert result["count"] == 0
        assert result["matches"] == []

    @pytest.mark.anyio
    async def test_search_invalid_env(self):
        result = json.loads(await opdev.search_example_operators("lfo", env="invalid"))
        assert not result["ok"]
        assert "Unknown env" in result["error"]

    @pytest.mark.anyio
    async def test_search_empty_query(self):
        result = json.loads(await opdev.search_example_operators(""))
        assert not result["ok"]

    @pytest.mark.anyio
    async def test_search_results_capped(self):
        # A broad search should not exceed MAX_SEARCH_RESULTS
        result = json.loads(await opdev.search_example_operators("float"))
        assert result["ok"]
        assert result["count"] <= opdev.MAX_SEARCH_RESULTS


# ---------------------------------------------------------------------------
# get_capability_guidance
# ---------------------------------------------------------------------------

class TestGetCapabilityGuidance:
    @pytest.mark.anyio
    async def test_known_capability(self):
        result = json.loads(await opdev.get_capability_guidance("file_drop"))
        assert result["ok"]
        assert result["capability"] == "file_drop"
        assert "explanation" in result
        assert "doc_topic" in result
        assert "example_operators" in result
        assert "code_snippet" in result
        assert len(result["example_operators"]) >= 1

    @pytest.mark.anyio
    async def test_all_capabilities_valid(self):
        for cap in opdev.CAPABILITY_GUIDANCE:
            result = json.loads(await opdev.get_capability_guidance(cap))
            assert result["ok"], f"Capability '{cap}' failed"
            assert "explanation" in result
            assert "doc_topic" in result
            assert "example_operators" in result
            assert "code_snippet" in result

    @pytest.mark.anyio
    async def test_unknown_capability(self):
        result = json.loads(await opdev.get_capability_guidance("nonexistent"))
        assert not result["ok"]
        assert "Unknown capability" in result["error"]
        # Should list available capabilities
        assert "file_drop" in result["error"]
        assert "child_op" in result["error"]

    @pytest.mark.anyio
    async def test_case_insensitive(self):
        result = json.loads(await opdev.get_capability_guidance("FILE_DROP"))
        assert result["ok"]
        assert result["capability"] == "file_drop"


# ---------------------------------------------------------------------------
# recommend_starting_point
# ---------------------------------------------------------------------------

class TestRecommendStartingPoint:
    @pytest.mark.anyio
    async def test_goal_mentions_operator(self):
        result = json.loads(await opdev.recommend_starting_point("something like the lfo operator"))
        assert result["ok"]
        assert result["approach"] == "clone_example"
        assert "lfo" in result["reasoning"]
        assert len(result["next_steps"]) >= 2

    @pytest.mark.anyio
    async def test_goal_mentions_capability(self):
        result = json.loads(await opdev.recommend_starting_point("operator that accepts file drops"))
        assert result["ok"]
        assert result["approach"] == "scaffold"
        assert "file_drop" in result["reasoning"]
        assert any("get_capability_guidance" in step for step in result["next_steps"])

    @pytest.mark.anyio
    async def test_generic_goal(self):
        result = json.loads(await opdev.recommend_starting_point("a cool effect"))
        assert result["ok"]
        assert result["approach"] == "scaffold"
        assert len(result["next_steps"]) >= 2

    @pytest.mark.anyio
    async def test_audio_env_inference(self):
        result = json.loads(await opdev.recommend_starting_point("an audio synthesizer"))
        assert result["ok"]
        assert "audio" in json.dumps(result["next_steps"])

    @pytest.mark.anyio
    async def test_gpu_env_inference(self):
        result = json.loads(await opdev.recommend_starting_point("a gpu shader effect"))
        assert result["ok"]
        assert "gpu" in json.dumps(result["next_steps"])

    @pytest.mark.anyio
    async def test_empty_goal(self):
        result = json.loads(await opdev.recommend_starting_point(""))
        assert not result["ok"]
