#include "cli/control_handlers_internal.h"

#include "app/app.h"
#include "app/operator_clone.h"          // clone_operator / clone_operator_from_source (clone_operator tool)
#include "app/project_paths.h"           // is_folder_project (reload default path)
#include "gpu/operator_scan.h"           // load_and_register_operator
#include "packages/package_manager.h"    // install_package / user_operators_dir
#include "packages/package_manifest.h"   // parse_package_manifest
#include "platform/platform.h"           // user_data_dir (scaffold default path)

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

// ADR-0024 Phase 7 (package/operator authoring): scaffold / validate / build / reload an operator
// package. These compose the existing package_manager (compile) + operator_scan (register) paths so
// an agent can author a compiled operator from the CLI. A LIVE hot-swap of an already-registered op
// is intentionally NOT hand-rolled here — OpRegistry::unregister_type's contract forbids retiring a
// type a live node still references; editing a watched op's source is the safe hot-reload path
// (ADR-0020 always-on watcher). reload_operator_package therefore only registers NOT-yet-live ops.

namespace vivid {
namespace {

namespace fs = std::filesystem;

// A minimal, known-good visual generator package operator: a solid color driven by two params
// (hue/bright). Structurally identical to the proven clone template (app/operator_clone.cpp) so a
// scaffold compiles against the shipped operator_api. __OPNAME__ → the scaffolded type name.
const char* kVisualStarter = R"SRC(// Scaffolded Vivid visual operator. Edit the WGSL / params, then reload_operator_package.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <array>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
const char* kWGSL = R"WGSL(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, hue: f32, bright: f32, p0: f32, p1: f32, p2: f32 };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let col = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + u.hue * 6.2831853);
    return vec4f(col * u.bright, 1.0);
}
)WGSL";
}  // namespace

struct __OPNAME__ : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "__OPNAME__";
    static constexpr const char* kDisplayName = "__OPNAME__";
    static constexpr const char* kSummary = "Scaffolded solid-color generator — edit me.";
    static constexpr std::array<const char*, 2> kKeywords = {"generator", "scaffold"};
    vivid::Param<float> hue    {"hue",    0.5f, 0.f, 1.f};
    vivid::Param<float> bright {"bright", 0.8f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout pl_ = nullptr; WGPURenderPipeline pipe_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~__OPNAME__() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&hue); o.push_back(&bright); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "__OPNAME__", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "__OPNAME__ U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0;
        e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "__OPNAME__ Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values;
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       p ? p[0] : hue.value, p ? p[1] : bright.value, 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "__OPNAME__");
    }
};
VIVID_REGISTER(__OPNAME__)
)SRC";

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
}

bool write_file(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::trunc);
    if (!out) return false;
    out << s;
    return static_cast<bool>(out);
}

// A single-operator vivid-package.json for a scaffolded op.
std::string scaffold_manifest(const std::string& name, const std::string& kind) {
    const bool gpu = (kind == "gpu_visual" || kind.empty());
    return "{\n  \"name\": \"" + name + "\",\n  \"version\": \"0.1.0\",\n"
           "  \"operators\": [ { \"name\": \"" + name + "\", \"source\": \"" + name + ".cpp\", "
           "\"kind\": \"" + (kind.empty() ? std::string("gpu_visual") : kind) + "\", "
           "\"gpu\": " + (gpu ? "true" : "false") + " } ]\n}\n";
}

// A C++-identifier-safe operator name (leading alpha/underscore, then alnum/underscore).
bool valid_op_name(const std::string& n) {
    if (n.empty() || !(std::isalpha((unsigned char)n[0]) || n[0] == '_')) return false;
    for (char ch : n) if (!(std::isalnum((unsigned char)ch) || ch == '_')) return false;
    return true;
}

bool is_current_folder_project_path(App* app, const std::string& path) {
    if (!app || app->project.current_project_path.empty()) return false;
    if (!vivid::project_paths::is_folder_project(app->project.current_project_path)) return false;
    std::error_code eca, ecb;
    const fs::path a = fs::weakly_canonical(path, eca);
    const fs::path b = fs::weakly_canonical(app->project.current_project_path, ecb);
    return !eca && !ecb && a == b;
}

// Parse a manifest and check every declared source exists on disk → a structured validation result.
json validate_manifest(const std::string& dir) {
    json r;
    PackageManifest mf = parse_package_manifest(dir);
    r["dir"] = dir;
    r["manifest_ok"] = mf.ok;
    if (!mf.ok) { r["valid"] = false; r["error"] = mf.error; return r; }
    r["name"] = mf.name;
    r["version"] = mf.version;
    if (mf.abi) r["abi"] = mf.abi;
    json ops = json::array();
    bool all_sources = true;
    std::error_code ec;
    for (const auto& op : mf.operators) {
        const fs::path src = fs::path(dir) / op.source;
        const bool exists = fs::exists(src, ec);
        all_sources = all_sources && exists;
        ops.push_back({ {"name", op.name}, {"source", op.source}, {"kind", op.kind},
                        {"source_exists", exists}, {"path", src.string()} });
    }
    r["operators"] = ops;
    const bool has_ops = !mf.operators.empty();
    r["valid"] = has_ops && all_sources;
    if (!has_ops) r["error"] = "manifest declares no operators";
    else if (!all_sources) r["error"] = "one or more operator sources are missing on disk";
    return r;
}

}  // namespace

