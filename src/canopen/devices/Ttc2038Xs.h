#pragma once

#include "../CanFrame.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace bmu_app::canopen::ttc2038xs
{

// Object dictionary entries used by the tool, taken from Ttc2038Xs.eds.
namespace od
{
constexpr uint16_t kDeviceType = 0x1000;
constexpr uint16_t kErrorRegister = 0x1001;
constexpr uint16_t kIdentity = 0x1018;
constexpr uint8_t kIdentityVendorId = 1;
constexpr uint8_t kIdentityProductCode = 2;
constexpr uint8_t kIdentityRevision = 3;
constexpr uint8_t kIdentitySerial = 4;

// 0x6000: Read Digital Input 8-bit, sub1..4 = DI 1-8, 9-16, 17-24, 25-32.
constexpr uint16_t kDigitalInput = 0x6000;
// 0x6200: Write Digital Output 8-bit, sub1 = DO 1-8.
constexpr uint16_t kDigitalOutput = 0x6200;
constexpr uint8_t kDigitalOutputSub = 1;
// 0x6401: Read Analog Input 16-bit, sub1..32 = AI 1..32 (INTEGER16).
constexpr uint16_t kAnalogInput = 0x6401;
constexpr uint8_t kAnalogInputCount = 32;

// SRDO 1 communication parameters (default COB-IDs from EDS [1301]).
constexpr uint16_t kSrdo1CommParams = 0x1301;

// 0x3000: "<I/O Pin Group> - Pin Mode" (UNSIGNED8, rw, one sub-index per pin).
// The I/O pin group is encoded in the object index: index = 0x3000 + (group<<7)
// (see "I/O Block" in the TTC 2038XS user manual). Each pin's mode is a numeric
// code; the allowed codes differ per pin group (see PinModeOption below).
constexpr uint16_t kPinModeBase = 0x3000;
inline constexpr uint16_t pin_mode_index(uint8_t io_pin_group)
{
    return static_cast<uint16_t>(kPinModeBase + (io_pin_group << 7));
}

// Within the 0x3000 I/O block the index decomposes as
//   index = 0x3000 | (io_pin_group << 7) | (object_function << 5) | object_id
// (see "I/O Block" in the user manual). object_function: 0 Config, 1 Operation,
// 2 Safety Config, 3 Safety Operation. Operation objects therefore start at
// group_base + 0x20.
constexpr uint16_t kIoOperationOffset = 0x20;
inline constexpr uint16_t io_operation_index(
    uint8_t io_pin_group, uint8_t object_id)
{
    return static_cast<uint16_t>(
        kPinModeBase + (io_pin_group << 7) + kIoOperationOffset + object_id);
}
} // namespace od

// Functional section a configured pin belongs to in the Control tab.
enum class IoFunction
{
    None,          // "Not used" — pin is unconfigured
    DigitalOutput, // DOP            — interactive (write level)
    DigitalInput,  // DIN            — read-only level
    AnalogInput,   // ADC modes      — read-only measurement
    PwmOutput,     // PWM / PWM PID  — interactive (duty cycle)
    LpoOutput,     // LPO PVG/VOUT   — interactive (duty cycle)
    TimerInput,    // TIN modes      — read-only
    SentInput,     // SENT           — read-only
};

const char *to_string(IoFunction f);

// How one pin mode maps to a Control-tab section and its native operation
// object. value_object_id is the Operation-function object-ID of the primary
// value object for that mode (level / measurement / duty cycle); the concrete
// index is od::io_operation_index(group, value_object_id) at sub = pin sub.
struct ModeBehavior
{
    IoFunction function{IoFunction::None};
    bool writable{false};   // true for outputs the tool can drive
    uint8_t value_object_id{0};
    const char *unit{""};   // display unit for the value ("mV", "mA", "%", ...)
};

// Classify a numeric pin-mode code into its Control-tab behavior.
ModeBehavior mode_behavior(uint8_t mode);


// ── Pin configuration model
// ───────────────────────────────────────────────────
// Static description of the device's configurable I/O pins, derived from
// Ttc2038Xs.eds and Ttc2038Xs.html. Each I/O pin group exposes a "Pin Mode"
// object (0x3000 + group<<7); every physical pin is one sub-index and accepts a
// group-specific set of numeric mode codes.

// One selectable pin-mode value for a pin group ({code, display name}).
struct PinModeOption
{
    uint8_t value;
    const char *name;
};

// One physical pin within a group ({sub-index, terminal label}).
struct PinInfo
{
    uint8_t sub;       // sub-index in the Pin Mode object (1-based)
    const char *label; // terminal/pin name, e.g. "J4"
};

// One I/O pin group: a Pin Mode object plus its pins and allowed modes.
struct PinGroup
{
    const char *name;                            // e.g. "ADC 4-mode"
    uint8_t io_pin_group;                         // group number (index bits)
    uint16_t pin_mode_index;                      // 0x3000 + (group<<7)
    std::vector<PinInfo> pins;                    // pins in sub-index order
    std::vector<PinModeOption> modes;             // allowed mode codes
};

// The curated set of configurable I/O pin groups (stable order).
const std::vector<PinGroup> &pin_groups();

// Human-readable name for a mode code within a group ("?" if not allowed).
const char *pin_mode_name(const PinGroup &group, uint8_t value);

// Default safety-related data object COB-IDs (Ttc2038Xs.eds [1301]).
// SRDO frames travel as a pair: a "normal" frame and a bit-inverted frame.
constexpr uint32_t kSrdo1NormalCobId = 257;   // 0x101
constexpr uint32_t kSrdo1InvertedCobId = 258; // 0x102

// Default safety timing from the EDS (informational, for display).
constexpr uint16_t kDefaultRefreshTimeMs = 50;
constexpr uint16_t kDefaultValidationTimeMs = 20;

// Decoded engineering view of the module's I/O.
struct IoSnapshot
{
    uint32_t digital_inputs{0}; // bits 0..31 = DI 1..32
    std::array<int16_t, od::kAnalogInputCount> analog_inputs{};
    uint8_t digital_outputs{0}; // last commanded DO 1..8
    bool inputs_valid{false};
    bool analog_valid{false};
};

// SRDO safety-data validation result for a captured normal/inverted pair.
struct SrdoStatus
{
    bool present{false}; // both frames have been observed
    bool valid{false};   // inverted frame is the bit-complement of normal
    std::array<uint8_t, 8> normal{};
    std::array<uint8_t, 8> inverted{};
    uint8_t length{0};
};

// Validates an SRDO pair: the second frame must be the bit-wise inverse of the
// first over its valid length (CANopen Safety, EN 50325-5).
SrdoStatus check_srdo(const CanFrame &normal, const CanFrame &inverted);

} // namespace bmu_app::canopen::ttc2038xs
