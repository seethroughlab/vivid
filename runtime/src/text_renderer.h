#pragma once

#include <vivid/types.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace vivid {

class Renderer;
class FontAtlas;

/**
 * @brief Text alignment options
 */
enum class TextAlign {
    Left,
    Center,
    Right
};

/**
 * @brief GPU-accelerated text renderer
 *
 * Renders text using font atlases with batched quad rendering.
 */
class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    /**
     * @brief Initialize the text renderer
     */
    bool init(Renderer& renderer);

    /**
     * @brief Render text to a texture
     * @param font Font atlas to use
     * @param text Text string to render
     * @param position Position in pixels (top-left of text)
     * @param color Text color (RGBA)
     * @param output Target texture
     * @param clearColor Clear color (negative alpha = no clear)
     */
    void renderText(FontAtlas& font, const std::string& text,
                    glm::vec2 position, glm::vec4 color,
                    Texture& output, const glm::vec4& clearColor = {0, 0, 0, -1});

    /**
     * @brief Render text with alignment
     */
    void renderTextAligned(FontAtlas& font, const std::string& text,
                           glm::vec2 position, TextAlign align, glm::vec4 color,
                           Texture& output, const glm::vec4& clearColor = {0, 0, 0, -1});

    /**
     * @brief Render text centered at a position
     */
    void renderTextCentered(FontAtlas& font, const std::string& text,
                            glm::vec2 center, glm::vec4 color,
                            Texture& output, const glm::vec4& clearColor = {0, 0, 0, -1});

private:
    struct TextVertex {
        glm::vec2 position;
        glm::vec2 uv;
        glm::vec4 color;
    };

    bool createPipeline();
    void renderBatch(FontAtlas& font, Texture& output, const glm::vec4& clearColor);

    Renderer* renderer_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout textureLayout_ = nullptr;
    WGPUBindGroupLayout uniformLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUSampler sampler_ = nullptr;

    std::vector<TextVertex> vertices_;
    std::vector<uint32_t> indices_;

    static constexpr int MAX_CHARS = 1024;
};

} // namespace vivid
