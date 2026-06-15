#include "Ttc2038XsDevice.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>

namespace bmu_app::canopen::ttc2038xs
{

const char *to_string(ParamGroup g)
{
    switch (g)
    {
    case ParamGroup::Identity:
        return "Identity";
    case ParamGroup::Communication:
        return "Communication";
    case ParamGroup::DigitalIo:
        return "Digital I/O";
    case ParamGroup::AnalogIo:
        return "Analog I/O";
    case ParamGroup::Safety:
        return "Safety";
    }
    return "?";
}

const std::vector<ParamDef> &parameter_table()
{
    // Curated from Ttc2038Xs.eds. Order is stable; the index is the handle the
    // UI uses to read/write a parameter.
    static const std::vector<ParamDef> kTable = {
        // -- Identity (read-only) --------------------------------------------
        {"Device Type",
         0x1000,
         0,
         ParamType::U32,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Device Name",
         0x1008,
         0,
         ParamType::String,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Hardware Version",
         0x1009,
         0,
         ParamType::String,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Software Version",
         0x100A,
         0,
         ParamType::String,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Vendor ID",
         0x1018,
         1,
         ParamType::U32,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Product Code",
         0x1018,
         2,
         ParamType::U32,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Revision Number",
         0x1018,
         3,
         ParamType::U32,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Serial Number",
         0x1018,
         4,
         ParamType::U32,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},
        {"Error Register",
         0x1001,
         0,
         ParamType::U8,
         ParamAccess::ReadOnly,
         "",
         ParamGroup::Identity},

        // -- Communication ---------------------------------------------------
        {"Producer Heartbeat Time",
         0x1017,
         0,
         ParamType::U16,
         ParamAccess::ReadWrite,
         "ms",
         ParamGroup::Communication},
        {"Consumer Heartbeat 0",
         0x1016,
         1,
         ParamType::U32,
         ParamAccess::ReadWrite,
         "",
         ParamGroup::Communication},
        {"EMCY COB-ID",
         0x1014,
         0,
         ParamType::U32,
         ParamAccess::ReadWrite,
         "",
         ParamGroup::Communication},

        // -- Digital I/O configuration ---------------------------------------
        {"Global Digital Interrupt Enable",
         0x6005,
         0,
         ParamType::U8,
         ParamAccess::ReadWrite,
         "",
         ParamGroup::DigitalIo},
        {"Digital Interrupt Mask (DI 1-8)",
         0x6006,
         1,
         ParamType::U8,
         ParamAccess::ReadWrite,
         "",
         ParamGroup::DigitalIo},

        // -- Analog I/O configuration ----------------------------------------
        {"AI 1 Interrupt Trigger",
         0x6421,
         1,
         ParamType::U8,
         ParamAccess::ReadWrite,
         "",
         ParamGroup::AnalogIo},

        // -- Safety / monitoring ---------------------------------------------
        {"Status Sticky Time",
         0x2001,
         0,
         ParamType::U16,
         ParamAccess::ReadWrite,
         "ms",
         ParamGroup::Safety},
        {"Board Temp Minimum",
         0x2040,
         0,
         ParamType::I16,
         ParamAccess::ReadWrite,
         "0.1 \xC2\xB0"
         "C",
         ParamGroup::Safety},
        {"Board Temp Maximum",
         0x2041,
         0,
         ParamType::I16,
         ParamAccess::ReadWrite,
         "0.1 \xC2\xB0"
         "C",
         ParamGroup::Safety},
        {"SSW 1 Mode",
         0x2020,
         1,
         ParamType::U8,
         ParamAccess::ReadWrite,
         "",
         ParamGroup::Safety},
        {"SSW 1 Status Sticky Time",
         0x2021,
         1,
         ParamType::U16,
         ParamAccess::ReadWrite,
         "ms",
         ParamGroup::Safety},
        {"SSW 1 Enable",
         0x202A,
         1,
         ParamType::U8,
         ParamAccess::ReadWrite,
         "",
         ParamGroup::Safety},
    };
    return kTable;
}

std::array<ErrorRegisterBit, 8> decode_error_register(uint8_t reg)
{
    return {{
        {"Generic error", (reg & 0x01) != 0},
        {"Current", (reg & 0x02) != 0},
        {"Voltage", (reg & 0x04) != 0},
        {"Temperature", (reg & 0x08) != 0},
        {"Communication", (reg & 0x10) != 0},
        {"Device profile", (reg & 0x20) != 0},
        {"Reserved", (reg & 0x40) != 0},
        {"Manufacturer specific", (reg & 0x80) != 0},
    }};
}

const char *emcy_error_text(uint16_t code)
{
    // CiA 301 emergency error code categories (matched by high byte/nibble).
    switch (code & 0xFF00)
    {
    case 0x0000:
        return "No error / error reset";
    case 0x1000:
        return "Generic error";
    case 0x2000:
    case 0x2100:
    case 0x2200:
    case 0x2300:
        return "Current";
    case 0x3000:
    case 0x3100:
    case 0x3200:
    case 0x3300:
        return "Voltage";
    case 0x4000:
    case 0x4100:
    case 0x4200:
        return "Temperature";
    case 0x5000:
        return "Device hardware";
    case 0x6000:
    case 0x6100:
    case 0x6200:
    case 0x6300:
        return "Device software";
    case 0x7000:
        return "Additional modules";
    case 0x9000:
        return "External error";
    case 0xF000:
        return "Additional functions";
    case 0xFF00:
        return "Device specific";
    default:
        break;
    }
    if ((code & 0xFF00) == 0x8000)
    {
        switch (code)
        {
        case 0x8110:
            return "CAN overrun (objects lost)";
        case 0x8120:
            return "CAN error passive";
        case 0x8130:
            return "Life guard / heartbeat error";
        case 0x8140:
            return "Recovered from bus-off";
        case 0x8150:
            return "CAN-ID collision";
        case 0x8210:
            return "PDO length error";
        case 0x8220:
            return "PDO length exceeded";
        case 0x8250:
            return "RPDO timeout";
        default:
            return "Communication / monitoring";
        }
    }
    return "Unknown";
}

const char *ecu_state_text(uint8_t state)
{
    // Per the TTC 2038XS user manual the device only applies received output
    // values and allows safety-switch control once the ECU state reaches
    // "Running (4)" (startup tests complete). Only that value is documented;
    // other raw values are surfaced as a pre-running phase.
    switch (state)
    {
    case 4:
        return "Running (outputs active)";
    default:
        return "Startup / pre-operational";
    }
}

const char *error_status_text(uint16_t status)
{
    // TTControl error-status table (low 16 bits of a status word / EMCY bytes
    // 6-7). See "Error Status" in the TTC 2038XS user manual.
    switch (status)
    {
    case 0x0000:
        return "OK";
    case 0x0001:
        return "Unknown error";
    case 0x1001:
        return "Short circuit to ground (startup test)";
    case 0x1002:
        return "Open load (startup test)";
    case 0x1003:
        return "Short circuit to battery (startup test)";
    case 0x1004:
        return "Short circuit to other output (startup test)";
    case 0x1005:
        return "Redundant shut-off pin startup test failed";
    case 0x1006:
        return "Overload active, disabled (not reenable-able yet)";
    case 0x1007:
        return "Overload active, disabled (reenable-able)";
    case 0x1008:
        return "Overload active, permanently disabled";
    case 0x1009:
        return "Short circuit to ground";
    case 0x100A:
        return "Open load";
    case 0x100B:
        return "Short circuit to battery";
    case 0x100C:
        return "Safety switch disabled";
    case 0x100D:
        return "Value out of range";
    case 0x100E:
        return "Limit min";
    case 0x100F:
        return "Limit max";
    case 0x1010:
        return "Busy";
    case 0x1011:
        return "Operation failed";
    case 0xF000:
        return "Pin mode/function not supported";
    case 0xF001:
        return "Pin disabled";
    case 0xF002:
        return "Initialization failed";
    case 0xF003:
        return "Too large";
    case 0xF004:
        return "Out of range";
    case 0xF005:
        return "Threshold exceeded";
    case 0xF006:
        return "Threshold deceeded";
    case 0xF007:
        return "Resource exhausted";
    case 0xF008:
        return "Request failed";
    case 0xF009:
        return "Safe state entered";
    case 0xF00A:
        return "Warning";
    case 0xF00B:
        return "CAN buffer overrun";
    case 0xF00C:
        return "CAN bus-off";
    case 0xF00D:
        return "CAN bus-off recovered";
    case 0xF00E:
        return "CAN passive";
    case 0xF00F:
        return "Trap present";
    case 0xF010:
        return "Object dictionary configuration missing";
    case 0xF011:
        return "Object dictionary configuration corrupted";
    case 0xF012:
        return "Object dictionary configuration incompatible";
    default:
        break;
    }
    if (status >= 0x2000 && status <= 0x2FFF)
    {
        return "Mode-specific fault (scope dependent)";
    }
    return "Unknown status";
}

const char *device_status_name(int index)
{
    // Order matches object 0x2403 sub1..13 (see TTC 2038XS safety manual).
    static const char *kNames[DeviceStatus::kDeviceStatusCount] = {
        "External Window Watchdog (SW serviced)",
        "External Window Watchdog (SMU serviced)",
        "Voltage Monitor",
        "Aurix 2G Safety Management Unit (SMU)",
        "5V Reference Voltage",
        "Current Measurement Reference",
        "Housing Voltage",
        "Timer Input Comparator Threshold Voltage",
        "Board Temperature",
        "Reverse Polarity Protection",
        "Shift Registers",
        "I/O Driver",
        "I/O Module Application",
    };
    if (index < 0 || index >= DeviceStatus::kDeviceStatusCount)
    {
        return "?";
    }
    return kNames[index];
}

const char *smu_alarm_name(int index)
{
    // Order matches object 0x2404 sub1..14.
    static const char *kNames[DeviceStatus::kSmuAlarmCount] = {
        "Alarm Group 0",
        "Alarm Group 1",
        "Alarm Group 2",
        "Alarm Group 3",
        "Alarm Group 4",
        "Alarm Group 5",
        "Alarm Group 6",
        "Alarm Group 7",
        "Alarm Group 8",
        "Alarm Group 9",
        "Alarm Group 10",
        "Alarm Group 11",
        "Alarm Group 20",
        "Alarm Group 21",
    };
    if (index < 0 || index >= DeviceStatus::kSmuAlarmCount)
    {
        return "?";
    }
    return kNames[index];
}

namespace
{
double monotonic_seconds()
{
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}
} // namespace

Ttc2038XsDevice::Ttc2038XsDevice(CanOpenClient &client) : client_(client)
{
    param_values_.resize(parameter_table().size());

    size_t pin_total = 0;
    for (const auto &g : pin_groups())
    {
        pin_total += g.pins.size();
    }
    pin_mode_values_.resize(pin_total);
    pin_io_values_.resize(pin_total);
}

void Ttc2038XsDevice::set_node_id(uint8_t node_id)
{
    node_id_ = node_id;
}

void Ttc2038XsDevice::reset()
{
    jobs_.clear();
    in_flight_ = false;
}

bool Ttc2038XsDevice::busy() const
{
    return in_flight_ || !jobs_.empty();
}

bool Ttc2038XsDevice::is_control() const
{
    return client_.is_connected() && client_.mode() == ClientMode::Control;
}

// ── Node discovery
// ────────────────────────────────────────────────────────────
std::vector<DetectedNode> Ttc2038XsDevice::detect_nodes() const
{
    // Build a per-node picture from every COB-ID observed in the traffic
    // statistics. Node-addressed function codes encode the node in the low 7
    // bits, so any frame reveals which nodes are active.
    std::map<uint8_t, DetectedNode> map;
    for (const auto &s : client_.cob_stats())
    {
        const FunctionCode fc = function_code(s.cob_id);
        const uint8_t node = node_of(s.cob_id);
        if (node == 0)
        {
            continue; // broadcast (NMT/SYNC), not node-addressed
        }
        DetectedNode &d = map[node];
        d.node_id = node;
        d.frames += s.count;
        switch (fc)
        {
        case FunctionCode::Heartbeat:
            d.heartbeat = true;
            break;
        case FunctionCode::SdoTx:
            d.sdo = true;
            break;
        case FunctionCode::Tpdo1:
        case FunctionCode::Tpdo2:
        case FunctionCode::Tpdo3:
        case FunctionCode::Tpdo4:
            d.tpdo = true;
            break;
        case FunctionCode::Sync: // 0x080 + node == EMCY
            d.emcy = true;
            break;
        default:
            break;
        }
    }

    // Fold in NMT state from the heartbeat-tracked node table.
    for (const auto &n : client_.nodes())
    {
        if (n.node_id == 0)
        {
            continue;
        }
        DetectedNode &d = map[n.node_id];
        d.node_id = n.node_id;
        d.nmt = n.state;
        d.heartbeat = d.heartbeat || n.heartbeat_count > 0;
    }

    // Mark nodes confirmed by the most recent active probe.
    std::vector<DetectedNode> out;
    out.reserve(map.size());
    for (auto &kv : map)
    {
        DetectedNode &d = kv.second;
        d.probed =
            std::find(scan_found_.begin(), scan_found_.end(), d.node_id) !=
            scan_found_.end();
        out.push_back(d);
    }
    // Include probe-only finds that produced no other traffic.
    for (uint8_t id : scan_found_)
    {
        if (map.find(id) == map.end())
        {
            DetectedNode d;
            d.node_id = id;
            d.probed = true;
            out.push_back(d);
        }
    }
    std::sort(
        out.begin(),
        out.end(),
        [](const DetectedNode &a, const DetectedNode &b) {
            return a.node_id < b.node_id;
        });
    return out;
}

void Ttc2038XsDevice::start_node_scan(uint8_t first, uint8_t last)
{
    if (!is_control() || scanning_)
    {
        return;
    }
    if (first < 1)
    {
        first = 1;
    }
    if (last > 127)
    {
        last = 127;
    }
    if (last < first)
    {
        return;
    }

    scan_found_.clear();
    scanning_ = true;
    scan_total_ = static_cast<int>(last - first + 1);
    scan_remaining_ = scan_total_;

    // Use a short per-probe timeout so absent nodes don't stall the scan.
    saved_sdo_timeout_ms_ = 1000;
    client_.set_sdo_timeout_ms(60);

    for (int id = first; id <= last; ++id)
    {
        Job j;
        j.is_write = false;
        j.kind = JobKind::ScanProbe;
        j.index = 0x1000; // Device Type — every CANopen node implements it
        j.sub = 0;
        j.channel = id;
        j.target_node = static_cast<uint8_t>(id);
        queue(j);
    }
}

void Ttc2038XsDevice::cancel_node_scan()
{
    if (!scanning_)
    {
        return;
    }
    // Drop pending probes; let any in-flight one finish naturally.
    for (auto it = jobs_.begin(); it != jobs_.end();)
    {
        if (it->kind == JobKind::ScanProbe)
        {
            it = jobs_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    scan_remaining_ = in_flight_ && current_.kind == JobKind::ScanProbe ? 1 : 0;
    if (scan_remaining_ == 0)
    {
        client_.set_sdo_timeout_ms(saved_sdo_timeout_ms_);
        scanning_ = false;
    }
}

void Ttc2038XsDevice::queue(const Job &job)
{
    jobs_.push_back(job);
}

void Ttc2038XsDevice::poll()
{
    // Refresh NMT/heartbeat from the passively observed node table (works in
    // both Monitor and Control modes).
    for (const auto &node : client_.nodes())
    {
        if (node.node_id == node_id_)
        {
            status_.nmt = node.state;
            status_.heartbeat_count = node.heartbeat_count;
            status_.last_heartbeat_us = node.last_heartbeat_us;
            break;
        }
    }
    // Sample "now" on the same steady clock used for heartbeat timestamps so the
    // UI can compute how stale the NMT state is.
    status_.status_now_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    // Periodic status polling (Control mode only; needs SDO uploads).
    if (auto_refresh_ && is_control() && jobs_.empty() && !in_flight_)
    {
        const double now = monotonic_seconds();
        if (now - last_refresh_s_ >= auto_interval_s_)
        {
            refresh_status();
            last_refresh_s_ = now;
        }
    }

    // Advance the in-flight transfer.
    if (in_flight_)
    {
        const SdoResult res = client_.sdo_result();
        if (res.status == SdoStatus::Done || res.status == SdoStatus::Failed)
        {
            dispatch_result(current_, res);
            in_flight_ = false;
        }
        return;
    }

    // Start the next queued transfer.
    if (jobs_.empty())
    {
        return;
    }
    current_ = jobs_.front();
    jobs_.pop_front();

    const uint8_t target =
        current_.target_node != 0 ? current_.target_node : node_id_;

    bool ok = false;
    if (current_.is_write)
    {
        ok = client_.sdo_write(
            target,
            current_.index,
            current_.sub,
            current_.write_data.data(),
            current_.write_data.size());
    }
    else
    {
        ok = client_.sdo_read(target, current_.index, current_.sub);
    }
    in_flight_ = ok;
    if (!ok)
    {
        if (current_.kind == JobKind::Parameter && current_.channel >= 0 &&
            current_.channel < static_cast<int>(param_values_.size()))
        {
            // Surface the failure to start (e.g. Monitor mode) on the
            // parameter.
            auto &pv = param_values_[current_.channel];
            pv.reading = false;
            pv.error = "SDO busy / not in Control mode";
        }
        else if (current_.kind == JobKind::ScanProbe)
        {
            // Could not even start the probe; count it as done so the scan
            // still terminates.
            dispatch_result(current_, SdoResult{});
        }
        else if (current_.kind == JobKind::NmtPollProbe)
        {
            // Could not start the probe (e.g. SDO busy): resolve it as a failed
            // probe so the UI does not hang on "pending".
            SdoResult r;
            r.message = "could not start probe (SDO busy)";
            dispatch_result(current_, r);
        }
        else if (
            current_.kind == JobKind::ConfigValidWrite ||
            current_.kind == JobKind::PinSignatureRead ||
            current_.kind == JobKind::SafetySignatureWrite)
        {
            // Could not start an apply-config step: fail the sequence cleanly.
            SdoResult r;
            r.message = "could not start SDO (busy / not in Control mode)";
            dispatch_result(current_, r);
        }
    }
}

int64_t Ttc2038XsDevice::decode_int(
    ParamType type, const std::vector<uint8_t> &data)
{
    uint64_t raw = 0;
    const size_t n = data.size();
    for (size_t i = 0; i < n && i < 8; ++i)
    {
        raw |= static_cast<uint64_t>(data[i]) << (8 * i);
    }
    switch (type)
    {
    case ParamType::I8:
        return static_cast<int8_t>(raw & 0xFF);
    case ParamType::I16:
        return static_cast<int16_t>(raw & 0xFFFF);
    case ParamType::I32:
        return static_cast<int32_t>(raw & 0xFFFFFFFF);
    default:
        return static_cast<int64_t>(raw);
    }
}

std::vector<uint8_t> Ttc2038XsDevice::encode_int(ParamType type, int64_t value)
{
    size_t bytes = 1;
    switch (type)
    {
    case ParamType::U8:
    case ParamType::I8:
        bytes = 1;
        break;
    case ParamType::U16:
    case ParamType::I16:
        bytes = 2;
        break;
    case ParamType::U32:
    case ParamType::I32:
        bytes = 4;
        break;
    case ParamType::String:
        bytes = 0;
        break;
    }
    std::vector<uint8_t> out(bytes);
    const auto u = static_cast<uint64_t>(value);
    for (size_t i = 0; i < bytes; ++i)
    {
        out[i] = static_cast<uint8_t>((u >> (8 * i)) & 0xFF);
    }
    return out;
}

void Ttc2038XsDevice::dispatch_result(const Job &job, const SdoResult &res)
{
    const bool ok = (res.status == SdoStatus::Done);

    switch (job.kind)
    {
    case JobKind::IdentityDeviceType:
        // Device Type is informational; nothing stored beyond identity flag.
        if (ok)
        {
            status_.identity_valid = true;
        }
        break;
    case JobKind::IdentityName:
        if (ok)
        {
            status_.device_name.assign(res.data.begin(), res.data.end());
        }
        break;
    case JobKind::IdentityHwVersion:
        if (ok)
        {
            status_.hw_version.assign(res.data.begin(), res.data.end());
        }
        break;
    case JobKind::IdentitySwVersion:
        if (ok)
        {
            status_.sw_version.assign(res.data.begin(), res.data.end());
        }
        break;
    case JobKind::IdentityVendor:
        if (ok)
        {
            status_.vendor_id =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
            status_.identity_valid = true;
        }
        break;
    case JobKind::IdentityProduct:
        if (ok)
        {
            status_.product_code =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
        }
        break;
    case JobKind::IdentityRevision:
        if (ok)
        {
            status_.revision =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
        }
        break;
    case JobKind::IdentitySerial:
        if (ok)
        {
            status_.serial =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
        }
        break;
    case JobKind::ErrorRegister:
        status_.error_register_valid = ok;
        if (ok && !res.data.empty())
        {
            status_.error_register = res.data[0];
        }
        break;
    case JobKind::BoardTemp:
        status_.board_temp_valid = ok;
        if (ok)
        {
            status_.board_temp_decideg =
                static_cast<int16_t>(decode_int(ParamType::I16, res.data));
        }
        break;
    case JobKind::EcuState:
        status_.ecu_state_valid = ok;
        if (ok && !res.data.empty())
        {
            status_.ecu_state = res.data[0];
        }
        break;
    case JobKind::ReportingDevice:
        status_.reporting_valid = ok;
        if (ok && !res.data.empty())
        {
            status_.reporting_device = res.data[0];
        }
        break;
    case JobKind::ReportingDeviceStatus:
        if (ok)
        {
            status_.reporting_device_status =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
        }
        break;
    case JobKind::DeviceStatusEntry:
        if (job.channel == 0)
        {
            status_.device_status = {};
            status_.device_status_valid = false;
        }
        if (ok && job.channel >= 0 &&
            job.channel < static_cast<int>(status_.device_status.size()))
        {
            status_.device_status[job.channel] =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
            status_.device_status_valid = true;
        }
        break;
    case JobKind::SmuAlarmEntry:
        if (job.channel == 0)
        {
            status_.smu_alarms = {};
            status_.smu_alarms_valid = false;
        }
        if (ok && job.channel >= 0 &&
            job.channel < static_cast<int>(status_.smu_alarms.size()))
        {
            status_.smu_alarms[job.channel] =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
            status_.smu_alarms_valid = true;
        }
        break;
    case JobKind::SupplyVoltageCpu:
        status_.supply_cpu_valid = ok;
        if (ok)
        {
            status_.supply_cpu_mv =
                static_cast<uint16_t>(decode_int(ParamType::U16, res.data));
        }
        break;
    case JobKind::SupplyVoltagePower:
        status_.supply_power_valid = ok;
        if (ok)
        {
            status_.supply_power_mv =
                static_cast<uint16_t>(decode_int(ParamType::U16, res.data));
        }
        break;
    case JobKind::PredefErrorCount:
        status_.predef_error_valid = ok;
        status_.predef_errors = {};
        if (ok && !res.data.empty())
        {
            uint8_t count = res.data[0];
            if (count > status_.predef_errors.size())
            {
                count = static_cast<uint8_t>(status_.predef_errors.size());
            }
            status_.predef_error_count = count;
            // Queue a read for each stored error entry (sub 1..count).
            for (uint8_t i = 0; i < count; ++i)
            {
                Job e;
                e.is_write = false;
                e.index = 0x1003;
                e.sub = static_cast<uint8_t>(i + 1);
                e.channel = i;
                e.kind = JobKind::PredefErrorEntry;
                queue(e);
            }
        }
        else
        {
            status_.predef_error_count = 0;
        }
        break;
    case JobKind::PredefErrorEntry:
        if (ok && job.channel >= 0 &&
            job.channel < static_cast<int>(status_.predef_errors.size()))
        {
            status_.predef_errors[job.channel] =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
        }
        break;
    case JobKind::DigitalInputBlock:
        if (ok && !res.data.empty())
        {
            const uint32_t mask = 0xFFu << (8 * job.channel);
            status_.digital_inputs &= ~mask;
            status_.digital_inputs |= static_cast<uint32_t>(res.data[0])
                                      << (8 * job.channel);
            status_.digital_inputs_valid = true;
        }
        break;
    case JobKind::AnalogInput:
        if (ok && job.channel >= 0 &&
            job.channel < static_cast<int>(status_.analog_inputs.size()))
        {
            status_.analog_inputs[job.channel] =
                static_cast<int16_t>(decode_int(ParamType::I16, res.data));
            status_.analog_inputs_valid = true;
        }
        break;
    case JobKind::SswStatus:
        if (ok)
        {
            status_.ssw_status =
                static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
            status_.ssw_valid = true;
        }
        break;
    case JobKind::SswEnableFeedback:
        if (ok && !res.data.empty())
        {
            status_.ssw_enable_feedback = res.data[0];
        }
        break;
    case JobKind::DigitalOutput:
        if (ok && job.is_write && !job.write_data.empty())
        {
            status_.digital_outputs = job.write_data[0];
            // Confirm the device applied the command by reading it back.
            read_digital_output_status();
        }
        break;
    case JobKind::DigitalOutputStatus:
        status_.digital_outputs_status_valid = ok;
        if (ok && !res.data.empty())
        {
            status_.digital_outputs_status = res.data[0];
        }
        break;
    case JobKind::Parameter: {
        if (job.channel < 0 ||
            job.channel >= static_cast<int>(param_values_.size()))
        {
            break;
        }
        auto &pv = param_values_[job.channel];
        const auto &def = parameter_table()[job.channel];
        pv.reading = false;
        if (!ok)
        {
            pv.valid = false;
            char buf[128];
            std::snprintf(
                buf,
                sizeof(buf),
                "%s (0x%08X)",
                res.message.c_str(),
                res.abort_code);
            pv.error = buf;
            break;
        }
        pv.error.clear();
        if (job.is_write)
        {
            // Re-read after a successful write to confirm the stored value.
            read_parameter(static_cast<size_t>(job.channel));
            break;
        }
        pv.valid = true;
        if (def.type == ParamType::String)
        {
            pv.text.assign(res.data.begin(), res.data.end());
        }
        else
        {
            pv.integer = decode_int(def.type, res.data);
        }
        break;
    }
    case JobKind::PinMode: {
        if (job.channel < 0 ||
            job.channel >= static_cast<int>(pin_mode_values_.size()))
        {
            break;
        }
        auto &pm = pin_mode_values_[job.channel];
        pm.reading = false;
        if (!ok)
        {
            pm.valid = false;
            char buf[128];
            std::snprintf(
                buf,
                sizeof(buf),
                "%s (0x%08X)",
                res.message.c_str(),
                res.abort_code);
            pm.error = buf;
            break;
        }
        pm.error.clear();
        if (job.is_write)
        {
            // Re-read after a successful write to confirm the applied mode.
            read_pin_mode(static_cast<size_t>(job.channel));
            break;
        }
        pm.valid = true;
        if (!res.data.empty())
        {
            pm.mode = res.data[0];
        }
        break;
    }
    case JobKind::PinIo: {
        if (job.channel < 0 ||
            job.channel >= static_cast<int>(pin_io_values_.size()))
        {
            break;
        }
        auto &iv = pin_io_values_[job.channel];
        iv.reading = false;
        if (!ok)
        {
            iv.valid = false;
            char buf[128];
            std::snprintf(
                buf,
                sizeof(buf),
                "%s (0x%08X)",
                res.message.c_str(),
                res.abort_code);
            iv.error = buf;
            break;
        }
        iv.error.clear();
        if (job.is_write)
        {
            // Re-read after a successful write to confirm the applied value.
            read_pin_io(static_cast<size_t>(job.channel));
            break;
        }
        iv.valid = true;
        // Decode based on the pin's configured function: 1-byte digital level,
        // otherwise a signed 16-bit measurement / duty-cycle value.
        ParamType decode_as = ParamType::I16;
        if (job.channel < static_cast<int>(pin_mode_values_.size()) &&
            pin_mode_values_[job.channel].valid)
        {
            const ModeBehavior beh =
                mode_behavior(pin_mode_values_[job.channel].mode);
            if (beh.function == IoFunction::DigitalInput ||
                beh.function == IoFunction::DigitalOutput)
            {
                decode_as = ParamType::U8;
            }
            else if (beh.unit != nullptr && beh.unit[0] == 'O') // "Ohm"
            {
                decode_as = ParamType::U32;
            }
        }
        iv.value = decode_int(decode_as, res.data);
        break;
    }
    case JobKind::Command:
        // Fire-and-forget; no stored result.
        break;
    case JobKind::Raw: {
        if (!ok)
        {
            char buf[128];
            std::snprintf(
                buf,
                sizeof(buf),
                "Failed: %s (0x%08X)",
                res.message.c_str(),
                res.abort_code);
            raw_result_ = buf;
        }
        else if (job.is_write)
        {
            raw_result_ = "Write OK";
        }
        else
        {
            std::string s;
            char tmp[4];
            for (size_t i = 0; i < res.data.size(); ++i)
            {
                std::snprintf(tmp, sizeof(tmp), "%02X", res.data[i]);
                if (i > 0)
                {
                    s += ' ';
                }
                s += tmp;
            }
            raw_result_ = s.empty() ? "(empty)" : s;
        }
        break;
    }
    case JobKind::ScanProbe: {
        // A node that returns data (Done) or replies with an SDO abort is
        // present on the bus; only a timeout (no response) means absent.
        const bool present =
            ok || (res.abort_code != 0 && res.abort_code != 0x05040000u);
        if (present)
        {
            const auto id = static_cast<uint8_t>(job.channel);
            if (std::find(scan_found_.begin(), scan_found_.end(), id) ==
                scan_found_.end())
            {
                scan_found_.push_back(id);
            }
        }
        if (scan_remaining_ > 0)
        {
            --scan_remaining_;
        }
        if (scan_remaining_ <= 0)
        {
            // Scan finished: restore the normal SDO timeout.
            client_.set_sdo_timeout_ms(saved_sdo_timeout_ms_);
            scanning_ = false;
        }
        break;
    }
    case JobKind::NmtPollProbe: {
        // The node is alive if it responded at all — a successful upload or any
        // SDO abort both prove it is communicating. Only a timeout (no reply)
        // means no response.
        const bool alive =
            ok || (res.abort_code != 0 && res.abort_code != 0x05040000u);
        status_.nmt_probe_pending = false;
        status_.nmt_probe_done = true;
        status_.nmt_probe_alive = alive;
        status_.nmt_probe_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        if (!alive)
        {
            status_.nmt_probe_error =
                res.message.empty() ? "no response (timeout)" : res.message;
        }
        else
        {
            status_.nmt_probe_error.clear();
        }
        break;
    }
    case JobKind::ConfigValidWrite: {
        // Apply step 1 done. On success, chain the signature read (0x2000).
        if (!ok)
        {
            apply_state_ = ApplyConfigState::Failed;
            char buf[160];
            std::snprintf(
                buf,
                sizeof(buf),
                "Configuration Valid write (0x13FE) failed: %s (0x%08X)",
                res.message.c_str(),
                res.abort_code);
            apply_message_ = buf;
            break;
        }
        apply_state_ = ApplyConfigState::ReadingSignature;
        apply_message_ = "Reading pin configuration signature (0x2000)...";
        Job r;
        r.is_write = false;
        r.index = 0x2000;
        r.sub = 0;
        r.kind = JobKind::PinSignatureRead;
        queue(r);
        break;
    }
    case JobKind::PinSignatureRead: {
        // Apply step 2 done. On success, write the read-back signature to 0x2010.
        if (!ok)
        {
            apply_state_ = ApplyConfigState::Failed;
            char buf[160];
            std::snprintf(
                buf,
                sizeof(buf),
                "Reading signature (0x2000) failed: %s (0x%08X)",
                res.message.c_str(),
                res.abort_code);
            apply_message_ = buf;
            break;
        }
        apply_signature_ =
            static_cast<uint32_t>(decode_int(ParamType::U32, res.data));
        apply_state_ = ApplyConfigState::WritingSignature;
        char buf[96];
        std::snprintf(
            buf,
            sizeof(buf),
            "Writing safety signature 0x%08X to 0x2010...",
            apply_signature_);
        apply_message_ = buf;
        Job w;
        w.is_write = true;
        w.index = 0x2010;
        w.sub = 0;
        w.kind = JobKind::SafetySignatureWrite;
        w.write_data = encode_int(ParamType::U32, apply_signature_);
        queue(w);
        break;
    }
    case JobKind::SafetySignatureWrite: {
        // Apply step 3 done.
        if (!ok)
        {
            apply_state_ = ApplyConfigState::Failed;
            char buf[160];
            std::snprintf(
                buf,
                sizeof(buf),
                "Writing signature (0x2010) failed: %s (0x%08X)",
                res.message.c_str(),
                res.abort_code);
            apply_message_ = buf;
            break;
        }
        apply_state_ = ApplyConfigState::Done;
        char buf[128];
        std::snprintf(
            buf,
            sizeof(buf),
            "Configuration applied (signature 0x%08X). You can now send NMT "
            "Start.",
            apply_signature_);
        apply_message_ = buf;
        break;
    }
    }
}

// ── Status / read
// ─────────────────────────────────────────────────────────────
void Ttc2038XsDevice::refresh_identity()
{
    Job j;
    j.is_write = false;
    j.index = 0x1000;
    j.sub = 0;
    j.kind = JobKind::IdentityDeviceType;
    queue(j);
    j = Job{};
    j.index = 0x1008;
    j.kind = JobKind::IdentityName;
    queue(j);
    j = Job{};
    j.index = 0x1009;
    j.kind = JobKind::IdentityHwVersion;
    queue(j);
    j = Job{};
    j.index = 0x100A;
    j.kind = JobKind::IdentitySwVersion;
    queue(j);
    j = Job{};
    j.index = od::kIdentity;
    j.sub = od::kIdentityVendorId;
    j.kind = JobKind::IdentityVendor;
    queue(j);
    j = Job{};
    j.index = od::kIdentity;
    j.sub = od::kIdentityProductCode;
    j.kind = JobKind::IdentityProduct;
    queue(j);
    j = Job{};
    j.index = od::kIdentity;
    j.sub = od::kIdentityRevision;
    j.kind = JobKind::IdentityRevision;
    queue(j);
    j = Job{};
    j.index = od::kIdentity;
    j.sub = od::kIdentitySerial;
    j.kind = JobKind::IdentitySerial;
    queue(j);
}

void Ttc2038XsDevice::refresh_status()
{
    Job j;
    // Error register.
    j.index = od::kErrorRegister;
    j.sub = 0;
    j.kind = JobKind::ErrorRegister;
    queue(j);
    // Predefined error field count (0x1003 sub0); entries are queued on result.
    j = Job{};
    j.index = 0x1003;
    j.sub = 0;
    j.kind = JobKind::PredefErrorCount;
    queue(j);
    // Board temperature (0x2048).
    j = Job{};
    j.index = 0x2048;
    j.kind = JobKind::BoardTemp;
    queue(j);
    // ECU diagnostic state (0x2400) + reporting device (0x2401 / 0x2402).
    j = Job{};
    j.index = 0x2400;
    j.kind = JobKind::EcuState;
    queue(j);
    j = Job{};
    j.index = 0x2401;
    j.kind = JobKind::ReportingDevice;
    queue(j);
    j = Job{};
    j.index = 0x2402;
    j.kind = JobKind::ReportingDeviceStatus;
    queue(j);
    // Core component health (0x2403 sub1..13).
    for (uint8_t sub = 1; sub <= DeviceStatus::kDeviceStatusCount; ++sub)
    {
        j = Job{};
        j.index = 0x2403;
        j.sub = sub;
        j.channel = sub - 1;
        j.kind = JobKind::DeviceStatusEntry;
        queue(j);
    }
    // SMU alarm groups (0x2404 sub1..14) - non-recoverable safe-state flag.
    for (uint8_t sub = 1; sub <= DeviceStatus::kSmuAlarmCount; ++sub)
    {
        j = Job{};
        j.index = 0x2404;
        j.sub = sub;
        j.channel = sub - 1;
        j.kind = JobKind::SmuAlarmEntry;
        queue(j);
    }
    // Supply voltages: BAT+ CPU (0x22A9 sub1), BAT+ Power (0x22C9 sub1).
    j = Job{};
    j.index = 0x22A9;
    j.sub = 1;
    j.kind = JobKind::SupplyVoltageCpu;
    queue(j);
    j = Job{};
    j.index = 0x22C9;
    j.sub = 1;
    j.kind = JobKind::SupplyVoltagePower;
    queue(j);
    // Digital inputs: 0x6000 sub 1..4 (8-bit blocks).
    for (uint8_t sub = 1; sub <= 4; ++sub)
    {
        j = Job{};
        j.index = od::kDigitalInput;
        j.sub = sub;
        j.channel = sub - 1;
        j.kind = JobKind::DigitalInputBlock;
        queue(j);
    }
    // Analog inputs: 0x6401 sub 1..32.
    for (uint8_t sub = 1; sub <= od::kAnalogInputCount; ++sub)
    {
        j = Job{};
        j.index = od::kAnalogInput;
        j.sub = sub;
        j.channel = sub - 1;
        j.kind = JobKind::AnalogInput;
        queue(j);
    }
    // Safety switch 1 status + enable feedback.
    j = Job{};
    j.index = 0x2028;
    j.sub = 1;
    j.kind = JobKind::SswStatus;
    queue(j);
    j = Job{};
    j.index = 0x202B;
    j.sub = 1;
    j.kind = JobKind::SswEnableFeedback;
    queue(j);
    // Digital output read-back (confirms applied state).
    j = Job{};
    j.index = od::kDigitalOutput;
    j.sub = od::kDigitalOutputSub;
    j.kind = JobKind::DigitalOutputStatus;
    queue(j);
}

void Ttc2038XsDevice::set_auto_refresh(bool on, double interval_s)
{
    auto_refresh_ = on;
    if (interval_s > 0.05)
    {
        auto_interval_s_ = interval_s;
    }
}

// ── Control
// ───────────────────────────────────────────────────────────────────
bool Ttc2038XsDevice::start_node()
{
    return client_.send_nmt(NmtCommand::Start, node_id_);
}

bool Ttc2038XsDevice::stop_node()
{
    return client_.send_nmt(NmtCommand::Stop, node_id_);
}

bool Ttc2038XsDevice::reset_node()
{
    return client_.send_nmt(NmtCommand::ResetNode, node_id_);
}

bool Ttc2038XsDevice::enter_pre_operational()
{
    return client_.send_nmt(NmtCommand::EnterPreOperational, node_id_);
}

bool Ttc2038XsDevice::set_digital_outputs(uint8_t mask)
{
    if (!is_control())
    {
        return false;
    }
    Job j;
    j.is_write = true;
    j.index = od::kDigitalOutput;
    j.sub = od::kDigitalOutputSub;
    j.kind = JobKind::DigitalOutput;
    j.write_data = {mask};
    queue(j);
    return true;
}

void Ttc2038XsDevice::read_digital_output_status()
{
    if (!is_control())
    {
        return;
    }
    Job j;
    j.is_write = false;
    j.index = od::kDigitalOutput;
    j.sub = od::kDigitalOutputSub;
    j.kind = JobKind::DigitalOutputStatus;
    queue(j);
}

bool Ttc2038XsDevice::request_safe_state(bool active)
{
    if (!is_control())
    {
        return false;
    }
    Job j;
    j.is_write = true;
    j.index = 0x2008; // Device - Request Safe State
    j.sub = 0;
    j.kind = JobKind::Command;
    j.write_data = {static_cast<uint8_t>(active ? 0x01 : 0x00)};
    queue(j);
    return true;
}

bool Ttc2038XsDevice::poll_nmt_state()
{
    if (!is_control())
    {
        return false;
    }
    status_.nmt_probe_pending = true;
    status_.nmt_probe_done = false;
    status_.nmt_probe_error.clear();
    // Read Producer Heartbeat Time (0x1017). The value is unimportant — any
    // response (success or even an SDO abort) proves the node is alive and
    // talking; only a timeout indicates no response.
    Job j;
    j.is_write = false;
    j.index = 0x1017;
    j.sub = 0;
    j.kind = JobKind::NmtPollProbe;
    queue(j);
    return true;
}

bool Ttc2038XsDevice::set_producer_heartbeat(uint16_t period_ms)
{
    if (!is_control())
    {
        return false;
    }
    // 0x1017 Producer Heartbeat Time, UNSIGNED16, milliseconds. Non-zero makes
    // the device emit periodic heartbeats so its NMT state is reported live.
    Job j;
    j.is_write = true;
    j.index = 0x1017;
    j.sub = 0;
    j.kind = JobKind::Command;
    j.write_data = {
        static_cast<uint8_t>(period_ms & 0xFF),
        static_cast<uint8_t>((period_ms >> 8) & 0xFF)};
    queue(j);
    return true;
}

double Ttc2038XsDevice::seconds_since_heartbeat() const
{
    if (status_.last_heartbeat_us == 0 ||
        status_.status_now_us < status_.last_heartbeat_us)
    {
        return -1.0;
    }
    return static_cast<double>(
               status_.status_now_us - status_.last_heartbeat_us) /
           1e6;
}

bool Ttc2038XsDevice::enable_development_mode()
{
    if (!is_control())
    {
        return false;
    }
    // Per the TTC 2038XS user manual, development mode skips the SRDO and pin
    // configuration verification when going Operational. Enable sequence
    // (must be done in NMT Pre-Operational, these are safety-config objects):
    //   1. write 0x65       to 0x2011 (Device - Enable Development Mode, U8)
    //   2. write 0x706C7664 to 0x2010 (Device - Safety Pin Config Signature,U32)
    Job j;
    j.is_write = true;
    j.index = 0x2011;
    j.sub = 0;
    j.kind = JobKind::Command;
    j.write_data = {0x65};
    queue(j);

    Job sig;
    sig.is_write = true;
    sig.index = 0x2010;
    sig.sub = 0;
    sig.kind = JobKind::Command;
    // 0x706C7664 little-endian ("dvlp").
    sig.write_data = {0x64, 0x76, 0x6C, 0x70};
    queue(sig);
    return true;
}

bool Ttc2038XsDevice::disable_development_mode()
{
    if (!is_control())
    {
        return false;
    }
    // Returning 0x2011 to its default (0x00) clears the development-mode enable.
    // A node reset / re-validation with a real signature is required for the
    // device to enforce verification again.
    Job j;
    j.is_write = true;
    j.index = 0x2011;
    j.sub = 0;
    j.kind = JobKind::Command;
    j.write_data = {0x00};
    queue(j);
    return true;
}

bool Ttc2038XsDevice::apply_configuration()
{
    if (!is_control())
    {
        apply_state_ = ApplyConfigState::Failed;
        apply_message_ = "Control mode required.";
        return false;
    }
    if (apply_state_ == ApplyConfigState::WritingConfigValid ||
        apply_state_ == ApplyConfigState::ReadingSignature ||
        apply_state_ == ApplyConfigState::WritingSignature)
    {
        return false; // already running
    }

    apply_signature_ = 0;
    apply_state_ = ApplyConfigState::WritingConfigValid;
    apply_message_ = "Writing Configuration Valid (0x13FE = 0xA5)...";

    // Step 1: write 0xA5 to Configuration Valid (0x13FE sub 0). The result
    // handler chains the remaining steps (read 0x2000, write 0x2010).
    Job j;
    j.is_write = true;
    j.index = 0x13FE;
    j.sub = 0;
    j.kind = JobKind::ConfigValidWrite;
    j.write_data = {0xA5};
    queue(j);
    return true;
}

// ── Configuration
// ─────────────────────────────────────────────────────────────
const ParamValue &Ttc2038XsDevice::parameter_value(size_t handle) const
{
    static const ParamValue kEmpty{};
    if (handle >= param_values_.size())
    {
        return kEmpty;
    }
    return param_values_[handle];
}

void Ttc2038XsDevice::read_parameter(size_t handle)
{
    if (handle >= parameter_table().size())
    {
        return;
    }
    const auto &def = parameter_table()[handle];
    param_values_[handle].reading = true;
    Job j;
    j.is_write = false;
    j.index = def.index;
    j.sub = def.sub;
    j.kind = JobKind::Parameter;
    j.channel = static_cast<int>(handle);
    queue(j);
}

void Ttc2038XsDevice::read_all_parameters()
{
    for (size_t i = 0; i < parameter_table().size(); ++i)
    {
        read_parameter(i);
    }
}

bool Ttc2038XsDevice::write_parameter(size_t handle, int64_t value)
{
    if (handle >= parameter_table().size())
    {
        return false;
    }
    const auto &def = parameter_table()[handle];
    if (def.access != ParamAccess::ReadWrite || def.type == ParamType::String)
    {
        return false;
    }
    if (!is_control())
    {
        return false;
    }
    Job j;
    j.is_write = true;
    j.index = def.index;
    j.sub = def.sub;
    j.kind = JobKind::Parameter;
    j.channel = static_cast<int>(handle);
    j.write_data = encode_int(def.type, value);
    queue(j);
    return true;
}

bool Ttc2038XsDevice::store_parameters()
{
    if (!is_control())
    {
        return false;
    }
    Job j;
    j.is_write = true;
    j.index = 0x1010; // Store Parameters
    j.sub = 1;
    j.kind = JobKind::Command;
    j.write_data = {0x73, 0x61, 0x76, 0x65}; // "save"
    queue(j);
    return true;
}

bool Ttc2038XsDevice::restore_defaults()
{
    if (!is_control())
    {
        return false;
    }
    Job j;
    j.is_write = true;
    j.index = 0x1011; // Restore Default Parameters
    j.sub = 1;
    j.kind = JobKind::Command;
    j.write_data = {0x6C, 0x6F, 0x61, 0x64}; // "load"
    queue(j);
    return true;
}

bool Ttc2038XsDevice::clear_error_history()
{
    if (!is_control())
    {
        return false;
    }
    // CiA 301: writing 0 to 0x1003 sub0 resets the predefined error field.
    Job j;
    j.is_write = true;
    j.index = 0x1003;
    j.sub = 0;
    j.kind = JobKind::Command;
    j.write_data = {0x00};
    queue(j);
    // Re-read so the UI reflects the cleared history.
    Job r;
    r.is_write = false;
    r.index = 0x1003;
    r.sub = 0;
    r.kind = JobKind::PredefErrorCount;
    queue(r);
    return true;
}

// ── Pin configuration
// ─────────────────────────────────────────────────────────
namespace
{
// Resolve a flat pin handle to its group and the pin's object index/sub-index.
// Returns false if the handle is out of range.
bool resolve_pin_handle(
    size_t handle, const PinGroup **group_out, uint16_t *index_out,
    uint8_t *sub_out)
{
    size_t base = 0;
    for (const auto &g : pin_groups())
    {
        if (handle < base + g.pins.size())
        {
            const PinInfo &pin = g.pins[handle - base];
            if (group_out != nullptr)
            {
                *group_out = &g;
            }
            if (index_out != nullptr)
            {
                *index_out = g.pin_mode_index;
            }
            if (sub_out != nullptr)
            {
                *sub_out = pin.sub;
            }
            return true;
        }
        base += g.pins.size();
    }
    return false;
}
} // namespace

size_t Ttc2038XsDevice::pin_mode_handle(size_t group_index, size_t pin_index)
{
    const auto &groups = pin_groups();
    size_t base = 0;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        if (g == group_index)
        {
            return base + pin_index;
        }
        base += groups[g].pins.size();
    }
    return base; // out of range; callers guard with pin_mode_count()
}

size_t Ttc2038XsDevice::pin_mode_count() const
{
    return pin_mode_values_.size();
}

const PinModeValue &Ttc2038XsDevice::pin_mode_value(size_t handle) const
{
    static const PinModeValue kEmpty{};
    if (handle >= pin_mode_values_.size())
    {
        return kEmpty;
    }
    return pin_mode_values_[handle];
}

void Ttc2038XsDevice::read_pin_mode(size_t handle)
{
    uint16_t index = 0;
    uint8_t sub = 0;
    if (!resolve_pin_handle(handle, nullptr, &index, &sub))
    {
        return;
    }
    pin_mode_values_[handle].reading = true;
    Job j;
    j.is_write = false;
    j.index = index;
    j.sub = sub;
    j.kind = JobKind::PinMode;
    j.channel = static_cast<int>(handle);
    queue(j);
}

void Ttc2038XsDevice::read_all_pin_modes()
{
    for (size_t i = 0; i < pin_mode_values_.size(); ++i)
    {
        read_pin_mode(i);
    }
}

bool Ttc2038XsDevice::write_pin_mode(size_t handle, uint8_t mode)
{
    uint16_t index = 0;
    uint8_t sub = 0;
    if (!resolve_pin_handle(handle, nullptr, &index, &sub))
    {
        return false;
    }
    if (!is_control())
    {
        return false;
    }
    Job j;
    j.is_write = true;
    j.index = index;
    j.sub = sub;
    j.kind = JobKind::PinMode;
    j.channel = static_cast<int>(handle);
    j.write_data = {mode};
    queue(j);
    return true;
}

// ── Pin I/O (per-pin operation objects, driven by the pin's mode)
// ─────────────
const PinIoValue &Ttc2038XsDevice::pin_io_value(size_t handle) const
{
    static const PinIoValue kEmpty{};
    if (handle >= pin_io_values_.size())
    {
        return kEmpty;
    }
    return pin_io_values_[handle];
}

void Ttc2038XsDevice::read_pin_io(size_t handle)
{
    const PinGroup *group = nullptr;
    uint8_t sub = 0;
    if (!resolve_pin_handle(handle, &group, nullptr, &sub) || group == nullptr)
    {
        return;
    }
    if (handle >= pin_mode_values_.size() || !pin_mode_values_[handle].valid)
    {
        return; // mode unknown — caller must read pin modes first
    }
    const ModeBehavior beh = mode_behavior(pin_mode_values_[handle].mode);
    if (beh.function == IoFunction::None)
    {
        return; // unconfigured pin
    }
    pin_io_values_[handle].reading = true;
    Job j;
    j.is_write = false;
    j.index = od::io_operation_index(group->io_pin_group, beh.value_object_id);
    j.sub = sub;
    j.kind = JobKind::PinIo;
    j.channel = static_cast<int>(handle);
    queue(j);
}

void Ttc2038XsDevice::read_all_pin_io()
{
    for (size_t i = 0; i < pin_io_values_.size(); ++i)
    {
        read_pin_io(i);
    }
}

bool Ttc2038XsDevice::write_pin_io(size_t handle, int64_t value)
{
    const PinGroup *group = nullptr;
    uint8_t sub = 0;
    if (!resolve_pin_handle(handle, &group, nullptr, &sub) || group == nullptr)
    {
        return false;
    }
    if (!is_control())
    {
        return false;
    }
    if (handle >= pin_mode_values_.size() || !pin_mode_values_[handle].valid)
    {
        return false;
    }
    const ModeBehavior beh = mode_behavior(pin_mode_values_[handle].mode);
    if (!beh.writable)
    {
        return false;
    }
    // Encode by function: DOP Level is a 1-byte boolean; PWM/LPO duty cycle is
    // a 16-bit value (device units).
    std::vector<uint8_t> data;
    if (beh.function == IoFunction::DigitalOutput)
    {
        data = {static_cast<uint8_t>(value != 0 ? 1 : 0)};
    }
    else
    {
        const auto u = static_cast<uint16_t>(value);
        data = {
            static_cast<uint8_t>(u & 0xFF),
            static_cast<uint8_t>((u >> 8) & 0xFF)};
    }
    Job j;
    j.is_write = true;
    j.index = od::io_operation_index(group->io_pin_group, beh.value_object_id);
    j.sub = sub;
    j.kind = JobKind::PinIo;
    j.channel = static_cast<int>(handle);
    j.write_data = data;
    queue(j);
    return true;
}

// ── Advanced raw access
// ───────────────────────────────────────────────────────
void Ttc2038XsDevice::read_raw(uint16_t index, uint8_t sub)
{
    raw_result_ = "Reading...";
    Job j;
    j.is_write = false;
    j.index = index;
    j.sub = sub;
    j.kind = JobKind::Raw;
    queue(j);
}

void Ttc2038XsDevice::write_raw(
    uint16_t index, uint8_t sub, const std::vector<uint8_t> &data)
{
    raw_result_ = "Writing...";
    Job j;
    j.is_write = true;
    j.index = index;
    j.sub = sub;
    j.kind = JobKind::Raw;
    j.write_data = data;
    queue(j);
}

} // namespace bmu_app::canopen::ttc2038xs
