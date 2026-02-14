// CLI Command Tests for Vivid
// ============================
// Tests the JSON output of vivid build, params, graph, and docs subcommands.
// Each test runs vivid as a subprocess via popen(), captures stdout, and validates JSON.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <cstdio>
#include <array>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// Get vivid binary path from CMake definition
static std::string getVividPath() {
    return VIVID_BINARY_PATH;
}

// Get source directory from CMake definition
static std::string getSourceDir() {
    return VIVID_SOURCE_DIR;
}

// Run a vivid command and capture stdout + exit code
static std::pair<std::string, int> runCommand(const std::string& args) {
    std::string cmd = "\"" + getVividPath() + "\" " + args;
#ifdef _WIN32
    cmd += " 2>NUL";
#else
    cmd += " 2>/dev/null";
#endif

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {"", -1};
    }

    std::string output;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = pclose(pipe);
#ifndef _WIN32
    // On Unix, pclose returns the status from waitpid; extract exit code
    if (WIFEXITED(status)) {
        status = WEXITSTATUS(status);
    }
#endif

    return {output, status};
}

// Extract JSON from output that may contain log lines before/after the JSON.
// Looks for lines starting with '{' or '[' (at column 0) and tries to parse
// a complete JSON document from that position.
static std::string extractJson(const std::string& output) {
    // Scan for potential JSON start positions (lines beginning with { or [)
    // Try from the end since JSON output is typically last
    std::vector<size_t> candidates;
    for (size_t i = 0; i < output.size(); ++i) {
        if ((output[i] == '{' || output[i] == '[') &&
            (i == 0 || output[i - 1] == '\n')) {
            candidates.push_back(i);
        }
    }

    // Try candidates in reverse order (last JSON block is most likely the result)
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        size_t start = *it;
        std::string sub = output.substr(start);
        if (json::accept(sub)) {
            // Parse to find the exact end, then return just the JSON portion
            return sub;
        }
        // json::accept may fail if there's trailing text; try to find the matching close
        char opener = output[start];
        char closer = (opener == '{') ? '}' : ']';
        int depth = 0;
        bool inString = false;
        for (size_t j = start; j < output.size(); ++j) {
            char c = output[j];
            if (c == '"' && (j == start || output[j - 1] != '\\')) {
                inString = !inString;
            }
            if (inString) continue;
            if (c == opener) depth++;
            else if (c == closer) depth--;
            if (depth == 0) {
                std::string candidate = output.substr(start, j - start + 1);
                if (json::accept(candidate)) {
                    return candidate;
                }
                break;
            }
        }
    }
    return "";
}

// Parse stdout as JSON, failing the test with context on parse error
static json parseJson(const std::string& output) {
    std::string jsonStr = extractJson(output);
    INFO("Raw output (last 500 chars): " << output.substr(output.size() > 500 ? output.size() - 500 : 0));
    INFO("Extracted JSON: " << jsonStr);
    REQUIRE(!jsonStr.empty());
    json j;
    try {
        j = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        INFO("JSON parse error: " << e.what());
        FAIL("Failed to parse JSON from command output");
    }
    return j;
}

// =============================================================================
// vivid build
// =============================================================================

TEST_CASE("build: success on valid project", "[cli][build]") {
    std::string project = getSourceDir() + "/projects/getting-started/01-hello-chain";
    auto [output, exitCode] = runCommand("build \"" + project + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j["success"].get<bool>() == true);
}

TEST_CASE("build: structured errors on broken chain", "[cli][build]") {
    std::string project = getSourceDir() + "/tests/fixtures/broken-chain";
    auto [output, exitCode] = runCommand("build \"" + project + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode != 0);

    json j = parseJson(output);
    REQUIRE(j["success"].get<bool>() == false);

    // Should have either "errors" array or "message" string
    if (j.contains("errors")) {
        REQUIRE(j["errors"].is_array());
        REQUIRE(j["errors"].size() > 0);

        // Each error should have at least a message
        for (const auto& err : j["errors"]) {
            REQUIRE(err.contains("message"));
            REQUIRE(!err["message"].get<std::string>().empty());
        }
    } else {
        REQUIRE(j.contains("message"));
        REQUIRE(!j["message"].get<std::string>().empty());
    }
}

