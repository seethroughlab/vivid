#pragma once
#include <vivid/operator.h>
#include <vivid/context.h>
#include <vector>
#include <map>
#include <memory>
#include <string>

namespace vivid {

class Renderer;

class Graph {
public:
    Graph();
    ~Graph();

    // Rebuild graph from operators (typically from HotLoader)
    void rebuild(const std::vector<Operator*>& operators);

    // Clear all operators
    void clear();

    // Lifecycle management
    void initAll(Context& ctx);
    void execute(Context& ctx);
    void cleanupAll();

    // State preservation for hot-reload
    std::map<std::string, std::unique_ptr<OperatorState>> saveAllStates();
    void restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states);

    // Preview capture for VS Code extension
    struct Preview {
        std::string operatorId;
        int sourceLine = 0;
        std::string base64Jpeg;
        int width = 0;
        int height = 0;
        OutputKind outputKind = OutputKind::Texture;
        float value = 0.0f;  // For Value outputs
    };
    std::vector<Preview> capturePreviews(Context& ctx, Renderer& renderer, int thumbSize = 128);

    // Get final output texture (last operator's "out")
    Texture* finalOutput(Context& ctx);

    // Accessors
    const std::vector<Operator*>& operators() const { return operators_; }
    bool empty() const { return operators_.empty(); }
    size_t size() const { return operators_.size(); }

private:
    std::vector<Operator*> operators_;
};

// Utility: Base64 encode binary data
std::string base64Encode(const std::vector<uint8_t>& data);

} // namespace vivid
