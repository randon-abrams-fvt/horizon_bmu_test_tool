#include "CanOpenClient.h"

#include <chrono>

namespace bmu_app::canopen
{

namespace
{
constexpr size_t kMaxTraffic = 2000;
} // namespace

const char *to_string(ClientMode m)
{
    switch (m)
    {
    case ClientMode::Monitor:
        return "Monitor (passive)";
    case ClientMode::Control:
        return "Control (master)";
    }
    return "?";
}

CanOpenClient::~CanOpenClient()
{
    disconnect();
}

uint64_t CanOpenClient::now_us() const
{
    const auto t = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t).count());
}

bool CanOpenClient::connect(
    uint16_t channel_handle, PcanBitrate bitrate, ClientMode mode)
{
    disconnect();

    mode_ = mode;
    const bool listen_only = (mode == ClientMode::Monitor);
    if (!channel_.open(channel_handle, bitrate, listen_only))
    {
        last_error_ = channel_.last_error();
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        nodes_.clear();
        last_frames_.clear();
        traffic_.clear();
        cob_stats_.clear();
        cob_last_us_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(sdo_mutex_);
        sdo_result_ = SdoResult{};
    }
    {
        std::lock_guard<std::mutex> lk(out_mutex_);
        out_queue_.clear();
    }
    sdo_ = SdoTransfer{};

    start_tick_us_ = now_us();
    running_.store(true);
    connected_.store(true);
    last_error_.clear();
    worker_ = std::thread(&CanOpenClient::run, this);
    return true;
}

void CanOpenClient::disconnect()
{
    running_.store(false);
    if (worker_.joinable())
    {
        worker_.join();
    }
    channel_.close();
    connected_.store(false);
}

// ── Worker thread
// ─────────────────────────────────────────────────────────────

void CanOpenClient::run()
{
    CanFrame frame;
    while (running_.load())
    {
        bool did_work = false;

        // Drain any received frames.
        while (channel_.read(frame))
        {
            handle_frame(frame, now_us());
            did_work = true;
        }

        // Execute queued outgoing actions (Control mode only).
        std::deque<OutAction> pending;
        {
            std::lock_guard<std::mutex> lk(out_mutex_);
            pending.swap(out_queue_);
        }
        for (auto &action : pending)
        {
            if (action.kind == OutAction::Kind::Frame)
            {
                if (channel_.write(action.frame))
                {
                    record_traffic(action.frame, /*tx=*/true);
                }
            }
            else // SdoStart
            {
                start_sdo(action.sdo_request, now_us());
            }
            did_work = true;
        }

        // SDO timeout supervision.
        if (sdo_.active && now_us() > sdo_.deadline_us)
        {
            finish_sdo(SdoStatus::Failed, 0x05040000, "SDO timeout");
        }

        if (!did_work)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
}

void CanOpenClient::handle_frame(const CanFrame &f, uint64_t now)
{
    record_traffic(f, /*tx=*/false);

    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        last_frames_[f.id] = f;
    }

    const FunctionCode fc = function_code(f.id);
    const uint8_t node = node_of(f.id);

    if (fc == FunctionCode::Heartbeat && f.dlc >= 1)
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        NodeInfo &info = nodes_[node];
        info.node_id = node;
        info.state = static_cast<NmtState>(f.data[0]);
        info.last_heartbeat_us = now;
        info.heartbeat_count++;
    }
    else if (fc == FunctionCode::SdoTx && sdo_.active && node == sdo_.node)
    {
        handle_sdo_response(f, now);
    }
}

// ── SDO client state machine
// ──────────────────────────────────────────────────

