#include "runtime/core/source_syntax_parser.h"
#include <cstdio>

int main() {
    vivid::SourceSyntaxParser parser;
    auto record = parser.parse("/Users/jeff/Developer/vivid/tests/fixtures/source_syntax_parser/multiple_types.cpp");
    
    printf("valid=%d types=%d register_calls=%d doc_comments=%d includes=%d symbols=%d\n",
           record.valid, (int)record.type_definitions.size(),
           (int)record.register_calls.size(), (int)record.doc_comment_ranges.size(),
           (int)record.include_targets.size(), (int)record.symbol_definitions.size());
    
    for (const auto& t : record.type_definitions) {
        printf("  type: name='%s' kind='%s' lines=%d-%d bases=%d\n",
               t.name.c_str(), t.kind.c_str(), t.start_line, t.end_line,
               (int)t.base_class_names.size());
        for (const auto& b : t.base_class_names) {
            printf("    base: '%s'\n", b.c_str());
        }
    }
    for (const auto& r : record.register_calls) {
        printf("  register: name='%s' arg='%s' line=%d\n",
               r.macro_name.c_str(), r.type_name.c_str(), r.line);
    }
    for (const auto& d : record.doc_comment_ranges) {
        printf("  doc_comment: lines=%d-%d\n", d.start_line, d.end_line);
    }
    for (const auto& inc : record.include_targets) {
        printf("  include: '%s' system=%d\n", inc.quoted_path.c_str(), inc.is_system);
    }
    for (const auto& s : record.symbol_definitions) {
        printf("  symbol: name='%s' kind='%s' lines=%d-%d\n",
               s.name.c_str(), s.kind.c_str(), s.start_line, s.end_line);
    }
    
    return 0;
}
