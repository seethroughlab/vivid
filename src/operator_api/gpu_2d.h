#pragma once

// 2D drawable pipeline — a split pipeline mirroring gpu_3d's VividSceneFragment model.
// Emitters produce VividDrawable2D records; modifiers transform or compose them;
// the terminal Render2D operator rasterises them to a texture that downstream
// texture-chain operators (Bloom, Feedback, etc.) can consume as usual.
//
// Phase E foundation — header-only, follows conventions of gpu_common.h and gpu_3d.h.
// See docs/plans/archive/2d-pipeline/ for the broader design history.

#include "operator_api/gpu_common.h"
#include "operator_api/type_id.h"
#include "operator_api/port_type_registry.h"
#include <cstdint>
#include <cmath>

namespace vivid::gpu {

// ---------------------------------------------------------------------------
// Per-instance data
// ---------------------------------------------------------------------------

// 48-byte per-instance record carried by the instance storage buffer that
// Render2D binds when rasterising an instanced drawable.
//
// `transform` stores a 2D affine matrix in column-major layout, packed to
// match WGSL's `mat3x2<f32>` memory layout for direct binding:
//
//     transform[0] = m00 (col 0, x component) = a
//     transform[1] = m01 (col 0, y component) = c
//     transform[2] = m10 (col 1, x component) = b
//     transform[3] = m11 (col 1, y component) = d
//     transform[4] = m20 (col 2, x component) = tx
//     transform[5] = m21 (col 2, y component) = ty
//
// For a point (x, y) the transformed position is:
//     x' = a*x + b*y + tx
//     y' = c*x + d*y + ty
//
// `_pad_xform` reaches 32-byte alignment before the color block so that WGSL
// can bind InstanceData2D as a struct with predictable field offsets.
struct InstanceData2D {
    float transform[6];   // mat3x2 column-major affine
    float _pad_xform[2];  // 8 bytes padding → 32
    float color[4];       // RGBA multiplier
};
static_assert(sizeof(InstanceData2D) == 48, "InstanceData2D must be 48 bytes");

// ---------------------------------------------------------------------------
// Per-glyph instance record for TEXT drawables
// ---------------------------------------------------------------------------
//
// One entry per glyph in the text run. The transform places the unit quad
// over the atlas cell; the uv_rect carries the glyph's sub-region within the
// atlas. Color is duplicated from the emitter for future per-glyph tinting.
struct GlyphInstance2D {
    float transform[6];   // mat3x2 column-major affine — glyph-quad local transform
    float _pad_xform[2];  // align to 32
    float uv_rect[4];     // u0, v0, u1, v1 in atlas space
    float color[4];       // RGBA multiplier (per-glyph; same for all in MVP)
};
static_assert(sizeof(GlyphInstance2D) == 64, "GlyphInstance2D must be 64 bytes");

// ---------------------------------------------------------------------------
// Instance array bundle (custom-ref port payload)
// ---------------------------------------------------------------------------

// Thin pointer+count view over per-instance data. Transported via
// VIVID_PORT_TRANSPORT_CUSTOM_REF (shared handle — the producer owns the
// storage; consumers treat `data` as read-only for the duration of a frame).
struct InstanceArray2D {
    const InstanceData2D* data;
    uint32_t              count;
    uint32_t              _pad0;
};

// ---------------------------------------------------------------------------
// Drawable enums
// ---------------------------------------------------------------------------

// Dispatch hint for Render2D's pipeline-variant cache. Not a strict type
// discriminator — Render2D infers additional pipeline flags (instanced,
// textured, blend mode, ...) from other fields on the drawable.
enum VividDrawable2DType : uint32_t {
    VIVID_DRAWABLE2D_SPRITE = 0,  // textured quad (uses texture_view, uv_rect)
    VIVID_DRAWABLE2D_SHAPE  = 1,  // SDF primitive (uses shape_sides, shape_star_factor, ...)
    VIVID_DRAWABLE2D_TEXT   = 2,  // glyph run (uses text_atlas_view, text_glyph_buffer)
    VIVID_DRAWABLE2D_MESH   = 3,  // arbitrary 2D geometry (future)
    VIVID_DRAWABLE2D_CUSTOM = 4,  // opaque custom shader path (uses custom_pipeline)
};

enum VividBlendMode : uint32_t {
    VIVID_BLEND_ALPHA    = 0,  // src-over (default)
    VIVID_BLEND_ADDITIVE = 1,
    VIVID_BLEND_MULTIPLY = 2,
    VIVID_BLEND_SCREEN   = 3,
    VIVID_BLEND_OVERLAY  = 4,
};

// ---------------------------------------------------------------------------
// VividDrawable2D — the core 2D pipeline primitive
// ---------------------------------------------------------------------------

// Flat tagged-union record, 312 bytes. Passed across the plugin boundary by
// pointer (CUSTOM_REF transport). Sub-blocks each carry a _reserved_* padding
// array so future phases can add fields without ABI breaks.
//
// Semantics:
//   - When `instance_buffer` is null, Render2D uses the single-instance
//     `transform` / `color` fields. When it is non-null, Render2D issues
//     Draw*(..., instance_count) with the buffer bound; the single-instance
//     transform/color are ignored.
//   - When `children` is non-null, the drawable is a composition node and
//     Render2D recursively collects its children (analog to SceneMerge).
//   - When `custom_pipeline` is non-null, Render2D uses it instead of its
//     cached variant pipeline.
//   - `z_layer` with `NaN` means "use traversal order". A finite value acts
//     as a stable-sort override; Render2D uses painter's algorithm with
//     depth test OFF.
struct VividDrawable2D {
    // --- Header ---
    VividDrawable2DType type;
    VividBlendMode      blend_mode;
    float               z_layer;              // NaN = traversal order
    float               _pad_header;
    uint32_t            _reserved_header[4];

