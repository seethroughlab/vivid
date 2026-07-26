#pragma once
#include <string>

namespace vivid {

// An operator that renders GLSL loaded from a project-relative asset file (a .glsl),
// rather than a compile-time shader literal. The VisualGraph resolves a node's `asset`
// against the project directory and pushes the absolute path here before process_gpu;
// the operator (re)loads on change and degrades to a no-op if the file is missing or
// fails to compile. Legacy host-side asset channel: package/core ops should prefer
// Param<FilePath>, which VisualGraph also resolves against the project directory before
// syncing into the operator/runtime context.
struct AssetShader {
    virtual ~AssetShader() = default;
    virtual void set_asset_path(const std::string& absolute_path) = 0;
};

}  // namespace vivid
