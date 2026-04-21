#include "runtime/core/main_helpers.h"
#include <nlohmann/json.hpp>
#include "runtime/core/file_watcher.h"
#include "runtime/core/hot_reload.h"
#include "runtime/core/runtime_core.h"
#include "runtime/control/runtime_api.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/operator_info_cache.h"
#include "runtime/core/build_console.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/gpu/gpu_context.h"
#include "runtime/gpu/mipmap_generator.h"
#include "runtime/gpu/wgsl_header_parser.h"
#include "runtime/packages/package_manager.h"
#include "ui/graph/node_graph.h"
#include "ui/rendering/thumbnail_cache.h"
#include "ui/rendering/renderer_2d.h"
#include "operator_api/data_driven_filter.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/types.h"
#include "operator_api/gpu_common.h"
#include "common/gpu_util.h"
#include <webgpu/wgpu.h>
#include <GLFW/glfw3.h>
#include <stb_image_write.h>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <cstring>

using vivid::to_sv;

namespace vivid {

bool is_srgb_format(WGPUTextureFormat fmt) {
    switch (fmt) {
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return true;
        default:
            return false;
    }
}



std::string url_encode(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[(c >> 4) & 0x0F]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

std::string platform_label() {
#if defined(__APPLE__)
    return "macOS";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

static void stbi_write_to_vec(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

std::string now_epoch_seconds_str() {
    auto now = std::chrono::system_clock::now();
    auto sec = std::chrono::time_point_cast<std::chrono::seconds>(now)
                   .time_since_epoch().count();
    return std::to_string(static_cast<long long>(sec));
}

std::vector<std::string> json_str_array(const nlohmann::json& arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) return out;
    for (const auto& v : arr) {
        if (v.is_string()) out.emplace_back(v.get<std::string>());
    }
    return out;
}

std::string trim_copy(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t comma = csv.find(',', pos);
        if (comma == std::string::npos) comma = csv.size();
        std::string tok = trim_copy(csv.substr(pos, comma - pos));
        if (!tok.empty()) out.push_back(tok);
        pos = comma + 1;
    }
    return out;
}

std::string join_csv(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        out += items[i];
    }
    return out;
}

void emit_clear_pass(WGPUCommandEncoder encoder, WGPUTextureView view, const double clear[4]) {
    WGPURenderPassColorAttachment color_att{};
    color_att.view = view;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = { clear[0], clear[1], clear[2], clear[3] };
    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = to_sv("Clear Pass");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

static vivid::WgslOperatorConfig clone_shader_operator_config(const vivid::WgslOperatorConfig& config) {
    return config;
}

static bool shader_param_matches(const vivid::WgslOperatorConfig::ParamDef& current,
                                 const vivid::WgslHeaderParam& next) {
    return current.name == next.name &&
           current.type == next.type &&
           current.default_value == next.default_value &&
           current.min_value == next.min_value &&
           current.max_value == next.max_value &&
           current.label == next.label &&
           current.choices == next.choices &&
           current.display_hint == next.display_hint &&
           current.group == next.group &&
           current.layout_columns == next.layout_columns &&
           current.layout_column_index == next.layout_column_index;
}

static bool shader_header_requires_rebuild(const vivid::WgslOperatorConfig& current,
                                           const vivid::WgslHeader& next) {
    if (current.name != next.name ||
        current.time_dependent != next.time_dependent ||
        current.inputs_specified != next.inputs_specified ||
        current.inputs.size() != next.inputs.size() ||
        current.params.size() != next.params.size()) {
        return true;
    }

    for (size_t i = 0; i < current.inputs.size(); ++i) {
        if (current.inputs[i].name != next.inputs[i].name)
            return true;
    }
    for (size_t i = 0; i < current.params.size(); ++i) {
        if (!shader_param_matches(current.params[i], next.params[i]))
            return true;
    }
    return false;
}

struct ShaderOperatorBackup {
    vivid::WgslOperatorConfig config;
    bool mark_user = false;
    std::string package_name;
};

static std::vector<ShaderOperatorBackup> backup_shader_operators_in_dir(
    vivid::OperatorRegistry& registry,
    const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    std::vector<ShaderOperatorBackup> backups;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(directory, ec);
    if (ec) dir = directory.lexically_normal();
    const std::string dir_s = dir.string();
    const std::string dir_prefix = dir_s.empty() ? dir_s : (dir_s + "/");

    for (const auto& type_name : registry.type_names()) {
        const std::string* source = registry.shader_operator_source(type_name);
        const vivid::WgslOperatorConfig* config = registry.shader_operator_config(type_name);
        if (!source || !config) continue;

        fs::path source_path = fs::weakly_canonical(*source, ec);
        if (ec) {
            ec.clear();
            source_path = fs::path(*source).lexically_normal();
        }
        const std::string source_s = source_path.string();
        if (source_s != dir_s && source_s.rfind(dir_prefix, 0) != 0)
            continue;

        ShaderOperatorBackup backup;
        backup.config = clone_shader_operator_config(*config);
        backup.mark_user = registry.is_user_shader_operator(type_name);
        if (const std::string* package_name = registry.package_for_type(type_name))
            backup.package_name = *package_name;
        backups.push_back(std::move(backup));
    }
    return backups;
}

static void restore_shader_operator_backups(vivid::OperatorRegistry& registry,
                                            const std::filesystem::path& directory,
                                            const std::vector<ShaderOperatorBackup>& backups) {
    registry.clear_shader_operators_in_dir(directory.string());
    for (const auto& backup : backups) {
        registry.register_shader_operator(
            std::make_shared<vivid::WgslOperatorConfig>(backup.config),
            backup.mark_user,
            backup.package_name);
    }
}

static void handle_shader_operator_change(const vivid::FileChangeEvent& change,
                                          vivid::OperatorRegistry& registry,
                                          vivid::RuntimeAPI& runtime_api,
                                          bool& has_gpu_ops,
                                          bool& has_audio,
                                          OperatorInfoCache* op_cache) {
    namespace fs = std::filesystem;

    std::string matched_type;
    const vivid::WgslOperatorConfig* current_config = nullptr;
    for (const auto& type_name : registry.type_names()) {
        const std::string* source = registry.shader_operator_source(type_name);
        if (!source || *source != change.file_path)
            continue;
        current_config = registry.shader_operator_config(type_name);
        if (!current_config)
            continue;
        matched_type = type_name;
        break;
    }
    if (!current_config)
        return;

    std::ifstream ifs(change.file_path);
    if (!ifs)
        return;
    std::ostringstream ss;
    ss << ifs.rdbuf();

    std::string parse_error;
    auto header = vivid::parse_wgsl_header(ss.str(), parse_error);
    if (!header)
        return;
    if (!shader_header_requires_rebuild(*current_config, *header))
        return;

    fs::path shader_dir = fs::path(change.file_path).parent_path();
    const auto backups = backup_shader_operators_in_dir(registry, shader_dir);
    const bool mark_user = registry.is_user_shader_operator(matched_type);
    std::string package_name;
    if (const std::string* pkg = registry.package_for_type(matched_type))
        package_name = *pkg;

    std::fprintf(stderr, "[vivid] Shader operator schema changed: %s — rescanning and rebuilding graph\n",
                 change.file_path.c_str());

    if (!registry.scan_shader_operators(shader_dir.string(), mark_user, package_name)) {
        std::fprintf(stderr, "[vivid] Shader operator rescan failed for %s; restoring previous descriptors\n",
                     change.file_path.c_str());
        restore_shader_operator_backups(registry, shader_dir, backups);
        if (op_cache) op_cache->invalidate_all();
        return;
    }

    if (op_cache) op_cache->invalidate_all();
    vivid::CommandResult rebuild_result = runtime_api.rebuild_current_graph(has_gpu_ops, has_audio);
    if (!rebuild_result.ok) {
        std::fprintf(stderr, "[vivid] Shader operator rebuild failed after schema change in %s: %s\n",
                     change.file_path.c_str(), rebuild_result.message.c_str());
        restore_shader_operator_backups(registry, shader_dir, backups);
        if (op_cache) op_cache->invalidate_all();
        vivid::CommandResult restore_result = runtime_api.rebuild_current_graph(has_gpu_ops, has_audio);
        if (!restore_result.ok) {
            std::fprintf(stderr, "[vivid] Failed to restore previous shader operator state for %s: %s\n",
                         change.file_path.c_str(), restore_result.message.c_str());
        }
        return;
    }
}

void poll_hot_reload(vivid::FileWatcher& fw, vivid::HotReloader& hr,
                            vivid::RuntimeCore& runtime, vivid::OperatorRegistry& registry,
                            vivid::RuntimeAPI& runtime_api, vivid::AudioEngine& audio_engine,
                            bool& has_gpu_ops, bool& has_audio, OperatorInfoCache* op_cache,
                            const std::string& operators_dir) {
    auto changes = fw.poll_changes();
    for (const auto& change : changes) {
        if (std::filesystem::path(change.file_path).extension() == ".wgsl") {
            handle_shader_operator_change(change, registry, runtime_api, has_gpu_ops, has_audio,
                                          op_cache);
            continue;
        }
        hr.queue_rebuild(change.target_name);
    }

    auto ready = hr.poll_ready();
    for (const auto& result : ready) {
        // Cache for runtime_health (Phase 8a). Always store, even on success,
        // so a previous failure stops looking like the current state once a
        // subsequent reload succeeds.
        runtime.set_last_reload(result);
        if (!result.success) {
            // Propagate compile errors to all nodes of this type so the UI can surface them.
            // Nodes keep running (old dylib still live); errored=true is NOT set.
            const std::string* type_name_ptr = registry.type_name_for_target(result.target_name);
            if (type_name_ptr) {
                const std::string& err = result.error_output.empty()
                    ? "Build failed (no output captured)" : result.error_output;
                if (auto* cg = runtime.compiled_graph()) {
                    for (auto& cn : cg->nodes) {
                        if (cn.type_name == *type_name_ptr)
                            cn.error_message = err;
                    }
                }
            }
            continue;
        }

        const std::string* type_name_ptr = registry.type_name_for_target(result.target_name);
        if (!type_name_ptr) {
            // New operator (just scaffolded) — load its dylib into the registry
            if (registry.register_loaded_operator(result.staged_dylib_path)) {
                // Register file watch for the new operator's source files
                if (!operators_dir.empty()) {
                    // Scan all env subdirs for the target directory
                    for (const char* domain : {"control", "audio", "gpu"}) {
                        std::string cpp_path = operators_dir + "/" + domain + "/" +
                                               result.target_name + "/" + result.target_name + ".cpp";
                        if (std::filesystem::exists(cpp_path)) {
                            fw.add_watch(cpp_path, result.target_name);
                            break;
                        }
                    }
                }
                std::fprintf(stderr, "[vivid] New operator '%s' loaded\n",
                    result.target_name.c_str());
                // Trigger recompile so placeholder nodes get replaced
                runtime_api.request_recompile();
            } else {
                std::fprintf(stderr, "[vivid] Hot-reload: failed to load new target '%s'\n",
                    result.target_name.c_str());
            }
            continue;
        }
        const std::string& tn = *type_name_ptr;

        std::fprintf(stderr, "[vivid] Hot-reload: reloading %s...\n", tn.c_str());

        bool is_audio_op = runtime.has_audio_cadence_type(tn);

        // Two-phase audio reload: destroy old instances BEFORE the dylib swap
        // so we can safely call the old dylib's destroy function.
        if (is_audio_op && has_audio) {
            audio_engine.pre_reload_operator(tn);
        }

        if (runtime.reload_operator(tn, registry, result.staged_dylib_path)) {
            bool audio_reload_ok = true;
            if (is_audio_op && has_audio) {
                audio_reload_ok = audio_engine.post_reload_operator(tn, registry);
            }
            if (!audio_reload_ok) {
                std::fprintf(stderr, "[vivid] Hot-reload: %s audio reload FAILED\n", tn.c_str());
            } else {
                if (auto* cg = runtime.compiled_graph()) {
                    for (auto& cn : cg->nodes) {
                        if (cn.type_name == tn)
                            cn.error_message.clear();
                    }
                }
                if (op_cache) op_cache->invalidate(tn);
                std::fprintf(stderr, "[vivid] Hot-reload: %s reloaded successfully\n", tn.c_str());
            }
        } else {
            std::fprintf(stderr, "[vivid] Hot-reload: %s reload FAILED\n", tn.c_str());
            // Recreate audio instances from the old (still-loaded) dylib
            if (is_audio_op && has_audio) {
                audio_engine.post_reload_operator(tn, registry);
            }
        }
    }
}

int add_watch_for_resolved_package(vivid::FileWatcher& fw, const vivid::PackageInfo& pkg) {
    namespace fs = std::filesystem;
    int count = 0;
    fs::path ops_dir = fs::path(pkg.path) / "operators";
    if (fs::exists(ops_dir)) {
        std::error_code ec_domain;
        for (const auto& domain_entry : fs::directory_iterator(ops_dir, ec_domain)) {
            if (ec_domain) break;
            if (!domain_entry.is_directory()) continue;

            std::error_code ec_op;
            for (const auto& op_entry : fs::directory_iterator(domain_entry.path(), ec_op)) {
                if (ec_op) break;
                if (!op_entry.is_directory()) continue;

                std::string op_name = op_entry.path().filename().string();
                std::string target = "pkg:" + pkg.name + ":" + op_name;

                std::error_code ec_file;
                for (const auto& file_entry : fs::directory_iterator(op_entry.path(), ec_file)) {
                    if (ec_file) break;
                    if (!file_entry.is_regular_file()) continue;
                    std::string fname = file_entry.path().filename().string();
                    if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".cpp") continue;
                    if (fw.add_watch(file_entry.path().string(), target)) count++;
                }
            }
        }
    }

    fs::path src_dir = fs::path(pkg.path) / "src";
    if (fs::exists(src_dir)) {
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(src_dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".cpp") continue;
            std::string op_name = entry.path().stem().string();
            std::string target = "pkg:" + pkg.name + ":" + op_name;
            if (fw.add_watch(entry.path().string(), target)) count++;
        }
    }

