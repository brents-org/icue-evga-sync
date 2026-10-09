#include "nvapi_i2c.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>

namespace nv {
namespace {

// nvapi_QueryInterface ids; these are what the NVAPI SDK's static lib resolves.
constexpr uint32_t kIdInitialize = 0x0150E828;
constexpr uint32_t kIdEnumPhysicalGpus = 0xE5AC921F;
constexpr uint32_t kIdGetPciIdentifiers = 0x2DDFB66E;
constexpr uint32_t kIdI2CWriteEx = 0x283AC65A;
constexpr uint32_t kIdI2CReadEx = 0x4D7B0709;

constexpr int kMaxPhysicalGpus = 64;
constexpr uint8_t kRgbI2cPort = 1;

// NV_I2C_INFO_V3 from nvapi.h.
struct I2CInfoV3 {
    uint32_t version;
    uint32_t displayMask;
    uint8_t isDdcPort;
    uint8_t i2cDevAddress;  // 8-bit form: addr7 << 1
    uint8_t* regAddress;
    uint32_t regAddrSize;
    uint8_t* data;
    uint32_t size;
    uint32_t i2cSpeed;     // deprecated field; 0xFFFF defers to i2cSpeedKhz
    uint32_t i2cSpeedKhz;  // 0 = driver default
    uint8_t portId;
    uint32_t isPortIdSet;
};
static_assert(sizeof(I2CInfoV3) == 64, "NV_I2C_INFO_V3 layout drifted");
constexpr uint32_t kI2CInfoV3Version = sizeof(I2CInfoV3) | (3u << 16);

using QueryInterfaceFn = void*(__cdecl*)(uint32_t);
using InitializeFn = int(__cdecl*)();
using EnumPhysicalGpusFn = int(__cdecl*)(GpuHandle*, uint32_t*);
using GetPciIdentifiersFn = int(__cdecl*)(GpuHandle, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
using I2CExFn = int(__cdecl*)(GpuHandle, I2CInfoV3*, uint32_t*);

}  // namespace

bool Api::Load() {
    if (Loaded()) return true;

    HMODULE dll = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) return false;
    auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(dll, "nvapi_QueryInterface"));
    if (!query) return false;

    auto init = reinterpret_cast<InitializeFn>(query(kIdInitialize));
    enumGpus_ = query(kIdEnumPhysicalGpus);
    pciIds_ = query(kIdGetPciIdentifiers);
    read_ = query(kIdI2CReadEx);
    void* write = query(kIdI2CWriteEx);
    if (!init || !enumGpus_ || !pciIds_ || !read_ || !write) return false;

    lastStatus_ = init();
    if (lastStatus_ != 0) return false;
    write_ = write;
    return true;
}

std::vector<Gpu> Api::EnumerateGpus() const {
    std::vector<Gpu> gpus;
    if (!Loaded()) return gpus;

    GpuHandle handles[kMaxPhysicalGpus] = {};
    uint32_t count = 0;
    lastStatus_ = reinterpret_cast<EnumPhysicalGpusFn>(enumGpus_)(handles, &count);
    if (lastStatus_ != 0) return gpus;

    for (uint32_t i = 0; i < count && i < kMaxPhysicalGpus; ++i) {
        uint32_t deviceId = 0, subsystemId = 0, revision = 0, ext = 0;
        if (reinterpret_cast<GetPciIdentifiersFn>(pciIds_)(handles[i], &deviceId, &subsystemId, &revision, &ext) != 0)
            continue;
        Gpu gpu;
        gpu.handle = handles[i];
        gpu.vendor = uint16_t(deviceId & 0xFFFF);
        gpu.device = uint16_t(deviceId >> 16);
        gpu.subVendor = uint16_t(subsystemId & 0xFFFF);
        gpu.subDevice = uint16_t(subsystemId >> 16);
        gpus.push_back(gpu);
    }
    return gpus;
}

bool Api::Read(GpuHandle gpu, uint8_t addr7, uint8_t reg, uint8_t* buf, uint32_t len) const {
    return Transfer(false, gpu, addr7, reg, buf, len);
}

bool Api::Write(GpuHandle gpu, uint8_t addr7, uint8_t reg, const uint8_t* buf, uint32_t len) const {
    // NvAPI takes a non-const pointer but does not modify the buffer on writes.
    return Transfer(true, gpu, addr7, reg, const_cast<uint8_t*>(buf), len);
}

bool Api::Transfer(bool write, GpuHandle gpu, uint8_t addr7, uint8_t reg, uint8_t* buf, uint32_t len) const {
    if (!Loaded()) return false;

    I2CInfoV3 info;
    std::memset(&info, 0, sizeof info);
    info.version = kI2CInfoV3Version;
    info.i2cDevAddress = uint8_t(addr7 << 1);
    info.regAddress = &reg;
    info.regAddrSize = 1;
    info.data = buf;
    info.size = len;
    info.i2cSpeed = 0xFFFF;
    info.portId = kRgbI2cPort;
    info.isPortIdSet = 1;

    uint32_t unknown = 0;
    auto fn = reinterpret_cast<I2CExFn>(write ? write_ : read_);
    lastStatus_ = fn(gpu, &info, &unknown);
    return lastStatus_ == 0;
}

}  // namespace nv
