// vivid-watchdog — optional supervisor for unattended Vivid deployments.
//
// Fork/exec's the vivid binary; when the child dies by a fatal signal
// (SIGSEGV / SIGBUS / SIGABRT / SIGFPE — the set caught by crash_guard.h),
// relaunches it with `--safe-mode` appended.  Gives up after
// --max-restarts consecutive crashes.
//
// All recovery intelligence (crash marker, safe mode disable set,
// quarantine scan) lives inside the vivid process itself.  The watchdog
// only contributes process supervision.
//
// Not supported on Windows (Vivid's primary target is macOS); a portable
// version would require CreateProcess / WaitForSingleObject.

#include <getopt.h>

#if !defined(_WIN32)
#  include <signal.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#endif

namespace {

#if !defined(_WIN32)

pid_t g_child_pid = 0;

// async-signal-safe: only touches a volatile-ish global and calls kill().
void forward_signal(int sig) {
    if (g_child_pid > 0) {
        (void)kill(g_child_pid, sig);
    }
}

bool is_fatal_crash_signal(int sig) {
    // Keep in lock-step with src/runtime/core/crash_guard.h.
    return sig == SIGSEGV || sig == SIGBUS || sig == SIGABRT || sig == SIGFPE;
}

const char* signame(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS:  return "SIGBUS";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGINT:  return "SIGINT";
        case SIGTERM: return "SIGTERM";
        case SIGKILL: return "SIGKILL";
        default:      return "?";
    }
}

std::string dirname_of(const std::string& path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return "";
    return path.substr(0, slash + 1);
}

// Best-effort auto-discovery of the vivid executable:
//   1) VIVID_PATH env var (if non-empty)
//   2) Sibling of the watchdog's own executable
//   3) Sibling derived from argv[0]
//   4) Bare "vivid" — execvp will resolve via PATH
std::string default_vivid_path(const char* argv0) {
    if (const char* env = std::getenv("VIVID_PATH"); env && *env)
        return env;

    char buf[4096] = {0};
#if defined(__APPLE__)
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0 && buf[0] != 0) {
        std::string dir = dirname_of(buf);
        if (!dir.empty()) return dir + "vivid";
    }
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        std::string dir = dirname_of(buf);
        if (!dir.empty()) return dir + "vivid";
    }
#endif
    if (argv0) {
        std::string dir = dirname_of(argv0);
        if (!dir.empty()) return dir + "vivid";
    }
    return "vivid";
}

void print_usage() {
    std::printf(
        "usage: vivid-watchdog [--vivid-path PATH] [--max-restarts N]\n"
        "                     [--restart-delay SECS] [--no-safe-mode-on-crash]\n"
        "                     [--help] [--] [vivid args...]\n"
        "\n"
        "Supervises vivid, relaunching with --safe-mode on fatal signal\n"
        "(SIGSEGV / SIGBUS / SIGABRT / SIGFPE).  Clean exits and user signals\n"
        "(SIGINT / SIGTERM / SIGKILL) are propagated without restart.\n"
        "\n"
        "  --vivid-path PATH           Path to vivid binary (default: sibling of\n"
        "                              this binary; overrideable via VIVID_PATH).\n"
        "  --max-restarts N            Give up after N consecutive crashes (default 5).\n"
        "  --restart-delay SECS        Delay before each restart (default 2).\n"
        "  --no-safe-mode-on-crash     Do NOT append --safe-mode on restart.\n"
        "  --help                      Show this help.\n");
}

#endif // !_WIN32

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    (void)argc; (void)argv;
    std::fprintf(stderr, "vivid-watchdog: not supported on Windows\n");
    return 2;
