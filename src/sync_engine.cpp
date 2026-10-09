#include "sync_engine.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "log.h"
#include "mapping.h"

namespace {

constexpr unsigned kDisconnectedPollMs = 500;
constexpr unsigned kUnboundRetryMs = 5000;
constexpr unsigned kConfigCheckMs = 1000;
// A gap this long between ticks means the machine slept and the card was
// probably power-cycled without us getting WM_POWERBROADCAST.
constexpr uint64_t kResumeGapMs = 10000;
constexpr int kMaxWriteFailures = 10;
constexpr int kRestoreAttempts = 3;
// Unchanged colors are not re-sent, so a write the MCU dropped would stick
// until the color next changes; a periodic full refresh bounds that.
constexpr uint64_t kRefreshMs = 2000;
// Each I2C write costs ~1 ms of CPU inside the NVIDIA driver (it polls the
// bus), so changes no one can see are not worth sending; the refresh above
// still lands the exact color within kRefreshMs.
constexpr int kDeadband = 2;

bool Close(const evga::Rgb& a, const evga::Rgb& b) {
    return std::abs(a.r - b.r) <= kDeadband && std::abs(a.g - b.g) <= kDeadband && std::abs(a.b - b.b) <= kDeadband;
}
// Matches OpenRGB's EVGA v3 direct mode, which drives per-zone colors with
// the zone-sync flag set.
constexpr bool kZoneSync = true;

uint64_t NowMs() {
    return GetTickCount64();
}

uint64_t FileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr)) return 0;
    return (uint64_t(attr.ftLastWriteTime.dwHighDateTime) << 32) | attr.ftLastWriteTime.dwLowDateTime;
}

std::string DescribeLeds(const std::vector<int>& leds) {
    std::string out;
    for (size_t i = 0; i < leds.size();) {
        size_t j = i;
        while (j + 1 < leds.size() && leds[j + 1] == leds[j] + 1) ++j;
        if (!out.empty()) out += ",";
        out += std::to_string(leds[i]);
        if (j > i) out += "-" + std::to_string(leds[j]);
        i = j + 1;
    }
    return out;
}

}  // namespace

SyncEngine::SyncEngine(const Card& card, icue::Source& source, std::wstring configPath)
    : card_(card), source_(source), configPath_(std::move(configPath)) {}

bool SyncEngine::Start(std::string& err) {
    if (!card_.ReadLayout(original_)) {
        err = "could not read the card's zone layout (NvAPI status " + std::to_string(card_.LastStatus()) + ")";
        return false;
    }
    for (int z = 0; z < evga::kZoneCount; ++z) {
        if (!card_.ReadStatic(z, originalStatic_[z])) {
            err = "could not read zone " + std::to_string(z + 1) + "'s static color";
            return false;
        }
        Log("card zone %d %-18s mode %u, %u LEDs", z + 1, evga::kZoneNames[z], original_.mode[z],
            original_.ledCount[z]);
    }
    ReloadConfigIfChanged(true);
    lastTickMs_ = NowMs();
    return true;
}

void SyncEngine::ReloadConfigIfChanged(bool force) {
    uint64_t stamp = FileStamp(configPath_);
    if (!force && stamp == configStamp_) return;
    cfg_ = LoadConfig(configPath_);
    configStamp_ = FileStamp(configPath_);
    if (!force) Log("config changed, reloading");
    needResolve_ = true;
}

void SyncEngine::OnResume() {
    Log("resume detected; re-initialising the card");
    owned_ = false;  // next tick re-sends unlock + modes
    std::fill(std::begin(lastValid_), std::end(lastValid_), false);
}

void SyncEngine::DesiredModes(uint8_t (&modes)[evga::kZoneCount]) const {
    for (int z = 0; z < evga::kZoneCount; ++z)
        modes[z] = (zones_[z].active && zones_[z].slot >= 0) ? evga::kModeStatic : original_.mode[z];
}

