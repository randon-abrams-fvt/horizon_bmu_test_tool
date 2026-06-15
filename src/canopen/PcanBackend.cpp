#include "PcanBackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace bmu_app::canopen
{

namespace
{

// ── PCANBasic ABI constants (from PCANBasic.h) ───────────────────────────────
using TPCANHandle = uint16_t;
using TPCANStatus = uint32_t;
using TPCANBaudrate = uint16_t;
using TPCANType = uint8_t;
using TPCANParameter = uint8_t;
using TPCANMessageType = uint8_t;

#pragma pack(push, 1)
struct TPCANMsg
{
    uint32_t ID;
    TPCANMessageType MSGTYPE;
    uint8_t LEN;
    uint8_t DATA[8];
};

struct TPCANTimestamp
{
    uint32_t millis;
    uint16_t millis_overflow;
    uint16_t micros;
};
#pragma pack(pop)

constexpr TPCANStatus kPcanErrorOk = 0x00000;
constexpr TPCANStatus kPcanErrorQrcvEmpty = 0x00020;
constexpr TPCANStatus kPcanErrorIllHw = 0x01400;

constexpr TPCANMessageType kMsgStandard = 0x00;
constexpr TPCANMessageType kMsgRtr = 0x01;
constexpr TPCANMessageType kMsgExtended = 0x02;
constexpr TPCANMessageType kMsgErrFrame = 0x40; // CAN error frame
constexpr TPCANMessageType kMsgStatus = 0x80;   // PCAN status frame

// Plug-and-play USB channels PCAN_USBBUS1..16.
constexpr TPCANHandle kUsbBus1 = 0x51;
constexpr int kMaxUsbChannels = 16;

// CAN_GetValue/CAN_SetValue parameters.
constexpr TPCANParameter kParamChannelCondition =
    0x21; // queryable availability
constexpr TPCANParameter kParamListenOnly = 0x08;

constexpr uint32_t kChannelAvailable = 0x01;
constexpr uint8_t kParameterOff = 0x00;
constexpr uint8_t kParameterOn = 0x01;

// Baudrate codes (Btr0Btr1) from PCANBasic.h.
TPCANBaudrate baud_code(PcanBitrate b)
{
    switch (b)
    {
    case PcanBitrate::Kbit_1000:
        return 0x0014;
    case PcanBitrate::Kbit_500:
        return 0x001C;
    case PcanBitrate::Kbit_250:
        return 0x011C;
    case PcanBitrate::Kbit_125:
        return 0x031C;
    case PcanBitrate::Kbit_100:
        return 0x432F;
    case PcanBitrate::Kbit_50:
        return 0x472F;
    case PcanBitrate::Kbit_20:
        return 0x532F;
    case PcanBitrate::Kbit_10:
        return 0x672F;
    }
    return 0x001C;
}

// ── Dynamically loaded entry points ──────────────────────────────────────────
using FnInitialize = TPCANStatus(__stdcall *)(
    TPCANHandle, TPCANBaudrate, TPCANType, uint32_t, uint16_t);
using FnUninitialize = TPCANStatus(__stdcall *)(TPCANHandle);
using FnRead =
    TPCANStatus(__stdcall *)(TPCANHandle, TPCANMsg *, TPCANTimestamp *);
using FnWrite = TPCANStatus(__stdcall *)(TPCANHandle, TPCANMsg *);
using FnGetValue =
    TPCANStatus(__stdcall *)(TPCANHandle, TPCANParameter, void *, uint32_t);
using FnSetValue =
    TPCANStatus(__stdcall *)(TPCANHandle, TPCANParameter, void *, uint32_t);
using FnGetErrorText = TPCANStatus(__stdcall *)(TPCANStatus, uint16_t, char *);

struct PcanApi
{
    bool loaded{false};
    HMODULE module{nullptr};
    FnInitialize initialize{nullptr};
    FnUninitialize uninitialize{nullptr};
    FnRead read{nullptr};
    FnWrite write{nullptr};
    FnGetValue get_value{nullptr};
    FnSetValue set_value{nullptr};
    FnGetErrorText get_error_text{nullptr};
};

const PcanApi &api()
{
    static PcanApi instance = [] {
        PcanApi a;
        a.module = ::LoadLibraryW(L"PCANBasic.dll");
        if (a.module == nullptr)
        {
            return a;
        }
        auto sym = [&](const char *name) {
            return ::GetProcAddress(a.module, name);
        };
        a.initialize = reinterpret_cast<FnInitialize>(sym("CAN_Initialize"));
        a.uninitialize =
            reinterpret_cast<FnUninitialize>(sym("CAN_Uninitialize"));
        a.read = reinterpret_cast<FnRead>(sym("CAN_Read"));
        a.write = reinterpret_cast<FnWrite>(sym("CAN_Write"));
        a.get_value = reinterpret_cast<FnGetValue>(sym("CAN_GetValue"));
        a.set_value = reinterpret_cast<FnSetValue>(sym("CAN_SetValue"));
        a.get_error_text =
            reinterpret_cast<FnGetErrorText>(sym("CAN_GetErrorText"));
        a.loaded = a.initialize && a.uninitialize && a.read && a.write;
        return a;
    }();
    return instance;
}

std::string error_text(TPCANStatus status)
{
    const auto &a = api();
    if (a.get_error_text != nullptr)
    {
        char buf[256] = {};
        if (a.get_error_text(status, 0x09 /*en*/, buf) == kPcanErrorOk)
        {
            return std::string(buf);
        }
    }
    char fallback[32] = {};
    std::snprintf(fallback, sizeof(fallback), "PCAN error 0x%X", status);
    return std::string(fallback);
}

} // namespace

const char *to_string(PcanBitrate b)
{
    switch (b)
    {
    case PcanBitrate::Kbit_1000:
        return "1 Mbit/s";
    case PcanBitrate::Kbit_500:
        return "500 kbit/s";
    case PcanBitrate::Kbit_250:
        return "250 kbit/s";
    case PcanBitrate::Kbit_125:
        return "125 kbit/s";
    case PcanBitrate::Kbit_100:
        return "100 kbit/s";
    case PcanBitrate::Kbit_50:
        return "50 kbit/s";
    case PcanBitrate::Kbit_20:
        return "20 kbit/s";
    case PcanBitrate::Kbit_10:
        return "10 kbit/s";
    }
    return "?";
}

bool PcanChannel::driver_available()
{
    return api().loaded;
}

std::vector<PcanChannelInfo> PcanChannel::list_channels()
{
    std::vector<PcanChannelInfo> result;
    const auto &a = api();
    if (!a.loaded)
    {
        return result;
    }

    for (int i = 0; i < kMaxUsbChannels; ++i)
    {
        const auto handle = static_cast<TPCANHandle>(kUsbBus1 + i);
        uint32_t condition = 0;
        if (a.get_value != nullptr &&
            a.get_value(
                handle,
                kParamChannelCondition,
                &condition,
                sizeof(condition)) == kPcanErrorOk &&
            (condition & kChannelAvailable) != 0)
        {
            PcanChannelInfo info;
            info.handle = handle;
            info.name = "PCAN-USB " + std::to_string(i + 1);
            result.push_back(info);
        }
    }
    return result;
}

PcanChannel::~PcanChannel()
{
    close();
}

bool PcanChannel::open(uint16_t handle, PcanBitrate bitrate, bool listen_only)
{
    const auto &a = api();
    if (!a.loaded)
    {
        last_error_ = "PCANBasic.dll not found (PEAK driver not installed)";
        return false;
    }
    if (open_)
    {
        close();
    }

    const TPCANStatus status =
        a.initialize(handle, baud_code(bitrate), 0, 0, 0);
    if (status != kPcanErrorOk)
    {
        last_error_ = error_text(status);
        return false;
    }

    handle_ = handle;
    open_ = true;
    listen_only_ = listen_only;

    if (a.set_value != nullptr)
    {
        uint8_t value = listen_only ? kParameterOn : kParameterOff;
        a.set_value(handle_, kParamListenOnly, &value, sizeof(value));
    }

    last_error_.clear();
    return true;
}

void PcanChannel::close()
{
    if (!open_)
    {
        return;
    }
    const auto &a = api();
    if (a.uninitialize != nullptr)
    {
        a.uninitialize(handle_);
    }
    open_ = false;
    listen_only_ = false;
    handle_ = 0;
}

bool PcanChannel::read(CanFrame &out)
{
    if (!open_)
    {
        return false;
    }
    const auto &a = api();

    TPCANMsg msg = {};
    TPCANTimestamp ts = {};
    const TPCANStatus status = a.read(handle_, &msg, &ts);
    if (status == kPcanErrorQrcvEmpty)
    {
        return false;
    }
    if (status != kPcanErrorOk)
    {
        // Status frames and bus errors are surfaced as "no data" to the caller;
        // record the text for diagnostics.
        last_error_ = error_text(status);
        return false;
    }

    // CAN_Read can also return OK while delivering a PCAN status frame or a CAN
    // error frame. These are not real bus messages: the four DATA bytes carry a
    // big-endian status/error code (e.g. 0x00000004 = BUSLIGHT). Treat them as
    // diagnostics, not traffic, so they don't appear as a bogus COB-ID 0x001.
    if ((msg.MSGTYPE & (kMsgStatus | kMsgErrFrame)) != 0)
    {
        const uint32_t code = (static_cast<uint32_t>(msg.DATA[0]) << 24) |
                              (static_cast<uint32_t>(msg.DATA[1]) << 16) |
                              (static_cast<uint32_t>(msg.DATA[2]) << 8) |
                              static_cast<uint32_t>(msg.DATA[3]);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "CAN bus status 0x%08X", code);
        last_error_ = buf;
        return false;
    }

    out.id = msg.ID;
    out.extended = (msg.MSGTYPE & kMsgExtended) != 0;
    out.rtr = (msg.MSGTYPE & kMsgRtr) != 0;
    out.dlc = msg.LEN > 8 ? 8 : msg.LEN;
    for (uint8_t i = 0; i < out.dlc; ++i)
    {
        out.data[i] = msg.DATA[i];
    }
    out.timestamp_us = static_cast<uint64_t>(ts.millis) * 1000ull + ts.micros;
    return true;
}

bool PcanChannel::write(const CanFrame &frame)
{
    if (!open_)
    {
        last_error_ = "channel not open";
        return false;
    }
    if (listen_only_)
    {
        last_error_ = "channel is in listen-only (monitor) mode";
        return false;
    }
    const auto &a = api();

    TPCANMsg msg = {};
    msg.ID = frame.id;
    msg.MSGTYPE = frame.extended ? kMsgExtended : kMsgStandard;
    if (frame.rtr)
    {
        msg.MSGTYPE |= kMsgRtr;
    }
    msg.LEN = frame.dlc > 8 ? 8 : frame.dlc;
    for (uint8_t i = 0; i < msg.LEN; ++i)
    {
        msg.DATA[i] = frame.data[i];
    }

    const TPCANStatus status = a.write(handle_, &msg);
    if (status != kPcanErrorOk)
    {
        last_error_ = error_text(status);
        return false;
    }
    return true;
}

} // namespace bmu_app::canopen
