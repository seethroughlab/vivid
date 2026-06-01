#include "runtime/graph/graph.h"
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include "test_helpers.h"

static ScopedTempDir* g_tmp = nullptr;

static std::string write_temp(const char* name, const char* content) {
    std::string path = (g_tmp->path / (std::string(name) + ".json")).string();
    std::ofstream f(path);
    f << content;
    return path;
}

int main() {
    ScopedTempDir tmp("graph");
    g_tmp = &tmp;

#include "test_graph_load_and_variations.inc"
#include "test_graph_variations_and_presets.inc"
#include "test_graph_session_schema.inc"
#include "test_graph_schema_and_versioning.inc"
#include "test_graph_sticky_notes_and_bridges.inc"
#include "test_graph_rename_node.inc"

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