void SyncEngine::Resolve() {
    // The timed retry keeps the LED ids of devices it already bound; an event
    // (config, topology, reconnect) or a failed sample fetches them afresh.
    std::vector<DeviceSlot> known;
    if (!needResolve_ && !refetchLeds_) known.swap(slots_);
    needResolve_ = false;
    retryResolve_ = false;
    refetchLeds_ = false;
    slots_.clear();
    for (auto& zb : zones_) zb = ZoneBinding{};

    // Resolve re-runs on every device event and, while a zone is unbound,
    // every few seconds; only report when the outcome changes.
    std::vector<std::string> report;
    auto note = [&report](const char* fmt, auto... args) {
        char buf[256];
        snprintf(buf, sizeof buf, fmt, args...);
        report.emplace_back(buf);
    };

    std::vector<mapping::Device> devices;
    std::string err;
    if (!source_.ListDevices(devices, err)) {
        note("listing iCUE devices failed: %s", err.c_str());
        retryResolve_ = true;
    }

    int deviceOf[evga::kZoneCount];
    for (int z = 0; z < evga::kZoneCount; ++z) {
        const auto& zc = cfg_.zones[z];
        zones_[z].active = zc.enabled == 1 || (zc.enabled == -1 && original_.ledCount[z] > 0);
        deviceOf[z] = zones_[z].active ? mapping::PickDevice(devices, zc.device) : -1;
        if (zones_[z].active && deviceOf[z] < 0) {
            note("zone %d: no iCUE device matches '%s' (%zu devices visible)", z + 1, zc.device.c_str(),
                devices.size());
            retryResolve_ = true;
        }
    }

    for (int z = 0; z < evga::kZoneCount; ++z) {
        if (deviceOf[z] < 0) continue;
        const auto& dev = devices[size_t(deviceOf[z])];

        // "auto" segments are split among the zones that share this device.
        int ordinal = 0, total = 0;
        for (int o = 0; o < evga::kZoneCount; ++o) {
            if (deviceOf[o] != deviceOf[z]) continue;
            if (o < z) ++ordinal;
            ++total;
        }
        std::vector<int> leds;
        std::string specErr;
        if (!mapping::ParseLedSpec(cfg_.zones[z].leds, dev.ledCount, ordinal, total, leds, specErr)) {
            note("zone %d: leds='%s' on %s: %s", z + 1, cfg_.zones[z].leds.c_str(), dev.model.c_str(),
                specErr.c_str());
            retryResolve_ = true;
            continue;
        }

        auto sameId = [&dev](const DeviceSlot& s) { return s.id == dev.id; };
        auto it = std::find_if(slots_.begin(), slots_.end(), sameId);
        if (it == slots_.end()) {
            DeviceSlot slot;
            auto prev = std::find_if(known.begin(), known.end(), sameId);
            if (prev != known.end()) {
                slot = std::move(*prev);
            } else {
                slot.id = dev.id;
                slot.model = dev.model;
                if (!source_.LedIds(dev.id, slot.ledIds, err)) {
                    note("zone %d: %s: %s", z + 1, dev.model.c_str(), err.c_str());
                    retryResolve_ = true;
                    continue;
                }
            }
            slots_.push_back(std::move(slot));
            it = slots_.end() - 1;
        }
        zones_[z].slot = int(it - slots_.begin());
        zones_[z].leds = std::move(leds);
        note("zone %d %-18s <- %s (%s) LEDs %s", z + 1, evga::kZoneNames[z], dev.model.c_str(),
            mapping::TypeName(dev.type), DescribeLeds(zones_[z].leds).c_str());
    }
    if (report != lastReport_) {
        for (const auto& line : report) Log("%s", line.c_str());
        lastReport_ = std::move(report);
    }
    nextResolveMs_ = NowMs() + kUnboundRetryMs;

    // Bindings changed: zones that gained or lost a source need new modes.
    if (owned_) {
        uint8_t modes[evga::kZoneCount];
        DesiredModes(modes);
        if (!std::equal(std::begin(modes), std::end(modes), std::begin(ownedModes_))) owned_ = false;
    }
}

bool SyncEngine::TakeOver() {
    uint8_t modes[evga::kZoneCount];
    DesiredModes(modes);
    if (!card_.WriteModes(modes, kZoneSync)) {
        Log("taking over the card failed (NvAPI status %d)", card_.LastStatus());
        return false;
    }
    std::copy(std::begin(modes), std::end(modes), std::begin(ownedModes_));
    std::fill(std::begin(lastValid_), std::end(lastValid_), false);
    if (!owned_) Log("driving the card from iCUE (%s)", source_.Versions().c_str());
    owned_ = true;
    writeFailures_ = 0;
    return true;
}

