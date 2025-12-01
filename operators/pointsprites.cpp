// PointSprites Operator
// Renders many points/circles using GPU instancing

#include <vivid/vivid.h>
#include <cmath>
#include <random>

using namespace vivid;

/**
 * @brief 2D instanced point/sprite rendering operator.
 *
 * Efficiently renders thousands of circles/points using GPU instancing.
 * Positions can be generated from patterns or come from value arrays.
 *
 * Usage:
 * @code
 * // Generate a grid of points
 * chain.add<PointSprites>("grid")
 *     .pattern(PointSprites::Grid)
 *     .count(100)
 *     .size(0.01f)
 *     .color(1.0f, 0.5f, 0.2f);
 *
 * // Random scattered points
 * chain.add<PointSprites>("scatter")
 *     .pattern(PointSprites::Random)
 *     .count(500)
 *     .size(0.005f)
 *     .animate(true);
 *
 * // Points from position arrays (x,y pairs)
 * chain.add<PointSprites>("custom")
 *     .positionsFrom("positionGenerator")  // Node outputting value array
 *     .size(0.02f);
 * @endcode
 */
class PointSprites : public Operator {
public:
    // Position generation patterns
    enum Pattern {
        Grid = 0,       // Regular grid
        Random = 1,     // Random positions
        Circle = 2,     // Points arranged in a circle
        Spiral = 3,     // Spiral pattern
        Custom = 4      // Positions from value array input
    };

    // Color modes
    enum ColorMode {
        Solid = 0,      // Single color for all
        Rainbow = 1,    // HSV rainbow based on index
        Gradient = 2,   // Gradient from color1 to color2
        Random_ = 3     // Random colors
    };

    PointSprites() = default;

    // Fluent API - Pattern
    PointSprites& pattern(Pattern p) { pattern_ = p; needsRebuild_ = true; return *this; }
    PointSprites& pattern(int p) { pattern_ = static_cast<Pattern>(p); needsRebuild_ = true; return *this; }

    // Fluent API - Count
    PointSprites& count(int c) { count_ = c; needsRebuild_ = true; return *this; }

    // Fluent API - Size
    PointSprites& size(float s) { size_ = s; return *this; }
    PointSprites& sizeVariation(float v) { sizeVariation_ = v; needsRebuild_ = true; return *this; }

    // Fluent API - Color
    PointSprites& color(float r, float g, float b, float a = 1.0f) {
        color1_ = {r, g, b, a};
        return *this;
    }
    PointSprites& color(const glm::vec4& c) { color1_ = c; return *this; }
    PointSprites& color2(float r, float g, float b, float a = 1.0f) {
        color2_ = {r, g, b, a};
        return *this;
    }
    PointSprites& colorMode(ColorMode m) { colorMode_ = m; needsRebuild_ = true; return *this; }
    PointSprites& colorMode(int m) { colorMode_ = static_cast<ColorMode>(m); needsRebuild_ = true; return *this; }

    // Fluent API - Animation
    PointSprites& animate(bool a) { animate_ = a; return *this; }
    PointSprites& animateSpeed(float s) { animateSpeed_ = s; return *this; }
    PointSprites& pulseSize(bool p) { pulseSize_ = p; return *this; }
    PointSprites& pulseSpeed(float s) { pulseSpeed_ = s; return *this; }

    // Fluent API - Pattern-specific
    PointSprites& gridCols(int c) { gridCols_ = c; needsRebuild_ = true; return *this; }
    PointSprites& circleRadius(float r) { circleRadius_ = r; needsRebuild_ = true; return *this; }
    PointSprites& spiralTurns(float t) { spiralTurns_ = t; needsRebuild_ = true; return *this; }
    PointSprites& margin(float m) { margin_ = m; needsRebuild_ = true; return *this; }

    // Fluent API - Custom positions
    PointSprites& positionsFrom(const std::string& node) { positionsNode_ = node; pattern_ = Custom; return *this; }

    // Fluent API - Background
    PointSprites& clearColor(float r, float g, float b, float a = 1.0f) { clearColor_ = {r, g, b, a}; return *this; }

