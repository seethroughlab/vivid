#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tree_sitter/api.h>
extern "C" TSLanguage* tree_sitter_cpp();

static void find_all(TSNode node, const char* src, uint32_t src_len, int depth) {
    const char* type = ts_node_type(node);
    if (strcmp(type, "struct_specifier") == 0) {
        uint32_t sb = ts_node_start_byte(node);
        uint32_t eb = ts_node_end_byte(node);
        printf("\n=== struct_specifier [%u,%u] ===\n", sb, eb);
        printf("text: %.*s\n", (int)(eb - sb), src + sb);
        uint32_t nc = ts_node_named_child_count(node);
        printf("named children: %u\n", nc);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode nc_node = ts_node_named_child(node, i);
            uint32_t nsb = ts_node_start_byte(nc_node);
            uint32_t neb = ts_node_end_byte(nc_node);
            printf("  [%u] type=%s [%u,%u] text=%.*s\n", i,
                   ts_node_type(nc_node), nsb, neb,
                   (int)(neb - nsb), src + nsb);
        }
    }
    if (strcmp(type, "call_expression") == 0) {
        uint32_t sb = ts_node_start_byte(node);
        uint32_t eb = ts_node_end_byte(node);
        printf("\n=== call_expression [%u,%u] ===\n", sb, eb);
        printf("text: %.*s\n", (int)(eb - sb), src + sb);
        uint32_t nc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode nc_node = ts_node_named_child(node, i);
            uint32_t nsb = ts_node_start_byte(nc_node);
            uint32_t neb = ts_node_end_byte(nc_node);
            printf("  [%u] type=%s [%u,%u] text=%.*s\n", i,
                   ts_node_type(nc_node), nsb, neb,
                   (int)(neb - nsb), src + nsb);
        }
    }
    if (strcmp(type, "comment") == 0) {
        uint32_t sb = ts_node_start_byte(node);
        uint32_t eb = ts_node_end_byte(node);
        printf("\n=== comment [%u,%u] ===\n", sb, eb);
        printf("text: %.*s\n", (int)(eb - sb), src + sb);
    }
    if (strcmp(type, "preproc_include") == 0) {
        uint32_t sb = ts_node_start_byte(node);
        uint32_t eb = ts_node_end_byte(node);
        printf("\n=== preproc_include [%u,%u] ===\n", sb, eb);
        printf("text: %.*s\n", (int)(eb - sb), src + sb);
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode cc_node = ts_node_child(node, i);
            uint32_t csb = ts_node_start_byte(cc_node);
            uint32_t ceb = ts_node_end_byte(cc_node);
            printf("  [%u] type=%s [%u,%u] text=%.*s\n", i,
                   ts_node_type(cc_node), csb, ceb,
                   (int)(ceb - csb), src + csb);
        }
    }
    
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++) {
        find_all(ts_node_child(node, i), src, src_len, depth + 1);
    }
}

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
    
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_cpp());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, src, sz);
    
    TSNode root = ts_tree_root_node(tree);
    printf("root type: %s\n", ts_node_type(root));
    
    find_all(root, src, sz, 0);
    
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    delete[] src;
    return 0;
}
