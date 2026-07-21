// Read a plugin's factory metadata without instantiating it. See plugin_probe.h.
//
// This TU is the ONLY place that opens a plugin purely to look at it, and it deliberately stops at
// the factory: VST3 `createInstance` and CLAP `create_plugin` are where the cost and the danger are
// (Surge XT's CLAP blocks ~90s inside create_plugin — reading its descriptor is milliseconds).
#include "audio/plugin_probe.h"

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include "audio/plugin_catalog.h"
#include "audio/plugin_class.h"
#include "platform/platform.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"

#include <clap/clap.h>
#include <nlohmann/json.hpp>

#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

using nlohmann::json;

namespace vivid::session {
namespace {

using GetPluginFactoryFunc = Steinberg::IPluginFactory* (*)();
using BundleEntryFn = bool (*)(CFBundleRef);
using BundleExitFn  = bool (*)();

std::string cid_hex(const char cid[16]) {
    static const char H[] = "0123456789ABCDEF";
    std::string s(32, '0');
    for (int i = 0; i < 16; ++i) {
        const auto b = static_cast<std::uint8_t>(cid[i]);
        s[i * 2]     = H[(b >> 4) & 0xF];
        s[i * 2 + 1] = H[b & 0xF];
    }
    return s;
}

ProbeResult probe_vst3(const std::string& path) {
    ProbeResult r;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path.c_str()),
        static_cast<CFIndex>(path.size()), true);
    if (!url) { r.error = "bad path"; return r; }
    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!bundle) { r.error = "CFBundleCreate failed"; return r; }

    if (!CFBundleLoadExecutableAndReturnError(bundle, nullptr)) {
        CFRelease(bundle);
        r.error = "bundle load failed";   // wrong arch, unsigned, broken — not our problem, just report
        return r;
    }
    auto entry = reinterpret_cast<BundleEntryFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
    auto exit_fn = reinterpret_cast<BundleExitFn>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
    auto get_factory = reinterpret_cast<GetPluginFactoryFunc>(
        CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
    if (!get_factory) {
        CFBundleUnloadExecutable(bundle); CFRelease(bundle);
        r.error = "no GetPluginFactory";
        return r;
    }
    // bundleEntry must run before GetPluginFactory (the plugin stashes the CFBundleRef for its
    // resource lookups) — same order the loader uses.
    if (entry && !entry(bundle)) {
        CFBundleUnloadExecutable(bundle); CFRelease(bundle);
        r.error = "bundleEntry failed";
        return r;
    }

    Steinberg::IPluginFactory* factory = get_factory();
    if (!factory) {
        if (exit_fn) exit_fn();
        CFBundleUnloadExecutable(bundle); CFRelease(bundle);
        r.error = "null factory";
        return r;
    }
    Steinberg::IPluginFactory2* factory2 = nullptr;
    if (factory->queryInterface(Steinberg::IPluginFactory2::iid, reinterpret_cast<void**>(&factory2))
            != Steinberg::kResultOk)
        factory2 = nullptr;

    const Steinberg::int32 count = factory->countClasses();
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::PClassInfo info{};
        if (factory->getClassInfo(i, &info) != Steinberg::kResultOk) continue;
        if (std::strcmp(info.category, "Audio Module Class") != 0) continue;   // skip controllers etc.
        ProbedClass pc;
        pc.uid  = cid_hex(info.cid);
        pc.name = info.name;
        pc.cls  = kClassUnknown;
        if (factory2) {
            Steinberg::PClassInfo2 info2{};
            if (factory2->getClassInfo2(i, &info2) == Steinberg::kResultOk) {
                pc.cls    = class_from_vst3_subcategories(info2.subCategories);
                pc.vendor = info2.vendor;
            }
        }
        r.classes.push_back(std::move(pc));
    }
    if (factory2) factory2->release();
    factory->release();
    if (exit_fn) exit_fn();
    CFBundleUnloadExecutable(bundle);
    CFRelease(bundle);

    r.ok = !r.classes.empty();
    if (!r.ok) r.error = "no audio module classes";
    return r;
}

