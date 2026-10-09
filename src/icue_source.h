#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "evga_v3.h"
#include "mapping.h"

struct CorsairSessionStateChanged;
struct CorsairEvent;

// Read-only iCUE SDK v4 client: it never requests control, it only asks iCUE
// what color each Corsair LED is currently showing.
namespace icue {

class Source {
public:
    ~Source();

    bool Connect(std::string& err);
    void Disconnect();

    bool Connected() const;
    // Main-thread housekeeping; call once per loop iteration.
    void Pump();
    int State() const { return state_.load(); }
    // True once after connecting and after any device (dis)connection.
    bool TakeTopologyChanged() { return topologyChanged_.exchange(false); }

    bool ListDevices(std::vector<mapping::Device>& out, std::string& err) const;
    bool LedIds(const std::string& deviceId, std::vector<unsigned>& ids, std::string& err) const;
    bool Sample(const std::string& deviceId, const std::vector<unsigned>& ids, std::vector<evga::Rgb>& out,
                std::string& err) const;
    std::string Versions() const;

private:
    static void OnState(void* ctx, const CorsairSessionStateChanged* e);
    static void OnEvent(void* ctx, const CorsairEvent* e);

    std::atomic<int> state_{0};
    std::atomic<bool> topologyChanged_{true};
    std::atomic<bool> needSubscribe_{false};
    bool connectCalled_ = false;
};

const char* StateName(int state);
const char* ErrorName(int error);

}  // namespace icue