void SyncEngine::Release(const char* why) {
    if (!owned_) return;
    owned_ = false;
    if (!cfg_.restore) {
        Log("%s; leaving the last colors on the card (restore=0)", why);
        return;
    }
    // A one-shot hand-back, so read it back rather than trust the write status.
    bool ok = false;
    for (int attempt = 0; attempt < kRestoreAttempts && !ok; ++attempt) {
        for (int z = 0; z < evga::kZoneCount; ++z)
            if (ownedModes_[z] == evga::kModeStatic) card_.WriteStatic(z, originalStatic_[z]);
        card_.WriteModes(original_.mode, original_.sync);
        ok = true;
        evga::ZoneLayout now;
        ok &= card_.ReadLayout(now) && std::equal(std::begin(now.mode), std::end(now.mode), std::begin(original_.mode));
        for (int z = 0; z < evga::kZoneCount && ok; ++z) {
            evga::StaticZone s;
            ok &= card_.ReadStatic(z, s) && s.color == originalStatic_[z].color &&
                  s.brightness == originalStatic_[z].brightness;
        }
    }
    Log("%s; %s the card's own lighting", why, ok ? "restored" : "FAILED to restore");
}

bool SyncEngine::PushFrame() {
    uint64_t now = NowMs();
    if (now >= nextRefreshMs_) {
        std::fill(std::begin(lastValid_), std::end(lastValid_), false);
        nextRefreshMs_ = now + kRefreshMs;
    }
    for (auto& slot : slots_) {
        std::string err;
        if (!source_.Sample(slot.id, slot.ledIds, slot.colors, err)) {
            if (err != lastSampleError_) Log("%s: %s", slot.model.c_str(), err.c_str());
            lastSampleError_ = err;
            // An unplug also raises a topology event, which resolves at once;
            // anything else retries on the unbound clock, not every frame.
            retryResolve_ = true;
            refetchLeds_ = true;
            return false;
        }
    }
    lastSampleError_.clear();

    for (int z = 0; z < evga::kZoneCount; ++z) {
        const auto& zb = zones_[z];
        if (!zb.active || zb.slot < 0) continue;
        evga::Rgb c = mapping::Blend(slots_[size_t(zb.slot)].colors, zb.leds);
        if (lastValid_[z] && Close(c, last_[z])) continue;
        evga::StaticZone value;
        value.brightness = uint8_t(cfg_.brightness);
        value.color = c;
        if (!card_.WriteStatic(z, value)) {
            if (++writeFailures_ == kMaxWriteFailures) {
                Log("%d consecutive I2C write failures (NvAPI status %d); re-initialising", writeFailures_,
                    card_.LastStatus());
                owned_ = false;
            }
            return false;
        }
        writeFailures_ = 0;
        last_[z] = c;
        lastValid_[z] = true;
    }
    return true;
}

unsigned SyncEngine::Tick() {
    uint64_t now = NowMs();
    if (now - lastTickMs_ > kResumeGapMs && owned_) OnResume();
    lastTickMs_ = now;

    source_.Pump();
    if (now >= nextConfigCheckMs_) {
        ReloadConfigIfChanged(false);
        nextConfigCheckMs_ = now + kConfigCheckMs;
    }

    int state = source_.State();
    if (state != lastLoggedState_) {
        Log("iCUE SDK session: %s", icue::StateName(state));
        lastLoggedState_ = state;
    }
    if (!source_.Connected()) {
        Release("iCUE is not reachable");
        needResolve_ = true;
        return kDisconnectedPollMs;
    }

    if (source_.TakeTopologyChanged()) needResolve_ = true;
    if (needResolve_ || (retryResolve_ && now >= nextResolveMs_)) Resolve();

    bool anyBound = std::any_of(std::begin(zones_), std::end(zones_), [](const ZoneBinding& z) { return z.slot >= 0; });
    if (!anyBound) {
        Release("no zone has an iCUE source");
        return kUnboundRetryMs;
    }
    if (!owned_ && !TakeOver()) return kUnboundRetryMs;

    PushFrame();
    unsigned period = 1000u / unsigned(cfg_.fps);
    unsigned spent = unsigned(NowMs() - now);
    return spent >= period ? 1u : period - spent;
}

void SyncEngine::Shutdown() {
    Release("shutting down");
}
