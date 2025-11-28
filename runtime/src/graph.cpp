#include "graph.h"
#include "renderer.h"
#include <iostream>
#include <algorithm>

// stb_image_write for JPEG encoding
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace vivid {

// Base64 encoding table
static const char base64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);

    size_t i = 0;
    while (i < data.size()) {
        uint32_t octet_a = i < data.size() ? data[i++] : 0;
        uint32_t octet_b = i < data.size() ? data[i++] : 0;
        uint32_t octet_c = i < data.size() ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        result += base64Chars[(triple >> 18) & 0x3F];
        result += base64Chars[(triple >> 12) & 0x3F];
        result += (i > data.size() + 1) ? '=' : base64Chars[(triple >> 6) & 0x3F];
        result += (i > data.size()) ? '=' : base64Chars[triple & 0x3F];
    }

    return result;
}

// Callback for stb_image_write to write to memory
static void jpegWriteCallback(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

Graph::Graph() = default;

Graph::~Graph() {
    clear();
}

void Graph::rebuild(const std::vector<Operator*>& operators) {
    // Note: We don't own these pointers - HotLoader manages their lifetime
    operators_ = operators;
    std::cout << "[Graph] Rebuilt with " << operators_.size() << " operator(s)\n";
}

void Graph::clear() {
    operators_.clear();
}

void Graph::initAll(Context& ctx) {
    for (auto* op : operators_) {
        if (op) {
            op->init(ctx);
        }
    }
}

void Graph::execute(Context& ctx) {
    for (auto* op : operators_) {
        if (op) {
            op->process(ctx);
        }
    }
}

void Graph::cleanupAll() {
    for (auto* op : operators_) {
        if (op) {
            op->cleanup();
        }
    }
}

std::map<std::string, std::unique_ptr<OperatorState>> Graph::saveAllStates() {
    std::map<std::string, std::unique_ptr<OperatorState>> states;

    for (auto* op : operators_) {
        if (op) {
            auto state = op->saveState();
            if (state) {
                states[op->id()] = std::move(state);
                std::cout << "[Graph] Saved state for: " << op->id() << "\n";
            }
        }
    }

    return states;
}

void Graph::restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states) {
    for (auto* op : operators_) {
        if (op) {
            auto it = states.find(op->id());
            if (it != states.end()) {
                op->loadState(std::move(it->second));
                std::cout << "[Graph] Restored state for: " << op->id() << "\n";
            }
        }
    }
}

Texture* Graph::finalOutput(Context& ctx) {
    // Get the "out" texture from the last operator
    // This is a simple approach - in future we'd track explicit output connections
    if (operators_.empty()) {
        return nullptr;
    }

    // First try to get "out" from the last operator's id
    auto* lastOp = operators_.back();
    if (lastOp && !lastOp->id().empty()) {
        Texture* tex = ctx.getInputTexture(lastOp->id(), "out");
        if (tex && tex->valid()) {
            return tex;
        }
    }

    // Fallback: just look for "out" directly
    return ctx.getInputTexture("out");
}

std::vector<Graph::Preview> Graph::capturePreviews(Context& ctx, Renderer& renderer, int thumbSize) {
    std::vector<Preview> previews;

    for (auto* op : operators_) {
        if (!op) continue;

        Preview preview;
        preview.operatorId = op->id();
        preview.sourceLine = op->sourceLine();
        preview.outputKind = op->outputKind();

        if (op->outputKind() == OutputKind::Texture) {
            // Get the operator's output texture
            Texture* tex = ctx.getInputTexture(op->id(), "out");
            if (!tex) {
                // Try just "out" for backwards compatibility
                tex = ctx.getInputTexture("out");
            }

            if (tex && tex->valid()) {
                preview.width = tex->width;
                preview.height = tex->height;

                // Read pixels from GPU
                std::vector<uint8_t> pixels = renderer.readTexturePixels(*tex);

                if (!pixels.empty()) {
                    // Calculate downscale factor to fit within thumbSize
                    int srcWidth = tex->width;
                    int srcHeight = tex->height;
                    int dstWidth = srcWidth;
                    int dstHeight = srcHeight;

                    if (srcWidth > thumbSize || srcHeight > thumbSize) {
                        float scale = std::min(
                            static_cast<float>(thumbSize) / srcWidth,
                            static_cast<float>(thumbSize) / srcHeight
                        );
                        dstWidth = std::max(1, static_cast<int>(srcWidth * scale));
                        dstHeight = std::max(1, static_cast<int>(srcHeight * scale));
                    }

                    // Downsample RGBA to RGB in one pass using nearest-neighbor
                    std::vector<uint8_t> rgbPixels;
                    rgbPixels.reserve(dstWidth * dstHeight * 3);

                    for (int y = 0; y < dstHeight; y++) {
                        int srcY = y * srcHeight / dstHeight;
                        for (int x = 0; x < dstWidth; x++) {
                            int srcX = x * srcWidth / dstWidth;
                            size_t srcIdx = (srcY * srcWidth + srcX) * 4;
                            rgbPixels.push_back(pixels[srcIdx]);     // R
                            rgbPixels.push_back(pixels[srcIdx + 1]); // G
                            rgbPixels.push_back(pixels[srcIdx + 2]); // B
                        }
                    }

                    // Encode as JPEG at reduced quality for smaller payloads
                    std::vector<uint8_t> jpegData;
                    int quality = 60;

                    stbi_write_jpg_to_func(
                        jpegWriteCallback,
                        &jpegData,
                        dstWidth,
                        dstHeight,
                        3,  // RGB channels
                        rgbPixels.data(),
                        quality
                    );

                    // Base64 encode
                    preview.base64Jpeg = base64Encode(jpegData);
                }
            }
        } else if (op->outputKind() == OutputKind::Value) {
            // Get scalar value
            preview.value = ctx.getInputValue(op->id(), "out", 0.0f);
        }

        previews.push_back(std::move(preview));
    }

    return previews;
}

} // namespace vivid
