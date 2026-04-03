// Unit tests for TextEditState and text editing free functions.

#include "ui/text_edit.h"
#include <cassert>
#include <cstdio>

using namespace vivid::ui;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define TEST(name) static void test_##name(); \
    static struct Register_##name { Register_##name() { tests.push_back({#name, test_##name}); } } reg_##name; \
    static void test_##name()

#include <vector>
#include <functional>

struct TestEntry { const char* name; std::function<void()> fn; };
static std::vector<TestEntry> tests;

// --- Tests ---

TEST(insert_at_end) {
    std::string buf = "abc";
    TextEditState st;
    st.reset(3);
    text_edit_insert(buf, st, "d");
    CHECK(buf == "abcd");
    CHECK(st.cursor == 4);
}

TEST(insert_at_beginning) {
    std::string buf = "abc";
    TextEditState st;
    st.cursor = 0;
    text_edit_insert(buf, st, "x");
    CHECK(buf == "xabc");
    CHECK(st.cursor == 1);
}

TEST(insert_in_middle) {
    std::string buf = "abc";
    TextEditState st;
    st.cursor = 1;
    text_edit_insert(buf, st, "XY");
    CHECK(buf == "aXYbc");
    CHECK(st.cursor == 3);
}

TEST(insert_replaces_selection) {
    std::string buf = "hello";
    TextEditState st;
    st.sel_start = 1;
    st.cursor = 4;
    text_edit_insert(buf, st, "X");
    CHECK(buf == "hXo");
    CHECK(st.cursor == 2);
    CHECK(!st.has_selection());
}

TEST(insert_with_filter) {
    std::string buf;
    TextEditState st;
    st.reset(0);
    auto digits_only = [](char c) { return c >= '0' && c <= '9'; };
    text_edit_insert(buf, st, "a1b2c3", digits_only);
    CHECK(buf == "123");
    CHECK(st.cursor == 3);
}

TEST(insert_max_len) {
    std::string buf = "ab";
    TextEditState st;
    st.reset(2);
    text_edit_insert(buf, st, "cdef", nullptr, 4);
    CHECK(buf == "abcd");
    CHECK(st.cursor == 4);
}

TEST(backspace_single_char) {
    std::string buf = "abc";
    TextEditState st;
    st.reset(3);
    text_edit_backspace(buf, st);
    CHECK(buf == "ab");
    CHECK(st.cursor == 2);
}

TEST(backspace_at_beginning) {
    std::string buf = "abc";
    TextEditState st;
    st.cursor = 0;
    text_edit_backspace(buf, st);
    CHECK(buf == "abc");
    CHECK(st.cursor == 0);
}

TEST(backspace_deletes_selection) {
    std::string buf = "hello";
    TextEditState st;
    st.sel_start = 1;
    st.cursor = 3;
    text_edit_backspace(buf, st);
    CHECK(buf == "hlo");
    CHECK(st.cursor == 1);
    CHECK(!st.has_selection());
}

TEST(backspace_in_middle) {
    std::string buf = "abcd";
    TextEditState st;
    st.cursor = 2;
    text_edit_backspace(buf, st);
    CHECK(buf == "acd");
    CHECK(st.cursor == 1);
}

TEST(delete_forward) {
    std::string buf = "abc";
    TextEditState st;
    st.cursor = 1;
    text_edit_delete_forward(buf, st);
    CHECK(buf == "ac");
    CHECK(st.cursor == 1);
}

TEST(delete_forward_at_end) {
    std::string buf = "abc";
    TextEditState st;
    st.reset(3);
    text_edit_delete_forward(buf, st);
    CHECK(buf == "abc");
    CHECK(st.cursor == 3);
}

TEST(delete_forward_with_selection) {
    std::string buf = "abcde";
    TextEditState st;
    st.sel_start = 1;
    st.cursor = 4;
    text_edit_delete_forward(buf, st);
    CHECK(buf == "ae");
    CHECK(st.cursor == 1);
    CHECK(!st.has_selection());
}

TEST(move_left) {
    TextEditState st;
    st.cursor = 3;
    text_edit_move_left(st, false);
    CHECK(st.cursor == 2);
    CHECK(!st.has_selection());
}

TEST(move_left_at_zero) {
    TextEditState st;
    st.cursor = 0;
    text_edit_move_left(st, false);
    CHECK(st.cursor == 0);
}

TEST(move_left_collapses_selection) {
    TextEditState st;
    st.sel_start = 1;
    st.cursor = 4;
    text_edit_move_left(st, false);
    CHECK(st.cursor == 1);
    CHECK(!st.has_selection());
}

TEST(move_left_shift_extends_selection) {
    TextEditState st;
    st.cursor = 3;
    text_edit_move_left(st, true);
    CHECK(st.cursor == 2);
    CHECK(st.sel_start == 3);
    CHECK(st.has_selection());
}

TEST(move_right) {
    TextEditState st;
    st.cursor = 2;
    text_edit_move_right(st, 5, false);
    CHECK(st.cursor == 3);
}

TEST(move_right_at_end) {
    TextEditState st;
    st.cursor = 5;
    text_edit_move_right(st, 5, false);
    CHECK(st.cursor == 5);
}

