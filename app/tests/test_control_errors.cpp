// Headless tests for the control server's reply shape + stable error codes
// (app/src/cli/control_errors.h). The agent/MCP client branches on `code`, so
// the codes are a contract: assert their exact stable strings and the reply JSON.
#include "cli/control_errors.h"
#include "test_helpers.h"

using namespace vivid::control;
using nlohmann::json;

static void test_ok() {
    json a = ok();
    CHECK(a["ok"] == true);

    json b = ok({ {"id", 7}, {"count", 3} });   // extras preserved alongside ok
    CHECK(b["ok"] == true);
    CHECK(b["id"] == 7);
    CHECK(b["count"] == 3);
}

static void test_err() {
    json e = err(code::kOutOfRange, "track 9 out of range [0,4)");
    CHECK(e["ok"] == false);
    CHECK(e["code"] == "out_of_range");
    CHECK(e["error"] == "track 9 out of range [0,4)");
}

static void test_code_strings_are_stable() {
    // These strings are a wire contract — pin them so a rename can't pass silently.
    CHECK(std::string(code::kBadJson)       == "bad_json");
    CHECK(std::string(code::kUnknownMethod) == "unknown_method");
    CHECK(std::string(code::kNoSession)     == "no_session");
    CHECK(std::string(code::kNoGraph)       == "no_graph");
    CHECK(std::string(code::kNoVgraph)      == "no_vgraph");
    CHECK(std::string(code::kNoTransport)   == "no_transport");
    CHECK(std::string(code::kBadArg)        == "bad_arg");
    CHECK(std::string(code::kOutOfRange)    == "out_of_range");
    CHECK(std::string(code::kNotFound)      == "not_found");
    CHECK(std::string(code::kIoError)       == "io_error");
    CHECK(std::string(code::kInternal)      == "internal");
    CHECK(std::string(code::kTimeout)       == "timeout");
}

int main() {
    test_ok();
    test_err();
    test_code_strings_are_stable();
    return vivid::test::summary("test_control_errors");
}
