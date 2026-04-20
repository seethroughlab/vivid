#include "runtime/operators/project_package.h"

#include "runtime/graph/graph.h"
#include "runtime/operators/operator_destination_policy.h"
#include "runtime/packages/package_manager.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace vivid {

std::pair<std::string, std::string> ensure_project_package(
    PackageManager& mgr, const Graph& graph) {
    namespace fs = std::filesystem;

    const std::vector<PackageInfo> packages = mgr.list();

    // Use an existing workspace project package if available.
    if (const auto* pkg = select_workspace_project_package(packages))
        return {pkg->path, pkg->name};

    if (graph.source_path().empty()) {
        std::fprintf(stderr,
            "[vivid] Project package: no saved graph; save the graph before "
            "scaffolding a project-local operator\n");
        return {};
    }

    fs::path pkg_dir = fs::path(graph.source_path()).parent_path() / "operators";
    static constexpr const char* kPkgName = "local-operators";

    if (!is_supported_package_layout(pkg_dir.string())) {
        fs::create_directories(pkg_dir / "src");

        // vivid-package.json
        {
            std::ofstream ofs(pkg_dir / "vivid-package.json");
            if (!ofs) {
                std::fprintf(stderr,
                    "[vivid] Project package: cannot create manifest in %s\n",
                    pkg_dir.string().c_str());
                return {};
            }
            ofs << "{\n"
                << "  \"name\": \"" << kPkgName << "\",\n"
                << "  \"version\": \"0.1.0\",\n"
                << "  \"vivid_core\": \">=0.1.0 <2.0.0\",\n"
                << "  \"description\": \"Project-local operators\",\n"
                << "  \"build\": \"cmake\",\n"
                << "  \"operators\": []\n"
                << "}\n";
        }

        // CMakeLists.txt
        {
            std::ofstream ofs(pkg_dir / "CMakeLists.txt");
            if (!ofs) {
                std::fprintf(stderr,
                    "[vivid] Project package: cannot create CMakeLists.txt in %s\n",
                    pkg_dir.string().c_str());
                return {};
            }
            ofs << "cmake_minimum_required(VERSION 3.20)\n"
                << "project(local_operators LANGUAGES CXX)\n\n"
                << "if(NOT DEFINED VIVID_SRC_DIR)\n"
                << "  message(FATAL_ERROR \"VIVID_SRC_DIR not set. Build through vivid package tooling.\")\n"
                << "endif()\n\n"
                << "set(CMAKE_CXX_STANDARD 17)\n"
                << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
                << "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n\n"
                << "include_directories(${VIVID_SRC_DIR}/src)\n\n"
                << "# Find WebGPU headers and library for GPU operators\n"
                << "if(DEFINED VIVID_BUILD_DIR)\n"
                << "  file(GLOB _wgpu_src_dirs \"${VIVID_BUILD_DIR}/_deps/wgpu*-src\")\n"
                << "  foreach(_dir ${_wgpu_src_dirs})\n"
                << "    if(EXISTS \"${_dir}/include/webgpu/webgpu.h\")\n"
                << "      include_directories(\"${_dir}/include\")\n"
                << "    endif()\n"
                << "  endforeach()\n"
                << "  # Find wgpu_native library\n"
                << "  find_library(WGPU_NATIVE wgpu_native PATHS\n"
                << "    \"${VIVID_BUILD_DIR}/vivid.app/Contents/MacOS\"\n"
                << "    \"${VIVID_BUILD_DIR}\" NO_DEFAULT_PATH)\n"
                << "endif()\n\n"
                << "if(DEFINED VIVID_PLUGIN_SUFFIX)\n"
                << "  set(VIVID_PACKAGE_PLUGIN_SUFFIX \"${VIVID_PLUGIN_SUFFIX}\")\n"
                << "else()\n"
                << "  set(VIVID_PACKAGE_PLUGIN_SUFFIX \"${CMAKE_SHARED_LIBRARY_SUFFIX}\")\n"
                << "endif()\n\n"
                << "# Highway SIMD (when available from Vivid core)\n"
                << "if(DEFINED VIVID_HIGHWAY_INCLUDE_DIR)\n"
                << "  include_directories(\"${VIVID_HIGHWAY_INCLUDE_DIR}\")\n"
                << "  add_compile_definitions(VIVID_HAS_HIGHWAY=1)\n"
                << "endif()\n"
                << "if(DEFINED VIVID_HIGHWAY_LIBRARY)\n"
                << "  set(HWY_LIB \"${VIVID_HIGHWAY_LIBRARY}\")\n"
                << "endif()\n\n"
                << "function(add_vivid_pkg_operator name src)\n"
                << "  add_library(${name} SHARED ${src})\n"
                << "  set_target_properties(${name} PROPERTIES PREFIX \"\" SUFFIX \"${VIVID_PACKAGE_PLUGIN_SUFFIX}\")\n"
                << "  if(WGPU_NATIVE)\n"
                << "    target_link_libraries(${name} PRIVATE ${WGPU_NATIVE})\n"
                << "  endif()\n"
                << "  if(HWY_LIB)\n"
                << "    target_link_libraries(${name} PRIVATE ${HWY_LIB})\n"
                << "  endif()\n"
                << "endfunction()\n\n"
                << "set(PKG_OPS)\n\n"
                << "foreach(op ${PKG_OPS})\n"
                << "  add_vivid_pkg_operator(${op} src/${op}.cpp)\n"
                << "endforeach()\n";
        }

        std::fprintf(stderr, "[vivid] Project package: scaffolded at %s\n",
                     pkg_dir.string().c_str());
    }

    // Check if already linked (handle stale symlinks).
    for (const auto& pkg : packages) {
        if (pkg.name != kPkgName) continue;
        if (pkg.linked) {
            std::error_code ec;
            auto target = fs::read_symlink(pkg.path, ec);
            if (!ec && fs::weakly_canonical(target) == fs::weakly_canonical(pkg_dir))
                return {pkg.path, pkg.name};  // already linked to the right place
            mgr.unlink(pkg.name);  // stale — remove and re-link
        } else {
            std::fprintf(stderr,
                "[vivid] Project package: '%s' already installed (not linked); "
                "uninstall it first\n", kPkgName);
            return {};
        }
        break;
    }

    auto link_result = mgr.link(pkg_dir.string());
    if (!link_result.success) {
        std::fprintf(stderr, "[vivid] Project package: failed to link: %s\n",
                     link_result.error.c_str());
        return {};
    }
    std::fprintf(stderr, "[vivid] Project package: linked '%s'\n",
                 link_result.info.name.c_str());
    return {link_result.info.path, link_result.info.name};
}

} // namespace vivid
