#pragma once
#include <vivid/operator.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid {

class HotLoader {
public:
    HotLoader();
    ~HotLoader();

    // Load a shared library containing operators
    bool load(const std::string& libraryPath);

    // Unload current library
    void unload();

    // Check if loaded
    bool isLoaded() const { return handle_ != nullptr; }

    // Get path of currently loaded library
    const std::string& libraryPath() const { return libraryPath_; }

    // Get loaded operators
    std::vector<Operator*>& operators() { return operators_; }
    const std::vector<Operator*>& operators() const { return operators_; }

private:
    void* handle_ = nullptr;
    std::string libraryPath_;
    std::vector<Operator*> operators_;

    using CreateFunc = Operator* (*)();
    using DestroyFunc = void (*)(Operator*);
    DestroyFunc destroyFunc_ = nullptr;
};

} // namespace vivid
