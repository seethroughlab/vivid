// InstancesFromSignal — one 3D instance per ACTIVE element of an incoming signal (from a Notes node, or
// any VividSignal producer, on its input edge). pos → layout position + hue, amp → scale; an element
// appearing spawns an instance, its leaving fades it out (chords bloom, arps trail). DRIVEN by the graph
// edge and agnostic to the source — it never refers to "notes", so a Beat or onset producer drives it
// unchanged. The 3D peer of core-visuals/Instancer: emits an InstanceArray3D for Instancer3D to draw.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/thumbnail_3d.h"
#include "operator_api/element_geom.h"   // VividSignal, input_signal

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// InstancesFromSignal — pack a signal's live element set into InstanceArray3D
// =============================================================================

struct InstancesFromSignal : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName        = "InstancesFromSignal";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_ADAPTER;   // ADR-0046
    static constexpr const char* kDisplayName = "Instances From Signal";
    static constexpr const char* kSummary =
        "Draws one 3D instance per element of an incoming signal (pitch->layout+colour, velocity->size); "
        "chords bloom, arps trail. Feed it a Notes node — or any signal producer.";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> size   {"size",   0.5f, 0.05f, 3.f};    // base instance scale
    vivid::Param<float> radius {"radius", 4.0f, 0.5f, 20.f};    // layout radius / half-width
    vivid::Param<int>   layout {"layout", 0, {"ring", "line", "orb", "wheel"}};
    vivid::Param<float> spin   {"spin",   0.15f, 0.f, 1.f};     // per-instance spin from its age
    vivid::Param<float> trail  {"trail",  0.5f, 0.f, 1.f};      // fade time after an element leaves
    vivid::Param<float> pulse  {"pulse",  0.6f, 0.f, 1.f};      // pop amount on each note-on FIRE
    vivid::Param<int>   palette{"palette", 0, {"Spectrum", "Rainbow", "Fire", "Ice", "Viridis"}};
    vivid::Param<float> persist{"persist", 0.f, 0.f, 1.f};      // 0: pop-and-vanish; 1: trail HOLDS a standing shape
    vivid::Param<float> pos_lo {"pos_lo", 0.f, 0.f, 1.f};       // frame the signal's pos window (e.g. its pitch range)
    vivid::Param<float> pos_hi {"pos_hi", 1.f, 0.f, 1.f};       // …stretched across the full layout + palette
    vivid::Param<float> center_x{"center_x", 0.f, -30.f, 30.f}; // offset the whole layout in space (place it off-origin)
    vivid::Param<float> center_y{"center_y", 0.f, -30.f, 30.f};
    vivid::Param<float> center_z{"center_z", 0.f, -30.f, 30.f};
    vivid::Param<int>   orient {"orient", 0, {"spin", "radial"}}; // radial: point each instance's local +Y OUTWARD (spikes/shards)
    vivid::Param<float> elongate{"elongate", 1.f, 1.f, 12.f};     // stretch along the instance's local +Y — spike length (needs orient=radial)

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&size); out.push_back(&radius); out.push_back(&layout);
        out.push_back(&spin); out.push_back(&trail); out.push_back(&pulse); out.push_back(&palette);
        out.push_back(&persist); out.push_back(&pos_lo); out.push_back(&pos_hi);
        out.push_back(&center_x); out.push_back(&center_y); out.push_back(&center_z);
        out.push_back(&orient); out.push_back(&elongate);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("signal", VIVID_PORT_INPUT, VividSignal));   // driven by a Notes node (or any signal)
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_OUTPUT, vivid::gpu::InstanceArray3D));
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        const float* p = ctx->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        const float baseSize = pv(0, size.value);
        const float rad      = pv(1, radius.value);
        const int   lay      = static_cast<int>(std::round(pv(2, static_cast<float>(layout.int_value()))));
        const float spinAmt  = pv(3, spin.value);
        const float maxAge   = 0.04f + 1.4f * pv(4, trail.value);   // fade seconds
        const float kickAmt  = pv(5, pulse.value);
        const int   pal      = static_cast<int>(std::round(pv(6, static_cast<float>(palette.int_value()))));
        const float persistAmt = std::clamp(pv(7, persist.value), 0.f, 1.f);
        const float posLo    = pv(8, pos_lo.value);
        const float posSpan  = pv(9, pos_hi.value) - posLo;
        const float cx       = pv(10, center_x.value);
        const float cy       = pv(11, center_y.value);
        const float cz       = pv(12, center_z.value);
        const int   ori      = static_cast<int>(std::round(pv(13, static_cast<float>(orient.int_value()))));
        const float elong    = std::max(1.f, pv(14, elongate.value));
        const float dt       = static_cast<float>(ctx->delta_time);
        const float t        = static_cast<float>(ctx->time);

        // --- update the aging set from the incoming signal's ACTIVE set (the driving edge) ---
        const VividSignal* sig = vivid::elements::input_signal(ctx, 0);
        for (auto& L : lives_) { L.age += dt; L.kick = std::max(0.f, L.kick - dt * 4.f); }   // ~0.25s pop decay
        if (sig && sig->active) {
            for (uint32_t i = 0; i < sig->active_count; ++i) {
                const VividElement& e = sig->active[i];
                auto it = std::find_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.id == e.id; });
                if (it != lives_.end()) { it->age = 0.f; it->amp = e.amp; it->pos = e.pos; }
                else if (lives_.size() < 256) lives_.push_back({ e.id, e.pos, e.amp, 0.f, 1.f });   // new element pops
            }
        }
        // A note-on FIRE re-pops the matching element (matched by pos) — so a re-struck HELD element,
        // which membership alone can't show, visibly kicks again.
        if (sig && sig->fired) {
            for (uint32_t i = 0; i < sig->fired_count; ++i) {
                const float fp = sig->fired[i].pos;
                auto it = std::find_if(lives_.begin(), lives_.end(), [&](const Live& L){ return std::fabs(L.pos - fp) < 0.006f; });
                if (it != lives_.end()) it->kick = 1.f;
            }
        }
        lives_.erase(std::remove_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.age > maxAge; }), lives_.end());

        // --- build one InstanceData3D per live element ---
        const uint32_t n = static_cast<uint32_t>(std::max<size_t>(1, lives_.size()));
        instances_.resize(n);
        if (lives_.empty()) {
            // keep a single collapsed instance so the bundle is never empty/degenerate
            auto& d = instances_[0]; d = vivid::gpu::InstanceData3D{};
            d.scale[0] = d.scale[1] = d.scale[2] = 0.0001f; d.color[3] = 0.f;
        } else {
            for (size_t i = 0; i < lives_.size(); ++i) {
                const Live& L = lives_[i];
                // `vis` (0..1) drives both size-fade and opacity. persist=0 → linear fade over the whole
                // life (pop-and-vanish, the original behaviour). persist=1 → hold near-full for most of the
                // life then fade only in the final ~15%, so a melodic line accumulates a STANDING shape.
                const float life     = std::clamp(L.age / maxAge, 0.f, 1.f);   // 0 fresh → 1 dead
                const float fadeFrac = 1.f - 0.85f * persistAmt;               // portion of life spent fading
                const float vis      = (life <= 1.f - fadeFrac) ? 1.f
                                     : (fadeFrac > 1e-4f ? std::clamp(1.f - (life - (1.f - fadeFrac)) / fadeFrac, 0.f, 1.f) : 0.f);
                // frame the incoming pos into [pos_lo, pos_hi] → stretch a narrow pitch band across the
                // full layout + palette (default 0..1 leaves it unchanged).
                const float h  = std::clamp(std::fabs(posSpan) > 1e-4f ? (L.pos - posLo) / posSpan : L.pos, 0.f, 1.f);
                const float k  = L.kick * kickAmt;

                float pos[3];
                place(lay, h, rad, t, pos);

                auto& d = instances_[i];
                d.position[0] = pos[0] + cx; d.position[1] = pos[1] + cy; d.position[2] = pos[2] + cz;
                const float sc = baseSize * (0.4f + 0.6f * L.amp) * (1.f + pulse_pop(k)) * (0.15f + 0.85f * vis);
                if (ori == 1) {
                    // radial: aim each instance's local +Y axis OUTWARD from the layout centre, so a Cone/
                    // Pyramid base becomes a spike growing off the core. The instanced shader applies
                    // Ry(rot_y)*Rx(rot_x) to the base +Y axis → (sin(rx)sin(ry), cos(rx), sin(rx)cos(ry)),
                    // so pitch = acos(dir.y), yaw = atan2(dir.x, dir.z) points +Y at `dir`. Elongate is a
                    // LOCAL-space (pre-rotation) stretch on +Y, so the spike stays thin and lengthens.
                    const float len = std::sqrt(pos[0]*pos[0] + pos[1]*pos[1] + pos[2]*pos[2]);
                    const float dx = len > 1e-4f ? pos[0]/len : 0.f;
                    const float dy = len > 1e-4f ? pos[1]/len : 1.f;
                    const float dz = len > 1e-4f ? pos[2]/len : 0.f;
                    d.rotation_x = std::acos(std::clamp(dy, -1.f, 1.f));
                    d.rotation_y = std::atan2(dx, dz);
                    d.scale[0] = d.scale[2] = sc;
                    d.scale[1] = sc * elong;
                } else {
                    d.scale[0] = d.scale[1] = d.scale[2] = sc;
                    d.rotation_y = spinAmt * L.age * 6.2831853f;
                    d.rotation_x = 0.f;
                }
                float c[3]; eval(pal, h, c);
                const float bright = 1.f + 1.4f * k;                 // note-on flash
                d.color[0] = std::clamp(c[0] * bright, 0.f, 1.f);
                d.color[1] = std::clamp(c[1] * bright, 0.f, 1.f);
                d.color[2] = std::clamp(c[2] * bright, 0.f, 1.f);
                d.color[3] = vis;
            }
        }

        bundle_.data  = instances_.data();
        bundle_.count = n;
        ctx->custom_outputs[0] = &bundle_;

        vivid::thumb3d::render_instances_cpu(ctx, thumb_, instances_.data(), n);
    }

    ~InstancesFromSignal() override { vivid::thumb3d::destroy(thumb_); }

