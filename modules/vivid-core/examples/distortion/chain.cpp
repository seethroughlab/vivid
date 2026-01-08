// Test video + grid + distortion effects
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    
    auto& video = chain.add<VideoPlayer>("video");
    video.setFile("assets/kitchen.mp4");
    video.setLoop(true);
    video.play();
    
    auto& gridCanvas = chain.add<Canvas>("gridCanvas");
    gridCanvas.size(ctx.width(), ctx.height());
    
    auto& base = chain.add<Composite>("base");
    base.inputA("video");
    base.inputB("gridCanvas");
    base.mode = BlendMode::Add;
    
    // Add ChromaticAberration
    auto& chroma = chain.add<ChromaticAberration>("chroma");
    chroma.input("base");
    chroma.amount = 0.02f;
    
    chain.output("chroma");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    
    auto& gridCanvas = chain.get<Canvas>("gridCanvas");
    gridCanvas.clear(0, 0, 0, 0);
    
    int w = ctx.width();
    int h = ctx.height();
    
    gridCanvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.25f);
    gridCanvas.lineWidth(1.0f);
    
    for (int x = 40; x < w; x += 40) {
        gridCanvas.beginPath();
        gridCanvas.moveTo(x, 0);
        gridCanvas.lineTo(x, h);
        gridCanvas.stroke();
    }
    for (int y = 40; y < h; y += 40) {
        gridCanvas.beginPath();
        gridCanvas.moveTo(0, y);
        gridCanvas.lineTo(w, y);
        gridCanvas.stroke();
    }
}

VIVID_CHAIN(setup, update)
