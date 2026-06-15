#pragma once

#include "../CanOpenClient.h"
#include "Ttc2038Xs.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace bmu_app::canopen::ttc2038xs
{

// ── Parameter model
// ─────────────────────────────────────────────────────────── The device
// exposes a curated, named set of object-dictionary entries so the UI never has
// to know raw indices, sub-indices or CANopen data types. Each entry is
// described once here; the device handles encode/decode and SDO transport
// behind read_parameter()/write_parameter().

enum class ParamType
{
    U8,
    U16,
    U32,
    I8,
    I16,
    I32,
    String,
};

enum class ParamAccess
{
    ReadOnly,
    ReadWrite,
};

enum class ParamGroup
{
    Identity,      // device identification (read-only)
    Communication, // heartbeat, EMCY, COB-IDs
    DigitalIo,     // digital input/output behaviour
    AnalogIo,      // analog input behaviour
    Safety,        // safety switch / safe-state / temperature limits
};

const char *to_string(ParamGroup g);

// Static description of one configurable / readable parameter.
struct ParamDef
{
    const char *name;
    uint16_t index;
    uint8_t sub;
    ParamType type;
    ParamAccess access;
    const char *unit; // "", "ms", "0.1 \xC2\xB0""C", ...
    ParamGroup group;
};

// Live value of a parameter, decoded into a common representation.
struct ParamValue
{
    bool valid{false};
    bool reading{false};
    int64_t integer{0}; // numeric types
    std::string text;   // string types / formatted display
    std::string error;  // populated when the last transfer failed
};

// The curated parameter table (stable order; index == handle used by the API).
const std::vector<ParamDef> &parameter_table();

// Live value of one pin's "Pin Mode" object (object 0x3000 + group<<7).
struct PinModeValue
{
    bool valid{false};
    bool reading{false};
    uint8_t mode{0};   // current numeric mode code read from the device
    std::string error; // populated when the last transfer failed
};

// Live value of one pin's primary operation object (level / measurement / duty
// cycle), addressed by the pin's configured mode. Stored per flat pin handle.
struct PinIoValue
{
    bool valid{false};
    bool reading{false};
    int64_t value{0};  // decoded: bool level, mV/mA/Ohm, or duty-cycle units
    std::string error; // populated when the last transfer failed
};

// ── Decoded device status
// ─────────────────────────────────────────────────────
struct DeviceStatus
{
    // Identity (read once)
    bool identity_valid{false};
    std::string device_name;
    std::string hw_version;
    std::string sw_version;
    uint32_t vendor_id{0};
    uint32_t product_code{0};
    uint32_t revision{0};
    uint32_t serial{0};

    // NMT (from heartbeat / boot-up)
    NmtState nmt{NmtState::Unknown};
    uint32_t heartbeat_count{0};
    // Freshness of the NMT state. The device only emits 0x700 frames on boot-up
    // (data 0x00) and as periodic heartbeats; with the producer heartbeat
    // disabled (0x1017 == 0, the default) only the single boot-up frame is seen,
    // so a latched "Boot-up" may be stale. These let the UI show how old the
    // last 0x700 frame is.
    uint64_t last_heartbeat_us{0}; // steady-clock us of the last 0x700 frame
    uint64_t status_now_us{0};     // steady-clock us sampled in poll()

    // Result of an active NMT-state poll (SDO read of 0x1017). A successful SDO
    // response proves the node is alive and communicating right now, regardless
    // of whether it is emitting heartbeats.
    bool nmt_probe_pending{false};
    bool nmt_probe_done{false};
    bool nmt_probe_alive{false};   // last probe got a response (or SDO abort)
    uint64_t nmt_probe_us{0};      // steady-clock us the probe completed
    std::string nmt_probe_error;   // populated when the probe failed (timeout)

    // Diagnostics
    bool error_register_valid{false};
    uint8_t error_register{0};
    bool board_temp_valid{false};
    int16_t board_temp_decideg{0}; // 0.1 °C steps

    // ECU diagnostic state (0x2400). The device only applies received output
    // values once this reaches the "running" state (startup tests complete).
    bool ecu_state_valid{false};
    uint8_t ecu_state{0};

    // Reporting device for the currently active error (0x2401 / 0x2402).
    bool reporting_valid{false};
    uint8_t reporting_device{0};
    uint32_t reporting_device_status{0};

    // Core component health (0x2403 sub1..13). 0 == OK, non-zero == fault.
    // A failure in any of these is a fatal error and forces the safe state.
    static constexpr int kDeviceStatusCount = 13;
    bool device_status_valid{false};
    std::array<uint32_t, kDeviceStatusCount> device_status{};

    // SMU alarm groups (0x2404 sub1..14). Any non-zero entry means the safe
    // state was activated by the SMU and is therefore non-recoverable until a
    // power cycle.
    static constexpr int kSmuAlarmCount = 14;
    bool smu_alarms_valid{false};
    std::array<uint32_t, kSmuAlarmCount> smu_alarms{};

    // Supply voltages (mV): BAT+ CPU (0x22A9 sub1) and BAT+ Power (0x22C9
    // sub1).
    bool supply_cpu_valid{false};
    uint16_t supply_cpu_mv{0};
    bool supply_power_valid{false};
    uint16_t supply_power_mv{0};

    // Predefined error field (0x1003): most-recent EMCY codes, newest first.
    bool predef_error_valid{false};
    uint8_t predef_error_count{0};
    std::array<uint32_t, 8> predef_errors{};

    // I/O
    bool digital_inputs_valid{false};
    uint32_t digital_inputs{0}; // bits 0..31 = DI 1..32
    bool analog_inputs_valid{false};
    std::array<int16_t, od::kAnalogInputCount> analog_inputs{};
    uint8_t digital_outputs{0}; // last commanded DO 1..8
    bool digital_outputs_status_valid{false};
    uint8_t digital_outputs_status{0}; // DO state read back from the device

    // Safety switch 1
    bool ssw_valid{false};
    uint32_t ssw_status{0};
    uint8_t ssw_enable_feedback{0};

    uint64_t last_status_us{0};
};

// Decoded CANopen error-register bit (CiA 301 object 0x1001).
struct ErrorRegisterBit
{
    const char *name;
    bool set;
};
std::array<ErrorRegisterBit, 8> decode_error_register(uint8_t reg);

// Human-readable category for a CiA 301 EMCY error code (high byte/nibble).
const char *emcy_error_text(uint16_t code);

// Best-effort label for the ECU diagnostic state value (object 0x2400).
const char *ecu_state_text(uint8_t state);

// Decode the low 16 bits of a TTControl status word (SSW status, device
// component status, pin status) into human-readable text. See the "Error
// Status" table in the TTC 2038XS user manual.
const char *error_status_text(uint16_t status);

// Name of one core component status entry (object 0x2403 sub1..13).
const char *device_status_name(int index);

// Name of one SMU alarm group entry (object 0x2404 sub1..14).
const char *smu_alarm_name(int index);

// One candidate node discovered on the bus.
struct DetectedNode
{
    uint8_t node_id{0};
    // Evidence gathered passively from observed COB-IDs.
    bool heartbeat{false}; // 0x700 + id
    bool sdo{false};       // 0x580 + id (SDO server response)
    bool tpdo{false};      // 0x180/280/380/480 + id
    bool emcy{false};      // 0x080 + id
    bool probed{false};    // confirmed by an active SDO probe (0x1000)
    NmtState nmt{NmtState::Unknown};
    uint64_t frames{0}; // total frames attributed to this node
};

// ── Device
// ────────────────────────────────────────────────────────────────────
// High-level wrapper around a CanOpenClient for a single TTC 2038XS node.
// Owns an SDO job sequencer; the UI calls poll() once per frame and otherwise
// uses only the typed methods below. No CANopen indices leak into the UI.
class Ttc2038XsDevice
{
  public:
    explicit Ttc2038XsDevice(CanOpenClient &client);

    void set_node_id(uint8_t node_id);
    uint8_t node_id() const
    {
        return node_id_;
    }

    // Service the SDO sequencer. Call once per UI frame.
    void poll();
    // Drop any queued/in-flight work (e.g. on disconnect).
    void reset();

    bool busy() const;       // an SDO transfer is queued or in flight
    bool is_control() const; // underlying client is in Control mode

    // --- Status / read -------------------------------------------------------
    void refresh_identity(); // queue identity + version reads
    void refresh_status();   // queue DI/AI/temp/error/SSW reads
    void set_auto_refresh(bool on, double interval_s);
    bool auto_refresh() const
    {
        return auto_refresh_;
    }
    const DeviceStatus &status() const
    {
        return status_;
    }

    // --- Control (Control mode only) -----------------------------------------
    bool start_node();
    bool stop_node();
    bool reset_node();
    bool enter_pre_operational();
    bool set_digital_outputs(uint8_t mask);
    void read_digital_output_status();
    bool request_safe_state(bool active);

    // --- NMT state polling ---------------------------------------------------
    // Actively probe the node's liveness by issuing an SDO read of Producer
    // Heartbeat Time (0x1017). A response (data or even an SDO abort) proves the
    // node is alive and communicating now; only a timeout means no response.
    // Results land in DeviceStatus (nmt_probe_*). Needs Control mode.
    bool poll_nmt_state();
    // Configure the device's producer heartbeat (0x1017, milliseconds). Writing
    // a non-zero value makes the device emit periodic heartbeats so its NMT
    // state is reported continuously; 0 disables it. Needs Control mode.
    bool set_producer_heartbeat(uint16_t period_ms);
    // Seconds since the last 0x700 (boot-up/heartbeat) frame, or a negative
    // value if none has been observed.
    double seconds_since_heartbeat() const;

    // Development mode (see TTC 2038XS user manual). When enabled, the device
    // skips SRDO + pin-configuration verification on the transition to
    // Operational. Intended for bench bring-up only — the device emits a
    // warning EMCY (source 0x0009, scope 0xFE, code 0xF00A) while it is active.
    // Both require Control mode and should be issued in NMT Pre-Operational.
    bool enable_development_mode();  // 0x65->0x2011, 0x706C7664->0x2010
    bool disable_development_mode(); // 0x00->0x2011

    // --- Validate & apply pin configuration ----------------------------------
    // Runs the user-manual sequence so the device will accept its configuration
    // and be allowed into Operational (this does NOT change the NMT state):
    //   1. write 0xA5 to Configuration Valid (0x13FE)
    //   2. read the computed signature from Pin Configuration Signature (0x2000)
    //   3. write that signature to Safety Pin Configuration Signature (0x2010)
    // Must be run in NMT Pre-Operational, in Control mode, after pin modes have
    // been written. Progress/result is exposed via apply_config_state().
    enum class ApplyConfigState
    {
        Idle,
        WritingConfigValid, // step 1 in flight
        ReadingSignature,   // step 2 in flight
        WritingSignature,   // step 3 in flight
        Done,
        Failed,
    };
    bool apply_configuration();
    ApplyConfigState apply_config_state() const
    {
        return apply_state_;
    }
    // Human-readable status / last error for the apply sequence.
    const std::string &apply_config_message() const
    {
        return apply_message_;
    }
    // The signature read back from 0x2000 (valid once past ReadingSignature).
    uint32_t apply_config_signature() const
    {
        return apply_signature_;
    }

    // --- Configuration -------------------------------------------------------
    const std::vector<ParamDef> &parameters() const
    {
        return parameter_table();
    }
    const ParamValue &parameter_value(size_t handle) const;
    void read_parameter(size_t handle);
    void read_all_parameters();
    bool write_parameter(size_t handle, int64_t value);
    bool store_parameters();    // 0x1010 "save"
    bool restore_defaults();    // 0x1011 "load"
    bool clear_error_history(); // write 0 to 0x1003 sub0

    // --- Pin configuration ---------------------------------------------------
    // Per-pin "Pin Mode" (object 0x3000 + group<<7). Pins are addressed by a
    // flat handle that enumerates pin_groups() in order; pin_mode_handle()
    // converts a (group index, pin index) pair to that handle.
    static size_t pin_mode_handle(size_t group_index, size_t pin_index);
    size_t pin_mode_count() const;
    const PinModeValue &pin_mode_value(size_t handle) const;
    void read_pin_mode(size_t handle);
    void read_all_pin_modes();
    bool write_pin_mode(size_t handle, uint8_t mode);

    // --- Pin I/O (Control tab) -----------------------------------------------
    // Read/write a configured pin's primary operation object, chosen from the
    // pin's current Pin Mode (DOP Level, DIN Level, ADC measurement, PWM/LPO
    // duty cycle, SENT). Addressed by the same flat pin handle as pin modes.
    // These rely on a prior read_pin_mode()/read_all_pin_modes() so the device
    // knows each pin's mode; pins read as "Not used" are skipped.
    const PinIoValue &pin_io_value(size_t handle) const;
    void read_pin_io(size_t handle);
    // Queue reads for every configured pin whose function is read-relevant
    // (all configured pins). Used by the Control tab's refresh.
    void read_all_pin_io();
    bool write_pin_io(size_t handle, int64_t value);

    // --- Node discovery ------------------------------------------------------
    // Passive: infer node IDs from the COB-IDs seen in observed traffic. Works
    // in Monitor mode and never disturbs the bus.
    std::vector<DetectedNode> detect_nodes() const;

    // Active: SDO-probe object 0x1000 on each node ID in [first, last]. Needs
    // Control mode. Results stream in via scan_results(); poll progress with
    // scanning()/scan_progress()/scan_total().
    void start_node_scan(uint8_t first = 1, uint8_t last = 127);
    void cancel_node_scan();
    bool scanning() const
    {
        return scanning_;
    }
    int scan_progress() const
    {
        return scan_total_ - scan_remaining_;
    }
    int scan_total() const
    {
        return scan_total_;
    }
    const std::vector<uint8_t> &scan_results() const
    {
        return scan_found_;
    }

    // --- Advanced: raw object access (still serialized through the queue)
    // -----
    void read_raw(uint16_t index, uint8_t sub);
    void write_raw(
        uint16_t index, uint8_t sub, const std::vector<uint8_t> &data);
    const std::string &raw_result() const
    {
        return raw_result_;
    }

  private:
    // What a completed SDO read maps onto, so poll() can decode it.
    enum class JobKind
    {
        IdentityDeviceType,
        IdentityName,
        IdentityHwVersion,
        IdentitySwVersion,
        IdentityVendor,
        IdentityProduct,
        IdentityRevision,
        IdentitySerial,
        ErrorRegister,
        BoardTemp,
        EcuState,              // 0x2400: ECU diagnostic state
        ReportingDevice,       // 0x2401: device reporting the active error
        ReportingDeviceStatus, // 0x2402: status of the reporting device
        DeviceStatusEntry,     // 0x2403 subN: channel = component index 0..12
        SmuAlarmEntry,         // 0x2404 subN: channel = alarm group index 0..13
        SupplyVoltageCpu,      // 0x22A9 sub1: BAT+ CPU voltage (mV)
        SupplyVoltagePower,    // 0x22C9 sub1: BAT+ Power voltage (mV)
        PredefErrorCount,      // 0x1003 sub0: number of stored errors
        PredefErrorEntry,      // 0x1003 subN: channel = entry index 0..N-1
        DigitalInputBlock,     // channel = block 0..3
        AnalogInput,           // channel = AI 0..31
        SswStatus,
        SswEnableFeedback,
        DigitalOutput,
        DigitalOutputStatus, // read-back of the commanded DO state
        Parameter,           // channel = parameter handle
        PinMode,             // channel = flat pin handle (see pin_mode_handle)
        PinIo,               // channel = flat pin handle; per-pin operation obj
        Command,             // fire-and-forget write (store/restore/safe-state)
        Raw,                 // advanced raw object access
        ScanProbe, // node-discovery probe (channel = candidate node id)
        NmtPollProbe, // active NMT liveness probe (SDO read of 0x1017)
        ConfigValidWrite,    // apply step 1: 0xA5 -> 0x13FE
        PinSignatureRead,    // apply step 2: read 0x2000
        SafetySignatureWrite, // apply step 3: signature -> 0x2010
    };

    struct Job
    {
        JobKind kind{JobKind::Command};
        bool is_write{false};
        uint16_t index{0};
        uint8_t sub{0};
        int channel{0};
        uint8_t target_node{0}; // 0 = use node_id_; else address this node
        std::vector<uint8_t> write_data;
    };

    void queue(const Job &job);
    void dispatch_result(const Job &job, const SdoResult &res);
    static int64_t decode_int(ParamType type, const std::vector<uint8_t> &data);
    static std::vector<uint8_t> encode_int(ParamType type, int64_t value);

    CanOpenClient &client_;
    uint8_t node_id_{2};

    std::deque<Job> jobs_;
    bool in_flight_{false};
    Job current_{};

    bool auto_refresh_{false};
    double auto_interval_s_{1.0};
    double last_refresh_s_{0.0};

    DeviceStatus status_{};
    std::vector<ParamValue> param_values_;
    std::vector<PinModeValue> pin_mode_values_;
    std::vector<PinIoValue> pin_io_values_;
    std::string raw_result_;

    // Validate & apply pin-configuration sequence state.
    ApplyConfigState apply_state_{ApplyConfigState::Idle};
    std::string apply_message_;
    uint32_t apply_signature_{0};

    // Active node-scan state.
    bool scanning_{false};
    int scan_total_{0};
    int scan_remaining_{0};
    uint32_t saved_sdo_timeout_ms_{1000};
    std::vector<uint8_t> scan_found_;
};

} // namespace bmu_app::canopen::ttc2038xs