void CanOpenClient::start_sdo(const SdoResult &request, uint64_t now)
{
    if (sdo_.active)
    {
        return; // should not happen: guarded by sdo_busy at submission
    }

    sdo_ = SdoTransfer{};
    sdo_.active = true;
    sdo_.is_write = request.is_write;
    sdo_.node = request.node;
    sdo_.index = request.index;
    sdo_.sub = request.sub;
    sdo_.toggle = false;
    sdo_.deadline_us = now + sdo_timeout_us_.load();

    CanFrame req;
    req.id = kSdoRxBase + request.node;
    req.dlc = 8;
    req.data = {};

    if (request.is_write)
    {
        const std::vector<uint8_t> &bytes = request.data;
        if (bytes.size() <= 4)
        {
            // Expedited download.
            const uint8_t n = static_cast<uint8_t>(4 - bytes.size());
            req.data[0] = sdo::kCcsDownload | (n << 2) | sdo::kExpedited |
                          sdo::kSizeIndicated;
            req.data[1] = static_cast<uint8_t>(request.index & 0xFF);
            req.data[2] = static_cast<uint8_t>(request.index >> 8);
            req.data[3] = request.sub;
            for (size_t i = 0; i < bytes.size(); ++i)
            {
                req.data[4 + i] = bytes[i];
            }
        }
        else
        {
            // Segmented download: initiate with total size, then send segments.
            sdo_.buffer = bytes;
            sdo_.expected = bytes.size();
            sdo_.write_offset = 0;
            req.data[0] = sdo::kCcsDownload | sdo::kSizeIndicated;
            req.data[1] = static_cast<uint8_t>(request.index & 0xFF);
            req.data[2] = static_cast<uint8_t>(request.index >> 8);
            req.data[3] = request.sub;
            const uint32_t total = static_cast<uint32_t>(bytes.size());
            req.data[4] = static_cast<uint8_t>(total & 0xFF);
            req.data[5] = static_cast<uint8_t>((total >> 8) & 0xFF);
            req.data[6] = static_cast<uint8_t>((total >> 16) & 0xFF);
            req.data[7] = static_cast<uint8_t>((total >> 24) & 0xFF);
        }
    }
    else
    {
        // Initiate upload (read).
        req.data[0] = sdo::kCcsUpload;
        req.data[1] = static_cast<uint8_t>(request.index & 0xFF);
        req.data[2] = static_cast<uint8_t>(request.index >> 8);
        req.data[3] = request.sub;
    }

    if (channel_.write(req))
    {
        record_traffic(req, /*tx=*/true);
    }
    else
    {
        finish_sdo(SdoStatus::Failed, 0, channel_.last_error().c_str());
    }
}

void CanOpenClient::send_sdo_segment_request(uint64_t now)
{
    CanFrame req;
    req.id = kSdoRxBase + sdo_.node;
    req.dlc = 8;
    req.data = {};

    if (sdo_.is_write)
    {
        // Build the next download segment from buffer/write_offset.
        const size_t remaining = sdo_.buffer.size() - sdo_.write_offset;
        const size_t chunk = remaining < 7 ? remaining : 7;
        const uint8_t n = static_cast<uint8_t>(7 - chunk);
        const bool last = (remaining <= 7);
        uint8_t cmd = sdo::kCcsDownloadSegment | (n << 1);
        if (sdo_.toggle)
        {
            cmd |= 0x10;
        }
        if (last)
        {
            cmd |= 0x01;
        }
        req.data[0] = cmd;
        for (size_t i = 0; i < chunk; ++i)
        {
            req.data[1 + i] = sdo_.buffer[sdo_.write_offset + i];
        }
        sdo_.write_offset += chunk;
    }
    else
    {
        // Request the next upload segment.
        uint8_t cmd = sdo::kCcsUploadSegment;
        if (sdo_.toggle)
        {
            cmd |= 0x10;
        }
        req.data[0] = cmd;
    }

    sdo_.deadline_us = now + sdo_timeout_us_.load();
    if (channel_.write(req))
    {
        record_traffic(req, /*tx=*/true);
    }
    else
    {
        finish_sdo(SdoStatus::Failed, 0, channel_.last_error().c_str());
    }
}

