#pragma once

#include <string>
#include <functional>
#include <memory>

namespace vivid {

/// Pseudo-terminal for running shell processes
/// Used by IDE panel terminal to run Claude Code or shell commands
class PTY {
public:
    PTY();
    ~PTY();

    // Non-copyable
    PTY(const PTY&) = delete;
    PTY& operator=(const PTY&) = delete;

    /// Start a shell process (default: user's shell from $SHELL)
    /// @param command Optional command to run (if empty, runs default shell)
    /// @param workingDir Working directory for the shell
    /// @return true if PTY was started successfully
    bool start(const std::string& command = "", const std::string& workingDir = "");

    /// Check if PTY is running
    bool isRunning() const;

    /// Write data to PTY (user input)
    /// @param data Input data to send to shell
    void write(const std::string& data);

    /// Read available output from PTY (non-blocking)
    /// @return Output data from shell, or empty string if none available
    std::string read();

    /// Set terminal size (for xterm.js resize events)
    /// @param cols Number of columns
    /// @param rows Number of rows
    void setSize(int cols, int rows);

    /// Stop the PTY process
    void stop();

    /// Get the PID of the child process
    int pid() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