    // Fluent API - Random seed
    PointSprites& seed(int s) { seed_ = s; needsRebuild_ = true; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
        generatePattern();
    }

    void process(Context& ctx) override {
        if (needsRebuild_) {
            generatePattern();
            needsRebuild_ = false;
        }

        // Handle custom positions from another node
        if (pattern_ == Custom && !positionsNode_.empty()) {
            auto positions = ctx.getInputValues(positionsNode_);
            updateFromPositionArray(positions);
        }

        // Update animation
        if (animate_) {
            phase_ += ctx.dt() * animateSpeed_;
            updateAnimation();
        }

        // Update size pulse
        float sizeMultiplier = 1.0f;
        if (pulseSize_) {
            sizeMultiplier = 0.5f + 0.5f * std::sin(ctx.time() * pulseSpeed_);
        }

        // Apply size multiplier
        std::vector<Circle2D> renderCircles = circles_;
        if (sizeMultiplier != 1.0f) {
            for (auto& c : renderCircles) {
                c.radius *= sizeMultiplier;
            }
        }

        // Render
        ctx.drawCircles(renderCircles, output_, clearColor_);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("pattern", static_cast<int>(pattern_), 0, 4),
            intParam("count", count_, 1, 10000),
            floatParam("size", size_, 0.001f, 0.2f),
            intParam("colorMode", static_cast<int>(colorMode_), 0, 3),
            floatParam("animSpeed", animateSpeed_, 0.0f, 5.0f),
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    void generatePattern() {
        circles_.clear();
        circles_.reserve(count_);

        std::mt19937 rng(seed_);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        switch (pattern_) {
            case Grid:
                generateGrid();
                break;

            case Random:
                for (int i = 0; i < count_; i++) {
                    float x = margin_ + dist(rng) * (1.0f - 2.0f * margin_);
                    float y = margin_ + dist(rng) * (1.0f - 2.0f * margin_);
                    float s = size_ * (1.0f - sizeVariation_ + dist(rng) * 2.0f * sizeVariation_);
                    glm::vec4 c = getColor(i, count_, dist(rng), rng);
                    circles_.emplace_back(glm::vec2(x, y), s, c);
                }
                break;

            case Circle: {
                for (int i = 0; i < count_; i++) {
                    float angle = (float)i / count_ * 2.0f * 3.14159f;
                    float x = 0.5f + circleRadius_ * std::cos(angle);
                    float y = 0.5f + circleRadius_ * std::sin(angle);
                    float s = size_ * (1.0f - sizeVariation_ + dist(rng) * 2.0f * sizeVariation_);
                    glm::vec4 c = getColor(i, count_, dist(rng), rng);
                    circles_.emplace_back(glm::vec2(x, y), s, c);
                }
                break;
            }

            case Spiral: {
                for (int i = 0; i < count_; i++) {
                    float t = (float)i / count_;
                    float angle = t * spiralTurns_ * 2.0f * 3.14159f;
                    float radius = circleRadius_ * t;
                    float x = 0.5f + radius * std::cos(angle);
                    float y = 0.5f + radius * std::sin(angle);
                    float s = size_ * (1.0f - sizeVariation_ + dist(rng) * 2.0f * sizeVariation_);
                    glm::vec4 c = getColor(i, count_, dist(rng), rng);
                    circles_.emplace_back(glm::vec2(x, y), s, c);
                }
                break;
            }

            case Custom:
                // Will be populated in process() from input array
                break;
        }

        // Store base positions for animation
        basePositions_.resize(circles_.size());
        for (size_t i = 0; i < circles_.size(); i++) {
            basePositions_[i] = circles_[i].position;
        }
    }

    void generateGrid() {
        int cols = gridCols_;
        if (cols <= 0) {
            cols = (int)std::sqrt((float)count_);
        }
        int rows = (count_ + cols - 1) / cols;

        float cellW = (1.0f - 2.0f * margin_) / cols;
        float cellH = (1.0f - 2.0f * margin_) / rows;

        std::mt19937 rng(seed_);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        int idx = 0;
        for (int row = 0; row < rows && idx < count_; row++) {
            for (int col = 0; col < cols && idx < count_; col++) {
                float x = margin_ + (col + 0.5f) * cellW;
                float y = margin_ + (row + 0.5f) * cellH;
                float s = size_ * (1.0f - sizeVariation_ + dist(rng) * 2.0f * sizeVariation_);
                glm::vec4 c = getColor(idx, count_, dist(rng), rng);
                circles_.emplace_back(glm::vec2(x, y), s, c);
                idx++;
            }
        }
    }

    void updateFromPositionArray(const std::vector<float>& positions) {
        // Expect interleaved x,y pairs
        int numPoints = positions.size() / 2;
        if (numPoints == 0) return;

        circles_.resize(numPoints);
        basePositions_.resize(numPoints);

        std::mt19937 rng(seed_);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (int i = 0; i < numPoints; i++) {
            float x = positions[i * 2];
            float y = positions[i * 2 + 1];
            circles_[i].position = glm::vec2(x, y);
            circles_[i].radius = size_;
            circles_[i].color = getColor(i, numPoints, dist(rng), rng);
            basePositions_[i] = circles_[i].position;
        }
    }

    void updateAnimation() {
        for (size_t i = 0; i < circles_.size(); i++) {
            float offset = (float)i / circles_.size() * 2.0f * 3.14159f;
            float dx = 0.01f * std::sin(phase_ + offset);
            float dy = 0.01f * std::cos(phase_ * 0.7f + offset);
            circles_[i].position = basePositions_[i] + glm::vec2(dx, dy);
        }
    }

    glm::vec4 getColor(int index, int total, float randomVal, std::mt19937& rng) {
        switch (colorMode_) {
            case Solid:
                return color1_;

            case Rainbow: {
                float hue = (float)index / total;
                return hsvToRgb(hue, 0.8f, 1.0f);
            }

            case Gradient: {
                float t = (float)index / std::max(1, total - 1);
                return glm::mix(color1_, color2_, t);
            }

            case Random_: {
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                return glm::vec4(dist(rng), dist(rng), dist(rng), 1.0f);
            }

            default:
                return color1_;
        }
    }

    glm::vec4 hsvToRgb(float h, float s, float v) {
        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
        float m = v - c;

        glm::vec3 rgb;
        if (h < 1.0f/6.0f)      rgb = {c, x, 0};
        else if (h < 2.0f/6.0f) rgb = {x, c, 0};
        else if (h < 3.0f/6.0f) rgb = {0, c, x};
        else if (h < 4.0f/6.0f) rgb = {0, x, c};
        else if (h < 5.0f/6.0f) rgb = {x, 0, c};
        else                    rgb = {c, 0, x};

        return glm::vec4(rgb + glm::vec3(m), 1.0f);
    }

    // Pattern settings
    Pattern pattern_ = Grid;
    int count_ = 100;
    int seed_ = 42;

    // Size settings
    float size_ = 0.02f;
    float sizeVariation_ = 0.0f;

    // Color settings
    ColorMode colorMode_ = Solid;
    glm::vec4 color1_{1.0f, 0.5f, 0.2f, 1.0f};
    glm::vec4 color2_{0.2f, 0.5f, 1.0f, 1.0f};

    // Animation
    bool animate_ = false;
    float animateSpeed_ = 1.0f;
    float phase_ = 0.0f;
    bool pulseSize_ = false;
    float pulseSpeed_ = 2.0f;

    // Pattern-specific
    int gridCols_ = 0;  // 0 = auto-calculate
    float circleRadius_ = 0.3f;
    float spiralTurns_ = 3.0f;
    float margin_ = 0.05f;

    // Custom positions
    std::string positionsNode_;

    // Output
    glm::vec4 clearColor_{0.0f, 0.0f, 0.0f, 1.0f};
    Texture output_;

    // Internal state
    bool needsRebuild_ = true;
    std::vector<Circle2D> circles_;
    std::vector<glm::vec2> basePositions_;
};

VIVID_OPERATOR(PointSprites)
