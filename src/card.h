#pragma once

#include <string>

#include "evga_v3.h"
#include "nvapi_i2c.h"

// An EVGA v3 RGB controller reached through NvAPI I2C.
class Card {
public:
    Card() = default;
    Card(const nv::Api& api, const nv::Gpu& gpu, const evga::SupportedCard& model)
        : api_(&api), gpu_(gpu), model_(&model) {}

    // Finds the first supported board that also answers a firmware read.
    static bool Find(const nv::Api& api, Card& out, std::string& why);

    const char* Name() const { return model_->name; }
    const nv::Gpu& Gpu() const { return gpu_; }

    bool ReadFirmware(uint8_t& major, uint8_t& minor) const;
    bool ReadLayout(evga::ZoneLayout& out) const;
    bool ReadStatic(int zone, evga::StaticZone& out) const;

    bool Unlock() const;
    bool WriteModes(const uint8_t (&modes)[evga::kZoneCount], bool sync) const;
    bool WriteStatic(int zone, const evga::StaticZone& value) const;

    int LastStatus() const { return api_->LastStatus(); }

private:
    // The MCU silently drops a transaction that arrives too soon after the
    // previous one (~5% of back-to-back writes, none at >= 1 ms spacing),
    // and NvAPI still reports success. Every transfer goes through Pace().
    void Pace() const;
    bool Read(uint8_t reg, uint8_t* buf, uint32_t len) const;
    bool Write(uint8_t reg, const uint8_t* buf, uint32_t len) const;

    const nv::Api* api_ = nullptr;
    nv::Gpu gpu_;
    const evga::SupportedCard* model_ = nullptr;
    mutable int64_t lastIo_ = 0;  // QueryPerformanceCounter ticks
};
