// Performance Panel Implementation
// Displays real-time graphs of FPS, frame time, memory usage, and DSP load

#include <vivid/devtools/panels/performance_panel.h>
#include <vivid/context.h>
#include <vivid/audio_graph.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/gui.h>
#include <vivid/gui/ring_buffer.h>
#include <vivid/gui/ui_style.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

// Platform-specific memory monitoring
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace vivid {

// Get process memory usage in bytes
static size_t getProcessMemoryUsage() {
#if defined(__APPLE__)
    task_vm_info_data_t vmInfo;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmInfo, &count) == KERN_SUCCESS) {
        return vmInfo.phys_footprint;
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        size_t size, resident;
        statm >> size >> resident;
        return resident * sysconf(_SC_PAGESIZE);
    }
    return 0;
#else
    return 0;
#endif
}

struct PerformancePanel::Impl {
    Context* ctx = nullptr;

    // History buffers (~5 seconds at 60fps = 300 samples)
    RingBuffer<float, 300> fpsHistory;
    RingBuffer<float, 300> frameTimeHistory;
    RingBuffer<float, 300> memoryHistory;      // In MB
    RingBuffer<float, 300> dspLoadHistory;     // In percent

    // Smoothed current values (exponential moving average)
    float smoothedFps = 60.0f;
    float smoothedMs = 16.67f;
    float smoothedMemory = 0.0f;
    float smoothedDspLoad = 0.0f;

    // UI state
    float scrollOffset = 0.0f;
    bool showFps = true;
    bool showFrameTime = true;
    bool showMemory = true;
    bool showDsp = true;

    // Color scheme for graphs
    glm::vec4 fpsColor = {0.4f, 0.8f, 0.4f, 1.0f};       // Green
    glm::vec4 frameTimeColor = {0.4f, 0.7f, 0.9f, 1.0f}; // Blue
    glm::vec4 memoryColor = {0.9f, 0.7f, 0.3f, 1.0f};    // Orange
    glm::vec4 dspColor = {0.8f, 0.4f, 0.8f, 1.0f};       // Purple
};

PerformancePanel::PerformancePanel()
    : m_impl(std::make_unique<Impl>())
{
    m_config.id = "performance";
    m_config.title = "Performance";
    m_config.bounds = {0, 0, 280, 400};
    m_config.dockSide = DockSide::Right;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 200.0f;
    m_config.minHeight = 200.0f;
}

PerformancePanel::~PerformancePanel() = default;

bool PerformancePanel::init(Context& ctx, WGPUTextureFormat /*surfaceFormat*/) {
    m_impl->ctx = &ctx;
    return true;
}

void PerformancePanel::shutdown() {
    m_impl.reset();
}

void PerformancePanel::update() {
    if (!m_impl || !m_impl->ctx) return;

    const float smoothing = 0.05f;  // Lower = smoother, higher = more responsive

    // Sample FPS from delta time
    float dt = m_impl->ctx->dt();
    if (dt > 0.0f) {
        float instantFps = 1.0f / dt;
        m_impl->smoothedFps += smoothing * (instantFps - m_impl->smoothedFps);
        m_impl->fpsHistory.push(m_impl->smoothedFps);

        float instantMs = dt * 1000.0f;
        m_impl->smoothedMs += smoothing * (instantMs - m_impl->smoothedMs);
        m_impl->frameTimeHistory.push(m_impl->smoothedMs);
    }

    // Sample memory usage
    size_t memBytes = getProcessMemoryUsage();
    float memMB = static_cast<float>(memBytes) / (1024.0f * 1024.0f);
    m_impl->smoothedMemory += smoothing * (memMB - m_impl->smoothedMemory);
    m_impl->memoryHistory.push(m_impl->smoothedMemory);

    // Sample DSP load if audio is active
    if (m_impl->ctx->hasChain()) {
        AudioGraph* audioGraph = m_impl->ctx->chain().audioGraph();
        if (audioGraph && !audioGraph->empty()) {
            float dspLoad = audioGraph->dspLoad() * 100.0f;
            m_impl->smoothedDspLoad += smoothing * (dspLoad - m_impl->smoothedDspLoad);
            m_impl->dspLoadHistory.push(m_impl->smoothedDspLoad);
            m_impl->showDsp = true;
        } else {
            m_impl->showDsp = false;
        }
    } else {
        m_impl->showDsp = false;
    }
}

void PerformancePanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                               const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) return;

    glm::vec4 renderBounds = beginRender(input, bounds);
    float x = renderBounds.x;
    float y = renderBounds.y;
    float w = renderBounds.z;
    float h = renderBounds.w;

    // Render panel chrome (background, title bar)
    bool showTitle = m_display.showTitleBar;
    renderChrome(canvas, x, y, w, h, style, showTitle, &input);

    // Handle close button
    if (closeButtonClicked()) {
        m_config.visible = false;
        return;
    }

    // Content area (below title bar)
    float titleH = showTitle ? style.titleBarHeight() : 0.0f;
    float contentX = x + style.padding();
    float contentY = y + titleH + style.padding();
    float contentW = w - style.padding() * 2;
    float contentH = h - titleH - style.padding() * 2;

    // Clip to content area
    canvas.beginClipRect(x, y + titleH, w, h - titleH);

    // Use Gui for widgets
    Gui gui(canvas, input);

    // Configure style for compact display
    gui.style().labelPosition = LabelPosition::Above;
    gui.style().valuePosition = ValuePosition::None;
    gui.style().spacing = 8.0f;

    gui.beginArea(contentX, contentY, contentW, contentH);

    const int monoFont = 2;
    float lineH = canvas.fontLineHeight(monoFont);
    if (lineH <= 0) lineH = 16.0f;

    const float graphHeight = 50.0f;
    char labelBuf[64];

    // FPS Graph
    if (m_impl->showFps) {
        // Color based on FPS value
        glm::vec4 fpsColor = m_impl->fpsColor;
        if (m_impl->smoothedFps < 30.0f) {
            fpsColor = style.error;
        } else if (m_impl->smoothedFps < 55.0f) {
            fpsColor = style.warning;
        }

        snprintf(labelBuf, sizeof(labelBuf), "FPS: %.1f", m_impl->smoothedFps);

        Gui::GraphConfig config;
        config.yMin = 0.0f;
        config.yMax = std::max(80.0f, m_impl->fpsHistory.max() * 1.1f);
        config.autoScaleY = false;
        config.showGrid = true;
        config.showYLabels = true;
        config.yFormat = "%.0f";

        Gui::GraphSeries series;
        series.data = m_impl->fpsHistory.data();
        series.count = m_impl->fpsHistory.size();
        series.offset = m_impl->fpsHistory.offset();
        series.color = fpsColor;
        series.lineWidth = 1.5f;
        series.filled = true;

        gui.graph(labelBuf, &series, 1, config, graphHeight);
    }

    // Frame Time Graph
    if (m_impl->showFrameTime) {
        // Color based on frame time
        glm::vec4 ftColor = m_impl->frameTimeColor;
        if (m_impl->smoothedMs > 33.3f) {
            ftColor = style.error;
        } else if (m_impl->smoothedMs > 20.0f) {
            ftColor = style.warning;
        }

        snprintf(labelBuf, sizeof(labelBuf), "Frame Time: %.2f ms", m_impl->smoothedMs);

        Gui::GraphConfig config;
        config.yMin = 0.0f;
        config.yMax = std::max(50.0f, m_impl->frameTimeHistory.max() * 1.2f);
        config.autoScaleY = false;
        config.showGrid = true;
        config.showYLabels = true;
        config.yFormat = "%.0f";

        Gui::GraphSeries series;
        series.data = m_impl->frameTimeHistory.data();
        series.count = m_impl->frameTimeHistory.size();
        series.offset = m_impl->frameTimeHistory.offset();
        series.color = ftColor;
        series.lineWidth = 1.5f;
        series.filled = true;

        gui.graph(labelBuf, &series, 1, config, graphHeight);
    }

    // Memory Graph
    if (m_impl->showMemory) {
        // Color based on memory usage
        glm::vec4 memColor = m_impl->memoryColor;
        if (m_impl->smoothedMemory > 2048.0f) {  // > 2GB
            memColor = style.error;
        } else if (m_impl->smoothedMemory > 500.0f) {  // > 500MB
            memColor = style.warning;
        } else {
            memColor = style.success;
        }

        // Format memory value
        if (m_impl->smoothedMemory >= 1024.0f) {
            snprintf(labelBuf, sizeof(labelBuf), "Memory: %.2f GB", m_impl->smoothedMemory / 1024.0f);
        } else {
            snprintf(labelBuf, sizeof(labelBuf), "Memory: %.1f MB", m_impl->smoothedMemory);
        }

        Gui::GraphConfig config;
        config.autoScaleY = true;
        config.showGrid = true;
        config.showYLabels = true;
        config.yFormat = "%.0f";

        Gui::GraphSeries series;
        series.data = m_impl->memoryHistory.data();
        series.count = m_impl->memoryHistory.size();
        series.offset = m_impl->memoryHistory.offset();
        series.color = memColor;
        series.lineWidth = 1.5f;
        series.filled = true;

        gui.graph(labelBuf, &series, 1, config, graphHeight);
    }

    // DSP Load Graph (only if audio is active)
    if (m_impl->showDsp && !m_impl->dspLoadHistory.empty()) {
        // Color based on DSP load
        glm::vec4 dspColor = m_impl->dspColor;
        if (m_impl->smoothedDspLoad > 80.0f) {
            dspColor = style.error;
        } else if (m_impl->smoothedDspLoad > 50.0f) {
            dspColor = style.warning;
        }

        snprintf(labelBuf, sizeof(labelBuf), "DSP Load: %.0f%%", m_impl->smoothedDspLoad);

        Gui::GraphConfig config;
        config.yMin = 0.0f;
        config.yMax = 100.0f;
        config.autoScaleY = false;
        config.showGrid = true;
        config.showYLabels = true;
        config.yFormat = "%.0f%%";

        Gui::GraphSeries series;
        series.data = m_impl->dspLoadHistory.data();
        series.count = m_impl->dspLoadHistory.size();
        series.offset = m_impl->dspLoadHistory.offset();
        series.color = dspColor;
        series.lineWidth = 1.5f;
        series.filled = true;

        gui.graph(labelBuf, &series, 1, config, graphHeight);
    }

    gui.endArea();
    canvas.endClipRect();
}

bool PerformancePanel::handleInput(const gui::InputState& input) {
    if (!m_config.visible || !m_impl) return false;

    // Basic hover detection for the panel
    float x = m_config.bounds.x;
    float y = m_config.bounds.y;
    float w = m_config.bounds.z;
    float h = m_config.bounds.w;

    bool hovered = input.mousePos.x >= x && input.mousePos.x < x + w &&
                   input.mousePos.y >= y && input.mousePos.y < y + h;

    return hovered;
}

} // namespace vivid