void CanOpenClient::handle_sdo_response(const CanFrame &f, uint64_t now)
{
    const uint8_t cmd = f.data[0];

    // Abort from server.
    if (cmd == sdo::kAbort)
    {
        const uint32_t abort = static_cast<uint32_t>(f.data[4]) |
                               (static_cast<uint32_t>(f.data[5]) << 8) |
                               (static_cast<uint32_t>(f.data[6]) << 16) |
                               (static_cast<uint32_t>(f.data[7]) << 24);
        finish_sdo(SdoStatus::Failed, abort, sdo_abort_text(abort));
        return;
    }

    if (sdo_.is_write)
    {
        const uint8_t scs = cmd & 0xE0;
        if (scs == sdo::kScsDownloadResp)
        {
            // Expedited or initiate-segmented response.
            if (!sdo_.buffer.empty() && sdo_.write_offset < sdo_.buffer.size())
            {
                // Segmented download: send first/next segment.
                sdo_.toggle = false;
                send_sdo_segment_request(now);
                return;
            }
            finish_sdo(SdoStatus::Done, 0, "Write OK");
            return;
        }
        if (scs == sdo::kScsDownloadSegment)
        {
            if (sdo_.write_offset >= sdo_.buffer.size())
            {
                finish_sdo(SdoStatus::Done, 0, "Write OK");
                return;
            }
            sdo_.toggle = !sdo_.toggle;
            send_sdo_segment_request(now);
            return;
        }
        finish_sdo(SdoStatus::Failed, 0, "Unexpected SDO write response");
        return;
    }

    // Read path.
    const uint8_t scs = cmd & 0xE0;
    if (scs == sdo::kScsUploadResp)
    {
        if (cmd & sdo::kExpedited)
        {
            size_t len = 4;
            if (cmd & sdo::kSizeIndicated)
            {
                len = 4 - ((cmd >> 2) & 0x03);
            }
            sdo_.buffer.assign(f.data.begin() + 4, f.data.begin() + 4 + len);
            finish_sdo(SdoStatus::Done, 0, "Read OK");
            return;
        }
        // Segmented upload: bytes 4..7 hold total size when size-indicated.
        if (cmd & sdo::kSizeIndicated)
        {
            sdo_.expected = static_cast<uint32_t>(f.data[4]) |
                            (static_cast<uint32_t>(f.data[5]) << 8) |
                            (static_cast<uint32_t>(f.data[6]) << 16) |
                            (static_cast<uint32_t>(f.data[7]) << 24);
        }
        sdo_.buffer.clear();
        sdo_.toggle = false;
        send_sdo_segment_request(now);
        return;
    }
    if (scs == sdo::kScsUploadSegment)
    {
        const uint8_t n = (cmd >> 1) & 0x07;
        const size_t count = 7 - n;
        for (size_t i = 0; i < count; ++i)
        {
            sdo_.buffer.push_back(f.data[1 + i]);
        }
        const bool last = (cmd & 0x01) != 0;
        if (last)
        {
            finish_sdo(SdoStatus::Done, 0, "Read OK");
            return;
        }
        sdo_.toggle = !sdo_.toggle;
        send_sdo_segment_request(now);
        return;
    }

    finish_sdo(SdoStatus::Failed, 0, "Unexpected SDO read response");
}

void CanOpenClient::finish_sdo(
    SdoStatus status, uint32_t abort_code, const char *msg)
{
    SdoResult result;
    result.status = status;
    result.is_write = sdo_.is_write;
    result.node = sdo_.node;
    result.index = sdo_.index;
    result.sub = sdo_.sub;
    result.abort_code = abort_code;
    result.message = msg ? msg : "";
    if (status == SdoStatus::Done && !sdo_.is_write)
    {
        result.data = sdo_.buffer;
    }

    {
        std::lock_guard<std::mutex> lk(sdo_mutex_);
        sdo_result_ = std::move(result);
    }
    sdo_ = SdoTransfer{};
}

// ── Public master operations
// ──────────────────────────────────────────────────

bool CanOpenClient::send_nmt(NmtCommand cmd, uint8_t node)
{
    if (!is_connected() || mode_ != ClientMode::Control)
    {
        return false;
    }
    CanFrame f;
    f.id = kNmtCobId;
    f.dlc = 2;
    f.data[0] = static_cast<uint8_t>(cmd);
    f.data[1] = node; // 0 = all nodes
    std::lock_guard<std::mutex> lk(out_mutex_);
    out_queue_.push_back(OutAction{OutAction::Kind::Frame, f, {}});
    return true;
}

bool CanOpenClient::send_frame(const CanFrame &frame)
{
    if (!is_connected() || mode_ != ClientMode::Control)
    {
        return false;
    }
    std::lock_guard<std::mutex> lk(out_mutex_);
    out_queue_.push_back(OutAction{OutAction::Kind::Frame, frame, {}});
    return true;
}

bool CanOpenClient::sdo_read(uint8_t node, uint16_t index, uint8_t sub)
{
    if (!is_connected() || mode_ != ClientMode::Control)
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(sdo_mutex_);
        if (sdo_result_.status == SdoStatus::Busy)
        {
            return false;
        }
        sdo_result_ = SdoResult{};
        sdo_result_.status = SdoStatus::Busy;
        sdo_result_.is_write = false;
        sdo_result_.node = node;
        sdo_result_.index = index;
        sdo_result_.sub = sub;
    }

    SdoResult req;
    req.is_write = false;
    req.node = node;
    req.index = index;
    req.sub = sub;
    std::lock_guard<std::mutex> lk(out_mutex_);
    out_queue_.push_back(OutAction{OutAction::Kind::SdoStart, {}, req});
    return true;
}