    count += fw.add_shader_operator_watches((fs::path(pkg.path) / "filters").string());
    return count;
}

void draw_custom_thumbnails(const vivid::RuntimeCore& runtime,
                                   vivid::ui::ThumbnailCache& cache,
                                   vivid::ui::NodeGraphUI& graph_ui,
                                   vivid::ui::Renderer2D* thumb_draw_renderer,
                                   WGPUDevice device,
                                   WGPUQueue queue,
                                   WGPUCommandEncoder encoder,
                                   double time,
                                   double delta_time,
                                   uint64_t frame,
                                   uint32_t thumb_w,
                                   uint32_t thumb_h,
                                   uint32_t thumb_logical_w,
                                   uint32_t thumb_logical_h,
                                   WGPUTextureFormat thumb_format,
                                   vivid::MipmapGenerator* mip_gen) {
    const auto* cg_thumb = runtime.compiled_graph();
    if (!cg_thumb) return;

    if (thumb_draw_renderer)
        thumb_draw_renderer->reset_ring();

    constexpr auto kSlowThumbnailWarn = std::chrono::milliseconds(16);

    std::unordered_set<std::string> custom_thumb_ids;
    for (const auto& cn : cg_thumb->nodes) {
        if (!cn.loader || !cn.instance || cn.missing_operator) continue;
        if (!cn.loader->has_draw_thumbnail()) continue;
        WGPUTextureView thumb_view = cache.get_or_create_render_view(cn.node_id);
        if (!thumb_view) continue;
        WGPUTexture thumb_tex = cache.get_texture(cn.node_id);

        VividThumbnailContext tctx{};
        tctx.time = time;
        tctx.delta_time = delta_time;
        tctx.frame = frame;
        tctx.param_values = cn.param_values.data();
        tctx.param_count = static_cast<uint32_t>(cn.param_values.size());
        tctx.output_values = cn.output_values.data();
        tctx.output_count = cn.output_port_count;
        tctx.string_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
        tctx.string_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
        tctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
        tctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
        tctx.device = device;
        tctx.queue = queue;
        tctx.command_encoder = encoder;
        tctx.thumbnail_texture = thumb_tex;
        tctx.thumbnail_texture_view = thumb_view;
        tctx.thumbnail_width = thumb_w;
        tctx.thumbnail_height = thumb_h;
        tctx.thumbnail_format = thumb_format;
        tctx.thumbnail_logical_width = thumb_logical_w;
        tctx.thumbnail_logical_height = thumb_logical_h;
        tctx.source_output_texture = cn.gpu ? cn.gpu->texture : nullptr;
        tctx.source_output_texture_view = cn.gpu ? cn.gpu->texture_view : nullptr;
        tctx.source_output_width = cn.gpu ? cn.gpu->tex_width : 0;
        tctx.source_output_height = cn.gpu ? cn.gpu->tex_height : 0;
        tctx.source_output_format = thumb_format;
        tctx.input_texture_views =
            (cn.gpu && !cn.gpu->resolved_tex_inputs.empty()) ? const_cast<WGPUTextureView*>(cn.gpu->resolved_tex_inputs.data())
                                                             : nullptr;
        tctx.input_texture_count = cn.gpu ? static_cast<uint32_t>(cn.gpu->resolved_tex_inputs.size()) : 0;
        tctx.operator_errored = 0;
        tctx.operator_error_msg = nullptr;

        // Populate 2D draw API if thumbnail renderer is available
        if (thumb_draw_renderer)
            vivid::ui::populate_draw_api(tctx.draw, *thumb_draw_renderer);

        // Clear thumbnail texture before operator draws
        {
            WGPURenderPassColorAttachment clear_att{};
            clear_att.view = thumb_view;
            clear_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            clear_att.loadOp = WGPULoadOp_Clear;
            clear_att.storeOp = WGPUStoreOp_Store;
            clear_att.clearValue = {0.0, 0.0, 0.0, 0.0};
            WGPURenderPassDescriptor clear_desc{};
            clear_desc.label = vivid_sv("Thumb Clear");
            clear_desc.colorAttachmentCount = 1;
            clear_desc.colorAttachments = &clear_att;
            WGPURenderPassEncoder clear_pass = wgpuCommandEncoderBeginRenderPass(encoder, &clear_desc);
            wgpuRenderPassEncoderEnd(clear_pass);
            wgpuRenderPassEncoderRelease(clear_pass);
        }

        const auto thumb_start = std::chrono::steady_clock::now();
        cn.loader->draw_thumbnail(cn.instance, &tctx);
        const auto thumb_elapsed = std::chrono::steady_clock::now() - thumb_start;
        if (thumb_elapsed > kSlowThumbnailWarn) {
            const auto millis =
                std::chrono::duration_cast<std::chrono::milliseconds>(thumb_elapsed).count();
            std::fprintf(stderr,
                         "[vivid] slow thumbnail draw for '%s' (%s): %lld ms\n",
                         cn.node_id.c_str(),
                         cn.type_name.c_str(),
                         static_cast<long long>(millis));
        }

        // Flush any 2D draw API calls onto the thumbnail texture
        if (thumb_draw_renderer)
            thumb_draw_renderer->flush(encoder, thumb_view, thumb_logical_w, thumb_logical_h);

        if (tctx.operator_errored) {
            std::fprintf(stderr, "[vivid] thumbnail render error for '%s': %s\n",
                         cn.node_id.c_str(),
                         tctx.operator_error_msg ? tctx.operator_error_msg : "unknown error");
            continue;  // fall back to default thumbnail
        }

        // mip 0 is now complete (operator draw + any 2D flush) — fill the
        // rest of the chain so display sampling has pre-filtered levels.
        if (mip_gen) {
            mip_gen->generate(encoder,
                              cache.mip_render_views(cn.node_id),
                              cache.mip_sample_views(cn.node_id));
        }
        custom_thumb_ids.insert(cn.node_id);
    }
    graph_ui.set_custom_thumbnail_nodes(std::move(custom_thumb_ids));
}


