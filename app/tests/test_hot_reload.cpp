// Headless tests for the P2.4 hot-reload infrastructure: the reload-compat
// classifier, the mtime file watcher, and the background-compile reloader.
#include "gpu/operator_loader.h"
#include "operator_api/value_model.h"   // VIVID_VALUE_TEXTURE / VIVID_MULTIPLICITY_*
#include "packages/file_watcher.h"
#include "packages/hot_reload.h"
#include "test_helpers.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace {
VividParamDescriptor mkparam(const char* name, uint32_t type) {
    VividParamDescriptor p{}; p.name = name; p.type = type; return p;
}
VividPortDescriptor mkport(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{}; p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR; return p;
}
VividOperatorDescriptor mkdesc(std::vector<VividParamDescriptor>& ps,
                               std::vector<VividPortDescriptor>& os, int gpu = 1) {
    VividOperatorDescriptor d{};
    d.name = "X"; d.has_process_gpu = gpu;
    d.multiplicity_behavior = VIVID_MULTIPLICITY_SCALAR_ONLY;
    d.param_count = (uint32_t)ps.size(); d.params = ps.empty() ? nullptr : ps.data();
    d.port_count  = (uint32_t)os.size(); d.ports  = os.empty() ? nullptr : os.data();
    return d;
}
}  // namespace

int main() {
    using namespace vivid;

    // ---- classify_hot_reload ----
    {
        std::vector<VividParamDescriptor> p1 = { mkparam("a", VIVID_PARAM_FLOAT) };
        std::vector<VividPortDescriptor>  o1 = { mkport("out", VIVID_PORT_OUTPUT) };
        VividOperatorDescriptor base = mkdesc(p1, o1);

        CHECK(classify_hot_reload(nullptr, &base) == HotReloadCompat::Compatible);   // first load
        CHECK(classify_hot_reload(&base, &base) == HotReloadCompat::Compatible);     // identical

        // Appended param → compatible (old params are a prefix).
        std::vector<VividParamDescriptor> p2 = { mkparam("a", VIVID_PARAM_FLOAT), mkparam("b", VIVID_PARAM_FLOAT) };
        std::vector<VividPortDescriptor>  o2 = o1;
        VividOperatorDescriptor added = mkdesc(p2, o2);
        CHECK(classify_hot_reload(&base, &added) == HotReloadCompat::Compatible);

        // Param type changed → incompatible.
        std::vector<VividParamDescriptor> p3 = { mkparam("a", VIVID_PARAM_INT) };
        std::vector<VividPortDescriptor>  o3 = o1;
        VividOperatorDescriptor typed = mkdesc(p3, o3);
        CHECK(classify_hot_reload(&base, &typed) == HotReloadCompat::Incompatible);

        // Port removed → incompatible.
        std::vector<VividParamDescriptor> p4 = p1;
        std::vector<VividPortDescriptor>  o4 = {};
        VividOperatorDescriptor noport = mkdesc(p4, o4);
        CHECK(classify_hot_reload(&base, &noport) == HotReloadCompat::Incompatible);

        // has_process_gpu differs → incompatible.
        std::vector<VividParamDescriptor> p5 = p1; std::vector<VividPortDescriptor> o5 = o1;
        VividOperatorDescriptor cpu = mkdesc(p5, o5, /*gpu*/0);
        CHECK(classify_hot_reload(&base, &cpu) == HotReloadCompat::Incompatible);

        // multiplicity_behavior differs → recompile-required.
        std::vector<VividParamDescriptor> p6 = p1; std::vector<VividPortDescriptor> o6 = o1;
        VividOperatorDescriptor mult = mkdesc(p6, o6);
        mult.multiplicity_behavior = VIVID_MULTIPLICITY_MAP;
        CHECK(classify_hot_reload(&base, &mult) == HotReloadCompat::RecompileRequired);
    }

    // ---- FileWatcher (mtime poll) ----
    {
        namespace fs = std::filesystem;
        const std::string path = (fs::temp_directory_path() / "vivid_watch_test.txt").string();
        { std::ofstream f(path); f << "v1"; }
        FileWatcher w;
        w.watch(path, "op");
        CHECK(w.poll_changes().empty());   // no change yet
        // Force a newer mtime and confirm it's reported once.
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(5));
        auto ch = w.poll_changes();
        CHECK(ch.size() == 1 && ch[0] == "op");
        CHECK(w.poll_changes().empty());   // not reported twice
        fs::remove(path);
    }

    // ---- HotReloader (background compile thread) ----
    {
        HotReloader hr;
        hr.start([](const std::string& t) {
            return ReloadResult{ t, "/staged/" + t + ".dylib", "", true };
        });
        hr.queue_rebuild("Foo");
        hr.queue_rebuild("Foo");   // coalesced while pending
        std::vector<ReloadResult> got;
        for (int i = 0; i < 200 && got.empty(); ++i) {
            got = hr.poll_ready();
            if (got.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(got.size() >= 1);
        CHECK(!got.empty() && got[0].target == "Foo" && got[0].success);
        hr.stop();
    }

    return vivid::test::summary("test_hot_reload");
}