TEST(move_right_collapses_selection) {
    TextEditState st;
    st.sel_start = 1;
    st.cursor = 3;
    text_edit_move_right(st, 5, false);
    CHECK(st.cursor == 3);
    CHECK(!st.has_selection());
}

TEST(move_right_shift) {
    TextEditState st;
    st.cursor = 2;
    text_edit_move_right(st, 5, true);
    CHECK(st.cursor == 3);
    CHECK(st.sel_start == 2);
}

TEST(home) {
    TextEditState st;
    st.cursor = 5;
    text_edit_home(st, false);
    CHECK(st.cursor == 0);
    CHECK(!st.has_selection());
}

TEST(home_shift) {
    TextEditState st;
    st.cursor = 5;
    text_edit_home(st, true);
    CHECK(st.cursor == 0);
    CHECK(st.sel_start == 5);
    CHECK(st.has_selection());
}

TEST(end) {
    TextEditState st;
    st.cursor = 2;
    text_edit_end(st, 8, false);
    CHECK(st.cursor == 8);
    CHECK(!st.has_selection());
}

TEST(end_shift) {
    TextEditState st;
    st.cursor = 2;
    text_edit_end(st, 8, true);
    CHECK(st.cursor == 8);
    CHECK(st.sel_start == 2);
}

TEST(select_all) {
    TextEditState st;
    st.cursor = 3;
    text_edit_select_all(st, 10);
    CHECK(st.sel_start == 0);
    CHECK(st.cursor == 10);
    CHECK(st.has_selection());
}

TEST(select_all_then_type_replaces) {
    std::string buf = "hello";
    TextEditState st;
    text_edit_select_all(st, 5);
    text_edit_insert(buf, st, "x");
    CHECK(buf == "x");
    CHECK(st.cursor == 1);
    CHECK(!st.has_selection());
}

TEST(copy) {
    std::string buf = "hello world";
    TextEditState st;
    st.sel_start = 6;
    st.cursor = 11;
    std::string copied = text_edit_copy(buf, st);
    CHECK(copied == "world");
    CHECK(buf == "hello world"); // unchanged
}

TEST(copy_no_selection) {
    std::string buf = "hello";
    TextEditState st;
    st.cursor = 3;
    std::string copied = text_edit_copy(buf, st);
    CHECK(copied.empty());
}

TEST(cut) {
    std::string buf = "hello world";
    TextEditState st;
    st.sel_start = 5;
    st.cursor = 11;
    std::string cut = text_edit_cut(buf, st);
    CHECK(cut == " world");
    CHECK(buf == "hello");
    CHECK(st.cursor == 5);
    CHECK(!st.has_selection());
}

TEST(cut_no_selection) {
    std::string buf = "hello";
    TextEditState st;
    st.cursor = 3;
    std::string cut = text_edit_cut(buf, st);
    CHECK(cut.empty());
    CHECK(buf == "hello");
}

TEST(empty_string_operations) {
    std::string buf;
    TextEditState st;
    st.reset(0);

    text_edit_backspace(buf, st);
    CHECK(buf.empty());
    CHECK(st.cursor == 0);

    text_edit_delete_forward(buf, st);
    CHECK(buf.empty());

    text_edit_move_left(st, false);
    CHECK(st.cursor == 0);

    text_edit_move_right(st, 0, false);
    CHECK(st.cursor == 0);

    text_edit_insert(buf, st, "a");
    CHECK(buf == "a");
    CHECK(st.cursor == 1);
}

TEST(clamp) {
    TextEditState st;
    st.cursor = 100;
    st.sel_start = 50;
    st.clamp(10);
    CHECK(st.cursor == 10);
    CHECK(st.sel_start == 10);
}

TEST(sel_min_max) {
    TextEditState st;
    st.sel_start = 5;
    st.cursor = 2;
    CHECK(st.sel_min() == 2);
    CHECK(st.sel_max() == 5);
    CHECK(st.has_selection());
}

TEST(no_selection) {
    TextEditState st;
    st.cursor = 3;
    st.sel_start = -1;
    CHECK(!st.has_selection());
}

TEST(same_anchor_no_selection) {
    TextEditState st;
    st.cursor = 3;
    st.sel_start = 3;
    CHECK(!st.has_selection());
}

TEST(filter_with_selection_replace) {
    std::string buf = "test";
    TextEditState st;
    st.sel_start = 0;
    st.cursor = 4;
    auto digits = [](char c) { return c >= '0' && c <= '9'; };
    text_edit_insert(buf, st, "abc123def456", digits);
    CHECK(buf == "123456");
    CHECK(st.cursor == 6);
}

TEST(max_len_with_existing_content) {
    std::string buf = "abc";
    TextEditState st;
    st.cursor = 1;
    text_edit_insert(buf, st, "XXXXXXXXXX", nullptr, 5);
    CHECK(buf == "aXXbc"); // can only add 2 more chars to reach max 5
    CHECK(st.cursor == 3);
}

// --- Main ---

int main() {
    for (const auto& t : tests) {
        std::printf("  %s...\n", t.name);
        t.fn();
    }
    std::printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
