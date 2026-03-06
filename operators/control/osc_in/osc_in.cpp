#include "operator_api/operator.h"

#include "ip/UdpSocket.h"
#include "osc/OscPacketListener.h"
#include "osc/OscReceivedElements.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct OscIn : vivid::OperatorBase {
    static constexpr const char* kName   = "OscIn";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> listen_port{"listen_port", 9000, 1, 65535};
    vivid::Param<vivid::FilePath> address{"address", "/vivid/value"};
    vivid::Param<bool> strict_address{"strict_address", true};

    OscIn() {
        vivid::semantic_tag(listen_port, "x_network_port");
        vivid::semantic_shape(listen_port, "int");

        vivid::semantic_tag(address, "x_osc_address");
        vivid::semantic_shape(address, "string");

        vivid::semantic_tag(strict_address, "enabled");
        vivid::semantic_shape(strict_address, "bool");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&listen_port);
        out.push_back(&address);
        out.push_back(&strict_address);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"trigger", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"connected", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"type", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    ~OscIn() override {
        stop_listener();
    }

    void process(const VividProcessContext* ctx) override {
        ensure_listener();

        float out_value = last_value_;
        float out_trigger = 0.0f;
        float out_type = last_type_;

        const uint64_t seq = seq_.load(std::memory_order_acquire);
        if (seq != consumed_seq_) {
            std::lock_guard<std::mutex> lock(value_mutex_);
            out_value = latest_value_;
            out_type = latest_type_;
            consumed_seq_ = seq;
            last_value_ = out_value;
            last_type_ = out_type;
            out_trigger = 1.0f;
        }

        ctx->output_values[0] = out_value;
        ctx->output_values[1] = out_trigger;
        ctx->output_values[2] = listener_ok_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        ctx->output_values[3] = out_type;
    }

private:
    class Listener final : public osc::OscPacketListener {
    public:
        explicit Listener(OscIn* owner)
            : owner_(owner) {}

    protected:
        void ProcessMessage(const osc::ReceivedMessage& message,
                            const IpEndpointName& /*remote*/) override {
            owner_->handle_message(message);
        }

    private:
        OscIn* owner_ = nullptr;
    };

    std::unique_ptr<Listener> listener_;
    std::unique_ptr<UdpListeningReceiveSocket> socket_;
    std::thread listen_thread_;

    int current_port_ = -1;
    std::string current_address_;
    bool current_strict_ = true;

    std::mutex value_mutex_;
    float latest_value_ = 0.0f;
    float latest_type_ = 0.0f;
    uint64_t consumed_seq_ = 0;

    std::atomic<uint64_t> seq_{0};
    std::atomic<bool> listener_ok_{false};

    float last_value_ = 0.0f;
    float last_type_ = 0.0f;

    void ensure_listener() {
        const int requested_port = listen_port.int_value();
        const std::string requested_address = normalized_address(address.str_value);
        const bool requested_strict = strict_address.bool_value();

        if (socket_ && requested_port == current_port_ && requested_address == current_address_
            && requested_strict == current_strict_) {
            return;
        }

        stop_listener();

        try {
            listener_ = std::make_unique<Listener>(this);
            socket_ = std::make_unique<UdpListeningReceiveSocket>(
                IpEndpointName(IpEndpointName::ANY_ADDRESS, requested_port), listener_.get());

            current_port_ = requested_port;
            current_address_ = requested_address;
            current_strict_ = requested_strict;

            listen_thread_ = std::thread([this]() {
                try {
                    socket_->Run();
                } catch (...) {
                }
                listener_ok_.store(false, std::memory_order_release);
            });

            listener_ok_.store(true, std::memory_order_release);
            std::fprintf(stderr, "[OscIn] Listening on UDP %d (%s, strict=%s)\n",
                         current_port_, current_address_.c_str(), current_strict_ ? "true" : "false");
        } catch (...) {
            socket_.reset();
            listener_.reset();
            current_port_ = -1;
            current_address_.clear();
            listener_ok_.store(false, std::memory_order_release);
            std::fprintf(stderr, "[OscIn] Failed to start listener on UDP %d\n", requested_port);
        }
    }

    void stop_listener() {
        listener_ok_.store(false, std::memory_order_release);

        if (socket_) {
            socket_->AsynchronousBreak();
        }
        if (listen_thread_.joinable()) {
            listen_thread_.join();
        }

        socket_.reset();
        listener_.reset();
        current_port_ = -1;
        current_address_.clear();
    }

    static std::string normalized_address(const std::string& in) {
        if (in.empty()) return "/vivid/value";
        if (in.front() == '/') return in;
        return "/" + in;
    }

    void handle_message(const osc::ReceivedMessage& message) {
        const char* addr = message.AddressPattern();
        if (current_strict_) {
            if (!addr || current_address_ != addr) return;
        } else {
            if (!addr) return;
            const std::string got(addr);
            if (got.rfind(current_address_, 0) != 0) return;
        }

        float value = 0.0f;
        float type = 0.0f;

        try {
            if (message.ArgumentCount() == 0) {
                value = 1.0f;
                type = 5.0f; // empty message/ping
            } else {
                auto it = message.ArgumentsBegin();
                const auto& arg = *it;
                if (arg.IsFloat()) {
                    value = arg.AsFloatUnchecked();
                    type = 1.0f;
                } else if (arg.IsDouble()) {
                    value = static_cast<float>(arg.AsDoubleUnchecked());
                    type = 1.0f;
                } else if (arg.IsInt32()) {
                    value = static_cast<float>(arg.AsInt32Unchecked());
                    type = 2.0f;
                } else if (arg.IsInt64()) {
                    value = static_cast<float>(arg.AsInt64Unchecked());
                    type = 2.0f;
                } else if (arg.IsBool()) {
                    value = arg.AsBoolUnchecked() ? 1.0f : 0.0f;
                    type = 3.0f;
                } else if (arg.IsString()) {
                    type = 4.0f;
                } else {
                    return;
                }
            }
        } catch (...) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(value_mutex_);
            latest_value_ = value;
            latest_type_ = type;
        }
        seq_.fetch_add(1, std::memory_order_release);
    }
};

VIVID_REGISTER(OscIn)
