#pragma once

#include <string>
#include <vector>

#include "evga_v3.h"

// How a GPU zone's color is derived from what iCUE renders on Corsair devices.
// Kept free of the iCUE SDK so it can be unit tested.
namespace mapping {

// Mirrors CorsairDeviceType bit values (checked by static_assert in icue_source.cpp).
enum DeviceType : unsigned {
    kKeyboard = 0x0001,
    kMouse = 0x0002,
    kMousemat = 0x0004,
    kHeadset = 0x0008,
    kHeadsetStand = 0x0010,
    kFanLedController = 0x0020,
    kLedController = 0x0040,
    kMemoryModule = 0x0080,
    kCooler = 0x0100,
    kMotherboard = 0x0200,
    kGraphicsCard = 0x0400,
    kTouchbar = 0x0800,
    kGameController = 0x1000,
};

struct Device {
    std::string id;
    std::string model;
    unsigned type = 0;
    int ledCount = 0;
};

const char* TypeName(unsigned type);
bool TypeFromName(const std::string& name, unsigned& type);

// selector: "" -> first device by kDefaultPriority, "type:<name>" -> first of
// that type, anything else -> case-insensitive substring of the model name.
// Returns an index into devices, or -1.
int PickDevice(const std::vector<Device>& devices, const std::string& selector);

// spec: "all", "auto" (the zoneOrdinal-th of zoneTotal equal segments), or a
// list such as "0-3,7". Indices refer to the device's LED order in iCUE.
bool ParseLedSpec(const std::string& spec, int ledCount, int zoneOrdinal, int zoneTotal, std::vector<int>& out,
                  std::string& err);

// Root-mean-square per channel: averaging in squared space keeps a mix of
// saturated LEDs bright instead of collapsing toward grey.
evga::Rgb Blend(const std::vector<evga::Rgb>& colors, const std::vector<int>& indices);

}  // namespace mapping
