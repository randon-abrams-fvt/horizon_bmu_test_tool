#include "BmuRuntime.h"
#include "BmuRouter.h"

#include <node_id_helpers.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>

#include <utility>

namespace bmu_app
{
static constexpr uint32_t k_debug_client_node_id = 9999u;

// ---------------------------------------------------------------------------
// RouterImpl — wraps BmuRouter so it has a stable address independent of
// BmuRuntime moves/rebuilds.
// ---------------------------------------------------------------------------
class BmuRuntime::RouterImpl
{
  public:
    BmuRouter router;
};

// ---------------------------------------------------------------------------
// BmuRuntime
// ---------------------------------------------------------------------------

BmuRuntime::BmuRuntime() = default;
BmuRuntime::~BmuRuntime()
{
    stop();
}

bool BmuRuntime::start(const BmuRuntimeConfig &config)
{
    if (is_running())
    {
        last_error_ = "Runtime is already running";
        return false;
    }

    // Build the descriptor-backed codec.
    codec_ = std::make_shared<proto_messages::codec::DynamicMessageCodec>(
        config.yaml_path, config.desc_path);

    if (!codec_->init())
    {
        last_error_ = "Failed to initialize codec: " + codec_->last_error();
        codec_.reset();
        return false;
    }

    router_ = std::make_unique<RouterImpl>();

    client_runtime_ = std::make_unique<message_runtime::ClientRuntime>(
        router_->router, codec_);

    client_runtime_->set_traffic_observer(this);

    message_runtime::ClientRuntimeConfig cli_cfg;
    cli_cfg.host = config.host;
    cli_cfg.port = config.port;
    cli_cfg.node_id = k_debug_client_node_id;

    if (!client_runtime_->start(cli_cfg))
    {
        last_error_ = client_runtime_->last_error();
        client_runtime_.reset();
        router_.reset();
        codec_.reset();
        return false;
    }

    return true;
}

void BmuRuntime::stop()
{
    if (client_runtime_)
    {
        client_runtime_->stop();
        client_runtime_.reset();
    }
    router_.reset();
    codec_.reset();
}

bool BmuRuntime::is_running() const
{
    return client_runtime_ && client_runtime_->is_running();
}

BmuStatusSnapshot BmuRuntime::latest_status() const
{
    std::lock_guard<std::mutex> lock(status_mutex_);
    return latest_status_;
}

std::vector<BmuTrafficEvent> BmuRuntime::drain_traffic_events()
{
    std::lock_guard<std::mutex> lock(traffic_mutex_);
    return std::exchange(pending_traffic_, {});
}

bool BmuRuntime::send_bmu_command(
    uint32_t bmu_node_id, bool hv_connect, bool hvil_closed)
{
    if (!is_running())
    {
        last_error_ = "Runtime is not running";
        return false;
    }

    const std::vector<std::pair<std::string, std::string>> fields = {
        {"s_id", std::to_string(k_debug_client_node_id)},
        {"d_id", std::to_string(bmu_node_id)},
        {"hv_connect", hv_connect ? "true" : "false"},
        {"hvil_closed", hvil_closed ? "true" : "false"},
    };

    message_runtime::DecodedEnvelope envelope;
    if (!build_envelope("bmu.BmuCommand", fields, envelope))
    {
        return false;
    }

    if (!client_runtime_->inject_simulated_message(std::move(envelope)))
    {
        last_error_ = client_runtime_->last_error();
        return false;
    }

    return true;
}

message_runtime::ClientSessionState BmuRuntime::session_state() const
{
    if (!client_runtime_)
    {
        return {};
    }
    return client_runtime_->session_state();
}

// ---------------------------------------------------------------------------
// ITrafficObserver
// ---------------------------------------------------------------------------

void BmuRuntime::on_traffic_event(const message_runtime::TrafficEvent &event)
{
    if (!event.envelope.has_value())
    {
        return;
    }

    const auto &env = *event.envelope;

    // Update the live status snapshot when we receive a BmuStatus.
    if (event.direction == message_runtime::TrafficDirection::rx &&
        env.type_name == "bmu.BmuStatus")
    {
        update_status_from_envelope(env);
    }

    // Buffer a traffic event for the UI thread to drain.
    BmuTrafficEvent te;
    te.is_rx = (event.direction == message_runtime::TrafficDirection::rx);
    te.type_name = env.type_name;
    te.s_id = env.s_id;
    te.d_id = env.d_id;
    if (env.captured_frame.has_value())
    {
        te.raw_bytes = env.captured_frame->raw_bytes;
    }
    else if (env.message)
    {
        // Simulated/application messages have no captured frame — serialize
        // the protobuf message so the traffic view can show a hex payload.
        const size_t sz = env.message->ByteSizeLong();
        te.raw_bytes.resize(sz);
        env.message->SerializeToArray(
            te.raw_bytes.data(), static_cast<int>(sz));
    }

    switch (event.origin)
    {
    case message_runtime::TrafficOrigin::network:
        te.origin = "network";
        break;
    case message_runtime::TrafficOrigin::simulation:
        te.origin = "simulation";
        break;
    case message_runtime::TrafficOrigin::application:
        te.origin = "application";
        break;
    case message_runtime::TrafficOrigin::server:
        te.origin = "server";
        break;
    default:
        te.origin = "unknown";
        break;
    }

    std::lock_guard<std::mutex> lock(traffic_mutex_);
    pending_traffic_.push_back(std::move(te));
}

void BmuRuntime::on_runtime_event(
    const message_runtime::RuntimeEvent & /*event*/)
{
    // Extend later (e.g. push runtime log entries to a panel).
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool BmuRuntime::build_envelope(
    const std::string &type_name,
    const std::vector<std::pair<std::string, std::string>> &fields,
    message_runtime::DecodedEnvelope &out)
{
    if (!codec_)
    {
        last_error_ = "Codec is not initialized";
        return false;
    }

    auto &backend = codec_->backend();

    if (!backend.reset_message(type_name))
    {
        last_error_ = "reset_message failed: " + backend.last_error();
        return false;
    }

    const auto &messages = backend.messages();
    const auto it = messages.find(type_name);
    if (it == messages.end() || !it->second.active_msg)
    {
        last_error_ = "Message type not found in codec: " + type_name;
        return false;
    }

    auto *msg = it->second.active_msg.get();

    for (const auto &[field_name, raw_value] : fields)
    {
        const auto *desc = msg->GetDescriptor();
        if (!desc)
        {
            continue;
        }
        const auto *fd = desc->FindFieldByName(field_name);
        if (!fd)
        {
            continue;
        }

        using FieldDescriptor = google::protobuf::FieldDescriptor;
        auto *refl = msg->GetReflection();

        switch (fd->cpp_type())
        {
        case FieldDescriptor::CPPTYPE_UINT32:
            refl->SetUInt32(
                msg, fd, static_cast<uint32_t>(std::stoul(raw_value)));
            break;
        case FieldDescriptor::CPPTYPE_BOOL:
            refl->SetBool(msg, fd, raw_value == "true" || raw_value == "1");
            break;
        case FieldDescriptor::CPPTYPE_STRING:
            refl->SetString(msg, fd, raw_value);
            break;
        default:
            break;
        }
    }

    out.type_name = type_name;
    out.decode_state = message_runtime::DecodeState::decoded;
    out.msg_id = codec_->compute_msg_id(type_name);
    out.message.reset(msg->New());
    out.message->CopyFrom(*msg);

    const auto src = message_runtime::lookup_source_node_id(*out.message);
    if (src.success && src.value.has_value())
    {
        out.s_id = *src.value;
    }

    const auto dst = message_runtime::lookup_destination_node_id(*out.message);
    if (dst.success && dst.value.has_value())
    {
        out.d_id = *dst.value;
    }

    return true;
}

void BmuRuntime::update_status_from_envelope(
    const message_runtime::DecodedEnvelope &envelope)
{
    if (!envelope.message ||
        envelope.decode_state != message_runtime::DecodeState::decoded)
    {
        return;
    }

    const auto *msg = envelope.message.get();
    const auto *desc = msg->GetDescriptor();
    const auto *refl = msg->GetReflection();
    if (!desc || !refl)
    {
        return;
    }

    auto get_bool = [&](const char *name) -> bool {
        const auto *fd = desc->FindFieldByName(name);
        return fd ? refl->GetBool(*msg, fd) : false;
    };
    auto get_u32 = [&](const char *name) -> uint32_t {
        const auto *fd = desc->FindFieldByName(name);
        return fd ? refl->GetUInt32(*msg, fd) : 0u;
    };

    BmuStatusSnapshot snap;
    snap.received = true;
    snap.s_id = envelope.s_id;
    snap.d_id = envelope.d_id;
    snap.hsm_state = get_u32("hsm_state");

    snap.do0_request = get_bool("do0_request");
    snap.do1_request = get_bool("do1_request");
    snap.do2_request = get_bool("do2_request");
    snap.do3_request = get_bool("do3_request");
    snap.do4_request = get_bool("do4_request");
    snap.do5_request = get_bool("do5_request");
    snap.do6_request = get_bool("do6_request");
    snap.do7_request = get_bool("do7_request");

    snap.do0_status = get_bool("do0_status");
    snap.do1_status = get_bool("do1_status");
    snap.do2_status = get_bool("do2_status");
    snap.do3_status = get_bool("do3_status");
    snap.do4_status = get_bool("do4_status");
    snap.do5_status = get_bool("do5_status");
    snap.do6_status = get_bool("do6_status");
    snap.do7_status = get_bool("do7_status");

    std::lock_guard<std::mutex> lock(status_mutex_);
    latest_status_ = snap;
}

} // namespace bmu_app
