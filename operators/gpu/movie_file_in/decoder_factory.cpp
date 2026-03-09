#include "decoder_factory.h"

#include "avf_decoder.h"
#include "hap_decoder.h"

DecoderRouteDecision decide_decoder_route(const VideoCodecProbeResult& probe,
                                          bool bc_supported) {
    DecoderRouteDecision decision{};
    decision.probe_hap = probe.ok && probe.is_hap;
    decision.probe_notchlc = probe.ok && probe.is_notchlc;
    if (decision.probe_hap && bc_supported) {
        decision.backend = DecoderBackend::HAP;
    } else {
        decision.backend = DecoderBackend::AVF;
        decision.hap_bc_unavailable_fallback = decision.probe_hap && !bc_supported;
    }
    return decision;
}

DecoderLoadResult load_video_decoder_for_path(const std::string& path,
                                              bool bc_supported,
                                              const std::atomic<bool>* cancel_flag) {
    DecoderLoadResult out{};
    if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
        out.diagnostics = "cancelled";
        return out;
    }

#ifdef __APPLE__
    out.probe = probe_video_codec_fourcc(path);
    const DecoderRouteDecision decision = decide_decoder_route(out.probe, bc_supported);

    if (out.probe.ok) {
        out.diagnostics = "codec='" + out.probe.fourcc_text + "'";
    }

    if (decision.backend == DecoderBackend::HAP) {
        auto hap = create_hap_decoder();
        if (hap && hap->open(path)) {
            if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
                out.diagnostics = "cancelled";
                return out;
            }
            hap->play();
            out.success = true;
            out.decoder = std::move(hap);
            out.diagnostics += " route=hap";
            return out;
        }
        if (!out.diagnostics.empty()) out.diagnostics += " ";
        out.diagnostics += "hap_open_failed";
    } else if (decision.hap_bc_unavailable_fallback) {
        if (!out.diagnostics.empty()) out.diagnostics += " ";
        out.diagnostics += "hap_bc_unavailable_fallback";
    }

    auto avf = create_avf_decoder();
    if (avf && avf->open(path)) {
        if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
            out.diagnostics = "cancelled";
            return out;
        }
        avf->play();
        out.success = true;
        out.decoder = std::move(avf);
        if (!out.diagnostics.empty()) out.diagnostics += " ";
        out.diagnostics += "route=avf";
        return out;
    }

    if (!out.diagnostics.empty()) out.diagnostics += " ";
    out.diagnostics += "avf_open_failed";
    if (decision.probe_notchlc) {
        out.diagnostics += " notchlc_decoder_unavailable";
    }
    return out;
#else
    (void)path;
    (void)bc_supported;
    (void)cancel_flag;
    out.diagnostics = "video_unsupported_platform";
    return out;
#endif
}