TEST_CASE("build: fails on nonexistent path", "[cli][build]") {
    auto [output, exitCode] = runCommand("build /nonexistent/path/to/project");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode != 0);
}

// =============================================================================
// vivid params
// =============================================================================

TEST_CASE("params: lists params for parameterized project", "[cli][params]") {
    std::string project = getSourceDir() + "/projects/getting-started/03-parameters";
    auto [output, exitCode] = runCommand("params \"" + project + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("parameters"));
    REQUIRE(j["parameters"].is_array());
    REQUIRE(j["parameters"].size() > 0);

    // Each parameter should have required fields
    for (const auto& p : j["parameters"]) {
        REQUIRE(p.contains("operator"));
        REQUIRE(p.contains("param"));
        REQUIRE(p.contains("type"));
        REQUIRE(p.contains("value"));
        REQUIRE(p.contains("min"));
        REQUIRE(p.contains("max"));
        REQUIRE(p.contains("default"));
    }
}

TEST_CASE("params: known param values are correct", "[cli][params]") {
    std::string project = getSourceDir() + "/projects/getting-started/03-parameters";
    auto [output, exitCode] = runCommand("params \"" + project + "\"");

    REQUIRE(exitCode == 0);
    json j = parseJson(output);

    // Find noise.scale — should be 4.0
    bool foundNoiseScale = false;
    bool foundBlurRadius = false;

    for (const auto& p : j["parameters"]) {
        std::string op = p["operator"].get<std::string>();
        std::string param = p["param"].get<std::string>();

        if (op == "noise" && param == "scale") {
            foundNoiseScale = true;
            REQUIRE(p["value"].get<float>() == Catch::Approx(4.0f));
        }
        if (op == "blur" && param == "radius") {
            foundBlurRadius = true;
        }
    }

    REQUIRE(foundNoiseScale);
    REQUIRE(foundBlurRadius);
}

TEST_CASE("params: works on minimal project", "[cli][params]") {
    std::string project = getSourceDir() + "/projects/getting-started/01-hello-chain";
    auto [output, exitCode] = runCommand("params \"" + project + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("parameters"));
    REQUIRE(j["parameters"].is_array());

    // 01-hello-chain has a Noise operator which has params
    REQUIRE(j["parameters"].size() > 0);
}

// =============================================================================
// vivid graph
// =============================================================================

TEST_CASE("graph: dumps topology for multi-op chain", "[cli][graph]") {
    std::string project = getSourceDir() + "/tests/fixtures/feedback-effects";
    auto [output, exitCode] = runCommand("graph \"" + project + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("operators"));
    REQUIRE(j["operators"].is_array());

    // feedback-effects has 4 operators: noise, feedback, ramp, comp
    REQUIRE(j["operators"].size() == 4);

    // Visual output should be "comp"
    REQUIRE(j.contains("visualOutput"));
    REQUIRE(j["visualOutput"].get<std::string>() == "comp");

    // No audio output
    REQUIRE(j.contains("audioOutput"));
    REQUIRE(j["audioOutput"].is_null());
}

TEST_CASE("graph: captures input connections", "[cli][graph]") {
    std::string project = getSourceDir() + "/tests/fixtures/feedback-effects";
    auto [output, exitCode] = runCommand("graph \"" + project + "\"");

    REQUIRE(exitCode == 0);
    json j = parseJson(output);

    // Build a map of operator name -> operator data
    std::map<std::string, json> ops;
    for (const auto& op : j["operators"]) {
        ops[op["name"].get<std::string>()] = op;
    }

    // feedback should have "noise" as input
    REQUIRE(ops.count("feedback"));
    REQUIRE(ops["feedback"].contains("inputs"));
    auto feedbackInputs = ops["feedback"]["inputs"];
    REQUIRE(feedbackInputs.is_array());
    bool feedbackHasNoise = false;
    for (const auto& inp : feedbackInputs) {
        if (inp.get<std::string>() == "noise") feedbackHasNoise = true;
    }
    REQUIRE(feedbackHasNoise);

    // comp should have 2 inputs (feedback + ramp)
    REQUIRE(ops.count("comp"));
    auto compInputs = ops["comp"]["inputs"];
    REQUIRE(compInputs.is_array());
    REQUIRE(compInputs.size() == 2);
}

