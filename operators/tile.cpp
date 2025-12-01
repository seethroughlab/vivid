// Tile Operator
// Repeats a texture in a grid with per-tile transforms

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

/**
 * @brief Texture tiling/repeating operator.
 *
 * Repeats an input texture in a grid pattern with optional per-tile
 * transformations (scale, rotation, offset).
 *
 * Usage:
 * @code
 * // Simple 4x4 tile grid
 * chain.add<Tile>("tiled")
 *     .input("source")
 *     .cols(4).rows(4);
 *
 * // Animated tile rotation
 * chain.add<Tile>("spinning")
 *     .input("source")
 *     .cols(3).rows(3)
 *     .rotatePerTile(true)
 *     .animateRotation(true);
 *
 * // Offset alternating rows (brick pattern)
 * chain.add<Tile>("bricks")
 *     .input("source")
 *     .cols(4).rows(8)
 *     .offsetOddRows(0.5f);
 * @endcode
 */
class Tile : public Operator {
public:
    Tile() = default;

    // Fluent API - Input
    Tile& input(const std::string& node) { inputNode_ = node; return *this; }

    // Fluent API - Grid size
    Tile& cols(int c) { cols_ = std::max(1, c); return *this; }
    Tile& rows(int r) { rows_ = std::max(1, r); return *this; }
    Tile& repeat(int n) { cols_ = rows_ = std::max(1, n); return *this; }

    // Fluent API - Tile spacing
    Tile& gapX(float g) { gapX_ = g; return *this; }
    Tile& gapY(float g) { gapY_ = g; return *this; }
    Tile& gap(float g) { gapX_ = gapY_ = g; return *this; }

    // Fluent API - Per-tile transforms
    Tile& scalePerTile(float s) { tileScale_ = s; return *this; }
    Tile& rotatePerTile(bool enable) { rotatePerTile_ = enable; return *this; }
    Tile& rotationAmount(float r) { rotationAmount_ = r; return *this; }
    Tile& randomRotation(bool enable) { randomRotation_ = enable; return *this; }

    // Fluent API - Row/column offsets
    Tile& offsetOddRows(float o) { oddRowOffset_ = o; return *this; }
    Tile& offsetOddCols(float o) { oddColOffset_ = o; return *this; }

    // Fluent API - Animation
    Tile& animateRotation(bool enable) { animateRotation_ = enable; return *this; }
    Tile& animateScale(bool enable) { animateScale_ = enable; return *this; }
    Tile& animateSpeed(float s) { animateSpeed_ = s; return *this; }

    // Fluent API - Mirroring
    Tile& mirrorX(bool m) { mirrorX_ = m; return *this; }
    Tile& mirrorY(bool m) { mirrorY_ = m; return *this; }
    Tile& mirrorAlternate(bool m) { mirrorAlternate_ = m; return *this; }

    // Fluent API - Background
    Tile& clearColor(float r, float g, float b, float a = 1.0f) {
        clearColor_ = glm::vec4(r, g, b, a);
        return *this;
    }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* inputTex = ctx.getInputTexture(inputNode_, "out");

        // Resize output to match input if needed
        if (inputTex && inputTex->valid() &&
            (output_.width != ctx.width() || output_.height != ctx.height())) {
            output_ = ctx.createTexture(ctx.width(), ctx.height());
        }

        // Build shader parameters
        Context::ShaderParams params;
        params.param0 = static_cast<float>(cols_);
        params.param1 = static_cast<float>(rows_);
        params.param2 = gapX_;
        params.param3 = gapY_;
        params.param4 = tileScale_;

        // Calculate rotation based on settings
        float rotation = 0.0f;
        if (animateRotation_) {
            rotation = ctx.time() * animateSpeed_;
        } else if (rotatePerTile_) {
            rotation = rotationAmount_;
        }
        params.param5 = rotation;

        // Pack flags into param6
        // bit 0: mirrorX, bit 1: mirrorY, bit 2: mirrorAlternate, bit 3: randomRotation
        int flags = 0;
        if (mirrorX_) flags |= 1;
        if (mirrorY_) flags |= 2;
        if (mirrorAlternate_) flags |= 4;
        if (randomRotation_) flags |= 8;
        params.param6 = static_cast<float>(flags);

        // Scale animation
        float scaleAnim = 1.0f;
        if (animateScale_) {
            scaleAnim = 0.8f + 0.2f * std::sin(ctx.time() * animateSpeed_);
        }
        params.param7 = scaleAnim;

        // Row/col offsets
        params.vec0X = oddRowOffset_;
        params.vec0Y = oddColOffset_;

        // Mode encodes additional settings
        params.mode = 0;

        ctx.runShader("shaders/tile.wgsl", inputTex, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("cols", cols_, 1, 20),
            intParam("rows", rows_, 1, 20),
            floatParam("gap", gapX_, 0.0f, 0.5f),
            floatParam("tileScale", tileScale_, 0.1f, 2.0f),
            floatParam("rotation", rotationAmount_, -3.14159f, 3.14159f),
            floatParam("oddRowOffset", oddRowOffset_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;

    // Grid settings
    int cols_ = 3;
    int rows_ = 3;
    float gapX_ = 0.0f;
    float gapY_ = 0.0f;

    // Per-tile transforms
    float tileScale_ = 1.0f;
    bool rotatePerTile_ = false;
    float rotationAmount_ = 0.0f;
    bool randomRotation_ = false;

    // Row/column offsets
    float oddRowOffset_ = 0.0f;
    float oddColOffset_ = 0.0f;

    // Animation
    bool animateRotation_ = false;
    bool animateScale_ = false;
    float animateSpeed_ = 1.0f;

    // Mirroring
    bool mirrorX_ = false;
    bool mirrorY_ = false;
    bool mirrorAlternate_ = false;

    // Background
    glm::vec4 clearColor_{0.0f, 0.0f, 0.0f, 1.0f};

    Texture output_;
};

VIVID_OPERATOR(Tile)
