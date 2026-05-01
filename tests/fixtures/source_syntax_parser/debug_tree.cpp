#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tree_sitter/api.h>
extern "C" TSLanguage* tree_sitter_cpp();

static void walk_dump(TSNode node, int depth) {
    const char* type = ts_node_type(node);
    uint32_t s = ts_node_start_byte(node);
    uint32_t e = ts_node_end_byte(node);
    const char* name = ts_node_string(node);
    printf("%*s type=%s name=%.*s\n", depth * 2, "", type, e - s, name);
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++) {
        walk_dump(ts_node_child(node, i), depth + 1);
    }
}

static void find_struct(TSNode node) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "struct_declaration") == 0) {
        printf("FOUND struct_declaration!\n");
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            printf("  child[%u]: type=%s\n", i, ts_node_type(child));
            uint32_t nc = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < nc; j++) {
                TSNode nc_child = ts_node_named_child(child, j);
                uint32_t ns = ts_node_start_byte(nc_child);
                uint32_t ne = ts_node_end_byte(nc_child);
                printf("    named_child[%u]: type=%s name=%.*s\n",
                       j, ts_node_type(nc_child), ne - ns, ts_node_string(nc_child) + ns);
            }
        }
    }
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++) {
        find_struct(ts_node_child(node, i));
    }
}

static void find_call(TSNode node) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "call_expression") == 0) {
        uint32_t ns = ts_node_start_byte(node);
        uint32_t ne = ts_node_end_byte(node);
        printf("FOUND call_expression: %.*s\n", ne - ns, ts_node_string(node) + ns);
        uint32_t cc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_named_child(node, i);
            uint32_t cs = ts_node_start_byte(child);
            uint32_t ce = ts_node_end_byte(child);
            printf("  named_child[%u]: type=%s name=%.*s\n",
                   i, ts_node_type(child), ce - cs, ts_node_string(child) + cs);
        }
    }
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++) {
        find_call(ts_node_child(node, i));
    }
}

static void find_include(TSNode node) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "preproc_include") == 0) {
        printf("FOUND preproc_include\n");
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            printf("  child[%u]: type=%s\n", i, ts_node_type(child));
            if (strcmp(ts_node_type(child), "string_literal") == 0) {
                uint32_t sc = ts_node_child_count(child);
                for (uint32_t j = 0; j < sc; j++) {
                    TSNode sc_child = ts_node_child(child, j);
                    uint32_t ss = ts_node_start_byte(sc_child);
                    uint32_t se = ts_node_end_byte(sc_child);
                    printf("    subchild[%u]: type=%s content=%.*s\n",
                           j, ts_node_type(sc_child), se - ss, ts_node_string(sc_child) + ss);
                }
            }
            if (strcmp(ts_node_type(child), "system_lib_string") == 0) {
                uint32_t ss = ts_node_start_byte(child);
                uint32_t se = ts_node_end_byte(child);
                printf("    system_lib_string content=%.*s\n", se - ss, ts_node_string(child) + ss);
            }
        }
    }
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++) {
        find_include(ts_node_child(node, i));
    }
}

static void find_comment(TSNode node) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "comment") == 0) {
        uint32_t s = ts_node_start_byte(node);
        uint32_t e = ts_node_end_byte(node);
        printf("FOUND comment: %.*s (line %u)\n",
               e - s, ts_node_string(node) + s,
               ts_node_start_point(node).row + 1);
    }
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++) {
        find_comment(ts_node_child(node, i));
    }
}

int main() {
    const char* source = 
        "#include \"operator_api/operator.h\"\n"
        "\n"
        "/**\n"
        " * @brief Test operator.\n"
        " */\n"
        "struct Noise : vivid::OperatorBase, vivid::GpuProcessable {\n"
        "    vivid::Param<float> scale {\"scale\", 4.0f, 0.1f, 100.0f};\n"
        "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n"
        "        out.push_back(&scale);\n"
        "    }\n"
        "};\n"
        "\n"
        "VIVID_REGISTER(Noise)\n";

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_cpp());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source, strlen(source));
    
    TSNode root = ts_tree_root_node(tree);
    
    printf("=== Tree dump ===\n");
    char* dump = ts_node_string(root);
    printf("%s\n", dump);
    free(dump);
    
    printf("\n=== First-level children ===\n");
    uint32_t count = ts_node_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(root, i);
        uint32_t cs = ts_node_start_byte(child);
        uint32_t ce = ts_node_end_byte(child);
        printf("  [%u] type=%s name=%.*s line=%u\n",
               i, ts_node_type(child), ce - cs, ts_node_string(child) + cs,
               ts_node_start_point(child).row + 1);
    }
    
    printf("\n=== Finding struct_declaration ===\n");
    find_struct(root);
    
    printf("\n=== Finding call_expression ===\n");
    find_call(root);
    
    printf("\n=== Finding preproc_include ===\n");
    find_include(root);
    
    printf("\n=== Finding comment ===\n");
    find_comment(root);
    
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 0;
}
