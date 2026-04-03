#pragma once

#include "ui/graph/dialog_types.h"
#include <filesystem>
#include <string>
#include <vector>

namespace vivid { class PackageManager; }

namespace vivid {

bool load_example_entry_from_graph(const std::filesystem::path& graph_path,
                                   const std::filesystem::path& graphs_root,
                                   vivid::ui::ExampleEntry& out);

bool load_graph_meta_edit_data(const std::string& graph_path,
                               vivid::ui::GraphMetaEditData& data,
                               std::string& error);

bool save_graph_meta_edit_data(const vivid::ui::GraphMetaEditData& data,
                               std::string& error);

std::vector<vivid::ui::ExampleEntry> discover_examples_with_packages(
    const std::filesystem::path& graphs_root,
    vivid::PackageManager* pkg_manager);

std::string resolve_graph_input_path(const std::string& input,
                                     const std::filesystem::path& graphs_root,
                                     const std::vector<vivid::ui::ExampleEntry>& examples);

std::filesystem::path expand_tilde_path(const std::string& input);

} // namespace vivid