bool CanOpenClient::sdo_write(
    uint8_t node, uint16_t index, uint8_t sub, const uint8_t *data, size_t len)
{
    if (!is_connected() || mode_ != ClientMode::Control)
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(sdo_mutex_);
        if (sdo_result_.status == SdoStatus::Busy)
        {
            return false;
        }
        sdo_result_ = SdoResult{};
        sdo_result_.status = SdoStatus::Busy;
        sdo_result_.is_write = true;
        sdo_result_.node = node;
        sdo_result_.index = index;
        sdo_result_.sub = sub;
    }

    SdoResult req;
    req.is_write = true;
    req.node = node;
    req.index = index;
    req.sub = sub;
    req.data.assign(data, data + len);
    std::lock_guard<std::mutex> lk(out_mutex_);
    out_queue_.push_back(OutAction{OutAction::Kind::SdoStart, {}, req});
    return true;
}

SdoResult CanOpenClient::sdo_result() const
{
    std::lock_guard<std::mutex> lk(sdo_mutex_);
    return sdo_result_;
}

void CanOpenClient::set_sdo_timeout_ms(uint32_t ms)
{
    if (ms < 10)
    {
        ms = 10;
    }
    if (ms > 5000)
    {
        ms = 5000;
    }
    sdo_timeout_us_.store(static_cast<uint64_t>(ms) * 1000ull);
}

// ── Observed state accessors
// ──────────────────────────────────────────────────

std::vector<NodeInfo> CanOpenClient::nodes() const
{
    std::lock_guard<std::mutex> lk(state_mutex_);
    std::vector<NodeInfo> out;
    out.reserve(nodes_.size());
    for (const auto &[id, info] : nodes_)
    {
        out.push_back(info);
    }
    return out;
}

std::optional<CanFrame> CanOpenClient::last_frame(uint32_t cob_id) const
{
    std::lock_guard<std::mutex> lk(state_mutex_);
    auto it = last_frames_.find(cob_id);
    if (it == last_frames_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::vector<TrafficRecord> CanOpenClient::recent_traffic(
    size_t max_records) const
{
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (traffic_.size() <= max_records)
    {
        return {traffic_.begin(), traffic_.end()};
    }
    return {traffic_.end() - static_cast<long>(max_records), traffic_.end()};
}

void CanOpenClient::clear_traffic()
{
    std::lock_guard<std::mutex> lk(state_mutex_);
    traffic_.clear();
    cob_stats_.clear();
    cob_last_us_.clear();
}

std::vector<CobStats> CanOpenClient::cob_stats() const
{
    std::lock_guard<std::mutex> lk(state_mutex_);
    std::vector<CobStats> out;
    out.reserve(cob_stats_.size());
    for (const auto &[id, stats] : cob_stats_)
    {
        out.push_back(stats);
    }
    return out;
}

void CanOpenClient::record_traffic(const CanFrame &f, bool tx)
{
    record_traffic(f, tx, now_us());
}

void CanOpenClient::record_traffic(const CanFrame &f, bool tx, uint64_t now)
{
    std::lock_guard<std::mutex> lk(state_mutex_);
    traffic_.push_back(TrafficRecord{f, tx});
    while (traffic_.size() > kMaxTraffic)
    {
        traffic_.pop_front();
    }

    CobStats &stats = cob_stats_[f.id];
    stats.cob_id = f.id;
    stats.tx = tx;
    stats.last_dlc = f.dlc;
    stats.last_data = f.data;
    stats.count++;

    auto last_it = cob_last_us_.find(f.id);
    if (last_it != cob_last_us_.end())
    {
        const double period_ms =
            static_cast<double>(now - last_it->second) / 1000.0;
        stats.last_period_ms = period_ms;
        if (stats.count == 2)
        {
            stats.avg_period_ms = period_ms;
            stats.min_period_ms = period_ms;
            stats.max_period_ms = period_ms;
        }
        else
        {
            // Exponential moving average smooths jitter while staying
            // responsive.
            constexpr double kAlpha = 0.2;
            stats.avg_period_ms =
                (1.0 - kAlpha) * stats.avg_period_ms + kAlpha * period_ms;
            if (period_ms < stats.min_period_ms)
                stats.min_period_ms = period_ms;
            if (period_ms > stats.max_period_ms)
                stats.max_period_ms = period_ms;
        }
    }
    cob_last_us_[f.id] = now;
}

} // namespace bmu_app::canopen
