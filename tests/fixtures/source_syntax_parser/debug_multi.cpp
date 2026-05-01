#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tree_sitter/api.h>
extern "C" TSLanguage* tree_sitter_cpp();

int main() {
    const char* filepath = "/Users/jeff/Developer/vivid/tests/fixtures/source_syntax_parser/multiple_types.cpp";
    FILE* f = fopen(filepath, "r");
    if (!f) { printf("cannot open\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = new char[sz + 1];
    fread(src, 1, sz, f);
    src[sz] = '\0';
    fclose(f);
    
    printf("Source length: %ld\n", sz);
    printf("Source:\n%s\n---\n", src);
    
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_cpp());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, src, sz);
    
    TSNode root = ts_tree_root_node(tree);
    
    // Walk and find struct_specifier nodes
    uint32_t cc = ts_node_child_count(root);
    for (uint32_t i = 0; i < cc; i++) {
        TSNode child = ts_node_child(root, i);
        if (strcmp(ts_node_type(child), "translation_unit") == 0) {
            uint32_t cc2 = ts_node_child_count(child);
            for (uint32_t j = 0; j < cc2; j++) {
                TSNode sub = ts_node_child(child, j);
                if (strcmp(ts_node_type(sub), "struct_specifier") == 0) {
                    uint32_t sb = ts_node_start_byte(sub);
                    uint32_t eb = ts_node_end_byte(sub);
                    printf("\n=== struct at [%u,%u] ===\n", sb, eb);
                    printf("text: %.*s\n", eb - sb, src + sb);
                    
                    uint32_t nc = ts_node_named_child_count(sub);
                    printf("named children: %u\n", nc);
                    for (uint32_t k = 0; k < nc; k++) {
                        TSNode nc_node = ts_node_named_child(sub, k);
                        uint32_t nsb = ts_node_start_byte(nc_node);
                        uint32_t neb = ts_node_end_byte(nc_node);
                        printf("  [%u] type=%s [%u,%u] text=%.*s\n", k,
                               ts_node_type(nc_node), nsb, neb,
                               neb - nsb, src + nsb);
                    }
                }
            }
        }
    }
    
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    delete[] src;
    return 0;
}
