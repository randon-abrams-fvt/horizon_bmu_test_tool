#pragma once

#include "CanFrame.h"
#include "CanOpenDefs.h"
#include "PcanBackend.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bmu_app::canopen
{

// Operating mode of the client relative to the bus.
enum class ClientMode
{
    // Active master: the tool can drive NMT, SDO and PDO traffic itself.
    Control,
    // Passive listen-only: the adapter never ACKs or transmits. Used to observe
    // the live conversation between the BMU and the device.
    Monitor
};

const char *to_string(ClientMode m);

// Liveness/heartbeat summary for a single observed CANopen node.
struct NodeInfo
{
    uint8_t node_id{0};
    NmtState state{NmtState::Unknown};
    uint64_t last_heartbeat_us{0};
    uint32_t heartbeat_count{0};
};

enum class SdoStatus
{
    Idle,
    Busy,
    Done,
    Failed,
};

// Result of the most recent SDO transaction (polled by the UI thread).
struct SdoResult
{
    SdoStatus status{SdoStatus::Idle};
    bool is_write{false};
    uint8_t node{0};
    uint16_t index{0};
    uint8_t sub{0};
    std::vector<uint8_t> data; // payload for completed reads
    uint32_t abort_code{0};
    std::string message;
};

// A captured frame plus its direction, for the traffic view.
struct TrafficRecord
{
    CanFrame frame;
    bool tx{false}; // true = transmitted by this tool
};

// Per-COB-ID traffic statistics: how many frames seen and the cycle time
// (period between consecutive frames of the same identifier).
struct CobStats
{
    uint32_t cob_id{0};
    bool tx{false};             // direction of the most recent frame
    uint64_t count{0};          // total frames observed for this COB-ID
    double last_period_ms{0.0}; // gap between the two most recent frames
    double avg_period_ms{0.0};  // exponential moving average of the period
    double min_period_ms{0.0};
    double max_period_ms{0.0};
    uint8_t last_dlc{0};
    std::array<uint8_t, 8> last_data{}; // payload of the most recent frame
};

// CanOpenClient owns one PcanChannel and a worker thread that continuously
// pumps the bus. It is the single point that touches the (non-thread-safe)
// channel; the UI thread interacts only through the thread-safe API below.
class CanOpenClient
{
  public:
    CanOpenClient() = default;
    ~CanOpenClient();

    CanOpenClient(const CanOpenClient &) = delete;
    CanOpenClient &operator=(const CanOpenClient &) = delete;

    // --- Lifecycle -----------------------------------------------------------
    bool connect(uint16_t channel_handle, PcanBitrate bitrate, ClientMode mode);
    void disconnect();
    bool is_connected() const
    {
        return connected_.load();
    }
    ClientMode mode() const
    {
        return mode_;
    }
    const std::string &last_error() const
    {
        return last_error_;
    }

    // --- Active master operations (no-op in Monitor mode) --------------------
    bool send_nmt(NmtCommand cmd, uint8_t node); // node 0 == all nodes
    bool send_frame(const CanFrame &frame);

    // Begins an SDO transfer. Returns false if another transfer is in flight or
    // the client is in Monitor mode. Poll sdo_result() for completion.
    bool sdo_read(uint8_t node, uint16_t index, uint8_t sub);
    bool sdo_write(
        uint8_t node,
        uint16_t index,
        uint8_t sub,
        const uint8_t *data,
        size_t len);
    SdoResult sdo_result() const;

    // Per-step SDO timeout. Lower values make node scanning responsive; the
    // default (1000 ms) suits normal transfers. Clamped to [10, 5000] ms.
    void set_sdo_timeout_ms(uint32_t ms);

    // --- Observed state ------------------------------------------------------
    std::vector<NodeInfo> nodes() const;
    std::optional<CanFrame> last_frame(uint32_t cob_id) const;
    // Returns up to max_records of the most recent traffic (newest last).
    std::vector<TrafficRecord> recent_traffic(size_t max_records) const;
    // Per-COB-ID message count and cycle-time statistics.
    std::vector<CobStats> cob_stats() const;
    void clear_traffic();

  private:
    // Outgoing actions queued by the UI thread, run on the worker thread.
    struct OutAction
    {
        enum class Kind
        {
            Frame,
            SdoStart
        } kind{Kind::Frame};
        CanFrame frame;        // for Kind::Frame
        SdoResult sdo_request; // for Kind::SdoStart (data = bytes to write)
    };

    // In-flight SDO transfer state machine bookkeeping (worker thread only).
    struct SdoTransfer
    {
        bool active{false};
        bool is_write{false};
        uint8_t node{0};
        uint16_t index{0};
        uint8_t sub{0};
        std::vector<uint8_t> buffer; // accumulated read bytes / pending writes
        size_t expected{0};          // declared size, 0 = unknown
        size_t write_offset{0};      // bytes already sent (segmented download)
        bool toggle{false};
        uint64_t deadline_us{0};
    };

    void run();
    void handle_frame(const CanFrame &f, uint64_t now_us);
    void handle_sdo_response(const CanFrame &f, uint64_t now_us);
    void start_sdo(const SdoResult &request, uint64_t now_us);
    void send_sdo_segment_request(uint64_t now_us);
    void finish_sdo(SdoStatus status, uint32_t abort_code, const char *msg);
    void record_traffic(const CanFrame &f, bool tx);
    void record_traffic(const CanFrame &f, bool tx, uint64_t now_us);
    uint64_t now_us() const;

    PcanChannel channel_;
    std::thread worker_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    ClientMode mode_{ClientMode::Monitor};
    std::string last_error_;
    uint64_t start_tick_us_{0};

    std::atomic<uint64_t> sdo_timeout_us_{1'000'000};

    mutable std::mutex out_mutex_;
    std::deque<OutAction> out_queue_;

    mutable std::mutex state_mutex_;
    std::map<uint8_t, NodeInfo> nodes_;
    std::map<uint32_t, CanFrame> last_frames_;
    std::deque<TrafficRecord> traffic_;
    std::map<uint32_t, CobStats> cob_stats_;
    std::map<uint32_t, uint64_t> cob_last_us_;

    mutable std::mutex sdo_mutex_;
    SdoResult sdo_result_;

    SdoTransfer sdo_; // worker-thread only
};

} // namespace bmu_app::canopen
