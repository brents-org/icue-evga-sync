// Unit tests for the hardware-free core: EVGA v3 packet layout and the
// iCUE-to-zone mapping. Run with `ctest` from the build directory.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "evga_v3.h"
#include "mapping.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

template <size_t N>
bool Bytes(const std::array<uint8_t, N>& got, std::vector<uint8_t> want) {
    return want.size() == N && std::memcmp(got.data(), want.data(), N) == 0;
}

std::vector<int> Leds(const std::string& spec, int count, int ordinal = 0, int total = 1) {
    std::vector<int> out;
    std::string err;
    if (!mapping::ParseLedSpec(spec, count, ordinal, total, out, err)) return {-1};
    return out;
}

void Packets() {
    // Byte-for-byte what Precision X1 / OpenRGB send.
    CHECK(Bytes(evga::UnlockPacket(), {0x04, 0xC6, 0xEB, 0xEA, 0x15}));

    uint8_t modes[evga::kZoneCount] = {evga::kModeStatic, evga::kModeStatic, 3, evga::kModeOff};
    CHECK(Bytes(evga::ModePacket(modes, true), {0x09, 1, 1, 3, 0, 0xFF, 0xFF, 0xFF, 0xFF, 1}));
    CHECK(Bytes(evga::ModePacket(modes, false), {0x09, 1, 1, 3, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0}));

    evga::StaticZone z;
    z.brightness = 0x80;
    z.color = {0x12, 0x34, 0x56};
    CHECK(Bytes(evga::StaticPacket(z), {0x04, 0x80, 0x12, 0x34, 0x56}));
}

void Parsing() {
    const uint8_t layoutBytes[10] = {0x09, 1, 2, 3, 0, 1, 1, 4, 0, 1};
    evga::ZoneLayout layout;
    CHECK(evga::ParseLayout(layoutBytes, sizeof layoutBytes, layout));
    CHECK(layout.mode[0] == 1 && layout.mode[1] == 2 && layout.mode[2] == 3 && layout.mode[3] == 0);
    CHECK(layout.ledCount[0] == 1 && layout.ledCount[2] == 4 && layout.ledCount[3] == 0);
    CHECK(layout.sync);
    CHECK(!evga::ParseLayout(layoutBytes, 9, layout));

    const uint8_t fw[6] = {5, 0, 0, 0, 0x02, 0x11};
    uint8_t major = 0, minor = 0;
    CHECK(evga::ParseFirmware(fw, sizeof fw, major, minor) && major == 2 && minor == 0x11);

    // A static read round-trips into the packet that restores it.
    const uint8_t st[5] = {0x04, 0xFF, 0x10, 0x20, 0x30};
    evga::StaticZone s;
    CHECK(evga::ParseStatic(st, sizeof st, s));
    CHECK(Bytes(evga::StaticPacket(s), {0x04, 0xFF, 0x10, 0x20, 0x30}));
}

void SupportedCards() {
    const auto* ftw3 = evga::FindSupportedCard(0x10DE, 0x2208, 0x3842, 0x3967);
    CHECK(ftw3 && std::strstr(ftw3->name, "3080 Ti FTW3 Ultra"));
    CHECK(!evga::FindSupportedCard(0x10DE, 0x2208, 0x10DE, 0x3967));  // NVIDIA Founders Edition subvendor
    CHECK(!evga::FindSupportedCard(0x1002, 0x2208, 0x3842, 0x3967));  // not NVIDIA
    CHECK(!evga::FindSupportedCard(0x10DE, 0x2208, 0x3842, 0xFFFF));  // unknown EVGA board
}

void LedSpecs() {
    CHECK((Leds("all", 4) == std::vector<int>{0, 1, 2, 3}));
    CHECK((Leds("", 2) == std::vector<int>{0, 1}));
    CHECK((Leds(" 0-2, 5 ", 8) == std::vector<int>{0, 1, 2, 5}));
    CHECK((Leds("3,1-3", 8) == std::vector<int>{1, 2, 3}));
    CHECK((Leds("7", 8) == std::vector<int>{7}));
    CHECK((Leds("8", 8) == std::vector<int>{-1}));    // out of range
    CHECK((Leds("3-1", 8) == std::vector<int>{-1}));  // reversed
    CHECK((Leds("a-b", 8) == std::vector<int>{-1}));
    CHECK((Leds("1,,2", 8) == std::vector<int>{-1}));
    CHECK((Leds("all", 0) == std::vector<int>{-1}));

    // auto: even contiguous segments covering every LED exactly once.
    CHECK((Leds("auto", 8, 0, 3) == std::vector<int>{0, 1}));
    CHECK((Leds("auto", 8, 1, 3) == std::vector<int>{2, 3, 4}));
    CHECK((Leds("AUTO", 8, 2, 3) == std::vector<int>{5, 6, 7}));
    // Fewer LEDs than zones: zones share rather than go dark.
    CHECK((Leds("auto", 1, 2, 3) == std::vector<int>{0}));
    CHECK((Leds("auto", 2, 1, 4) == std::vector<int>{0}));
    CHECK((Leds("auto", 2, 3, 4) == std::vector<int>{1}));
}

void Devices() {
    std::vector<mapping::Device> devs = {
        {"k", "K70 RGB PRO", mapping::kKeyboard, 120},
        {"h", "Commander CORE", mapping::kCooler, 0},  // no LEDs: never a source
        {"r", "VENGEANCE RGB PRO", mapping::kMemoryModule, 10},
        {"f", "LL120 RGB Fan", mapping::kFanLedController, 64},
    };
    CHECK(mapping::PickDevice(devs, "") == 3);  // fan hub outranks RAM and keyboard
    CHECK(mapping::PickDevice(devs, "type:memory") == 2);
    CHECK(mapping::PickDevice(devs, "TYPE:Keyboard") == 0);
    CHECK(mapping::PickDevice(devs, "type:cooler") == -1);
    CHECK(mapping::PickDevice(devs, "type:nonsense") == -1);
    CHECK(mapping::PickDevice(devs, "ll120") == 3);
    CHECK(mapping::PickDevice(devs, "  vengeance ") == 2);
    CHECK(mapping::PickDevice(devs, "commander") == -1);
    CHECK(mapping::PickDevice(devs, "nope") == -1);
    CHECK(mapping::PickDevice({}, "") == -1);

    unsigned type = 0;
    CHECK(mapping::TypeFromName("fan", type) && type == mapping::kFanLedController);
    CHECK(std::strcmp(mapping::TypeName(mapping::kCooler), "cooler") == 0);
}

void Blending() {
    std::vector<evga::Rgb> c = {{255, 0, 0}, {0, 0, 255}, {10, 20, 30}};
    CHECK((mapping::Blend(c, {2}) == evga::Rgb{10, 20, 30}));
    // RMS: red + blue gives a bright purple, not the (128,0,128) a plain mean would.
    CHECK((mapping::Blend(c, {0, 1}) == evga::Rgb{180, 0, 180}));
    CHECK((mapping::Blend(c, {}) == evga::Rgb{0, 0, 0}));
    CHECK((mapping::Blend(c, {2, 99, -1}) == evga::Rgb{10, 20, 30}));
}

}  // namespace

int main() {
    Packets();
    Parsing();
    SupportedCards();
    LedSpecs();
    Devices();
    Blending();
    if (g_failures == 0) std::printf("all sync_core tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
