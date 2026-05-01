#include "runtime/core/source_syntax_parser.h"
#include <cstdio>

int main() {
    // Test with the fixture file
    auto record = vivid::SourceSyntaxParser::parse(
        "/Users/jeff/Developer/vivid/tests/fixtures/source_syntax_parser/multiple_types.cpp");
    
    printf("valid=%d\n", record.valid);
    printf("path=%s\n", record.path.c_str());
    printf("type_definitions=%zu\n", record.type_definitions.size());
    for (const auto& td : record.type_definitions) {
        printf("  type: name='%s' kind='%s' start=%d end=%d bases=%zu\n",
               td.name.c_str(), td.kind.c_str(), td.start_line, td.end_line,
               td.base_class_names.size());
        for (const auto& base : td.base_class_names) {
            printf("    base: '%s'\n", base.c_str());
        }
    }
    printf("register_calls=%zu\n", record.register_calls.size());
    for (const auto& rc : record.register_calls) {
        printf("  register: macro='%s' type='%s' line=%d\n",
               rc.macro_name.c_str(), rc.type_name.c_str(), rc.line);
    }
    printf("doc_comment_ranges=%zu\n", record.doc_comment_ranges.size());
    for (const auto& dcr : record.doc_comment_ranges) {
        printf("  doc: start=%d end=%d\n", dcr.start_line, dcr.end_line);
    }
    printf("include_targets=%zu\n", record.include_targets.size());
    for (const auto& inc : record.include_targets) {
        printf("  include: path='%s' system=%d\n", inc.quoted_path.c_str(), inc.is_system);
    }
    printf("symbol_definitions=%zu\n", record.symbol_definitions.size());
    for (const auto& sym : record.symbol_definitions) {
        printf("  symbol: name='%s' kind='%s' start=%d end=%d\n",
               sym.name.c_str(), sym.kind.c_str(), sym.start_line, sym.end_line);
    }
    return 0;
}
