#include "evga_v3.h"

namespace evga {
namespace {

constexpr uint16_t kNvidiaVendor = 0x10DE;
constexpr uint16_t kEvgaSubVendor = 0x3842;

const SupportedCard kSupportedCards[] = {
#include "supported_cards.inc"
};

}  // namespace

const char* const kZoneNames[kZoneCount] = {
    "Front Logo",
    "End Plate Logo",
    "Back Logo",
    "Addressable Header",
};

std::array<uint8_t, 5> UnlockPacket() {
    return {0x04, 0xC6, 0xEB, 0xEA, 0x15};
}

std::array<uint8_t, 10> ModePacket(const uint8_t (&modes)[kZoneCount], bool sync) {
    // [len, mode z1..z4, led count z1..z4 (kKeep), zone sync]
    std::array<uint8_t, 10> pkt;
    pkt.fill(kKeep);
    pkt[0] = uint8_t(pkt.size() - 1);
    for (int z = 0; z < kZoneCount; ++z) pkt[1 + z] = modes[z];
    pkt[9] = sync ? 1 : 0;
    return pkt;
}

std::array<uint8_t, 5> StaticPacket(const StaticZone& zone) {
    return {0x04, zone.brightness, zone.color.r, zone.color.g, zone.color.b};
}

bool ParseFirmware(const uint8_t* buf, size_t len, uint8_t& major, uint8_t& minor) {
    if (len < 6) return false;
    major = buf[4];
    minor = buf[5];
    return true;
}

bool ParseLayout(const uint8_t* buf, size_t len, ZoneLayout& out) {
    if (len < 10) return false;
    for (int z = 0; z < kZoneCount; ++z) {
        out.mode[z] = buf[1 + z];
        out.ledCount[z] = buf[5 + z];
    }
    out.sync = buf[9] != 0;
    return true;
}

bool ParseStatic(const uint8_t* buf, size_t len, StaticZone& out) {
    if (len < 5) return false;
    out.brightness = buf[1];
    out.color = {buf[2], buf[3], buf[4]};
    return true;
}

const SupportedCard* FindSupportedCard(uint16_t vendor, uint16_t device, uint16_t subVendor, uint16_t subDevice) {
    if (vendor != kNvidiaVendor || subVendor != kEvgaSubVendor) return nullptr;
    for (const auto& card : kSupportedCards)
        if (card.device == device && card.subDevice == subDevice) return &card;
    return nullptr;
}

}  // namespace evga