bool capture_surface_png(vivid::GpuContext& gpu,
                                vivid::FrameState& frame,
                                int fb_w, int fb_h,
                                SurfaceCaptureResult& out,
                                std::string& error) {
    if (!gpu.surface_supports_copy_src()) {
        error = "surface does not support interface capture";
        return false;
    }
    const uint32_t ss_w = static_cast<uint32_t>(fb_w);
    const uint32_t ss_h = static_cast<uint32_t>(fb_h);
    const uint32_t bpp = 4;
    const uint32_t unpadded_row = ss_w * bpp;
    static constexpr uint32_t kGpuRowAlignment = 256;
    const uint32_t aligned_row = (unpadded_row + kGpuRowAlignment - 1) & ~(kGpuRowAlignment - 1);
    const uint64_t buf_size = static_cast<uint64_t>(aligned_row) * ss_h;

    WGPUBufferDescriptor staging_desc{};
    staging_desc.label = to_sv("Screenshot Staging");
    staging_desc.size = buf_size;
    staging_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    staging_desc.mappedAtCreation = false;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(gpu.device(), &staging_desc);
    if (!staging) {
        error = "failed to allocate screenshot staging buffer";
        return false;
    }

    WGPUTexelCopyTextureInfo src{};
    src.texture = frame.texture;
    src.mipLevel = 0;
    src.origin = { 0, 0, 0 };
    src.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = aligned_row;
    dst.layout.rowsPerImage = ss_h;

    WGPUExtent3D copy_size = { ss_w, ss_h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(frame.encoder, &src, &dst, &copy_size);

    gpu.end_frame(frame);

    // Wait for GPU work to complete
    {
        bool work_done = false;
        WGPUQueueWorkDoneCallbackInfo work_cb{};
        work_cb.mode = WGPUCallbackMode_AllowSpontaneous;
        work_cb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* ud1, void*) {
            *static_cast<bool*>(ud1) = true;
        };
        work_cb.userdata1 = &work_done;
        wgpuQueueOnSubmittedWorkDone(gpu.queue(), work_cb);
        while (!work_done)
            wgpuDevicePoll(gpu.device(), true, nullptr);
    }

    bool map_done = false;
    WGPUBufferMapCallbackInfo map_cb{};
    map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    map_cb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* ud1, void*) {
        *static_cast<bool*>(ud1) = true;
    };
    map_cb.userdata1 = &map_done;
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, buf_size, map_cb);
    while (!map_done)
        wgpuDevicePoll(gpu.device(), true, nullptr);

    const uint8_t* mapped = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(staging, 0, buf_size));
    if (!mapped) {
        wgpuBufferUnmap(staging);
        wgpuBufferRelease(staging);
        error = "failed to map screenshot staging buffer";
        return false;
    }

    std::vector<uint8_t> pixels(ss_w * ss_h * bpp);
    for (uint32_t y = 0; y < ss_h; ++y) {
        const uint8_t* src_row = mapped + y * aligned_row;
        uint8_t* dst_row = pixels.data() + y * unpadded_row;
        for (uint32_t x = 0; x < ss_w; ++x) {
            dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // R <- B
            dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G <- G
            dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // B <- R
            dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A <- A
        }
    }

    wgpuBufferUnmap(staging);
    wgpuBufferRelease(staging);

    out.width = ss_w;
    out.height = ss_h;
    out.png_data.clear();
    out.png_data.reserve(ss_w * ss_h);
    stbi_write_png_to_func(stbi_write_to_vec, &out.png_data, ss_w, ss_h, 4,
                           pixels.data(), static_cast<int>(ss_w * bpp));
    if (out.png_data.empty()) {
        error = "PNG encoding failed";
        return false;
    }
    return true;
}

bool try_capture_screenshot(const std::string& path, vivid::GpuContext& gpu,
                                   vivid::FrameState& frame, int fb_w, int fb_h,
                                   uint64_t frame_count, int delay, GLFWwindow* window) {
    if (path.empty() || static_cast<int>(frame_count) < delay) {
        return false;
    }

    SurfaceCaptureResult capture;
    std::string error;
    if (!capture_surface_png(gpu, frame, fb_w, fb_h, capture, error)) {
        std::fprintf(stderr, "[vivid] Screenshot FAILED: %s\n", error.c_str());
        glfwSetWindowShouldClose(window, 1);
        return true;
    }

    std::ofstream out(path, std::ios::binary);
    if (out) {
        out.write(reinterpret_cast<const char*>(capture.png_data.data()),
                  static_cast<std::streamsize>(capture.png_data.size()));
    }

    if (out.good()) {
        std::fprintf(stderr, "[vivid] Screenshot saved: %s\n", path.c_str());
    } else {
        std::fprintf(stderr, "[vivid] Screenshot FAILED: %s\n", path.c_str());
    }

    glfwSetWindowShouldClose(window, 1);
    return true;  // frame already submitted
}

} // namespace vivid
