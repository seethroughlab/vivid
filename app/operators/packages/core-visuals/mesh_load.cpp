// Core visual package operator: MeshLoad — load a glTF / GLB file and publish it as a MESH VALUE on
// a custom-ref output port, for the composable geometry pipeline (MeshLoad -> MeshDisplace ->
// MeshRender). Unlike Model (which loads AND renders in one node), MeshLoad only produces geometry;
// a downstream node decides how to modify and draw it. That is the whole point of the geometry-
// operator upgrade: a mesh flows between nodes as a first-class value, so a custom node can sit in
// the middle and deform it without re-implementing loading or rendering.
//
// A source (no inputs). Its output is a VividMesh custom-ref, NOT a texture, so it has no visible
// thumbnail — the render happens downstream in MeshRender. baseColor is not carried on the value
// channel (MeshRender is flat-lit); that matches the distortion use case and keeps the mesh pure.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/geom.h"              // publish_mesh

#define STB_IMAGE_IMPLEMENTATION            // this TU owns stb_image (the parser needs it for baseColor)
#include "operator_api/gltf_mesh.h"         // CPU glTF parse
#include "operator_api/mesh_render.h"       // GpuMesh upload -> VividMesh

#include <array>
#include <cstdio>
#include <string>

struct MeshLoadOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "MeshLoad";
    static constexpr const char* kDisplayName = "Mesh Load";
    static constexpr const char* kSummary = "Load a 3D model (glTF/GLB) and output it as a mesh for the geometry pipeline.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "3d"};

    vivid::Param<vivid::FilePath> path{"file", ""};

    MeshLoadOp() {
        vivid::description(path, "3D model file to load (glTF .gltf / .glb)");
        vivid::asset_kind(path, "model");
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&path); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("mesh", VIVID_PORT_OUTPUT, VividMesh));
    }

    ~MeshLoadOp() override { gpu_.release(); tex_.release(); }

    void process_gpu(const VividGpuContext* c) override {
        if (path.str_value != loaded_path_) { loaded_path_ = path.str_value; load_mesh(c); }
        if (gpu_.valid()) vivid::geom::publish_mesh(c, 0, &gpu_.mesh);   // downstream reads it this frame
    }

private:
    vivid::geom::GpuMesh          gpu_;
    vivid::geom::BaseColorTexture tex_;   // the glTF's baseColor, carried on the mesh so MeshRender textures it
    std::string loaded_path_ = "\x01";   // sentinel != any path so the first load fires

    void load_mesh(const VividGpuContext* c) {
        gpu_.release(); tex_.release();
        vivid::geom::CpuMesh md; std::string e;
        if (loaded_path_.empty() || !vivid::geom::load_gltf(loaded_path_, md, e)) {
            if (!loaded_path_.empty()) std::fprintf(stderr, "[MeshLoad] load failed (%s): %s\n", loaded_path_.c_str(), e.c_str());
            return;
        }
        gpu_.upload(c, md);   // upload's fill_view() zeroes base_color, so attach the material AFTER it
        if (!md.tex_rgba.empty()) { tex_.set(c, md.tex_rgba.data(), (uint32_t)md.tw, (uint32_t)md.th); gpu_.mesh.base_color = tex_.view(); }
        std::fprintf(stderr, "[MeshLoad] loaded %s: %u verts, %u indices, tex %dx%d\n",
                     loaded_path_.c_str(), gpu_.vert_n, gpu_.index_n, md.tw, md.th);
    }
};

VIVID_REGISTER(MeshLoadOp)
