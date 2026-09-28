// NetMD over Haiku's USB Kit: the raw transport and a roster that reports recorders as they
// come and go.
#pragma once
#include "core/NetMD.h"
#include <USBKit.h>
#include <functional>
#include <mutex>
#include <string>

namespace amp {

class NetMDUsbTransport : public netmd::Transport {
public:
    NetMDUsbTransport() {}
    ~NetMDUsbTransport() override;

    bool Open(const char* path, std::string& error);
    void Close();
    bool IsOpen() const { return fDevice != nullptr; }

    ssize_t VendorIn(uint8_t request, void* data, size_t length) override;
    ssize_t VendorOut(uint8_t request, const void* data, size_t length) override;
    ssize_t BulkOut(const void* data, size_t length) override;

private:
    BUSBDevice* fDevice = nullptr;
    const BUSBEndpoint* fBulkOut = nullptr;
};

// A recorder on the bus: its raw device path and what it is.
struct NetMDDeviceId {
    std::string path;           // "/dev/bus/usb/1/2"
    uint16_t vendor = 0, product = 0;
    std::string name;
};

// Watches the USB bus for NetMD recorders. The callbacks run on the roster's own thread.
class NetMDRoster : public BUSBRoster {
public:
    std::function<void(const NetMDDeviceId&)> onAdded;
    std::function<void(const std::string& path)> onRemoved;

    status_t DeviceAdded(BUSBDevice* device) override;
    void DeviceRemoved(BUSBDevice* device) override;
};

} // namespace amp