ProbeResult probe_clap(const std::string& path) {
    ProbeResult r;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
        static_cast<CFIndex>(path.size()), true);
    if (!url) { r.error = "bad path"; return r; }
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    if (!bundle) { r.error = "CFBundleCreate failed"; return r; }

    auto* entry = static_cast<const clap_plugin_entry_t*>(
        CFBundleGetDataPointerForName(bundle, CFSTR("clap_entry")));
    if (!entry || !entry->init(path.c_str())) {
        CFRelease(bundle);
        r.error = "no clap_entry";
        return r;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const std::uint32_t n = factory ? factory->get_plugin_count(factory) : 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor(factory, i);
        if (!d) continue;
        ProbedClass pc;
        pc.id     = d->id ? d->id : "";
        pc.name   = d->name ? d->name : "";
        pc.vendor = d->vendor ? d->vendor : "";
        pc.cls    = class_from_clap_features(d->features);   // NO create_plugin: that's the 90s stall
        r.classes.push_back(std::move(pc));
    }
    entry->deinit();
    CFRelease(bundle);

    r.ok = !r.classes.empty();
    if (!r.ok) r.error = "no plugins in factory";
    return r;
}

}  // namespace

ProbeResult probe_plugin(const std::string& path, int format) {
    if (path.empty()) { ProbeResult r; r.error = "empty path"; return r; }
    return (format == kFmtCLAP) ? probe_clap(path) : probe_vst3(path);
}

// ---- the subprocess ----
//
// The child prints ONE json object and exits; the parent reads it. Anything else (a crash, a hang,
// garbage on stdout) is treated as "this plugin is poison" and recorded so we never open it again.

namespace {

// A bundle can hold several classes; the catalog shows one row, so pick what the user would mean.
const ProbedClass* pick_primary(const ProbeResult& r) {
    for (const ProbedClass& c : r.classes) if (c.cls == kClassInstrument) return &c;
    for (const ProbedClass& c : r.classes) if (c.cls == kClassEffect)     return &c;
    return r.classes.empty() ? nullptr : &r.classes.front();
}

}  // namespace

int run_probe_subprocess_main(const std::string& path, int format) {
    const ProbeResult r = probe_plugin(path, format);
    json j;
    j["ok"] = r.ok;
    if (!r.ok) j["error"] = r.error;
    int cls = kClassFailed;
    if (r.ok) {
        if (const ProbedClass* pc = pick_primary(r)) {
            cls = pc->cls;
            j["name"] = pc->name;
            j["vendor"] = pc->vendor;
            j["uid"] = pc->uid;
        }
    }
    j["cls"] = cls;
    // Write the verdict to our dedicated descriptor (see kProbeVerdictFd). Fall back to stdout when
    // it isn't open — that's the manual `--probe-plugin` invocation from a terminal, where a human
    // is reading, not a parser.
    const std::string line = j.dump() + "\n";
    if (::fcntl(kProbeVerdictFd, F_GETFD) != -1) {
        ssize_t off = 0;
        while (off < static_cast<ssize_t>(line.size())) {
            const ssize_t n = ::write(kProbeVerdictFd, line.data() + off,
                                      line.size() - static_cast<std::size_t>(off));
            if (n <= 0) break;
            off += n;
        }
    } else {
        std::printf("%s", line.c_str());
        std::fflush(stdout);
    }
    return 0;
}

bool plugin_supports_host_arch(const std::string& bundle) {
#if defined(__APPLE__)
#if defined(__arm64__) || defined(__aarch64__)
    constexpr uint32_t kHostCpu = 0x0100000cu;   // CPU_TYPE_ARM64
#elif defined(__x86_64__)
    constexpr uint32_t kHostCpu = 0x01000007u;   // CPU_TYPE_X86_64
#else
    return true;   // unknown host: don't presume
#endif
    namespace fs = std::filesystem;
    const fs::path b(bundle);
    const fs::path exe = b / "Contents" / "MacOS" / b.stem();
    std::ifstream in(exe, std::ios::binary);
    if (!in) return true;   // can't tell => let the probe try and report for real

    auto be32 = [](const unsigned char* p) -> uint32_t {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    };
    auto le32 = [](const unsigned char* p) -> uint32_t {
        return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]);
    };
    unsigned char hdr[8]{};
    if (!in.read(reinterpret_cast<char*>(hdr), 8)) return true;
    const uint32_t magic = be32(hdr);
    if (magic == 0xcafebabeu || magic == 0xcafebabfu) {          // FAT (universal): scan the slices
        const uint32_t narch = be32(hdr + 4);
        const size_t entry = (magic == 0xcafebabfu) ? 32u : 20u;  // fat_arch_64 vs fat_arch
        for (uint32_t i = 0; i < narch && i < 64; ++i) {
            unsigned char a[4]{};
            in.seekg(static_cast<std::streamoff>(8 + i * entry));
            if (!in.read(reinterpret_cast<char*>(a), 4)) break;
            if (be32(a) == kHostCpu) return true;
        }
        return false;
    }
    // Thin Mach-O: cputype is the second word (little-endian for 0xfeedfacf/0xfeedface).
    const uint32_t thin = le32(hdr);
    if (thin == 0xfeedfacfu || thin == 0xfeedfaceu) return le32(hdr + 4) == kHostCpu;
    return true;   // not a Mach-O we understand: let the probe speak
