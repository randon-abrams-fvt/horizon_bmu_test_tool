#include "Ttc2038Xs.h"

namespace bmu_app::canopen::ttc2038xs
{

SrdoStatus check_srdo(const CanFrame &normal, const CanFrame &inverted)
{
    SrdoStatus status;
    status.present = true;
    status.length = normal.dlc < inverted.dlc ? normal.dlc : inverted.dlc;

    bool ok = (normal.dlc == inverted.dlc);
    for (uint8_t i = 0; i < 8; ++i)
    {
        status.normal[i] = normal.data[i];
        status.inverted[i] = inverted.data[i];
    }
    for (uint8_t i = 0; i < status.length && ok; ++i)
    {
        if (static_cast<uint8_t>(~normal.data[i]) != inverted.data[i])
        {
            ok = false;
        }
    }
    status.valid = ok;
    return status;
}

const std::vector<PinGroup> &pin_groups()
{
    // Curated from Ttc2038Xs.eds (pin lists) and Ttc2038Xs.html (allowed mode
    // codes per group). Order is stable. The Pin Mode object index is
    // 0x3000 + (io_pin_group << 7).
    static const std::vector<PinModeOption> kAdc4Modes = {
        {0, "Not used"},
        {1, "ADC Voltage"},
        {2, "ADC Current"},
        {3, "ADC Ratiometric"},
        {4, "ADC Resistance"},
        {5, "DIN"},
    };
    static const std::vector<PinModeOption> kTinSentModes = {
        {0, "Not used"},
        {1, "ADC Voltage"},
        {5, "DIN"},
        {14, "SENT"},
        {15, "TIN PWD"},
        {17, "TIN CNT"},
        {18, "TIN INC"},
    };
    static const std::vector<PinModeOption> kTinClModes = {
        {0, "Not used"},
        {1, "ADC Voltage"},
        {2, "ADC Current"},
        {5, "DIN"},
        {15, "TIN PWD"},
        {16, "TIN PWD CL"},
        {17, "TIN CNT"},
        {18, "TIN INC"},
    };
    static const std::vector<PinModeOption> kDopModes = {
        {0, "Not used"},
        {1, "ADC Voltage"},
        {5, "DIN"},
        {6, "DOP"},
        {8, "LPO PVG"},
        {9, "LPO VOUT"},
        {10, "LPO VOUT PID"},
        {11, "LPO ESO"},
    };
    static const std::vector<PinModeOption> kPwmHsModes = {
        {0, "Not used"},
        {1, "ADC Voltage"},
        {5, "DIN"},
        {12, "PWM"},
        {13, "PWM PID"},
    };
    static const std::vector<PinModeOption> kPwmLsModes = {
        {0, "Not used"},
        {1, "ADC Voltage"},
        {3, "ADC Ratiometric"},
        {5, "DIN"},
        {12, "PWM"},
        {13, "PWM PID"},
    };

    static const std::vector<PinGroup> kGroups = {
        {"ADC 4-mode",
         0,
         od::pin_mode_index(0),
         {{1, "J4"},
          {2, "H4"},
          {3, "E4"},
          {4, "D4"},
          {5, "C4"},
          {6, "B4"},
          {7, "A3"},
          {8, "A4"}},
         kAdc4Modes},
        {"TIN/SENT",
         2,
         od::pin_mode_index(2),
         {{1, "E3"}, {2, "D3"}, {3, "C3"}, {4, "B3"}},
         kTinSentModes},
        {"TIN/CL",
         3,
         od::pin_mode_index(3),
         {{1, "G4"}, {2, "F4"}},
         kTinClModes},
        {"DOP HS 4A+CS/LPO",
         4,
         od::pin_mode_index(4),
         {{1, "K2"}, {2, "J2"}, {3, "H2"}, {4, "G2"}, {5, "F2"}, {6, "E2"}},
         kDopModes},
        {"PWM HS 4A+CM+FM",
         5,
         od::pin_mode_index(5),
         {{1, "K1"},
          {2, "J1"},
          {3, "H1"},
          {4, "G1"},
          {5, "F1"},
          {6, "E1"},
          {7, "D1"},
          {8, "C1"}},
         kPwmHsModes},
        {"PWM LS 4A+CM+FM",
         8,
         od::pin_mode_index(8),
         {{1, "A1"}, {2, "B1"}},
         kPwmLsModes},
    };
    return kGroups;
}

const char *pin_mode_name(const PinGroup &group, uint8_t value)
{
    for (const auto &m : group.modes)
    {
        if (m.value == value)
        {
            return m.name;
        }
    }
    return "?";
}

const char *to_string(IoFunction f)
{
    switch (f)
    {
    case IoFunction::None:
        return "Unused";
    case IoFunction::DigitalOutput:
        return "Digital Output";
    case IoFunction::DigitalInput:
        return "Digital Input";
    case IoFunction::AnalogInput:
        return "Analog Input";
    case IoFunction::PwmOutput:
        return "PWM Output";
    case IoFunction::LpoOutput:
        return "Low Power Output";
    case IoFunction::TimerInput:
        return "Timer Input";
    case IoFunction::SentInput:
        return "SENT Input";
    }
    return "?";
}

ModeBehavior mode_behavior(uint8_t mode)
{
    // Operation-function object-IDs (added to group_base + 0x20):
    //   DIN Level = 1, ADC Voltage = 2, ADC Current = 3, ADC Resistance = 4,
    //   SENT Fast Data = 4, LPO/PWM Duty Cycle = 9, DOP Level = 15.
    // Mode codes are shared across all pin groups (see Ttc2038Xs.html).
    switch (mode)
    {
    case 0: // Not used
        return {IoFunction::None, false, 0, ""};
    case 1: // ADC Voltage
        return {IoFunction::AnalogInput, false, 2, "mV"};
    case 2: // ADC Current
        return {IoFunction::AnalogInput, false, 3, "mA"};
    case 3: // ADC Ratiometric (voltage)
        return {IoFunction::AnalogInput, false, 2, "mV"};
    case 4: // ADC Resistance
        return {IoFunction::AnalogInput, false, 4, "Ohm"};
    case 5: // DIN
        return {IoFunction::DigitalInput, false, 1, ""};
    case 6: // DOP
        return {IoFunction::DigitalOutput, true, 15, ""};
    case 8:  // LPO PVG
    case 9:  // LPO VOUT
    case 10: // LPO VOUT PID
    case 11: // LPO ESO
        return {IoFunction::LpoOutput, true, 9, "%"};
    case 12: // PWM
    case 13: // PWM PID
        return {IoFunction::PwmOutput, true, 9, "%"};
    case 14: // SENT
        return {IoFunction::SentInput, false, 4, ""};
    case 15: // TIN PWD
    case 16: // TIN PWD CL
    case 17: // TIN CNT
    case 18: // TIN INC
        return {IoFunction::TimerInput, false, 1, ""};
    default:
        return {IoFunction::None, false, 0, ""};
    }
}

} // namespace bmu_app::canopen::ttc2038xs
