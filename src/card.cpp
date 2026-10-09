#include "card.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

namespace {

constexpr int64_t kMinGapUs = 2000;

int64_t QpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int64_t QpcFrequency() {
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return freq;
}

// Sub-millisecond sleeps without spinning or raising the global timer resolution.
void SleepUs(int64_t us) {
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                 TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -us * 10;  // relative, 100 ns units
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
    } else {
        Sleep(DWORD((us + 999) / 1000));
    }
}

}  // namespace

bool Card::Find(const nv::Api& api, Card& out, std::string& why) {
    auto gpus = api.EnumerateGpus();
    if (gpus.empty()) {
        why = "NvAPI reported no physical NVIDIA GPUs";
        return false;
    }
    char buf[160];
    why.clear();
    for (const auto& gpu : gpus) {
        const auto* model = evga::FindSupportedCard(gpu.vendor, gpu.device, gpu.subVendor, gpu.subDevice);
        if (!model) {
            snprintf(buf, sizeof buf, "%04X:%04X sub %04X:%04X is not a known EVGA v3 board; ",
                     gpu.vendor, gpu.device, gpu.subVendor, gpu.subDevice);
            why += buf;
            continue;
        }
        Card card(api, gpu, *model);
        uint8_t major = 0, minor = 0;
        if (!card.ReadFirmware(major, minor)) {
            snprintf(buf, sizeof buf, "%s did not answer at I2C 0x%02X (NvAPI status %d); ",
                     model->name, evga::kI2cAddress, api.LastStatus());
            why += buf;
            continue;
        }
        out = card;
        return true;
    }
    return false;
}

void Card::Pace() const {
    if (lastIo_ != 0) {
        int64_t elapsedUs = (QpcNow() - lastIo_) * 1000000 / QpcFrequency();
        if (elapsedUs < kMinGapUs) SleepUs(kMinGapUs - elapsedUs);
    }
}

bool Card::Read(uint8_t reg, uint8_t* buf, uint32_t len) const {
    Pace();
    bool ok = api_->Read(gpu_.handle, evga::kI2cAddress, reg, buf, len);
    lastIo_ = QpcNow();
    return ok;
}

bool Card::Write(uint8_t reg, const uint8_t* buf, uint32_t len) const {
    Pace();
    bool ok = api_->Write(gpu_.handle, evga::kI2cAddress, reg, buf, len);
    lastIo_ = QpcNow();
    return ok;
}

bool Card::ReadFirmware(uint8_t& major, uint8_t& minor) const {
    uint8_t buf[6] = {};
    return Read(evga::kRegFirmware, buf, sizeof buf) && evga::ParseFirmware(buf, sizeof buf, major, minor);
}

bool Card::ReadLayout(evga::ZoneLayout& out) const {
    uint8_t buf[10] = {};
    return Read(evga::kRegMode, buf, sizeof buf) && evga::ParseLayout(buf, sizeof buf, out);
}

bool Card::ReadStatic(int zone, evga::StaticZone& out) const {
    uint8_t buf[5] = {};
    return Read(uint8_t(evga::kRegStatic + zone), buf, sizeof buf) && evga::ParseStatic(buf, sizeof buf, out);
}

bool Card::Unlock() const {
    auto pkt = evga::UnlockPacket();
    return Write(evga::kRegEnable, pkt.data(), uint32_t(pkt.size()));
}

bool Card::WriteModes(const uint8_t (&modes)[evga::kZoneCount], bool sync) const {
    if (!Unlock()) return false;
    auto pkt = evga::ModePacket(modes, sync);
    return Write(evga::kRegMode, pkt.data(), uint32_t(pkt.size()));
}

bool Card::WriteStatic(int zone, const evga::StaticZone& value) const {
    auto pkt = evga::StaticPacket(value);
    return Write(uint8_t(evga::kRegStatic + zone), pkt.data(), uint32_t(pkt.size()));
}
