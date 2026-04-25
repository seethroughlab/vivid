#include "runtime/operators/operator_source_docs.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include "test_helpers.h"

namespace fs = std::filesystem;

static bool g_all_ok = true;

static std::string find_repo_root() {
    for (const auto& candidate : {
             fs::current_path(),
             fs::current_path().parent_path(),
             fs::current_path().parent_path().parent_path(),
         }) {
        if (fs::exists(candidate / "src" / "operator_api" / "operator.h"))
            return candidate.string();
    }
    return {};
}

static const nlohmann::json* find_named_doc(const nlohmann::json& arr, const char* name) {
    if (!arr.is_array()) return nullptr;
    for (const auto& item : arr) {
        if (item.contains("name") && item["name"].is_string() &&
            item["name"].get<std::string>() == name) {
            return &item;
        }
    }
    return nullptr;
}

int main() {
    std::fprintf(stderr, "\n=== Test: OperatorSourceDocs ===\n\n");

    vivid::OperatorSourceDocs resolver;

    // Fixture root: direct source parsing, multiline continuations, and no-doc fallback.
    const fs::path fixture_root = fs::current_path() / ".test_source_docs_root";
    fs::remove_all(fixture_root);
    fs::create_directories(fixture_root / "operators" / "control" / "mock_op");
    {
        std::ofstream ofs(fixture_root / "operators" / "control" / "mock_op" / "mock_op.cpp");
        ofs << R"(#include "operator_api/operator.h"

/**
 * @brief Mock operator for source-doc parsing tests.
 *
 * First body line.
 * Second body line.
 * @tip First tip line
 *   continues on the next line.
 * @see ClockAu, EnvelopeAu
 * @param amount Amount control.
 *   Continued amount detail.
 * @input gate Gate input doc.
 * @output value Value output doc.
 * @recipe MockOp/value -> Gain/gain
 * @pitfall This is only a fixture.
 * @family test-fixture
 * @best_used_with Gain, Filter
 * @common_companions ClockAu, EnvelopeAu
 */
struct MockOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "MockOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(MockOp)
)";
    }
    {
        std::ofstream ofs(fixture_root / "operators" / "control" / "mock_op" / "no_doc_op.cpp");
        ofs << R"(#include "operator_api/operator.h"
struct NoDocOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "NoDocOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};
VIVID_REGISTER(NoDocOp)
)";
    }

    resolver.set_core_source_root(fixture_root.string());
    auto mock = resolver.resolve_core("MockOp");
    check(mock.is_object(), "MockOp docs resolve");
    if (mock.is_object()) {
        check(mock.value("has_docs", false), "MockOp has_docs=true");
        check(mock.contains("brief") && mock["brief"].is_string() &&
                  mock["brief"].get<std::string>() == "Mock operator for source-doc parsing tests.",
              "MockOp parses @brief");
        check(mock.contains("body") && mock["body"].is_string() &&
                  mock["body"].get<std::string>() == "First body line.\nSecond body line.",
              "MockOp preserves multiline body");
        check(mock.contains("source_path") && mock["source_path"].is_string() &&
                  mock["source_path"].get<std::string>() ==
                      "operators/control/mock_op/mock_op.cpp",
              "MockOp reports source_path");
        check(mock.contains("operator_family") && mock["operator_family"].is_string() &&
                  mock["operator_family"].get<std::string>() == "test-fixture",
              "MockOp parses @family");
        check(mock.contains("tips") && mock["tips"].is_array() && mock["tips"].size() == 1 &&
                  mock["tips"][0].get<std::string>() == "First tip line continues on the next line.",
              "MockOp preserves multiline @tip");
        check(mock.contains("related") && mock["related"].is_array() &&
                  mock["related"].size() == 2,
              "MockOp parses @see");
        const auto* amount = find_named_doc(mock["params"], "amount");
        check(amount && amount->contains("doc") &&
                  (*amount)["doc"].get<std::string>() == "Amount control. Continued amount detail.",
              "MockOp parses multiline @param");
        const auto* gate = find_named_doc(mock["inputs"], "gate");
        check(gate && gate->contains("doc") &&
                  (*gate)["doc"].get<std::string>() == "Gate input doc.",
              "MockOp parses @input");
        const auto* value = find_named_doc(mock["outputs"], "value");
        check(value && value->contains("doc") &&
                  (*value)["doc"].get<std::string>() == "Value output doc.",
              "MockOp parses @output");
    }

    auto no_doc = resolver.resolve_core("NoDocOp");
    check(no_doc.is_object(), "NoDocOp resolution returns metadata object");
    if (no_doc.is_object()) {
        check(no_doc.contains("has_docs") && no_doc["has_docs"].is_boolean() &&
                  !no_doc["has_docs"].get<bool>(),
              "NoDocOp has_docs=false without doc block");
    }

    const std::string repo_root = find_repo_root();
    check(!repo_root.empty(), "resolved repo root for wrapper fixtures");
    if (!repo_root.empty()) {
        resolver.set_core_source_root(repo_root);

        auto clock_au = resolver.resolve_core("Clock");
        check(clock_au.is_object() && clock_au.value("has_docs", false),
              "ClockAu resolves docs from source");
        if (clock_au.is_object()) {
            check(clock_au.contains("source_path") && clock_au["source_path"].is_string() &&
                      clock_au["source_path"].get<std::string>() ==
                          "operators/control/clock/clock_core.h",
                  "ClockAu resolves shared ClockCore docs");
            const auto* beat_phase = find_named_doc(clock_au["outputs"], "beat_phase");
            check(beat_phase && beat_phase->contains("doc") &&
                      !(*beat_phase)["doc"].get<std::string>().empty(),
                  "ClockAu exposes output docs from ClockCore");
        }

        auto env_au = resolver.resolve_core("Envelope");
        check(env_au.is_object() && env_au.value("has_docs", false),
              "EnvelopeAu resolves docs from source");
        if (env_au.is_object()) {
            check(env_au.contains("source_path") && env_au["source_path"].is_string() &&
                      env_au["source_path"].get<std::string>() ==
                          "operators/control/envelope/envelope.h",
                  "EnvelopeAu resolves shared Envelope docs");
            const auto* gate = find_named_doc(env_au["inputs"], "gate");
            check(gate && gate->contains("doc") &&
                      (*gate)["doc"].get<std::string>().find("Rising edges start the ADSR") != std::string::npos,
                  "EnvelopeAu exposes gate guidance");
            check(env_au.contains("pitfalls") && env_au["pitfalls"].is_array() &&
                      !env_au["pitfalls"].empty() &&
                      env_au["pitfalls"][0].get<std::string>().find("beat_phase") != std::string::npos,
                  "EnvelopeAu includes beat_phase pitfall guidance");
        }

    }

    fs::remove_all(fixture_root);

    if (!g_all_ok) {
        std::fprintf(stderr, "\n=== SOME FAILED ===\n");
        return 1;
    }
    std::fprintf(stderr, "\n=== ALL PASSED ===\n");
    return 0;
}
