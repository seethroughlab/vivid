#pragma once

#include "runtime/settings.h"
#include "runtime/operator_destination_policy.h"
#include "runtime/package_manager.h"
#include <filesystem>
#include <string>

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

namespace vivid {

std::filesystem::path default_workspace_root();

struct ScaffoldDestination {
    std::filesystem::path root;
    bool package_layout = false;
    std::string package_name;
    std::string warning;
};

bool resolve_scaffold_destination(const std::string& destination,
                                  const std::string& source_dir,
                                  PackageManager& pm,
                                  const Settings* settings,
                                  ScaffoldDestination& out,
                                  std::string& error);

bool copy_tree_missing(const std::filesystem::path& src,
                       const std::filesystem::path& dst);

bool copy_tree_overwrite_newer(const std::filesystem::path& src,
                               const std::filesystem::path& dst);

// Returns true if settings were modified (caller should save).
bool ensure_workspace_seeded(const std::filesystem::path& resources_dir,
                             Settings& settings,
                             std::filesystem::path& workspace_root);

} // namespace vivid
