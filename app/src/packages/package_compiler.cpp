#include "packages/package_compiler.h"
#include "platform/platform.h"

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <unistd.h>
#include <sys/wait.h>

// Toolchain + header/lib paths, injected by CMake (see app/CMakeLists.txt). Guarded
// so the file still compiles (and degrades gracefully) if a define is absent.
#ifndef VIVID_PKG_CXX
#define VIVID_PKG_CXX "clang++"
#endif
#ifndef VIVID_PKG_SRC_DIR
#define VIVID_PKG_SRC_DIR ""
#endif
#ifndef VIVID_PKG_WEBGPU_INCLUDE_DIR
#define VIVID_PKG_WEBGPU_INCLUDE_DIR ""
#endif
#ifndef VIVID_PKG_WEBGPU_LIB_DIR
#define VIVID_PKG_WEBGPU_LIB_DIR ""
#endif
#ifndef VIVID_PKG_ARCH
#define VIVID_PKG_ARCH ""
#endif

namespace vivid {

namespace {
// Run argv (argv[0] = program), capturing combined stdout+stderr. Returns the exit
// code (or -1 if exec/fork failed). Uses fork+execvp so no shell quoting is needed.
int run_capture(const std::vector<std::string>& argv, std::string& out) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return -1; }
    if (pid == 0) {  // child
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[0]); ::close(pipefd[1]);
        std::vector<char*> c;
        c.reserve(argv.size() + 1);
        for (const auto& a : argv) c.push_back(const_cast<char*>(a.c_str()));
        c.push_back(nullptr);
        execvp(c[0], c.data());
        _exit(127);  // exec failed
    }
    ::close(pipefd[1]);
    char buf[4096]; ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(n));
    ::close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void add_inc(std::vector<std::string>& argv, const std::string& dir) {
    if (!dir.empty()) { argv.push_back("-I"); argv.push_back(dir); }
}

// ADR-0020: the compile toolchain must work on an END-USER machine, not only a dev checkout.
// The VIVID_PKG_* defines bake in the dev repo/build paths; here we prefer bundle-relative dirs
// resolved off the executable (the same <exe>/../Resources pattern shader_library + main use),
// and fall back to the baked defines only when the shipped dirs aren't present (non-bundle dev).
struct Toolchain {
    std::string cxx;        // compiler (program name searched on PATH, or an absolute path)
    std::string inc_src;    // -I dir containing operator_api/ (Resources, or app/src in dev)
    std::string inc_wgpu;   // -I dir containing webgpu/ (Resources/webgpu, or wgpu include in dev)
    std::string lib_wgpu;   // -L dir holding libwgpu_native (Contents/MacOS, or wgpu lib in dev)
};

Toolchain resolve_toolchain() {
    namespace fs = std::filesystem;
    Toolchain t;
    t.inc_src  = VIVID_PKG_SRC_DIR;
    t.inc_wgpu = VIVID_PKG_WEBGPU_INCLUDE_DIR;
    t.lib_wgpu = VIVID_PKG_WEBGPU_LIB_DIR;

    // Compiler: a baked ABSOLUTE path that doesn't exist on this machine (an installed .app carrying
    // the dev's Xcode path) falls back to a PATH-resolved clang++ (the macOS Command Line Tools).
    t.cxx = VIVID_PKG_CXX;
    if (t.cxx.empty() || (t.cxx.find('/') != std::string::npos && !fs::exists(t.cxx))) t.cxx = "clang++";

    const std::string exe = platform::executable_path();
    if (!exe.empty()) {
        const fs::path exe_dir = fs::path(exe).parent_path();
        // Bundle: <exe>/../Resources ; non-bundle install: <exe>/ (siblings of the binary).
        for (const fs::path base : { (exe_dir / ".." / "Resources").lexically_normal(), exe_dir }) {
            if (fs::exists(base / "operator_api"))          t.inc_src  = base.string();
            if (fs::exists(base / "webgpu" / "webgpu"))     t.inc_wgpu = (base / "webgpu").string();
        }
        // libwgpu_native lives next to the executable (Contents/MacOS), already shipped.
        if (fs::exists(exe_dir / (std::string("libwgpu_native") + platform::plugin_suffix())))
            t.lib_wgpu = exe_dir.string();
    }
    return t;
}
}  // namespace

