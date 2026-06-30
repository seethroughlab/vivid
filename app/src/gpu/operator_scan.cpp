#include "gpu/operator_scan.h"

#include "gpu/operator_loader.h"
#include "gpu/loaded_operator.h"
#include "gpu/op_runtime.h"
#include "operator_api/types.h"

#include <filesystem>
#include <cstdio>

namespace vivid {

int scan_operator_dir(const std::string& dir, OpRegistry& reg,
                      std::vector<std::unique_ptr<OperatorLoader>>& loaders) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return 0;

    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".dylib") continue;

        auto loader = std::make_unique<OperatorLoader>();
        if (!loader->load(entry.path().c_str())) {
            std::fprintf(stderr, "[vivid] operator load failed (%s): %s — %s\n",
                         entry.path().filename().c_str(),
                         loader->last_error().code.c_str(),
                         loader->last_error().message.c_str());
            continue;
        }
        const VividOperatorDescriptor* d = loader->descriptor();
        const std::string name = d->name;
        if (reg.has(name)) {
            std::fprintf(stderr, "[vivid] operator '%s' (%s) shadowed by an existing "
                         "registration — skipped\n", name.c_str(),
                         entry.path().filename().c_str());
            continue;
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
                     name.c_str(), entry.path().filename().c_str());
        ++count;
    }
    return count;
}

}  // namespace vivid
