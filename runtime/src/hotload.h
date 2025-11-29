#pragma once
#include <vivid/operator.h>
#include <vivid/chain.h>
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

    // Check if using Chain API (setup/update pattern)
    bool usesChainAPI() const { return setupFunc_ != nullptr; }

    // Chain API entry points
    void callSetup(Chain& chain) {
        if (setupFunc_) setupFunc_(chain);
    }

    void callUpdate(Chain& chain, Context& ctx) {
        if (updateFunc_) updateFunc_(chain, ctx);
    }

    // Legacy API: Get loaded operators (single-operator pattern)
    std::vector<Operator*>& operators() { return operators_; }
    const std::vector<Operator*>& operators() const { return operators_; }

private:
    void* handle_ = nullptr;
    std::string libraryPath_;
    std::vector<Operator*> operators_;

    // Legacy single-operator API
    using CreateFunc = Operator* (*)();
    using DestroyFunc = void (*)(Operator*);
    DestroyFunc destroyFunc_ = nullptr;

    // Chain API
    using SetupFunc = void (*)(Chain&);
    using UpdateFunc = void (*)(Chain&, Context&);
    SetupFunc setupFunc_ = nullptr;
    UpdateFunc updateFunc_ = nullptr;
};

} // namespace vivid
