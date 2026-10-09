#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// EVGA "v3" RGB MCU used on RTX 30-series (Ampere) boards: 7-bit I2C address
// 0x2D on GPU port 1. Register map follows OpenRGB's EVGAGPUv3Controller,
// which was derived from Precision X1 traffic captures.
namespace evga {

constexpr uint8_t kI2cAddress = 0x2D;
constexpr int kZoneCount = 4;
constexpr uint8_t kKeep = 0xFF;  // filler byte: "leave this field unchanged"

enum Register : uint8_t {
    kRegFirmware = 0xB1,
    kRegEnable = 0xB2,
    kRegMode = 0xC0,
    kRegStatic = 0xC1,  // + zone index
    // 0x90 commits the live config to flash. Deliberately never written:
    // this tool streams colors many times a second and must not wear the part.
};

enum ZoneMode : uint8_t {
    kModeOff = 0,
    kModeStatic = 1,
};

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

struct ZoneLayout {
    uint8_t mode[kZoneCount] = {};
    uint8_t ledCount[kZoneCount] = {};
    bool sync = false;
};

struct StaticZone {
    uint8_t brightness = 0xFF;
    Rgb color;
};

struct SupportedCard {
    uint16_t device;
    uint16_t subDevice;
    const char* name;
};

extern const char* const kZoneNames[kZoneCount];

// The MCU ignores writes until it sees this unlock packet on 0xB2; it has to be
// re-sent after the card loses power (boot, sleep/resume).
std::array<uint8_t, 5> UnlockPacket();
std::array<uint8_t, 10> ModePacket(const uint8_t (&modes)[kZoneCount], bool sync);
std::array<uint8_t, 5> StaticPacket(const StaticZone& zone);

bool ParseFirmware(const uint8_t* buf, size_t len, uint8_t& major, uint8_t& minor);
bool ParseLayout(const uint8_t* buf, size_t len, ZoneLayout& out);
bool ParseStatic(const uint8_t* buf, size_t len, StaticZone& out);

// nullptr unless the PCI ids are an EVGA Ampere board known to carry this MCU.
const SupportedCard* FindSupportedCard(uint16_t vendor, uint16_t device, uint16_t subVendor, uint16_t subDevice);

}  // namespace evga
