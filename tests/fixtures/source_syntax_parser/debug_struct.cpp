#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tree_sitter/api.h>
extern "C" TSLanguage* tree_sitter_cpp();

static void find_struct(TSNode node, const std::string& source) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "struct_specifier") == 0) {
        printf("\n=== struct_specifier ===\n");
        printf("  start_byte=%u end_byte=%u\n", ts_node_start_byte(node), ts_node_end_byte(node));
        printf("  text: %.*s\n", ts_node_end_byte(node) - ts_node_start_byte(node), ts_node_string(node));
        
        uint32_t cc = ts_node_child_count(node);
        printf("  total children: %u\n", cc);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            printf("  child[%u]: type=%s is_named=%d\n", i, ts_node_type(child),
                   ts_node_is_named(child));
            if (ts_node_is_named(child)) {
                printf("    is_named_child[%u] text: %.*s\n", i,
                       ts_node_end_byte(child) - ts_node_start_byte(child),
                       ts_node_string(child));
            }
        }
        
        uint32_t nc = ts_node_named_child_count(node);
        printf("  named children: %u\n", nc);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode child = ts_node_named_child(node, i);
            uint32_t cs = ts_node_start_byte(child);
            uint32_t ce = ts_node_end_byte(child);
            printf("  named_child[%u]: type=%s text=%.*s\n", i,
                   ts_node_type(child), ce - cs, ts_node_string(child));
            
            // For the name field, check its named children
            if (strcmp(ts_node_type(child), "name") == 0) {
                uint32_t nn = ts_node_named_child_count(child);
                printf("    name->named_children: %u\n", nn);
                for (uint32_t j = 0; j < nn; j++) {
                    TSNode nc = ts_node_named_child(child, j);
                    printf("      nc[%u]: type=%s text=%.*s\n", j,
                           ts_node_type(nc), ts_node_end_byte(nc) - ts_node_start_byte(nc),
                           ts_node_string(nc));
                }
            }
        }
        
        // Also check base_class_clause
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            if (strcmp(ts_node_type(child), "base_class_clause") == 0) {
                printf("\n  === base_class_clause ===\n");
                uint32_t bc = ts_node_child_count(child);
                for (uint32_t j = 0; j < bc; j++) {
                    TSNode bc_node = ts_node_child(child, j);
                    printf("  bc[%u]: type=%s text=%.*s\n", j,
                           ts_node_type(bc_node), ts_node_end_byte(bc_node) - ts_node_start_byte(bc_node),
                           ts_node_string(bc_node));
                    uint32_t nc2 = ts_node_named_child_count(bc_node);
                    for (uint32_t k = 0; k < nc2; k++) {
                        TSNode nc2_node = ts_node_named_child(bc_node, k);
                        printf("    nc[%u]: type=%s text=%.*s\n", k,
                               ts_node_type(nc2_node), ts_node_end_byte(nc2_node) - ts_node_start_byte(nc2_node),
                               ts_node_string(nc2_node));
                    }
                }
            }
        }
    }
    
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++) {
        find_struct(ts_node_child(node, i), source);
    }
}

int main() {
    const char* filepath = "/Users/jeff/Developer/vivid/tests/fixtures/source_syntax_parser/multiline_class.cpp";
    FILE* f = fopen(filepath, "r");
    if (!f) { printf("cannot open\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = new char[sz + 1];
    fread(src, 1, sz, f);
    src[sz] = '\0';
    fclose(f);
    
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_cpp());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, src, sz);
    
    TSNode root = ts_tree_root_node(tree);
    find_struct(root, std::string(src));
    
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    delete[] src;
    return 0;
}
