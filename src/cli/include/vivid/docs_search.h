// Vivid Documentation Search
// Shared utilities for searching docs, recipes, and examples.
// Used by both the CLI (vivid docs) and MCP server (search_docs, get_recipe, get_example).

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>

namespace vivid::docs {

namespace fs = std::filesystem;

/// Find the docs/ directory by searching common locations
fs::path findDocsDir();

/// Find the modules/ directory by searching common locations
fs::path findModulesDir();

/// Get all discoverable doc files as (path, human-readable title) pairs
std::vector<std::pair<std::string, std::string>> getDocFiles();

/// Load a documentation file by filename (or absolute path)
std::string loadDocFile(const std::string& filename);

/// Search all docs for a query string, returning ranked JSON results
nlohmann::json searchDocs(const std::string& query, int maxResults = 10);

/// Parse all recipes from RECIPES.md
/// If name is empty, returns a list of all recipe names/descriptions.
/// If name is provided, returns the matching recipe with full code.
nlohmann::json getRecipes(const std::string& name = "");

/// Find code examples using a specific operator
nlohmann::json findExamples(const std::string& operatorName);

} // namespace vivid::docs