    // --- Single-instance transform (ignored when instance_buffer is set) ---
    float    transform[6];                    // mat3x2 column-major
    float    _pad_xform[2];
    float    color[4];
    uint32_t _reserved_xform[4];

    // --- Sprite payload ---
    WGPUTextureView texture_view;
    WGPUSampler     texture_sampler;
    float           uv_rect[4];               // u0, v0, u1, v1
    uint32_t        _reserved_sprite[4];

    // --- Shape payload (SDF primitives) ---
    uint32_t shape_sides;                     // 0 = circle, N = polygon/star
    float    shape_star_factor;               // 0 = polygon, >0 = star
    float    shape_softness;
    uint32_t _pad_shape;
    uint32_t _reserved_shape[4];

    // --- Text payload ---
    WGPUTextureView text_atlas_view;
    WGPUBuffer      text_glyph_buffer;        // per-glyph: atlas uv + local offset
    uint32_t        text_glyph_count;
    uint32_t        _pad_text;
    uint32_t        _reserved_text[4];

    // --- Custom pipeline override ---
    WGPURenderPipeline custom_pipeline;
    WGPUBindGroup      custom_material_binds;
    uint32_t           _reserved_custom[4];

    // --- Instancing ---
    WGPUBuffer instance_buffer;
    uint32_t   instance_count;
    uint32_t   _pad_inst;
    uint32_t   _reserved_inst[4];

    // --- Composition ---
    VividDrawable2D** children;
    uint32_t          child_count;
    uint32_t          _pad_composition;
    uint32_t          _reserved_composition[4];
};

// ---------------------------------------------------------------------------
// Sanity checks — pin the struct size so ABI drift is loud at compile time
// ---------------------------------------------------------------------------

// Compute the expected size block-by-block. If a future phase legitimately
// adds a field by consuming reserved slots, this number stays the same; only
// deliberate layout changes need to update it (and every plugin rebuilt).
static_assert(sizeof(VividDrawable2D) == 312, "VividDrawable2D layout changed — bump the stable_type_id and rebuild all plugins");

} // namespace vivid::gpu

// ---------------------------------------------------------------------------
// Port-type registrations — must be visible before the port helpers below
// that expand VIVID_CUSTOM_REF_PORT<T>(). Mirrors gpu_3d.h's pattern of
// splitting the namespace around the DECLARE calls.
// ---------------------------------------------------------------------------

VIVID_DECLARE_CUSTOM_REF_TYPE(vivid::gpu::VividDrawable2D,
                              "seethroughlab.vivid.drawable_2d_v1",
                              "VividDrawable2D",
                              false);

VIVID_DECLARE_CUSTOM_REF_TYPE(vivid::gpu::InstanceArray2D,
                              "seethroughlab.vivid.instance_array_2d_v1",
                              "InstanceArray2D",
                              false);

