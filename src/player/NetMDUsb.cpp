#include "NetMDUsb.h"
#include <OS.h>

namespace amp {

namespace {

// BUSBDevice::Location() is relative to the USB device root ("/1/2").
std::string DevicePath(const BUSBDevice* device)
{
    std::string location = device->Location() ? device->Location() : "";
    if (location.compare(0, 5, "/dev/") == 0)
        return location;
    return "/dev/bus/usb" + location;
}

} // namespace

NetMDUsbTransport::~NetMDUsbTransport()
{
    Close();
}

bool NetMDUsbTransport::Open(const char* path, std::string& error)
{
    Close();
    BUSBDevice* device = new BUSBDevice(path);
    if (device->InitCheck() != B_OK) {
        delete device;
        error = "cannot open the recorder's USB device";
        return false;
    }
    const BUSBConfiguration* configuration = device->ActiveConfiguration();
    if (!configuration && device->CountConfigurations() > 0) {
        configuration = device->ConfigurationAt(0);
        if (device->SetConfiguration(configuration) != B_OK)
            configuration = nullptr;
    }
    const BUSBInterface* interface = configuration && configuration->CountInterfaces() > 0
        ? configuration->InterfaceAt(0) : nullptr;
    const BUSBEndpoint* bulkOut = nullptr;
    for (uint32 i = 0; interface && i < interface->CountEndpoints(); i++) {
        const BUSBEndpoint* endpoint = interface->EndpointAt(i);
        if (endpoint && endpoint->IsBulk() && endpoint->IsOutput()) {
            bulkOut = endpoint;
            break;
        }
    }
    if (!bulkOut) {
        delete device;
        error = "the recorder has no bulk data endpoint";
        return false;
    }
    fDevice = device;
    fBulkOut = bulkOut;
    return true;
}

void NetMDUsbTransport::Close()
{
    delete fDevice; // owns the configuration, interface and endpoint objects
    fDevice = nullptr;
    fBulkOut = nullptr;
}

ssize_t NetMDUsbTransport::VendorIn(uint8_t request, void* data, size_t length)
{
    if (!fDevice)
        return B_NO_INIT;
    return fDevice->ControlTransfer(0xc1, request, 0, 0, (uint16)length, data);
}

ssize_t NetMDUsbTransport::VendorOut(uint8_t request, const void* data, size_t length)
{
    if (!fDevice)
        return B_NO_INIT;
    return fDevice->ControlTransfer(0x41, request, 0, 0, (uint16)length, const_cast<void*>(data));
}

ssize_t NetMDUsbTransport::BulkOut(const void* data, size_t length)
{
    if (!fBulkOut)
        return B_NO_INIT;
    return fBulkOut->BulkTransfer(const_cast<void*>(data), length);
}

status_t NetMDRoster::DeviceAdded(BUSBDevice* device)
{
    const netmd::KnownDevice* known = netmd::FindKnownDevice(device->VendorID(), device->ProductID());
    if (!known)
        return B_ERROR; // not ours: the roster forgets it
    NetMDDeviceId id;
    id.path = DevicePath(device);
    id.vendor = device->VendorID();
    id.product = device->ProductID();
    id.name = known->name;
    if (onAdded)
        onAdded(id);
    return B_OK;
}

void NetMDRoster::DeviceRemoved(BUSBDevice* device)
{
    if (onRemoved)
        onRemoved(DevicePath(device));
}

} // namespace amp
