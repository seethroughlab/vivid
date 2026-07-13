#include "gpu/builtin_ops.h"

#include "gpu/op_runtime.h"
#include "gpu/shader_op.h"
#include "gpu/effect_op.h"
#include "gpu/asset_shader.h"   // AssetShader (CustomShader data-driven .glsl)
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "gpu/freetype_font.h"   // FreeType face wrapper (hinted glyph bitmaps + outlines)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The built-in visuals operators, expressed against the lifted operator ABI.
// Each owns a GLSL ShaderOp/EffectOp (proven primitives) and renders it in
// process_gpu from the VividGpuContext. Shaders authored in GLSL — wgpu-native's
// naga translates them; WGSL operators (the other authoring path) coexist under
// the same runtime. This file is GPU-linked (only compiled into vivid::session).
namespace vivid {
namespace {

// --- shared GLSL owned by the built-in operator descriptors ---


VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}


// --- Blur: 1-input GLSL effect (1 param) ---










}  // namespace

void register_builtin_ops(OpRegistry& reg) {
}

}  // namespace vivid
