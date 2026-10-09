#pragma once

#include <cstdint>
#include <vector>

namespace nv {

using GpuHandle = void*;

struct Gpu {
    GpuHandle handle = nullptr;
    uint16_t vendor = 0;
    uint16_t device = 0;
    uint16_t subVendor = 0;
    uint16_t subDevice = 0;
};

// Minimal NvAPI binding: resolves the few I2C entry points from the driver's
// nvapi64.dll through nvapi_QueryInterface, so no NVAPI SDK is needed.
class Api {
public:
    bool Load();
    bool Loaded() const { return write_ != nullptr; }

    std::vector<Gpu> EnumerateGpus() const;

    // Register-addressed transfers on GPU I2C port 1, where board RGB MCUs live.
    bool Read(GpuHandle gpu, uint8_t addr7, uint8_t reg, uint8_t* buf, uint32_t len) const;
    bool Write(GpuHandle gpu, uint8_t addr7, uint8_t reg, const uint8_t* buf, uint32_t len) const;

    int LastStatus() const { return lastStatus_; }

private:
    bool Transfer(bool write, GpuHandle gpu, uint8_t addr7, uint8_t reg, uint8_t* buf, uint32_t len) const;

    void* enumGpus_ = nullptr;
    void* pciIds_ = nullptr;
    void* read_ = nullptr;
    void* write_ = nullptr;
    mutable int lastStatus_ = 0;
};

}  // namespace nv
