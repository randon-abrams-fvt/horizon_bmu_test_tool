#include "CanOpenDefs.h"

namespace bmu_app::canopen
{

const char *to_string(NmtState s)
{
    switch (s)
    {
    case NmtState::BootUp:
        return "Boot-up";
    case NmtState::Stopped:
        return "Stopped";
    case NmtState::Operational:
        return "Operational";
    case NmtState::PreOperational:
        return "Pre-operational";
    case NmtState::Unknown:
    default:
        return "Unknown";
    }
}

const char *sdo_abort_text(uint32_t abort_code)
{
    switch (abort_code)
    {
    case 0x05030000:
        return "Toggle bit not alternated";
    case 0x05040000:
        return "SDO protocol timed out";
    case 0x05040001:
        return "Invalid/unknown command specifier";
    case 0x06010000:
        return "Unsupported access to an object";
    case 0x06010001:
        return "Attempt to read a write-only object";
    case 0x06010002:
        return "Attempt to write a read-only object";
    case 0x06020000:
        return "Object does not exist in the dictionary";
    case 0x06040041:
        return "Object cannot be mapped to the PDO";
    case 0x06060000:
        return "Access failed due to a hardware error";
    case 0x06070010:
        return "Data type/length mismatch";
    case 0x06070012:
        return "Data type mismatch, length too high";
    case 0x06070013:
        return "Data type mismatch, length too low";
    case 0x06090011:
        return "Sub-index does not exist";
    case 0x06090030:
        return "Value range of parameter exceeded";
    case 0x08000000:
        return "General error";
    case 0x08000020:
        return "Data cannot be transferred to the application";
    case 0x08000022:
        return "Data cannot be transferred (device state)";
    default:
        return "SDO abort";
    }
}

} // namespace bmu_app::canopen