ToolchainStatus probe_package_toolchain() {
    namespace fs = std::filesystem;
    const Toolchain t = resolve_toolchain();
    ToolchainStatus s;
    s.cxx = t.cxx; s.inc_src = t.inc_src; s.inc_wgpu = t.inc_wgpu; s.lib_wgpu = t.lib_wgpu;
    std::error_code ec;
    // Compiler: an absolute path must exist; a bare name (e.g. "clang++") is searched on PATH.
    if (t.cxx.find('/') != std::string::npos) {
        if (fs::exists(t.cxx, ec)) { s.cxx_found = true; s.cxx_resolved_path = t.cxx; }
    } else if (const char* path = std::getenv("PATH")) {
        std::string p(path);
        for (size_t start = 0; start <= p.size(); ) {
            const size_t colon = p.find(':', start);
            const std::string dir = p.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
            if (!dir.empty() && fs::exists(fs::path(dir) / t.cxx, ec)) {
                s.cxx_found = true; s.cxx_resolved_path = (fs::path(dir) / t.cxx).string(); break;
            }
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
    }
    s.headers_present = fs::exists(fs::path(t.inc_src) / "operator_api", ec) &&
                        fs::exists(fs::path(t.inc_wgpu) / "webgpu", ec);
    s.libwgpu_present = fs::exists(
        fs::path(t.lib_wgpu) / (std::string("libwgpu_native") + platform::plugin_suffix()), ec);
    return s;
}

PackageCompileResult PackageCompiler::compile_operator(const std::string& package_dir,
                                                       const PackageOperator& op,
                                                       const std::string& out_dir) {
    namespace fs = std::filesystem;
    PackageCompileResult r;
    r.op_name = op.name;

    const fs::path src = fs::path(package_dir) / op.source;
    if (!fs::exists(src)) {
        r.error_output = "source not found: " + src.string();
        return r;
    }
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    const fs::path out = fs::path(out_dir) / (op.name + platform::plugin_suffix());
    r.dylib_path = out.string();

    const Toolchain tc = resolve_toolchain();
    std::vector<std::string> argv = { tc.cxx, "-std=c++17", "-shared", "-fPIC", "-O2" };
    if (std::string(VIVID_PKG_ARCH).size()) { argv.push_back("-arch"); argv.push_back(VIVID_PKG_ARCH); }
    add_inc(argv, tc.inc_src);                 // operator_api/ headers
    add_inc(argv, package_dir);                // package-local headers
    if (op.gpu) {
        add_inc(argv, tc.inc_wgpu);
        if (!tc.lib_wgpu.empty()) {
            argv.push_back("-L"); argv.push_back(tc.lib_wgpu);
            argv.push_back("-lwgpu_native");
            argv.push_back("-Wl,-rpath," + tc.lib_wgpu);   // resolve @rpath/libwgpu_native.dylib at load
        }
    }
    argv.push_back("-o"); argv.push_back(out.string());
    argv.push_back(src.string());              // compiled directly (VIVID_REGISTER provides exports)

    std::fprintf(stderr, "[vivid] compiling package operator '%s' (%s) with %s\n",
                 op.name.c_str(), src.filename().c_str(), tc.cxx.c_str());
    std::string output;
    const int code = run_capture(argv, output);
    if (code != 0) {
        if (code == 127 && output.empty())     // execvp couldn't find/launch the compiler
            r.error_output = "C++ compiler '" + tc.cxx + "' not found — install the Xcode "
                             "Command Line Tools (xcode-select --install)";
        else
            r.error_output = output.empty() ? ("compiler exited " + std::to_string(code)) : output;
        return r;
    }
    r.success = true;
    return r;
}

}  // namespace vivid
