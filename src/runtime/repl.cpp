#include "runtime/repl.h"
#include "runtime/runtime_api.h"
#include "runtime/text_renderer.h"
#include <GLFW/glfw3.h>
#include <sstream>
#include <algorithm>

namespace vivid {

Repl::Repl(RuntimeAPI& api) : api_(api) {}

void Repl::on_char(unsigned int codepoint) {
    if (codepoint < 32 || codepoint > 126) return;
    input_.insert(input_.begin() + cursor_, static_cast<char>(codepoint));
    cursor_++;
}

void Repl::on_key(int key, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    switch (key) {
        case GLFW_KEY_ENTER: {
            if (!input_.empty()) {
                pending_.ready = true;
                pending_.line = input_;
                history_.push_back(input_);
                history_index_ = -1;
            }
            input_.clear();
            cursor_ = 0;
            break;
        }
        case GLFW_KEY_BACKSPACE: {
            if (cursor_ > 0) {
                input_.erase(cursor_ - 1, 1);
                cursor_--;
            }
            break;
        }
        case GLFW_KEY_DELETE: {
            if (cursor_ < input_.size()) {
                input_.erase(cursor_, 1);
            }
            break;
        }
        case GLFW_KEY_LEFT: {
            if (cursor_ > 0) cursor_--;
            break;
        }
        case GLFW_KEY_RIGHT: {
            if (cursor_ < input_.size()) cursor_++;
            break;
        }
        case GLFW_KEY_HOME: {
            cursor_ = 0;
            break;
        }
        case GLFW_KEY_END: {
            cursor_ = input_.size();
            break;
        }
        case GLFW_KEY_UP: {
            if (history_.empty()) break;
            if (history_index_ == -1) {
                saved_input_ = input_;
                history_index_ = static_cast<int>(history_.size()) - 1;
            } else if (history_index_ > 0) {
                history_index_--;
            }
            input_ = history_[history_index_];
            cursor_ = input_.size();
            break;
        }
        case GLFW_KEY_DOWN: {
            if (history_index_ == -1) break;
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                history_index_++;
                input_ = history_[history_index_];
            } else {
                history_index_ = -1;
                input_ = saved_input_;
            }
            cursor_ = input_.size();
            break;
        }
        case GLFW_KEY_ESCAPE: {
            input_.clear();
            cursor_ = 0;
            history_index_ = -1;
            break;
        }
        case GLFW_KEY_A: {
            if (mods & GLFW_MOD_SUPER) { cursor_ = 0; }
            break;
        }
        case GLFW_KEY_E: {
            if (mods & GLFW_MOD_SUPER) { cursor_ = input_.size(); }
            break;
        }
        case GLFW_KEY_U: {
            if (mods & GLFW_MOD_CONTROL) {
                input_.erase(0, cursor_);
                cursor_ = 0;
            }
            break;
        }
    }
}

bool Repl::update(bool& has_gpu_ops, bool& has_audio) {
    if (!pending_.ready) return false;
    pending_.ready = false;

    execute(pending_.line, has_gpu_ops, has_audio);
    return api_.has_pending();
}

