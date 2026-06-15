#pragma once

#include "CanFrame.h"

#include <cstdint>

namespace bmu_app::canopen
{

// ── CANopen function codes (top 4 bits of an 11-bit COB-ID) ──────────────────
enum class FunctionCode : uint8_t
{
    Nmt = 0x0,       // 0x000 broadcast NMT control
    Sync = 0x1,      // 0x080 SYNC / EMCY base
    Time = 0x2,      // 0x100
    Tpdo1 = 0x3,     // 0x180 + node
    Rpdo1 = 0x4,     // 0x200 + node
    Tpdo2 = 0x5,     // 0x280 + node
    Rpdo2 = 0x6,     // 0x300 + node
    Tpdo3 = 0x7,     // 0x380 + node
    Rpdo3 = 0x8,     // 0x400 + node
    Tpdo4 = 0x9,     // 0x480 + node
    Rpdo4 = 0xA,     // 0x500 + node
    SdoTx = 0xB,     // 0x580 + node (server -> client)
    SdoRx = 0xC,     // 0x600 + node (client -> server)
    Heartbeat = 0xE, // 0x700 + node (NMT error control)
};

constexpr uint32_t kNmtCobId = 0x000;
constexpr uint32_t kSyncCobId = 0x080;
constexpr uint32_t kEmcyBase = 0x080;  // 0x080 + node
constexpr uint32_t kSdoTxBase = 0x580; // server response base
constexpr uint32_t kSdoRxBase = 0x600; // client request base
constexpr uint32_t kHeartbeatBase = 0x700;

inline FunctionCode function_code(uint32_t cob_id)
{
    return static_cast<FunctionCode>((cob_id >> 7) & 0x0F);
}

inline uint8_t node_of(uint32_t cob_id)
{
    return static_cast<uint8_t>(cob_id & 0x7F);
}

// ── NMT ──────────────────────────────────────────────────────────────────────
enum class NmtCommand : uint8_t
{
    Start = 0x01, // enter Operational
    Stop = 0x02,  // enter Stopped
    EnterPreOperational = 0x80,
    ResetNode = 0x81,
    ResetCommunication = 0x82,
};

// NMT state as reported in the heartbeat / boot-up message data byte.
enum class NmtState : uint8_t
{
    BootUp = 0x00,
    Stopped = 0x04,
    Operational = 0x05,
    PreOperational = 0x7F,
    Unknown = 0xFF,
};

const char *to_string(NmtState s);

// ── SDO
// ─────────────────────────────────────────────────────────────────────── SDO
// command specifier bits used by expedited/segmented transfers.
namespace sdo
{
constexpr uint8_t kCcsDownload = 0x20; // client -> server write
constexpr uint8_t kCcsUpload = 0x40;   // client -> server read
constexpr uint8_t kScsUploadResp = 0x40;
constexpr uint8_t kScsDownloadResp = 0x60;
constexpr uint8_t kCcsUploadSegment = 0x60;
constexpr uint8_t kScsUploadSegment = 0x00;
constexpr uint8_t kCcsDownloadSegment = 0x00;
constexpr uint8_t kScsDownloadSegment = 0x20;
constexpr uint8_t kAbort = 0x80;

constexpr uint8_t kExpedited = 0x02;
constexpr uint8_t kSizeIndicated = 0x01;
} // namespace sdo

const char *sdo_abort_text(uint32_t abort_code);

} // namespace bmu_app::canopen
