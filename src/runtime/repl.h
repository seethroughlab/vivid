#ifndef VIVID_RUNTIME_REPL_H
#define VIVID_RUNTIME_REPL_H

#include <string>
#include <vector>

namespace vivid {

class RuntimeAPI;
class TextRenderer;

class Repl {
public:
    explicit Repl(RuntimeAPI& api);

    // GLFW callbacks (routed via glfwSetWindowUserPointer)
    void on_char(unsigned int codepoint);
    void on_key(int key, int action, int mods);

    // Process pending command via RuntimeAPI. Returns true if topology changed.
    bool update(bool& has_gpu_ops, bool& has_audio);

    // Draw REPL overlay at bottom of window
    void draw(TextRenderer& tr, uint32_t window_width, uint32_t window_height);

private:
    struct PendingCommand {
        bool ready = false;
        std::string line;
    };

    void execute(const std::string& line, bool& has_gpu_ops, bool& has_audio);
    std::string help_text() const;

    RuntimeAPI& api_;

    // Input state
    std::string input_;
    size_t cursor_ = 0;

    // History
    std::vector<std::string> history_;
    int history_index_ = -1;   // -1 = not browsing history
    std::string saved_input_;  // input before history browsing

    // Output lines (most recent commands/results)
    static constexpr size_t kMaxOutputLines = 8;
    std::vector<std::string> output_lines_;

    // Pending command to execute in update()
    PendingCommand pending_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_REPL_H