private:
    // Op-local aging set (mirrors core-visuals/Instancer): a live persists while its element is present on
    // the input edge (age reset each frame it's in the set) and fades over `trail` seconds after it leaves.
    struct Live { int id; float pos; float amp; float age; float kick; };   // kick = decaying note-on pop
    std::vector<Live> lives_;
    std::vector<vivid::gpu::InstanceData3D> instances_;
    vivid::gpu::InstanceArray3D bundle_{};
    vivid::thumb3d::State thumb_{};

    static float pulse_pop(float k) { return 0.8f * k; }   // spawn/re-strike pop scaled by `pulse`

    // pos (0..1 primary axis) → a 3D position per layout.
    static void place(int lay, float pos, float rad, float t, float out[3]) {
        const float wob = 0.12f * std::sin(t * 1.7f + pos * 60.f);   // gentle vertical float, keeps it alive
        if (lay == 3) {              // wheel: a CAMERA-FACING circle in the XY plane (pos → clock angle)
            const float a = pos * 6.2831853f;
            out[0] = std::cos(a) * rad; out[1] = std::sin(a) * rad; out[2] = wob;
        } else if (lay == 1) {       // line: spread along X
            out[0] = (pos - 0.5f) * 2.f * rad; out[1] = wob; out[2] = 0.f;
        } else if (lay == 2) {       // orb: spherical spiral by pos
            const float y  = (pos - 0.5f) * 2.f * rad;
            const float rr = std::sqrt(std::max(0.f, rad * rad - y * y));
            const float a  = pos * 6.2831853f * 5.f;                 // wind around as pos climbs
            out[0] = std::cos(a) * rr; out[1] = y; out[2] = std::sin(a) * rr;
        } else {                     // ring (default): angle by pos
            const float a = pos * 6.2831853f;
            out[0] = std::cos(a) * rad; out[1] = wob; out[2] = std::sin(a) * rad;
        }
    }

    // Inigo Quilez cosine palette (copied from LanePalette) — color = a + b*cos(2π(c*t + d)).
    static void cosine(float t, const float a[3], const float b[3], const float c[3], const float d[3], float out[3]) {
        for (int k = 0; k < 3; ++k)
            out[k] = std::clamp(a[k] + b[k] * std::cos(6.28318531f * (c[k] * t + d[k])), 0.f, 1.f);
    }
    static void eval(int pal, float t, float out[3]) {
        switch (pal) {
            case 1: { const float a[3]={0.5f,0.5f,0.5f}, b[3]={0.5f,0.5f,0.5f}, c[3]={1,1,1}, d[3]={0.0f,0.33f,0.67f};
                cosine(t, a, b, c, d, out); break; }
            case 2: { const float a[3]={0.5f,0.2f,0.1f}, b[3]={0.5f,0.3f,0.2f}, c[3]={1.0f,1.0f,1.0f}, d[3]={0.0f,0.15f,0.2f};
                cosine(t, a, b, c, d, out); break; }
            case 3: { const float a[3]={0.4f,0.5f,0.7f}, b[3]={0.3f,0.4f,0.3f}, c[3]={1.0f,1.0f,0.5f}, d[3]={0.6f,0.5f,0.4f};
                cosine(t, a, b, c, d, out); break; }
            case 4: { const float a[3]={0.4f,0.5f,0.35f}, b[3]={0.35f,0.45f,0.3f}, c[3]={1.0f,1.0f,1.0f}, d[3]={0.7f,0.5f,0.1f};
                cosine(t, a, b, c, d, out); break; }
            default:{ const float a[3]={0.5f,0.5f,0.5f}, b[3]={0.5f,0.5f,0.5f}, c[3]={1.0f,1.0f,1.0f}, d[3]={0.9f,0.6f,0.3f};
                cosine(t, a, b, c, d, out); break; }
        }
    }
};

VIVID_REGISTER(InstancesFromSignal)
VIVID_THUMBNAIL(InstancesFromSignal)
