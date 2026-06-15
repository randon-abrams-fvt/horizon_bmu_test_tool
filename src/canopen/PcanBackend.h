#pragma once

#include "CanFrame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bmu_app::canopen
{

// Minimal subset of the PEAK PCANBasic API, declared locally so the project
// builds without the PEAK System SDK installed. The real entry points are
// resolved from PCANBasic.dll at runtime (see PcanBackend.cpp). When the DLL
// is absent, the backend reports as unavailable instead of failing to link.

// PCANBasic channel handles (plug-and-play USB busses 1..16).
struct PcanChannelInfo
{
    uint16_t handle{0}; // TPCANHandle value (e.g. 0x51 = PCAN_USBBUS1)
    std::string name;   // human readable, e.g. "PCAN-USB 1"
};

// Supported CANopen bitrates. The TTC 2038XS EDS only declares 500 kbit/s,
// but the tool offers the common set for flexibility on other busses.
enum class PcanBitrate
{
    Kbit_1000,
    Kbit_500,
    Kbit_250,
    Kbit_125,
    Kbit_100,
    Kbit_50,
    Kbit_20,
    Kbit_10,
};

const char *to_string(PcanBitrate b);

// Thin RAII wrapper over a single PCANBasic channel. Not thread-safe; intended
// to be owned and driven by a single worker thread (see CanOpenClient).
class PcanChannel
{
  public:
    PcanChannel() = default;
    ~PcanChannel();

    PcanChannel(const PcanChannel &) = delete;
    PcanChannel &operator=(const PcanChannel &) = delete;

    // True when PCANBasic.dll was found and its entry points resolved.
    static bool driver_available();

    // Enumerate channels the driver reports as attached/available.
    static std::vector<PcanChannelInfo> list_channels();

    // Opens the channel. When listen_only is true the adapter is put into
    // listen-only mode so it never acknowledges or transmits (passive monitor).
    bool open(uint16_t handle, PcanBitrate bitrate, bool listen_only);
    void close();
    bool is_open() const
    {
        return open_;
    }

    // Non-blocking read of a single frame. Returns true when a frame was read.
    bool read(CanFrame &out);

    // Transmits a frame. Fails when not open or in listen-only mode.
    bool write(const CanFrame &frame);

    const std::string &last_error() const
    {
        return last_error_;
    }

  private:
    uint16_t handle_{0};
    bool open_{false};
    bool listen_only_{false};
    std::string last_error_;
};

} // namespace bmu_app::canopen
