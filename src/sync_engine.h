#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "card.h"
#include "config.h"
#include "icue_source.h"

// Copies iCUE's live colors onto the card's zones. Owns the card only while
// iCUE is reachable; otherwise the card runs its own (flash-saved) lighting.
class SyncEngine {
public:
    SyncEngine(const Card& card, icue::Source& source, std::wstring configPath);

    // Snapshots the card's current lighting so it can be handed back later.
    bool Start(std::string& err);
    // One iteration; returns milliseconds until it wants to run again.
    unsigned Tick();
    // The card lost power (sleep, hibernate) and forgot the unlock + modes.
    void OnResume();
    // Final hand-back on exit or logoff. Safe to call twice.
    void Shutdown();

private:
    struct DeviceSlot {
        std::string id;
        std::string model;
        std::vector<unsigned> ledIds;
        std::vector<evga::Rgb> colors;
    };
    struct ZoneBinding {
        bool active = false;  // config wants this zone driven
        int slot = -1;        // index into slots_, -1 = unbound
        std::vector<int> leds;
    };

    void ReloadConfigIfChanged(bool force);
    void Resolve();
    bool TakeOver();
    void Release(const char* why);
    void DesiredModes(uint8_t (&modes)[evga::kZoneCount]) const;
    bool PushFrame();

    const Card& card_;
    icue::Source& source_;
    std::wstring configPath_;
    Config cfg_;
    uint64_t configStamp_ = 0;

    evga::ZoneLayout original_;
    evga::StaticZone originalStatic_[evga::kZoneCount];

    std::vector<DeviceSlot> slots_;
    ZoneBinding zones_[evga::kZoneCount];
    bool needResolve_ = true;     // re-resolve on the next tick, refetching LED ids
    bool retryResolve_ = false;   // re-resolve at nextResolveMs_
    bool refetchLeds_ = false;    // and refetch LED ids then, too
    uint64_t nextResolveMs_ = 0;
    uint64_t nextConfigCheckMs_ = 0;
    uint64_t lastTickMs_ = 0;
    uint64_t nextRefreshMs_ = 0;

    bool owned_ = false;
    uint8_t ownedModes_[evga::kZoneCount] = {};
    evga::Rgb last_[evga::kZoneCount];
    bool lastValid_[evga::kZoneCount] = {};
    int writeFailures_ = 0;
    int lastLoggedState_ = -1;
    std::string lastSampleError_;
    std::vector<std::string> lastReport_;
};
