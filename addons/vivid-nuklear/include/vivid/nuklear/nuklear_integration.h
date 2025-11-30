#pragma once

/**
 * @file nuklear_integration.h
 * @brief Nuklear immediate-mode GUI integration for Vivid operators
 *
 * Provides a simple way to add debug UI controls to your Vivid chains.
 * Unlike ImGUI, Nuklear is a single-header ANSI C library with no WebGPU
 * API compatibility issues - it uses software rendering.
 *
 * Usage:
 *
 *   #include <vivid/nuklear/nuklear_integration.h>
 *
 *   static vivid::nuklear::NuklearUI ui;
 *   static Texture uiTexture;
 *   static float myValue = 0.5f;
 *
 *   void setup(Chain& chain) {
 *       // UI will be composited over your main output
 *       chain.add<Noise>("noise").scale(4.0f);
 *       chain.add<Composite>("final")
 *           .a("noise")
 *           .b("ui")  // ui texture from update()
 *           .mode(0); // alpha over
 *       chain.setOutput("final");
 *   }
 *
 *   void update(Chain& chain, Context& ctx) {
 *       if (!ui.isInitialized()) {
 *           ui.init(ctx.width(), ctx.height());
 *           uiTexture = ctx.createTexture();
 *       }
 *
 *       // Handle input
 *       ui.inputBegin();
 *       ui.inputMouse(ctx.mouseX(), ctx.mouseY(),
 *                     ctx.isMouseDown(0), ctx.isMouseDown(1));
 *       ui.inputEnd();
 *
 *       // Draw UI
 *       if (ui.begin("Controls", 10, 10, 200, 150)) {
 *           ui.layoutRow(30, 1);
 *           ui.label("My Value:");
 *           ui.slider(&myValue, 0.0f, 1.0f, 0.01f);
 *           if (ui.button("Reset")) {
 *               myValue = 0.5f;
 *           }
 *       }
 *       ui.end();
 *
 *       // Render UI to texture
 *       ui.render(ctx, uiTexture);
 *       ctx.setTextureForNode("ui", uiTexture);
 *   }
 */

#include <vivid/vivid.h>
#include <string>
#include <vector>
#include <memory>

// Forward declare Nuklear context
struct nk_context;
struct nk_font_atlas;
struct nk_font;

namespace vivid {
namespace nuklear {

/**
 * @brief Nuklear UI wrapper for Vivid
 *
 * Manages a Nuklear context with software rendering.
 * The rendered UI is uploaded to a Vivid texture.
 */
class NuklearUI {
public:
    NuklearUI();
    ~NuklearUI();

    // Non-copyable
    NuklearUI(const NuklearUI&) = delete;
    NuklearUI& operator=(const NuklearUI&) = delete;

    /**
     * @brief Initialize the UI context
     * @param width Framebuffer width
     * @param height Framebuffer height
     * @param fontSize Font size in pixels (default 14)
     */
    void init(int width, int height, float fontSize = 14.0f);

    /**
     * @brief Check if UI is initialized
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Resize the framebuffer
     */
    void resize(int width, int height);

    // Input handling
    void inputBegin();
    void inputMouse(int x, int y, bool leftDown, bool rightDown);
    void inputKey(int key, bool down);
    void inputChar(char c);
    void inputScroll(float x, float y);
    void inputEnd();

    // Window creation
    /**
     * @brief Begin a UI window
     * @param title Window title
     * @param x X position
     * @param y Y position
     * @param w Width
     * @param h Height
     * @param flags Window flags (NK_WINDOW_* flags)
     * @return true if window is visible and accepting input
     */
    bool begin(const std::string& title, float x, float y, float w, float h, unsigned int flags = 0);
    void end();

    // Layout
    void layoutRow(float height, int columns);
    void layoutRowDynamic(float height, int columns);
    void layoutRowStatic(float height, int itemWidth, int columns);

    // Widgets
    void label(const std::string& text);
    void labelColored(const std::string& text, unsigned char r, unsigned char g, unsigned char b);
    bool button(const std::string& text);
    bool checkbox(const std::string& text, bool* active);
    bool slider(float* value, float min, float max, float step);
    bool sliderInt(int* value, int min, int max, int step);
    bool property(const std::string& name, float* value, float min, float max, float step, float incPerPixel);
    bool propertyInt(const std::string& name, int* value, int min, int max, int step, int incPerPixel);
    void progress(float current, float max, bool modifiable = false);
    bool colorPicker(float* r, float* g, float* b, float* a = nullptr);
    bool combo(const std::string& selected, const std::vector<std::string>& items, int* selectedIndex, int itemHeight = 25);
    void text(const std::string& text);
    bool editString(std::string& buffer, int maxLength = 256);

    // Spacing and separators
    void spacing(int columns);
    void separator();

    // Groups (scrollable regions)
    bool groupBegin(const std::string& title, unsigned int flags = 0);
    void groupEnd();

    // Trees
    bool treePush(const std::string& title, bool* state);
    void treePop();

    // Render to texture
    /**
     * @brief Render the UI to a Vivid texture
     * @param ctx Vivid context
     * @param output Target texture (must be created with ctx.createTexture())
     */
    void render(Context& ctx, Texture& output);

    /**
     * @brief Get raw pixel data (RGBA, 4 bytes per pixel)
     */
    const unsigned char* getPixels() const { return pixels_.data(); }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    /**
     * @brief Access the raw Nuklear context for advanced usage
     */
    nk_context* getNkContext() { return ctx_; }

private:
    void clear();
    void renderToBuffer();

    nk_context* ctx_ = nullptr;
    nk_font_atlas* atlas_ = nullptr;
    nk_font* font_ = nullptr;

    std::vector<unsigned char> pixels_;
    std::vector<unsigned char> fontAtlasImage_;
    int width_ = 0;
    int height_ = 0;
    int fontAtlasW_ = 0;
    int fontAtlasH_ = 0;
    bool initialized_ = false;
    bool needsRender_ = true;
};

} // namespace nuklear
} // namespace vivid
