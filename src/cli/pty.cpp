// PTY implementation for IDE panel terminal
// macOS/Linux implementation using forkpty

#include <vivid/pty.h>
#include <iostream>
#include <cstring>

#ifdef __APPLE__
#include <util.h>  // forkpty on macOS
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <pwd.h>
#elif defined(__linux__)
#include <pty.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <pwd.h>
#else
// Windows stub - PTY not supported
#endif

namespace vivid {

class PTY::Impl {
public:
#if defined(__APPLE__) || defined(__linux__)
    int masterFd = -1;
    pid_t childPid = -1;
    bool running = false;

    ~Impl() {
        stop();
    }

    bool start(const std::string& command, const std::string& workingDir) {
        if (running) {
            stop();
        }

        struct winsize ws;
        ws.ws_col = 80;
        ws.ws_row = 24;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;

        childPid = forkpty(&masterFd, nullptr, nullptr, &ws);

        if (childPid < 0) {
            std::cerr << "[PTY] forkpty failed: " << strerror(errno) << "\n";
            return false;
        }

        if (childPid == 0) {
            // Child process
            if (!workingDir.empty()) {
                if (chdir(workingDir.c_str()) != 0) {
                    std::cerr << "[PTY] Failed to change directory: " << strerror(errno) << "\n";
                }
            }

            // Get user's default shell
            const char* shell = getenv("SHELL");
            if (!shell) {
                struct passwd* pw = getpwuid(getuid());
                shell = pw ? pw->pw_shell : "/bin/sh";
            }

            // Set up environment for interactive shell
            // Use xterm-256color since libvterm supports full xterm emulation
            setenv("TERM", "xterm-256color", 1);

            if (command.empty()) {
                // Run interactive shell
                execlp(shell, shell, "-l", nullptr);
            } else {
                // Run specific command
                execlp(shell, shell, "-c", command.c_str(), nullptr);
            }

            // If exec fails
            std::cerr << "[PTY] exec failed: " << strerror(errno) << "\n";
            _exit(1);
        }

        // Parent process
        // Set non-blocking mode for reading
        int flags = fcntl(masterFd, F_GETFL, 0);
        fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);

        running = true;
        std::cout << "[PTY] Started shell (pid: " << childPid << ")\n";
        return true;
    }

    bool isRunning() const {
        if (!running || childPid <= 0) return false;

        // Check if child is still alive
        int status;
        pid_t result = waitpid(childPid, &status, WNOHANG);
        if (result > 0) {
            // Child has exited
            const_cast<Impl*>(this)->running = false;
            return false;
        }
        return true;
    }

    void write(const std::string& data) {
        if (!running || masterFd < 0) {
            std::cerr << "[PTY] Write ignored (not running)\n";
            return;
        }
        ssize_t written = ::write(masterFd, data.c_str(), data.size());
        if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "[PTY] Write error: " << strerror(errno) << "\n";
        }
    }

    std::string read() {
        if (!running || masterFd < 0) return "";

        char buffer[4096];
        std::string result;

        while (true) {
            ssize_t n = ::read(masterFd, buffer, sizeof(buffer));
            if (n > 0) {
                result.append(buffer, n);
            } else if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No more data available
                    break;
                } else {
                    std::cerr << "[PTY] Read error: " << strerror(errno) << "\n";
                    break;
                }
            } else {
                // EOF - child closed
                running = false;
                break;
            }
        }

        return result;
    }

    void setSize(int cols, int rows) {
        if (masterFd < 0) return;

        struct winsize ws;
        ws.ws_col = cols;
        ws.ws_row = rows;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;

        if (ioctl(masterFd, TIOCSWINSZ, &ws) < 0) {
            std::cerr << "[PTY] Failed to set terminal size: " << strerror(errno) << "\n";
        }
    }

    void stop() {
        if (childPid > 0) {
            // Send SIGTERM first, then SIGKILL if needed
            kill(childPid, SIGTERM);
            usleep(50000);  // 50ms

            int status;
            if (waitpid(childPid, &status, WNOHANG) == 0) {
                // Still running, force kill
                kill(childPid, SIGKILL);
                waitpid(childPid, &status, 0);
            }
            childPid = -1;
        }

        if (masterFd >= 0) {
            close(masterFd);
            masterFd = -1;
        }

        running = false;
    }

    int pid() const {
        return childPid;
    }
#else
    // Windows stub
    bool running = false;

    bool start(const std::string&, const std::string&) {
        std::cerr << "[PTY] Not supported on this platform\n";
        return false;
    }

    bool isRunning() const { return false; }
    void write(const std::string&) {}
    std::string read() { return ""; }
    void setSize(int, int) {}
    void stop() {}
    int pid() const { return -1; }
#endif
};

PTY::PTY() : m_impl(std::make_unique<Impl>()) {}
PTY::~PTY() = default;

bool PTY::start(const std::string& command, const std::string& workingDir) {
    return m_impl->start(command, workingDir);
}

bool PTY::isRunning() const {
    return m_impl->isRunning();
}

void PTY::write(const std::string& data) {
    m_impl->write(data);
}

std::string PTY::read() {
    return m_impl->read();
}

void PTY::setSize(int cols, int rows) {
    m_impl->setSize(cols, rows);
}

void PTY::stop() {
    m_impl->stop();
}

int PTY::pid() const {
    return m_impl->pid();
}

} // namespace vivid
