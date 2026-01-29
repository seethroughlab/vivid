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
#elif defined(_WIN32)
// Windows ConPTY implementation
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <algorithm>
#include <cstdio>
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
#elif defined(_WIN32)
    // Windows ConPTY implementation
    HPCON m_hPC = nullptr;           // Pseudo console handle
    HANDLE m_hPipeIn = nullptr;      // Read from child (our end)
    HANDLE m_hPipeOut = nullptr;     // Write to child (our end)
    HANDLE m_hProcess = nullptr;     // Child process handle
    DWORD m_processId = 0;
    bool m_running = false;
    int m_cols = 80;
    int m_rows = 24;

    ~Impl() {
        stop();
    }

    bool start(const std::string& command, const std::string& workingDir) {
        if (m_running) {
            stop();
        }

        // Create the pipes for ConPTY communication
        HANDLE hPipeInRead = nullptr, hPipeInWrite = nullptr;
        HANDLE hPipeOutRead = nullptr, hPipeOutWrite = nullptr;

        if (!CreatePipe(&hPipeInRead, &hPipeInWrite, nullptr, 0)) {
            std::cerr << "[PTY] CreatePipe (in) failed: " << GetLastError() << "\n";
            return false;
        }
        if (!CreatePipe(&hPipeOutRead, &hPipeOutWrite, nullptr, 0)) {
            std::cerr << "[PTY] CreatePipe (out) failed: " << GetLastError() << "\n";
            CloseHandle(hPipeInRead);
            CloseHandle(hPipeInWrite);
            return false;
        }

        // Create the pseudo console
        COORD size = { static_cast<SHORT>(m_cols), static_cast<SHORT>(m_rows) };
        HRESULT hr = CreatePseudoConsole(size, hPipeInRead, hPipeOutWrite, 0, &m_hPC);
        if (FAILED(hr)) {
            std::cerr << "[PTY] CreatePseudoConsole failed: 0x" << std::hex << hr << std::dec << "\n";
            CloseHandle(hPipeInRead);
            CloseHandle(hPipeInWrite);
            CloseHandle(hPipeOutRead);
            CloseHandle(hPipeOutWrite);
            return false;
        }

        // Close the handles that the pseudo console now owns
        CloseHandle(hPipeInRead);
        CloseHandle(hPipeOutWrite);

        // Keep our ends of the pipes
        m_hPipeIn = hPipeOutRead;   // We read from this
        m_hPipeOut = hPipeInWrite;  // We write to this

        // Set up the startup info with the pseudo console
        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof(STARTUPINFOEXW);

        // Initialize the attribute list
        SIZE_T attrListSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, attrListSize));
        if (!si.lpAttributeList) {
            std::cerr << "[PTY] HeapAlloc failed\n";
            cleanup();
            return false;
        }

        if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize)) {
            std::cerr << "[PTY] InitializeProcThreadAttributeList failed: " << GetLastError() << "\n";
            HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
            cleanup();
            return false;
        }

        if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_hPC,
                sizeof(HPCON), nullptr, nullptr)) {
            std::cerr << "[PTY] UpdateProcThreadAttribute failed: " << GetLastError() << "\n";
            DeleteProcThreadAttributeList(si.lpAttributeList);
            HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
            cleanup();
            return false;
        }

        // Build the command line
        std::wstring cmdLine = getShellCommand(command);

        // Convert working directory to wide string
        std::wstring wWorkingDir;
        if (!workingDir.empty()) {
            wWorkingDir = utf8ToWide(workingDir);
        }

        PROCESS_INFORMATION pi = {};
        // Use EXTENDED_STARTUPINFO_PRESENT so the pseudo console attribute is used
        // Also use CREATE_NEW_PROCESS_GROUP to prevent console inheritance
        DWORD creationFlags = EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_PROCESS_GROUP;
        BOOL success = CreateProcessW(
            nullptr,                                    // lpApplicationName
            const_cast<LPWSTR>(cmdLine.c_str()),       // lpCommandLine
            nullptr,                                    // lpProcessAttributes
            nullptr,                                    // lpThreadAttributes
            FALSE,                                      // bInheritHandles
            creationFlags,                             // dwCreationFlags
            nullptr,                                    // lpEnvironment
            wWorkingDir.empty() ? nullptr : wWorkingDir.c_str(), // lpCurrentDirectory
            &si.StartupInfo,                           // lpStartupInfo
            &pi                                        // lpProcessInformation
        );

        // Clean up attribute list
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

        if (!success) {
            std::cerr << "[PTY] CreateProcessW failed: " << GetLastError() << "\n";
            cleanup();
            return false;
        }

        m_hProcess = pi.hProcess;
        m_processId = pi.dwProcessId;
        CloseHandle(pi.hThread);  // Don't need thread handle

        m_running = true;
        std::cout << "[PTY] Started shell (pid: " << m_processId << ")\n";
        return true;
    }

    bool isRunning() const {
        if (!m_running || m_hProcess == nullptr) {
            return false;
        }

        DWORD exitCode;
        if (GetExitCodeProcess(m_hProcess, &exitCode)) {
            if (exitCode != STILL_ACTIVE) {
                const_cast<Impl*>(this)->m_running = false;
                return false;
            }
        }
        return true;
    }

    void write(const std::string& data) {
        if (!m_running || m_hPipeOut == nullptr) {
            return;
        }
        DWORD written;
        WriteFile(m_hPipeOut, data.c_str(), static_cast<DWORD>(data.size()),
                  &written, nullptr);
    }

    std::string read() {
        if (!m_running || m_hPipeIn == nullptr) return "";

        std::string result;
        char buffer[4096];

        while (true) {
            DWORD available = 0;
            if (!PeekNamedPipe(m_hPipeIn, nullptr, 0, nullptr, &available, nullptr)) {
                // Pipe error (process may have exited)
                break;
            }

            if (available == 0) {
                // No data available
                break;
            }

            DWORD bytesRead = 0;
            DWORD toRead = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
            if (ReadFile(m_hPipeIn, buffer, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                result.append(buffer, bytesRead);
            } else {
                break;
            }
        }

        return result;
    }

    void setSize(int cols, int rows) {
        m_cols = cols;
        m_rows = rows;
        if (m_hPC != nullptr) {
            COORD size = { static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
            HRESULT hr = ResizePseudoConsole(m_hPC, size);
            if (FAILED(hr)) {
                std::cerr << "[PTY] ResizePseudoConsole failed: 0x" << std::hex << hr << std::dec << "\n";
            }
        }
    }

    void stop() {
        if (m_hProcess != nullptr) {
            // Send Ctrl+C first (graceful termination)
            if (m_hPipeOut != nullptr) {
                char ctrlC = 3;
                DWORD written;
                WriteFile(m_hPipeOut, &ctrlC, 1, &written, nullptr);
            }
            Sleep(50);  // Give process time to exit

            // Check if still running, then terminate
            DWORD exitCode;
            if (GetExitCodeProcess(m_hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                TerminateProcess(m_hProcess, 0);
                WaitForSingleObject(m_hProcess, 1000);
            }
            CloseHandle(m_hProcess);
            m_hProcess = nullptr;
        }

        cleanup();
        m_running = false;
        m_processId = 0;
    }

    int pid() const {
        return static_cast<int>(m_processId);
    }

private:
    void cleanup() {
        if (m_hPipeIn != nullptr) {
            CloseHandle(m_hPipeIn);
            m_hPipeIn = nullptr;
        }
        if (m_hPipeOut != nullptr) {
            CloseHandle(m_hPipeOut);
            m_hPipeOut = nullptr;
        }
        if (m_hPC != nullptr) {
            ClosePseudoConsole(m_hPC);
            m_hPC = nullptr;
        }
    }

    std::wstring utf8ToWide(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                       static_cast<int>(str.size()), nullptr, 0);
        std::wstring result(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                           static_cast<int>(str.size()), &result[0], size);
        return result;
    }

    std::wstring getShellCommand(const std::string& command) {
        if (!command.empty()) {
            // Run a specific command via cmd.exe /c
            return L"cmd.exe /c " + utf8ToWide(command);
        }

        // Try PowerShell first (more modern, better ConPTY support)
        // -NoLogo suppresses the banner, -NoExit keeps it running
        return L"powershell.exe -NoLogo -NoExit";
    }
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
