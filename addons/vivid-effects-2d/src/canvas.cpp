#include <vivid/effects/canvas.h>
#include <vivid/context.h>
#include "canvas_renderer.h"
#include "font_atlas.h"
#include <iostream>

namespace vivid::effects {

Canvas::Canvas()
    : m_renderer(std::make_unique<CanvasRenderer>())
    , m_font(std::make_unique<FontAtlas>())
{
}

Canvas::~Canvas() {
    cleanup();
}

bool Canvas::loadFont(Context& ctx, const std::string& path, float fontSize) {
    return m_font->load(ctx, path, fontSize);
}

void Canvas::clear(float r, float g, float b, float a) {
    m_clearColor = glm::vec4(r, g, b, a);
    m_frameBegun = true;
    m_renderer->begin(m_width, m_height, m_clearColor);
}

void Canvas::rectFilled(float x, float y, float w, float h, const glm::vec4& color) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    m_renderer->rectFilled(x, y, w, h, color);
}

void Canvas::rect(float x, float y, float w, float h, float lineWidth, const glm::vec4& color) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    m_renderer->rect(x, y, w, h, lineWidth, color);
}

void Canvas::circleFilled(float x, float y, float radius, const glm::vec4& color, int segments) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    m_renderer->circleFilled(x, y, radius, color, segments);
}

void Canvas::circle(float x, float y, float radius, float lineWidth, const glm::vec4& color, int segments) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    m_renderer->circle(x, y, radius, lineWidth, color, segments);
}

void Canvas::line(float x1, float y1, float x2, float y2, float width, const glm::vec4& color) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    m_renderer->line(x1, y1, x2, y2, width, color);
}

void Canvas::triangleFilled(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    m_renderer->triangleFilled(a, b, c, color);
}

void Canvas::text(const std::string& str, float x, float y, const glm::vec4& color) {
    if (!m_frameBegun) {
        clear(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    }
    if (m_font && m_font->valid()) {
        m_renderer->text(*m_font, str, x, y, color);
    } else {
        static int warnCount = 0;
        if (warnCount++ < 5) {
            std::cerr << "[Canvas::text] Warning: font not valid for text '" << str << "'\n";
        }
    }
}

void Canvas::textCentered(const std::string& str, float x, float y, const glm::vec4& color) {
    if (!m_font || !m_font->valid()) return;

    glm::vec2 size = m_font->measureText(str);
    text(str, x - size.x / 2, y + size.y / 2, color);
}

glm::vec2 Canvas::measureText(const std::string& str) const {
    if (!m_font || !m_font->valid()) {
        return glm::vec2(0);
    }
    return m_font->measureText(str);
}

void Canvas::init(Context& ctx) {
    createOutput(ctx, m_width, m_height);

    if (!m_renderer->init(ctx)) {
        std::cerr << "[Canvas] Failed to initialize renderer\n";
        return;
    }

    m_initialized = true;
}

void Canvas::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }
    // Canvas uses declared size() - no auto-resize

    // Auto-begin frame if user didn't call clear()
    if (!m_frameBegun) {
        m_renderer->begin(m_width, m_height, m_clearColor);
    }

    // Render all batched primitives to our output texture
    m_renderer->render(ctx, m_output, m_outputView);

    // Reset for next frame
    m_frameBegun = false;
    didCook();
}

void Canvas::cleanup() {
    if (m_renderer) {
        m_renderer->cleanup();
    }
    if (m_font) {
        m_font->cleanup();
    }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
