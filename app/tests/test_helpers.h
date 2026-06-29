#pragma once
// Minimal, dependency-free test harness for Vivid's headless tests.
// Pattern adopted from vivid-classic's tests/test_helpers.h: stderr-based
// assertions, a process exit code of 0 == pass, non-zero == failure count.
// Each test file writes its own `int main()` and ends with `return summary("name");`.
#include <cstdio>
#include <cmath>

namespace vivid::test {

inline int& fail_count()  { static int n = 0; return n; }
inline int& check_count() { static int n = 0; return n; }

inline void report(bool ok, const char* expr, const char* file, int line) {
    ++check_count();
    if (!ok) {
        ++fail_count();
        std::fprintf(stderr, "  FAIL %s:%d  %s\n", file, line, expr);
    }
}

inline int summary(const char* name) {
    if (fail_count() == 0) {
        std::fprintf(stderr, "ok   %s (%d checks)\n", name, check_count());
        return 0;
    }
    std::fprintf(stderr, "FAIL %s (%d/%d checks failed)\n", name, fail_count(), check_count());
    return 1;
}

}  // namespace vivid::test

#define CHECK(cond)         ::vivid::test::report((cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, e) ::vivid::test::report(std::fabs((double)(a) - (double)(b)) <= (e), \
                                                  #a " ~= " #b, __FILE__, __LINE__)
