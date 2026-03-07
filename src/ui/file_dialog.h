#pragma once

#include <string>

namespace vivid::ui {

std::string open_file_dialog();
std::string open_directory_dialog();
std::string save_file_dialog(const std::string& default_name = {});

} // namespace vivid::ui
