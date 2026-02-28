// Audio test operator that throws an exception on first process() call.
// Used to test audio thread exception handling.
#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include <stdexcept>

struct AudioThrowingOp : vivid::OperatorBase {
    static constexpr const char* kName   = "AudioThrowingOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> level{"level", 0.5f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&level);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        throw std::runtime_error("AudioThrowingOp: intentional test exception");
    }
};

VIVID_REGISTER(AudioThrowingOp)
