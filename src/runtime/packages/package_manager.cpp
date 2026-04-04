#include "runtime/packages/package_manager.h"

#include "runtime/core/build_console.h"

namespace vivid {

PackageManager::PackageManager(PackageCompiler& compiler, OperatorRegistry& registry)
    : compiler_(compiler)
    , registry_(registry) {}

void PackageManager::set_build_console(BuildConsole* console) {
    build_console_ = console;
    compiler_.set_build_console(console);
}

void PackageManager::set_resolver(PackageResolver resolver) {
    resolver_ = std::move(resolver);
}

} // namespace vivid
