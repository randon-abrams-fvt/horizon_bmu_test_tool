#pragma once

#include <client_runtime.h>
#include <decoded_envelope.h>
#include <traffic_observer.h>

#include <dynamic_message_codec.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace bmu_app
{

// Snapshot of the most recently received BmuStatus fields.
struct BmuStatusSnapshot
{
    bool received{false};

    uint32_t hsm_state{0};

    bool do0_request{false};
    bool do1_request{false};
    bool do2_request{false};
    bool do3_request{false};
    bool do4_request{false};
    bool do5_request{false};
    bool do6_request{false};
    bool do7_request{false};

    bool do0_status{false};
    bool do1_status{false};
    bool do2_status{false};
    bool do3_status{false};
    bool do4_status{false};
    bool do5_status{false};
    bool do6_status{false};
    bool do7_status{false};

    std::optional<uint32_t> s_id;
    std::optional<uint32_t> d_id;
};

// A traffic event safe to pass to the UI thread.
struct BmuTrafficEvent
{
    bool is_rx{true}; // true = received from BMU, false = sent to BMU
    std::string type_name;
    std::optional<uint32_t> s_id;
    std::optional<uint32_t> d_id;
    std::string origin; // "network", "simulation", etc.
    std::vector<uint8_t> raw_bytes;
};

struct BmuRuntimeConfig
{
    std::string host{"127.0.0.1"};
    uint16_t port{1111};
    std::string yaml_path; // path to bmu_messages.yaml
    std::string desc_path; // path to bmu_messages.desc
};

// BmuRuntime owns the message_runtime::ClientRuntime and the dynamic codec.
// It implements ITrafficObserver to capture inbound BmuStatus frames.
// The UI thread calls drain_*() once per render frame.
class BmuRuntime : public message_runtime::ITrafficObserver
{
  public:
    BmuRuntime();
    ~BmuRuntime() override;

    BmuRuntime(const BmuRuntime &) = delete;
    BmuRuntime &operator=(const BmuRuntime &) = delete;

    // --- Lifecycle -----------------------------------------------------------
    bool start(const BmuRuntimeConfig &config);
    void stop();
    bool is_running() const;

    const std::string &last_error() const
    {
        return last_error_;
    }

    // --- UI thread accessors -------------------------------------------------

    // Returns a copy of the latest BmuStatus (mutex-protected).
    BmuStatusSnapshot latest_status() const;

    // Drains buffered traffic events since the last call.
    std::vector<BmuTrafficEvent> drain_traffic_events();

    // Builds and injects a BmuCommand with the supplied field values.
    // s_id = 9999 debug client, d_id = bmu_node_id.
    bool send_bmu_command(
        uint32_t bmu_node_id, bool hv_connect, bool hvil_closed);

    // Returns the current client session state.
    message_runtime::ClientSessionState session_state() const;

    // --- ITrafficObserver ----------------------------------------------------
    void on_traffic_event(const message_runtime::TrafficEvent &event) override;
    void on_runtime_event(const message_runtime::RuntimeEvent &event) override;

  private:
    bool build_envelope(
        const std::string &type_name,
        const std::vector<std::pair<std::string, std::string>> &fields,
        message_runtime::DecodedEnvelope &out_envelope);

    void update_status_from_envelope(
        const message_runtime::DecodedEnvelope &envelope);

    std::unique_ptr<message_runtime::ClientRuntime> client_runtime_;
    std::shared_ptr<proto_messages::codec::DynamicMessageCodec> codec_;

    // Owned separately so the Router and Observer have stable addresses.
    class RouterImpl;
    std::unique_ptr<RouterImpl> router_;

    mutable std::mutex status_mutex_;
    BmuStatusSnapshot latest_status_;

    mutable std::mutex traffic_mutex_;
    std::vector<BmuTrafficEvent> pending_traffic_;

    std::string last_error_;
};

} // namespace bmu_app