namespace vivid::gpu {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Zero-initialise a drawable to identity transform / white color / default
// everything. Callers should then set `type` plus whichever payload fields
// their drawable needs.
inline void drawable_identity(VividDrawable2D& d) {
    d = VividDrawable2D{};
    // Identity mat3x2: [1, 0, 0, 1, 0, 0] — row-major [a, c, b, d, tx, ty]
    // in column-major storage.
    d.transform[0] = 1.0f;
    d.transform[3] = 1.0f;
    d.color[0] = 1.0f;
    d.color[1] = 1.0f;
    d.color[2] = 1.0f;
    d.color[3] = 1.0f;
    d.z_layer = NAN;  // traversal order
    d.uv_rect[0] = 0.0f;
    d.uv_rect[1] = 0.0f;
    d.uv_rect[2] = 1.0f;
    d.uv_rect[3] = 1.0f;
}

// Compose two mat3x2 affines in column-major storage: `out = a ∘ b`
// (b applied first, then a — the same order as matrix multiplication a * b).
// All three pointers may alias.
inline void drawable_transform_compose(float out[6], const float a[6], const float b[6]) {
    const float a00 = a[0], a10 = a[1];
    const float a01 = a[2], a11 = a[3];
    const float a02 = a[4], a12 = a[5];
    const float b00 = b[0], b10 = b[1];
    const float b01 = b[2], b11 = b[3];
    const float b02 = b[4], b12 = b[5];

    out[0] = a00 * b00 + a01 * b10;
    out[1] = a10 * b00 + a11 * b10;
    out[2] = a00 * b01 + a01 * b11;
    out[3] = a10 * b01 + a11 * b11;
    out[4] = a00 * b02 + a01 * b12 + a02;
    out[5] = a10 * b02 + a11 * b12 + a12;
}

// Build a TRS affine in column-major storage.
inline void drawable_transform_trs(float out[6], float tx, float ty,
                                   float rotation_radians,
                                   float sx, float sy) {
    const float c = std::cos(rotation_radians);
    const float s = std::sin(rotation_radians);
    out[0] =  c * sx;   // col0.x (a)
    out[1] =  s * sx;   // col0.y (c)
    out[2] = -s * sy;   // col1.x (b)
    out[3] =  c * sy;   // col1.y (d)
    out[4] =  tx;       // col2.x (tx)
    out[5] =  ty;       // col2.y (ty)
}

// ---------------------------------------------------------------------------
// Port declaration helpers
// ---------------------------------------------------------------------------

// Custom-ref port carrying a VividDrawable2D. Mirrors scene_port() from gpu_3d.h.
inline VividPortDescriptor drawable_port(const char* name, VividPortDirection dir) {
    return VIVID_CUSTOM_REF_PORT(name, dir, VividDrawable2D);
}

// Custom-ref port carrying an InstanceArray2D bundle.
inline VividPortDescriptor instance_array_port(const char* name, VividPortDirection dir) {
    return VIVID_CUSTOM_REF_PORT(name, dir, InstanceArray2D);
}

// Typed input accessor — casts void* → VividDrawable2D*. Returns nullptr
// when the custom input at ordinal `idx` is unconnected.
inline VividDrawable2D* drawable_input(const VividGpuContext* ctx, uint32_t idx) {
    if (!ctx->custom_inputs || idx >= ctx->custom_input_count) return nullptr;
    return static_cast<VividDrawable2D*>(ctx->custom_inputs[idx]);
}

// Typed input accessor for an InstanceArray2D bundle.
inline const InstanceArray2D* instance_array_input(const VividGpuContext* ctx, uint32_t idx) {
    if (!ctx->custom_inputs || idx >= ctx->custom_input_count) return nullptr;
    return static_cast<const InstanceArray2D*>(ctx->custom_inputs[idx]);
}

// Populate the TEXT payload on a drawable. `d` must have already been cleared
// via drawable_identity(). `blend` is the alpha-blend mode used by Render2D
// for the text glyphs (ALPHA is the right default for anti-aliased text).
inline void drawable_text(VividDrawable2D& d,
                           WGPUTextureView atlas_view,
                           WGPUBuffer      glyph_buffer,
                           uint32_t        glyph_count,
                           VividBlendMode  blend = VIVID_BLEND_ALPHA) {
    d.type              = VIVID_DRAWABLE2D_TEXT;
    d.blend_mode        = blend;
    d.text_atlas_view   = atlas_view;
    d.text_glyph_buffer = glyph_buffer;
    d.text_glyph_count  = glyph_count;
}

} // namespace vivid::gpu