#else
    (void)bundle;
    return true;
#endif
}

ProbeRun probe_plugin_subprocess(const std::string& path, int format, int timeout_ms) {
    ProbeRun out;
    out.cls = kClassFailed;

    // Answer the cheap, decisive question first — without loading anything.
    if (!plugin_supports_host_arch(path)) {
        out.permanent = true;
        std::fprintf(stderr, "[vivid] plugin '%s' has no slice for this CPU (Intel-only) — skipping\n",
                     path.c_str());
        return out;
    }

    const std::string exe = platform::executable_path();
    if (exe.empty()) { out.crashed = true; return out; }

    int fds[2];
    if (::pipe(fds) != 0) { out.crashed = true; return out; }

    const std::string fmt = std::to_string(format);
    const char* argv[] = { exe.c_str(), "--probe-plugin", path.c_str(), "--format", fmt.c_str(), nullptr };

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], kProbeVerdictFd);   // the verdict channel
    // ALSO fold the child's stdout into the same pipe. Some plugins close fd 3 while loading (Sitala
    // does), which makes the child fall back to writing its verdict on stdout — so if we don't capture
    // stdout we get an EMPTY verdict and wrongly poison a perfectly good plugin. Banners on stdout are
    // harmless: the reader scans for the one JSON line carrying our "cls" key.
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    // The child's stderr stays attached to ours (plugin logs land in our log, useful for debugging).
    pid_t pid = -1;
    const int rc = ::posix_spawn(&pid, exe.c_str(), &fa, nullptr,
                                 const_cast<char* const*>(argv), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(fds[1]);
    if (rc != 0 || pid <= 0) { ::close(fds[0]); out.crashed = true; return out; }

    // Read whatever the child says (it exits right after one line).
    std::string buf;
    { char chunk[512];
      ssize_t n;
      while ((n = ::read(fds[0], chunk, sizeof chunk)) > 0) buf.append(chunk, static_cast<std::size_t>(n)); }
    ::close(fds[0]);

    // Parse the verdict the child already emitted (one line, written BEFORE it tears down). A
    // self-declared-OK verdict means the risky step — reading the plugin's factory descriptor —
    // already succeeded; a crash or hang on TEARDOWN afterward (Sitala and plenty of others unload
    // messily) is benign and must NOT poison an otherwise-loadable plugin. So capture an ok verdict
    // up front and let it stand regardless of how the process finally exits.
    // The verdict is one JSON line; with the child's stdout folded in, plugin banners may surround
    // it. Scan for the last line that parses as an object carrying our "cls" key.
    json verdict;
    bool parsed = false;
    for (std::size_t start = 0; start < buf.size();) {
        const std::size_t nl = buf.find('\n', start);
        const std::string ln = buf.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = (nl == std::string::npos) ? buf.size() : nl + 1;
        if (ln.find("\"cls\"") == std::string::npos) continue;
        try { json j = json::parse(ln); if (j.is_object() && j.contains("cls")) { verdict = std::move(j); parsed = true; } }
        catch (...) {}
    }
    const bool ok_verdict = parsed && verdict.value("ok", false);

    // Reap, with a deadline: a plugin that HANGS *before* emitting a verdict would otherwise hang the
    // worker forever. On timeout we SIGKILL — but a verdict it already gave us still counts.
    int status = 0;
    bool reaped = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) { reaped = true; break; }
        if (w < 0) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Poison ONLY a plugin that gave us nothing usable AND died/hung — i.e. crashed while reading its
    // own factory (a real hazard). A clean exit with a definite verdict (even ok:false) is trusted;
    // so is an ok verdict followed by a messy teardown.
    const bool clean_exit = reaped && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok_verdict && !clean_exit) { out.crashed = true; return out; }

    if (parsed) {
        out.cls    = verdict.value("cls", static_cast<int>(kClassFailed));
        out.name   = verdict.value("name", std::string());
        out.vendor = verdict.value("vendor", std::string());
        out.uid    = verdict.value("uid", std::string());
    } else {
        out.cls = kClassFailed;   // it exited cleanly but said nothing useful
    }
    return out;
}

}  // namespace vivid::session
