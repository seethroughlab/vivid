// WebView - Interactive Shape Example
//
// This example demonstrates HTML UI controlling Vivid shapes in real-time.
// The HTML sliders directly modify operator parameters.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/webview/webview_all.h>
#include <nlohmann/json.hpp>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::webview;
using json = nlohmann::json;

// Shape parameters controlled by HTML
static float g_shapeSize = 0.3f;
static float g_shapeRotation = 0.0f;
static float g_shapePosX = 0.0f;
static float g_shapePosY = 0.0f;
static float g_colorR = 1.0f;
static float g_colorG = 0.5f;
static float g_colorB = 0.2f;
static bool g_animating = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Background gradient
    auto& grad = chain.add<Gradient>("gradient");
    grad.colorA.set(0.1f, 0.1f, 0.15f, 1.0f);
    grad.colorB.set(0.2f, 0.15f, 0.25f, 1.0f);
    grad.angle = 45.0f;

    // Shape - a circle that we'll control
    auto& shape = chain.add<Shape>("shape");
    shape.type = ShapeType::Ellipse;
    shape.size.set(g_shapeSize, g_shapeSize);
    shape.position.set(0.5f + g_shapePosX, 0.5f + g_shapePosY);
    shape.color.set(g_colorR, g_colorG, g_colorB, 1.0f);
    shape.softness = 0.01f;
    shape.thickness = 0.0f;  // Filled shape, not ring

    // Composite shape over gradient
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("gradient");
    comp1.inputB("shape");
    comp1.mode = BlendMode::Over;

    // WebView UI overlay
    auto& ui = chain.add<WebView>("ui");
    ui.setUrl("file://assets/controls.html");
    ui.setSize(ctx.width(), ctx.height());
    ui.setTransparent(true);
    ui.setInputEnabled(true);

    // Handle parameter updates from JavaScript
    ui.registerCallback("updateParam", [](const std::string& args) {
        try {
            auto data = json::parse(args);
            std::string param = data["param"];

            if (param == "size") {
                g_shapeSize = data["value"].get<float>();
            } else if (param == "rotation") {
                g_shapeRotation = data["value"].get<float>();
            } else if (param == "posX") {
                g_shapePosX = data["value"].get<float>();
            } else if (param == "posY") {
                g_shapePosY = data["value"].get<float>();
            } else if (param == "colorR") {
                g_colorR = data["value"].get<float>();
            } else if (param == "colorG") {
                g_colorG = data["value"].get<float>();
            } else if (param == "colorB") {
                g_colorB = data["value"].get<float>();
            } else if (param == "animate") {
                g_animating = data["value"].get<bool>();
            }
        } catch (const std::exception& e) {
            std::cerr << "[Chain] JSON parse error: " << e.what() << std::endl;
        }
    });

    // Composite UI over everything
    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("ui");
    comp2.mode = BlendMode::Over;

    chain.output("comp2");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Interactive Shape Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Use the HTML controls to modify the shape!" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& shape = chain.get<Shape>("shape");
    auto& ui = chain.get<WebView>("ui");

    // Apply animation if enabled
    float rotation = g_shapeRotation;
    float posX = g_shapePosX;
    float posY = g_shapePosY;

    if (g_animating) {
        float t = ctx.time();
        rotation += t * 1.5f;  // Spin (radians)
        posX += 0.1f * std::sin(t * 2.0f);  // Wobble
        posY += 0.1f * std::cos(t * 2.0f);
    }

    // Update shape parameters
    shape.size.set(g_shapeSize, g_shapeSize);
    shape.position.set(0.5f + posX, 0.5f + posY);
    shape.rotation = rotation;
    shape.color.set(g_colorR, g_colorG, g_colorB, 1.0f);

    // Update UI size if window resized
    if (ui.webviewWidth() != ctx.width() || ui.webviewHeight() != ctx.height()) {
        ui.setSize(ctx.width(), ctx.height());
    }
}

VIVID_CHAIN(setup, update)