TEST_CASE("graph: works on single-operator chain", "[cli][graph]") {
    std::string project = getSourceDir() + "/projects/getting-started/01-hello-chain";
    auto [output, exitCode] = runCommand("graph \"" + project + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("operators"));
    REQUIRE(j["operators"].is_array());
    REQUIRE(j["operators"].size() > 0);

    // Visual output should be "noise"
    REQUIRE(j["visualOutput"].get<std::string>() == "noise");
}

// =============================================================================
// vivid docs
// =============================================================================

TEST_CASE("docs: search finds known term", "[cli][docs]") {
    auto [output, exitCode] = runCommand("docs search noise");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.is_array());
    REQUIRE(j.size() > 0);

    // Each result should have file, section, context, score
    for (const auto& match : j) {
        REQUIRE(match.contains("file"));
        REQUIRE(match.contains("section"));
        REQUIRE(match.contains("context"));
        REQUIRE(match.contains("score"));
    }
}

TEST_CASE("docs: search returns exit 1 for no results", "[cli][docs]") {
    auto [output, exitCode] = runCommand("docs search xyzzy_nonexistent_zzz");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode != 0);
}

TEST_CASE("docs: recipe lists all", "[cli][docs]") {
    auto [output, exitCode] = runCommand("docs recipe");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("count"));
    REQUIRE(j["count"].get<int>() > 0);
    REQUIRE(j.contains("recipes"));
    REQUIRE(j["recipes"].is_array());
    REQUIRE(j["recipes"].size() > 0);

    // Each recipe should have a name
    for (const auto& recipe : j["recipes"]) {
        REQUIRE(recipe.contains("name"));
        REQUIRE(!recipe["name"].get<std::string>().empty());
    }
}

TEST_CASE("docs: recipe shows specific recipe", "[cli][docs]") {
    // First get the list to find a valid recipe name
    auto [listOutput, listExit] = runCommand("docs recipe");
    REQUIRE(listExit == 0);
    json list = parseJson(listOutput);
    REQUIRE(list["recipes"].size() > 0);

    std::string firstName = list["recipes"][0]["name"].get<std::string>();
    auto [output, exitCode] = runCommand("docs recipe \"" + firstName + "\"");

    INFO("Exit code: " << exitCode);
    INFO("Recipe name: " << firstName);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("name"));
    REQUIRE(j.contains("code"));
    REQUIRE(!j["code"].get<std::string>().empty());
}

TEST_CASE("docs: example finds Noise", "[cli][docs]") {
    auto [output, exitCode] = runCommand("docs example Noise");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("operator"));
    REQUIRE(j["operator"].get<std::string>() == "Noise");
    REQUIRE(j.contains("count"));
    REQUIRE(j["count"].get<int>() > 0);
    REQUIRE(j.contains("examples"));
    REQUIRE(j["examples"].is_array());
    REQUIRE(j["examples"].size() > 0);
}

TEST_CASE("docs: example returns exit 1 for unknown operator", "[cli][docs]") {
    auto [output, exitCode] = runCommand("docs example XyzzyNonexistent");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode != 0);

    json j = parseJson(output);
    REQUIRE(j["count"].get<int>() == 0);
}

TEST_CASE("docs: example finds Bloom", "[cli][docs]") {
    auto [output, exitCode] = runCommand("docs example Bloom");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j["count"].get<int>() > 0);
}
