#include "hotload.h"
#include <iostream>

#if defined(_WIN32)
    #include <windows.h>
    #define LOAD_LIBRARY(path) LoadLibraryA(path)
    #define GET_SYMBOL(handle, name) GetProcAddress((HMODULE)handle, name)
    #define CLOSE_LIBRARY(handle) FreeLibrary((HMODULE)handle)
    #define LIB_ERROR() "Windows error"
#else
    #include <dlfcn.h>
    #define LOAD_LIBRARY(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
    #define GET_SYMBOL(handle, name) dlsym(handle, name)
    #define CLOSE_LIBRARY(handle) dlclose(handle)
    #define LIB_ERROR() dlerror()
#endif

namespace vivid {

HotLoader::HotLoader() = default;

HotLoader::~HotLoader() {
    unload();
}

bool HotLoader::load(const std::string& libraryPath) {
    // Unload any existing library first
    unload();

    // Clear previous errors
#ifndef _WIN32
    dlerror();
#endif

    handle_ = LOAD_LIBRARY(libraryPath.c_str());
    if (!handle_) {
        std::cerr << "[HotLoader] Failed to load " << libraryPath << ": " << LIB_ERROR() << "\n";
        return false;
    }

    libraryPath_ = libraryPath;

    // Get create function
    auto createFunc = reinterpret_cast<CreateFunc>(GET_SYMBOL(handle_, "vivid_create_operator"));
    if (!createFunc) {
        std::cerr << "[HotLoader] Missing vivid_create_operator in " << libraryPath << "\n";
        CLOSE_LIBRARY(handle_);
        handle_ = nullptr;
        libraryPath_.clear();
        return false;
    }

    // Get destroy function
    destroyFunc_ = reinterpret_cast<DestroyFunc>(GET_SYMBOL(handle_, "vivid_destroy_operator"));
    if (!destroyFunc_) {
        std::cerr << "[HotLoader] Missing vivid_destroy_operator in " << libraryPath << "\n";
        CLOSE_LIBRARY(handle_);
        handle_ = nullptr;
        libraryPath_.clear();
        return false;
    }

    // Create operator instance
    Operator* op = createFunc();
    if (op) {
        operators_.push_back(op);
        std::cout << "[HotLoader] Created operator from " << libraryPath << "\n";
    } else {
        std::cerr << "[HotLoader] vivid_create_operator returned nullptr\n";
    }

    std::cout << "[HotLoader] Loaded " << libraryPath << " (" << operators_.size() << " operators)\n";
    return true;
}

void HotLoader::unload() {
    // Destroy operators
    if (destroyFunc_) {
        for (auto* op : operators_) {
            if (op) {
                destroyFunc_(op);
            }
        }
    }
    operators_.clear();

    // Close library
    if (handle_) {
        CLOSE_LIBRARY(handle_);
        handle_ = nullptr;
        std::cout << "[HotLoader] Unloaded " << libraryPath_ << "\n";
    }

    libraryPath_.clear();
    destroyFunc_ = nullptr;
}

} // namespace vivid