// Package/operator authoring: scaffold a new package, validate it, compile it (no register), and
// reload (register not-yet-live ops). See install_operator_package (introspection) for the
// compile+register-live variant; clone_operator (fork-a-built-in) lands separately.
void register_package_handlers(Handlers& handlers_) {

    // validate_operator_package — parse <path>/vivid-package.json and confirm each declared operator
    // source exists on disk. Read-only pre-flight: no compile, no live mutation.
    handlers_["validate_operator_package"] = [](const ControlCtx&, const json& b) {
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "validate_operator_package needs \"path\" (a package directory)");
        std::error_code ec;
        if (!fs::is_directory(path, ec)) return err(code::kBadArg, "not a directory: " + path);
        json v = validate_manifest(path);
        json r = ok();
        r.update(v);
        r["summary"] = v.value("valid", false)
                           ? "package '" + v.value("name", std::string()) + "' is valid (" +
                                 std::to_string(v.value("operators", json::array()).size()) + " operator(s))"
                           : std::string("package is invalid: ") + v.value("error", std::string("unknown"));
        return r;
    };

    // build_operator_package — compile every operator in the package to a .dylib in a temp build dir,
    // WITHOUT registering anything live. A "does it build?" check that never touches the running
    // catalog (use install_operator_package to compile + register live, or reload_operator_package).
    handlers_["build_operator_package"] = [](const ControlCtx&, const json& b) {
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "build_operator_package needs \"path\" (a package directory)");
        std::error_code ec;
        if (!fs::is_directory(path, ec)) return err(code::kBadArg, "not a directory: " + path);
        const fs::path out = fs::temp_directory_path(ec) / ("vivid-build-" + fs::path(path).filename().string());
        fs::create_directories(out, ec);
        PackageInstallResult ir = install_package(path, out.string());
        if (!ir.ok) return err(code::kBadArg, ir.error);
        json ops = json::array();
        int built = 0, failed = 0;
        for (const auto& ci : ir.compiles) {
            json jo = { {"name", ci.op_name}, {"compiled", ci.success} };
            if (ci.success) { jo["dylib"] = ci.dylib_path; ++built; }
            else { jo["error"] = ci.error_output; ++failed; }
            ops.push_back(jo);
        }
        json r = ok();
        r["package"] = ir.name;
        r["build_dir"] = out.string();
        r["operators"] = ops;
        r["built"] = built;
        r["failed"] = failed;
        r["ok_all"] = (failed == 0);
        r["summary"] = std::to_string(built) + " built, " + std::to_string(failed) + " failed";
        return r;
    };

    // reload_operator_package — recompile the package and register any operator NOT already live
    // (a newly-authored op appears in list_operators immediately, no restart). Already-registered
    // operators are reported as live: edit their source to hot-reload (ADR-0020 watcher) — this tool
    // never unregisters a live type (that would dangle nodes referencing it). `path` defaults to the
    // current folder project's package dir.
    handlers_["reload_operator_package"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app context");
        std::string path = b.value("path", std::string());
        if (path.empty()) {   // default: the open folder project's co-located package
            const std::string proj = c.app->project.current_project_path;
            if (!proj.empty() && vivid::project_paths::is_folder_project(proj)) path = proj;
        }
        if (path.empty()) return err(code::kBadArg, "reload_operator_package needs \"path\" (or an open folder project)");
        std::error_code ec;
        if (!fs::exists(fs::path(path) / "vivid-package.json", ec))
            return err(code::kBadArg, "no vivid-package.json in " + path);
        PackageInstallResult ir = install_package(path, path);
        if (!ir.ok) return err(code::kBadArg, ir.error);
        json ops = json::array();
        int registered = 0, live = 0;
        for (const auto& ci : ir.compiles) {
            json jo = { {"name", ci.op_name}, {"compiled", ci.success} };
            if (!ci.success) { jo["error"] = ci.error_output; }
            else {
                const RegisterResult rr = load_and_register_operator_ex(ci.dylib_path, c.app->op_registry, c.app->op_loaders);
                if (rr.ok) {
                    jo["registered"] = true; jo["op"] = rr.op_name; ++registered;
                    if (is_current_folder_project_path(c.app, path)) c.app->project_operator_types.insert(rr.op_name);
                } else if (rr.shadowed) {
                    jo["registered"] = false; jo["note"] = "already live — edit source to hot-reload"; ++live;
                } else {
                    // A real load failure (dlopen / ABI / missing symbols) — surface WHY, not "already live".
                    jo["registered"] = false;
                    jo["error_key"] = rr.error_key;
                    jo["error"] = rr.error_msg;
                    if (rr.quarantined) jo["note"] = "operator is quarantined (repeat crashes) — not registered";
                }
            }
            ops.push_back(jo);
        }
        if (registered > 0) c.app->file_drops.rebuild(c.app->op_loaders);   // pick up any new drop handlers
        json r = ok();
        r["package"] = ir.name;
        r["operators"] = ops;
        r["newly_registered"] = registered;
        r["already_live"] = live;
        r["summary"] = std::to_string(registered) + " newly registered, " + std::to_string(live) + " already live";
        return r;
    };

    // scaffold_operator_package — write a fresh single-operator package (a known-good starter source
    // + manifest) and validate the output. `name` becomes the operator type; `kind` defaults to
    // gpu_visual (the only templated domain today); `path` defaults to <user_data>/scaffold/<name>.
    // The caller then edits the source and build/reload_operator_package.
    handlers_["scaffold_operator_package"] = [](const ControlCtx&, const json& b) {
        const std::string name = b.value("name", std::string());
        if (name.empty()) return err(code::kBadArg, "scaffold_operator_package needs \"name\"");
        if (!valid_op_name(name))
            return err(code::kBadArg, "name must be a C++ identifier (letters/digits/underscore, no leading digit)");
        const std::string kind = b.value("kind", b.value("domain", std::string("gpu_visual")));
        if (kind != "gpu_visual")
            return err(code::kBadArg, "only kind=gpu_visual is scaffolded today (requested: " + kind + ")");
        std::string path = b.value("path", std::string());
        if (path.empty()) {
            const std::string data = platform::user_data_dir();
            if (data.empty()) return err(code::kInternal, "no user data dir for default scaffold path");
            path = (fs::path(data) / "scaffold" / name).string();
        }
        std::error_code ec;
        if (fs::exists(fs::path(path) / "vivid-package.json", ec))
            return err(code::kBadArg, "a package already exists at " + path);
        fs::create_directories(path, ec);
        if (ec) return err(code::kIoError, "could not create scaffold dir: " + ec.message());
        std::string src = kVisualStarter;
        replace_all(src, "__OPNAME__", name);
        const fs::path srcpath = fs::path(path) / (name + ".cpp");
        if (!write_file(srcpath, src)) return err(code::kIoError, "could not write source: " + srcpath.string());
        if (!write_file(fs::path(path) / "vivid-package.json", scaffold_manifest(name, kind)))
            return err(code::kIoError, "could not write manifest");
        json r = ok();
        r["name"] = name;
        r["kind"] = kind;
        r["path"] = path;
        r["source"] = srcpath.string();
        r["manifest"] = (fs::path(path) / "vivid-package.json").string();
        r["validation"] = validate_manifest(path);   // per ADR: scaffold output is validated
        r["next_tools"] = {"build_operator_package", "reload_operator_package"};
        r["summary"] = "scaffolded gpu_visual operator '" + name + "' at " + path;
        return r;
    };

    // clone_operator — fork a compiled operator into a fresh editable copy under `new_name`, compiled
    // + registered live (the caller can spawn/edit it immediately). Mirrors fork_shader for compiled
    // ops. Two backed cases: a built-in with a clone template (e.g. Plasma), or any operator whose
    // source is on disk + watched (a prior clone / installed / project C++ op). A built-in dylib with
    // no editable source can't be cloned (its source didn't ship) — use fork_shader for shaders.
    handlers_["clone_operator"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app context");
        const std::string op = b.value("op", std::string());
        const std::string new_name = b.value("new_name", std::string());
        if (op.empty() || new_name.empty())
            return err(code::kBadArg, "clone_operator needs \"op\" (the operator to clone) and \"new_name\"");
        if (!valid_op_name(new_name))
            return err(code::kBadArg, "new_name must be a C++ identifier (letters/digits/underscore, no leading digit)");
        if (c.app->op_registry.has(new_name))
            return err(code::kBadArg, "an operator named '" + new_name + "' already exists");

        CloneResult cr;
        if (operator_has_clone_template(op)) {
            cr = clone_operator(c.app->op_registry, c.app->op_loaders, op, new_name);
        } else {
            const std::string src = c.app->hot_reload.source_for(op);
            if (src.empty())
                return err(code::kBadArg, "no editable source for '" + op + "' to clone — it's a built-in "
                                          "without a clone template or a shipped dylib; shaders use fork_shader");
            cr = clone_operator_from_source(c.app->op_registry, c.app->op_loaders, op, src, new_name);
        }
        if (!cr.ok) return err(code::kBadArg, cr.error.empty() ? "clone failed" : cr.error);
        c.app->file_drops.rebuild(c.app->op_loaders);   // pick up any new drop handlers
        // ADR-0020 W2: watch the fresh clone so editing its source hot-reloads live, and so it can be
        // re-cloned by source (source_for() now resolves). Mirrors the UI Clone & Edit path.
        const std::string pkgdir = fs::path(cr.source_path).parent_path().string();
        c.app->hot_reload.watch_manifest(c.app->op_loaders, parse_package_manifest(pkgdir));
        json r = ok();
        r["op"] = cr.name;
        r["cloned_from"] = op;
        r["source"] = cr.source_path;
        r["summary"] = "cloned '" + op + "' -> '" + cr.name + "' (compiled + registered live)";
        return r;
    };
}

}  // namespace vivid
