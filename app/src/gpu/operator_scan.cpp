#include "gpu/operator_scan.h"

#include "gpu/operator_loader.h"
#include "gpu/loaded_operator.h"
#include "gpu/op_runtime.h"
#include "operator_api/types.h"
#include "platform/platform.h"

#include <filesystem>
#include <cstdio>

namespace vivid {

RegisterResult load_and_register_operator_ex(const std::string& dylib_path, OpRegistry& reg,
                                             std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                                             const std::set<std::string>* quarantined) {
    RegisterResult r;
    auto loader = std::make_unique<OperatorLoader>();
    if (!loader->load(dylib_path.c_str())) {
        r.error_key = loader->last_error().code;      // dlopen_failed / abi_mismatch / missing_* …
        r.error_msg = loader->last_error().message;
        std::fprintf(stderr, "[vivid] operator load failed (%s): %s — %s\n",
                     std::filesystem::path(dylib_path).filename().c_str(),
                     r.error_key.c_str(), r.error_msg.c_str());
        return r;
    }
    const VividOperatorDescriptor* d = loader->descriptor();
    const std::string name = d->name;
    r.op_name = name;   // known from here on, even when we decline to register
    if (quarantined && quarantined->count(name)) {   // ADR-0018: a repeat crasher is disabled by default
        r.quarantined = true;
        std::fprintf(stderr, "[vivid] operator '%s' quarantined (repeat crashes) — not registered\n", name.c_str());
        return r;
    }
    if (reg.has(name)) {
        r.shadowed = true;
        std::fprintf(stderr, "[vivid] operator '%s' (%s) shadowed by an existing "
                     "registration — skipped\n", name.c_str(),
                     std::filesystem::path(dylib_path).filename().c_str());
        return r;
    }
    // Carry the dylib descriptor's discovery metadata into the registry.
    OpMeta meta;
    if (d->display_name) meta.display_name = d->display_name;
    if (d->summary)      meta.summary = d->summary;
    for (uint32_t i = 0; i < d->keyword_count; ++i)
        if (d->keywords && d->keywords[i]) meta.keywords.emplace_back(d->keywords[i]);
    meta.has = !meta.display_name.empty() || !meta.summary.empty() || !meta.keywords.empty();

    // The factory captures the loader's raw pointer; the unique_ptr we move into
    // `loaders` keeps the pointee address stable for the whole run.
    OperatorLoader* raw = loader.get();
    reg.register_type(name, [raw] {
        return std::unique_ptr<OperatorBase>(new LoadedOperator(raw));
    }, std::move(meta));
    loaders.push_back(std::move(loader));
    std::fprintf(stderr, "[vivid] loaded operator '%s' (%s)\n",
                 name.c_str(), std::filesystem::path(dylib_path).filename().c_str());
    r.ok = true;
    return r;
}

std::string load_and_register_operator(const std::string& dylib_path, OpRegistry& reg,
                                       std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                                       const std::set<std::string>* quarantined) {
    const RegisterResult r = load_and_register_operator_ex(dylib_path, reg, loaders, quarantined);
    return r.ok ? r.op_name : std::string();   // preserve the "" == not-registered contract
}

int scan_operator_dir(const std::string& dir, OpRegistry& reg,
                      std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                      const std::set<std::string>* quarantined) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return 0;

    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != platform::plugin_suffix()) continue;
        if (!load_and_register_operator(entry.path().string(), reg, loaders, quarantined).empty()) ++count;
    }
    return count;
}

}  // namespace vivid