void Repl::execute(const std::string& line, bool& has_gpu_ops, bool& has_audio) {
    // Parse: split on whitespace
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    if (tokens.empty()) return;

    const auto& verb = tokens[0];
    CommandResult result = {false, "unknown command '" + verb + "' (try 'help')"};

    if (verb == "set" && tokens.size() >= 3) {
        // set node/param value
        std::string node, param;
        auto slash = tokens[1].find('/');
        if (slash != std::string::npos) {
            node = tokens[1].substr(0, slash);
            param = tokens[1].substr(slash + 1);
            try {
                float val = std::stof(tokens[2]);
                result = api_.set_param(node, param, val);
            } catch (...) {
                result = {false, "invalid number '" + tokens[2] + "'"};
            }
        } else {
            result = {false, "expected node/param (e.g. lfo1/frequency)"};
        }
    } else if (verb == "get" && tokens.size() >= 2) {
        std::string node, param;
        auto slash = tokens[1].find('/');
        if (slash != std::string::npos) {
            node = tokens[1].substr(0, slash);
            param = tokens[1].substr(slash + 1);
            result = api_.get_param(node, param);
        } else {
            result = {false, "expected node/param"};
        }
    } else if (verb == "add" && tokens.size() >= 3) {
        result = api_.add_node(tokens[1], tokens[2]);
    } else if (verb == "remove" && tokens.size() >= 2) {
        result = api_.remove_node(tokens[1]);
    } else if (verb == "connect" && tokens.size() >= 3) {
        result = api_.connect(tokens[1], tokens[2]);
    } else if (verb == "disconnect" && tokens.size() >= 3) {
        result = api_.disconnect(tokens[1], tokens[2]);
    } else if (verb == "inspect" && tokens.size() >= 2) {
        result = api_.inspect(tokens[1]);
    } else if (verb == "list") {
        result = api_.list_nodes();
    } else if (verb == "types") {
        result = api_.list_types();
    } else if (verb == "save") {
        if (tokens.size() >= 2)
            result = api_.save_as(tokens[1]);
        else
            result = api_.save();
    } else if (verb == "reload") {
        result = api_.reload(has_gpu_ops, has_audio);
    } else if (verb == "help") {
        result = {true, help_text()};
    } else if (tokens.size() < 2 && (verb == "set" || verb == "get" || verb == "add" ||
               verb == "remove" || verb == "connect" || verb == "disconnect" || verb == "inspect")) {
        result = {false, verb + ": not enough arguments (try 'help')"};
    }

    // Apply any pending topology changes
    if (api_.has_pending()) {
        api_.apply_pending(has_gpu_ops, has_audio);
    }

    // Push result lines to output
    std::istringstream lines(result.message);
    std::string out_line;
    while (std::getline(lines, out_line)) {
        output_lines_.push_back((result.ok ? "" : "error: ") + out_line);
    }
    while (output_lines_.size() > kMaxOutputLines) {
        output_lines_.erase(output_lines_.begin());
    }
}

std::string Repl::help_text() const {
    return "set <node/param> <value>  - set parameter\n"
           "get <node/param>          - get parameter\n"
           "add <Type> <id>           - add node\n"
           "remove <id>               - remove node\n"
           "connect <from> <to>       - connect ports\n"
           "disconnect <from> <to>    - disconnect ports\n"
           "inspect <id>              - inspect node\n"
           "list                      - list all nodes\n"
           "types                     - list operator types\n"
           "save [path]               - save graph\n"
           "reload                    - reload from disk\n"
           "help                      - show this help";
}

void Repl::draw(TextRenderer& tr, uint32_t window_width, uint32_t window_height) {
    float line_h = tr.line_height();
    float padding = 6.0f;
    float total_lines = static_cast<float>(output_lines_.size()) + 1.0f; // +1 for input
    float panel_height = total_lines * line_h + padding * 2.0f;
    float panel_y = static_cast<float>(window_height) - panel_height;

    // Dark background strip
    tr.draw_rect(0, panel_y, static_cast<float>(window_width), panel_height,
                 0.08f, 0.09f, 0.11f, 0.85f);

    // Thin separator line
    tr.draw_rect(0, panel_y, static_cast<float>(window_width), 1.0f,
                 0.3f, 0.35f, 0.4f, 0.6f);

    float text_x = padding + 4.0f;
    float y = panel_y + padding;

    // Output lines (dimmer)
    for (const auto& line : output_lines_) {
        if (line.substr(0, 6) == "error:") {
            tr.draw_text(text_x, y, line.c_str(), 0.9f, 0.35f, 0.35f);
        } else {
            tr.draw_text(text_x, y, line.c_str(), 0.65f, 0.7f, 0.75f);
        }
        y += line_h;
    }

    // Input line with prompt
    std::string prompt_line = "> " + input_;
    tr.draw_text(text_x, y, prompt_line.c_str(), 0.9f, 0.92f, 0.95f);

    // Cursor (blinking block)
    // cursor_ is position in input_; account for "> " prefix
    std::string before_cursor = "> " + input_.substr(0, cursor_);
    float cursor_x = text_x + tr.text_width(before_cursor.c_str());
    tr.draw_rect(cursor_x, y + 2.0f, 2.0f, line_h - 4.0f,
                 0.9f, 0.92f, 0.95f, 0.8f);
}

} // namespace vivid
