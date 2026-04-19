#pragma once

#include <string>

namespace vivid::ui {

struct FileDialogTestStats {
    int invocation_count = 0;
    int open_file_count = 0;
    int open_directory_count = 0;
    int save_file_count = 0;
    int save_directory_count = 0;
};

std::string open_file_dialog();
std::string open_directory_dialog();
std::string save_file_dialog(const std::string& default_name = {},
                             const std::string& allowed_extension = {});
std::string save_directory_dialog(const std::string& default_name = {});
void reset_file_dialog_test_stats();
FileDialogTestStats file_dialog_test_stats();

} // namespace vivid::ui
