#include "packages/package_compiler.h"

#include <filesystem>
#include <cstdio>
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

void add_inc(std::vector<std::string>& argv, const char* dir) {
    if (dir && *dir) { argv.push_back("-I"); argv.push_back(dir); }
}
}  // namespace

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
    const fs::path out = fs::path(out_dir) / (op.name + ".dylib");
    r.dylib_path = out.string();

    std::vector<std::string> argv = { VIVID_PKG_CXX, "-std=c++17", "-shared", "-fPIC", "-O2" };
    if (std::string(VIVID_PKG_ARCH).size()) { argv.push_back("-arch"); argv.push_back(VIVID_PKG_ARCH); }
    add_inc(argv, VIVID_PKG_SRC_DIR);          // operator_api/ headers
    add_inc(argv, package_dir.c_str());        // package-local headers
    if (op.gpu) {
        add_inc(argv, VIVID_PKG_WEBGPU_INCLUDE_DIR);
        const std::string lib = VIVID_PKG_WEBGPU_LIB_DIR;
        if (!lib.empty()) {
            argv.push_back("-L"); argv.push_back(lib);
            argv.push_back("-lwgpu_native");
            argv.push_back("-Wl,-rpath," + lib);   // resolve @rpath/libwgpu_native.dylib at load
        }
    }
    argv.push_back("-o"); argv.push_back(out.string());
    argv.push_back(src.string());              // compiled directly (VIVID_REGISTER provides exports)

    std::fprintf(stderr, "[vivid] compiling package operator '%s' (%s)\n",
                 op.name.c_str(), src.filename().c_str());
    std::string output;
    const int code = run_capture(argv, output);
    if (code != 0) {
        r.error_output = output.empty() ? ("compiler exited " + std::to_string(code)) : output;
        return r;
    }
    r.success = true;
    return r;
}

}  // namespace vivid