#else
    std::string vivid_path;
    int  max_restarts       = 5;
    int  restart_delay      = 2;
    bool safe_mode_on_crash = true;

    static const option longopts[] = {
        {"vivid-path",            required_argument, nullptr, 'p'},
        {"max-restarts",          required_argument, nullptr, 'n'},
        {"restart-delay",         required_argument, nullptr, 'd'},
        {"no-safe-mode-on-crash", no_argument,       nullptr, 's'},
        {"help",                  no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    int c;
    // Leading '+' stops getopt from reordering: everything after the first
    // non-option is treated as forwarded args to vivid.
    while ((c = getopt_long(argc, argv, "+p:n:d:sh", longopts, nullptr)) != -1) {
        switch (c) {
            case 'p': vivid_path = optarg; break;
            case 'n': max_restarts  = std::atoi(optarg); break;
            case 'd': restart_delay = std::atoi(optarg); break;
            case 's': safe_mode_on_crash = false; break;
            case 'h': print_usage(); return 0;
            default:  print_usage(); return 2;
        }
    }
    if (max_restarts < 0)  max_restarts  = 0;
    if (restart_delay < 0) restart_delay = 0;

    if (vivid_path.empty())
        vivid_path = default_vivid_path(argv[0]);

    std::vector<std::string> forwarded;
    for (int i = optind; i < argc; ++i) forwarded.emplace_back(argv[i]);

    // Forward SIGINT/SIGTERM to the current child so Ctrl-C / supervisor
    // shutdown cleanly stops both processes.
    struct sigaction sa{};
    sa.sa_handler = forward_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART: we want waitpid to return EINTR
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP,  &sa, nullptr);

    int  crash_count   = 0;
    bool last_was_crash = false;

    for (;;) {
        // Build argv for this attempt.
        std::vector<std::string> run_args;
        run_args.reserve(forwarded.size() + 2);
        run_args.push_back(vivid_path);
        for (const auto& a : forwarded) run_args.push_back(a);
        if (last_was_crash && safe_mode_on_crash)
            run_args.emplace_back("--safe-mode");

        std::vector<char*> cargv;
        cargv.reserve(run_args.size() + 1);
        for (auto& s : run_args)
            cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) {
            std::fprintf(stderr, "[watchdog] fork failed: %s\n",
                         std::strerror(errno));
            return 1;
        }
        if (pid == 0) {
            // Child: replace image with vivid.
            execvp(cargv[0], cargv.data());
            std::fprintf(stderr, "[watchdog] execvp '%s' failed: %s\n",
                         cargv[0], std::strerror(errno));
            _exit(127);
        }

        g_child_pid = pid;

        int status = 0;
        for (;;) {
            pid_t r = waitpid(pid, &status, 0);
            if (r == pid) break;
            if (r < 0 && errno == EINTR) continue;
            std::fprintf(stderr, "[watchdog] waitpid failed: %s\n",
                         std::strerror(errno));
            g_child_pid = 0;
            return 1;
        }
        g_child_pid = 0;

        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            return code;  // clean exit — propagate
        }
        if (WIFSIGNALED(status)) {
            const int sig = WTERMSIG(status);
            if (!is_fatal_crash_signal(sig)) {
                // User / supervisor initiated stop — shell convention 128+sig.
                std::fprintf(stderr,
                             "[watchdog] vivid terminated by %s — stopping.\n",
                             signame(sig));
                return 128 + sig;
            }
            crash_count++;
            if (crash_count > max_restarts) {
                std::fprintf(stderr,
                             "[watchdog] giving up after %d consecutive crashes "
                             "(last: %s)\n",
                             max_restarts, signame(sig));
                return 1;
            }
            std::fprintf(stderr,
                         "[watchdog] vivid died by %s (attempt %d of %d); "
                         "restarting in %ds%s\n",
                         signame(sig), crash_count, max_restarts, restart_delay,
                         safe_mode_on_crash ? " with --safe-mode" : "");
            if (restart_delay > 0) sleep(static_cast<unsigned>(restart_delay));
            last_was_crash = true;
            continue;
        }
        // Stopped or continued (unusual for non-traced processes) — loop.
    }
#endif
}
