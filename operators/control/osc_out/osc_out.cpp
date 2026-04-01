#include "operator_api/operator.h"

#include "ip/IpEndpointName.h"
#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
/**
 * @brief OSC client sending values over UDP.
 *
 * Sends OSC messages to a target host and port. Configurable to send
 * on trigger, every frame, or on value change.
 *
 * @param send_mode When to transmit: on_trigger, every_frame, or on_change.
 * @see OscIn, MidiInput
 */
struct OscOut : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "OscOut";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> host{"host", "127.0.0.1"};
    vivid::Param<int> target_port{"target_port", 9000, 1, 65535};
    vivid::Param<vivid::FilePath> address{"address", "/vivid/value"};
    vivid::Param<int> value_type{"value_type", 0, {"float", "int", "bool"}};
    vivid::Param<int> send_mode{"send_mode", 0, {"on_trigger", "every_frame", "on_change"}};

    OscOut() {
        vivid::description(host, "Target hostname or IP address");
        vivid::description(target_port, "UDP port on the target host");
        vivid::description(address, "OSC address to send to (e.g. /vivid/value)");
        vivid::description(value_type, "Data type sent in the OSC message: float, int, or bool");
        vivid::description(send_mode, "When to transmit: on trigger, every frame, or on value change");

        vivid::semantic_tag(host, "x_network_host");
        vivid::semantic_shape(host, "string");

        vivid::semantic_tag(target_port, "x_network_port");
        vivid::semantic_shape(target_port, "int");

        vivid::semantic_tag(address, "x_osc_address");
        vivid::semantic_shape(address, "string");

        vivid::semantic_tag(value_type, "x_osc_value_type");
        vivid::semantic_shape(value_type, "enum");

        vivid::semantic_tag(send_mode, "x_osc_send_mode");
        vivid::semantic_shape(send_mode, "enum");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&host);
        out.push_back(&target_port);
        out.push_back(&address);
        out.push_back(&value_type);
        out.push_back(&send_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"sent", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"connected", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const std::string h = host.str_value.empty() ? "127.0.0.1" : host.str_value;
        const int p = target_port.int_value();
        const std::string a = normalized_address(address.str_value);

        ensure_socket(h, p);

        const bool trig = ctx->input_values[0] > 0.5f;
        const bool rising = trig && !prev_trigger_;
        prev_trigger_ = trig;

        const float in_value = ctx->input_values[1];

        bool should_send = false;
        switch (send_mode.int_value()) {
            case 0: should_send = rising; break;
            case 1: should_send = true; break;
            case 2: should_send = !has_last_sent_ || std::fabs(in_value - last_sent_value_) > 1e-6f; break;
            default: should_send = rising; break;
        }

        bool sent = false;
        if (socket_ && should_send) {
            sent = send_message(a, in_value);
            if (sent) {
                has_last_sent_ = true;
                last_sent_value_ = in_value;
            }
        }

        ctx->output_values[0] = sent ? 1.0f : 0.0f;
        ctx->output_values[1] = socket_ ? 1.0f : 0.0f;
    }

private:
    std::unique_ptr<UdpTransmitSocket> socket_;
    std::string current_host_;
    int current_port_ = -1;

    bool prev_trigger_ = false;
    bool has_last_sent_ = false;
    float last_sent_value_ = 0.0f;

    void ensure_socket(const std::string& host_name, int port) {
        if (socket_ && host_name == current_host_ && port == current_port_) return;

        socket_.reset();
        current_host_.clear();
        current_port_ = -1;

        try {
            socket_ = std::make_unique<UdpTransmitSocket>(IpEndpointName(host_name.c_str(), port));
            current_host_ = host_name;
            current_port_ = port;
            std::fprintf(stderr, "[OscOut] Ready for UDP %s:%d\n", current_host_.c_str(), current_port_);
        } catch (...) {
            socket_.reset();
            std::fprintf(stderr, "[OscOut] Failed to create socket for %s:%d\n", host_name.c_str(), port);
        }
    }

    bool send_message(const std::string& addr, float value) {
        if (!socket_) return false;

        char buffer[1024];
        osc::OutboundPacketStream p(buffer, sizeof(buffer));

        try {
            p << osc::BeginMessage(addr.c_str());
            switch (value_type.int_value()) {
                case 0:
                    p << value;
                    break;
                case 1:
                    p << static_cast<osc::int32>(value);
                    break;
                case 2:
                    p << (value > 0.5f);
                    break;
                default:
                    p << value;
                    break;
            }
            p << osc::EndMessage;
            socket_->Send(p.Data(), static_cast<int>(p.Size()));
            return true;
        } catch (...) {
            return false;
        }
    }

    static std::string normalized_address(const std::string& in) {
        if (in.empty()) return "/vivid/value";
        if (in.front() == '/') return in;
        return "/" + in;
    }
};

VIVID_REGISTER(OscOut)
