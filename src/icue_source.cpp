#include "icue_source.h"

#include <iCUESDK/iCUESDK.h>

#include <cstdio>
#include <cstring>

namespace icue {
namespace {

static_assert(mapping::kKeyboard == CDT_Keyboard && mapping::kMouse == CDT_Mouse &&
                  mapping::kMousemat == CDT_Mousemat && mapping::kHeadset == CDT_Headset &&
                  mapping::kHeadsetStand == CDT_HeadsetStand && mapping::kFanLedController == CDT_FanLedController &&
                  mapping::kLedController == CDT_LedController && mapping::kMemoryModule == CDT_MemoryModule &&
                  mapping::kCooler == CDT_Cooler && mapping::kMotherboard == CDT_Motherboard &&
                  mapping::kGraphicsCard == CDT_GraphicsCard && mapping::kTouchbar == CDT_Touchbar &&
                  mapping::kGameController == CDT_GameController,
              "mapping::DeviceType must mirror CorsairDeviceType");

void CopyId(const std::string& id, CorsairDeviceId& out) {
    std::memset(out, 0, sizeof out);
    strncpy_s(out, id.c_str(), sizeof out - 1);
}

std::string Fail(const char* what, CorsairError e) {
    return std::string(what) + ": " + ErrorName(e);
}

}  // namespace

Source::~Source() {
    Disconnect();
}

bool Source::Connect(std::string& err) {
    if (connectCalled_) return true;
    CorsairError e = CorsairConnect(&Source::OnState, this);
    if (e != CE_Success) {
        err = Fail("CorsairConnect", e);
        return false;
    }
    connectCalled_ = true;
    return true;
}

void Source::Disconnect() {
    if (!connectCalled_) return;
    CorsairUnsubscribeFromEvents();
    CorsairDisconnect();
    connectCalled_ = false;
    state_ = CSS_Closed;
}

bool Source::Connected() const {
    return state_.load() == CSS_Connected;
}

void Source::OnState(void* ctx, const CorsairSessionStateChanged* e) {
    auto* self = static_cast<Source*>(ctx);
    self->state_ = e->state;
    if (e->state == CSS_Connected) {
        self->needSubscribe_ = true;
        self->topologyChanged_ = true;
    }
}

void Source::Pump() {
    // Subscribing is only valid once connected and must be redone after every
    // reconnect; it is done here rather than from the SDK's callback thread.
    if (Connected() && needSubscribe_.exchange(false)) {
        CorsairUnsubscribeFromEvents();
        CorsairSubscribeForEvents(&Source::OnEvent, this);
    }
}

void Source::OnEvent(void* ctx, const CorsairEvent* e) {
    if (e->id == CEI_DeviceConnectionStatusChangedEvent) static_cast<Source*>(ctx)->topologyChanged_ = true;
}

bool Source::ListDevices(std::vector<mapping::Device>& out, std::string& err) const {
    out.clear();
    CorsairDeviceFilter filter{CDT_All};
    std::vector<CorsairDeviceInfo> infos(CORSAIR_DEVICE_COUNT_MAX);
    int count = 0;
    CorsairError e = CorsairGetDevices(&filter, int(infos.size()), infos.data(), &count);
    if (e != CE_Success) {
        err = Fail("CorsairGetDevices", e);
        return false;
    }
    for (int i = 0; i < count; ++i) {
        mapping::Device d;
        d.id = infos[i].id;
        d.model = infos[i].model;
        d.type = unsigned(infos[i].type);
        d.ledCount = infos[i].ledCount;
        out.push_back(std::move(d));
    }
    return true;
}

bool Source::LedIds(const std::string& deviceId, std::vector<unsigned>& ids, std::string& err) const {
    CorsairDeviceId id;
    CopyId(deviceId, id);
    std::vector<CorsairLedPosition> pos(CORSAIR_DEVICE_LEDCOUNT_MAX);
    int count = 0;
    CorsairError e = CorsairGetLedPositions(id, int(pos.size()), pos.data(), &count);
    if (e != CE_Success) {
        err = Fail("CorsairGetLedPositions", e);
        return false;
    }
    ids.clear();
    for (int i = 0; i < count; ++i) ids.push_back(pos[i].id);
    return true;
}

bool Source::Sample(const std::string& deviceId, const std::vector<unsigned>& ids, std::vector<evga::Rgb>& out,
                    std::string& err) const {
    CorsairDeviceId id;
    CopyId(deviceId, id);
    std::vector<CorsairLedColor> colors(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) colors[i].id = ids[i];
    CorsairError e = CorsairGetLedColors(id, int(colors.size()), colors.data());
    if (e != CE_Success) {
        err = Fail("CorsairGetLedColors", e);
        return false;
    }
    out.resize(colors.size());
    for (size_t i = 0; i < colors.size(); ++i) out[i] = {colors[i].r, colors[i].g, colors[i].b};
    return true;
}

std::string Source::Versions() const {
    CorsairSessionDetails d{};
    if (CorsairGetSessionDetails(&d) != CE_Success) return "unknown";
    char buf[128];
    snprintf(buf, sizeof buf, "SDK client %d.%d.%d, server %d.%d.%d, iCUE %d.%d.%d", d.clientVersion.major,
             d.clientVersion.minor, d.clientVersion.patch, d.serverVersion.major, d.serverVersion.minor,
             d.serverVersion.patch, d.serverHostVersion.major, d.serverHostVersion.minor, d.serverHostVersion.patch);
    return buf;
}

const char* StateName(int state) {
    switch (state) {
        case CSS_Closed: return "closed";
        case CSS_Connecting: return "connecting";
        case CSS_Timeout: return "timeout (iCUE not answering)";
        case CSS_ConnectionRefused: return "refused (enable SDK in iCUE Settings)";
        case CSS_ConnectionLost: return "lost";
        case CSS_Connected: return "connected";
        default: return "invalid";
    }
}

const char* ErrorName(int error) {
    switch (error) {
        case CE_Success: return "success";
        case CE_NotConnected: return "not connected (iCUE down, or SDK disabled in iCUE Settings)";
        case CE_NoControl: return "no control";
        case CE_IncompatibleProtocol: return "incompatible protocol";
        case CE_InvalidArguments: return "invalid arguments";
        case CE_InvalidOperation: return "invalid operation";
        case CE_DeviceNotFound: return "device not found";
        case CE_NotAllowed: return "not allowed";
        default: return "unknown error";
    }
}

}  // namespace icue
