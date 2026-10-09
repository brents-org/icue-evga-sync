#include "mapping.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace mapping {
namespace {

struct TypeEntry {
    unsigned type;
    const char* name;
};

const TypeEntry kTypes[] = {
    {kKeyboard, "keyboard"},
    {kMouse, "mouse"},
    {kMousemat, "mousemat"},
    {kHeadset, "headset"},
    {kHeadsetStand, "headsetstand"},
    {kFanLedController, "fan"},
    {kLedController, "ledcontroller"},
    {kMemoryModule, "memory"},
    {kCooler, "cooler"},
    {kMotherboard, "motherboard"},
    {kGraphicsCard, "graphicscard"},
    {kTouchbar, "touchbar"},
    {kGameController, "gamecontroller"},
};

// Case-mounted devices first: they are what a GPU sits next to.
const unsigned kDefaultPriority[] = {
    kCooler, kFanLedController, kLedController, kMemoryModule, kMotherboard,
    kKeyboard, kMousemat, kHeadsetStand, kMouse, kHeadset, kTouchbar, kGameController,
};

std::string Lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

bool ParseInt(const std::string& s, int& out) {
    if (s.empty() || s.size() > 4) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    out = std::atoi(s.c_str());
    return true;
}

int FirstOfType(const std::vector<Device>& devices, unsigned type) {
    for (size_t i = 0; i < devices.size(); ++i)
        if (devices[i].type == type && devices[i].ledCount > 0) return int(i);
    return -1;
}

}  // namespace

const char* TypeName(unsigned type) {
    for (const auto& t : kTypes)
        if (t.type == type) return t.name;
    return "unknown";
}

bool TypeFromName(const std::string& name, unsigned& type) {
    std::string n = Lower(Trim(name));
    for (const auto& t : kTypes) {
        if (n == t.name) {
            type = t.type;
            return true;
        }
    }
    return false;
}

int PickDevice(const std::vector<Device>& devices, const std::string& selector) {
    std::string sel = Trim(selector);
    if (sel.empty()) {
        for (unsigned type : kDefaultPriority) {
            int i = FirstOfType(devices, type);
            if (i >= 0) return i;
        }
        return -1;
    }
    std::string lower = Lower(sel);
    if (lower.rfind("type:", 0) == 0) {
        unsigned type = 0;
        return TypeFromName(lower.substr(5), type) ? FirstOfType(devices, type) : -1;
    }
    for (size_t i = 0; i < devices.size(); ++i)
        if (devices[i].ledCount > 0 && Lower(devices[i].model).find(lower) != std::string::npos) return int(i);
    return -1;
}

bool ParseLedSpec(const std::string& spec, int ledCount, int zoneOrdinal, int zoneTotal, std::vector<int>& out,
                  std::string& err) {
    out.clear();
    if (ledCount <= 0) {
        err = "device has no LEDs";
        return false;
    }
    std::string s = Lower(Trim(spec));
    if (s.empty() || s == "all") {
        for (int i = 0; i < ledCount; ++i) out.push_back(i);
        return true;
    }
    if (s == "auto") {
        if (zoneTotal <= 0 || zoneOrdinal < 0 || zoneOrdinal >= zoneTotal) {
            err = "bad zone ordinal";
            return false;
        }
        // Equal contiguous segments; with fewer LEDs than zones, zones share LEDs.
        int begin = zoneOrdinal * ledCount / zoneTotal;
        int end = (zoneOrdinal + 1) * ledCount / zoneTotal;
        if (end <= begin) end = begin + 1;
        for (int i = begin; i < end && i < ledCount; ++i) out.push_back(i);
        return true;
    }

    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string item = Trim(s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
        size_t dash = item.find('-');
        int lo = 0, hi = 0;
        bool ok = dash == std::string::npos
                      ? ParseInt(item, lo) && (hi = lo, true)
                      : ParseInt(Trim(item.substr(0, dash)), lo) && ParseInt(Trim(item.substr(dash + 1)), hi);
        if (!ok || lo > hi) {
            err = "bad LED range '" + item + "'";
            return false;
        }
        if (hi >= ledCount) {
            err = "LED " + std::to_string(hi) + " out of range (device has " + std::to_string(ledCount) + ")";
            return false;
        }
        for (int i = lo; i <= hi; ++i) out.push_back(i);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

evga::Rgb Blend(const std::vector<evga::Rgb>& colors, const std::vector<int>& indices) {
    double r = 0, g = 0, b = 0;
    int n = 0;
    for (int i : indices) {
        if (i < 0 || size_t(i) >= colors.size()) continue;
        r += double(colors[i].r) * colors[i].r;
        g += double(colors[i].g) * colors[i].g;
        b += double(colors[i].b) * colors[i].b;
        ++n;
    }
    if (n == 0) return {};
    auto ch = [n](double sq) { return uint8_t(std::lround(std::min(255.0, std::sqrt(sq / n)))); };
    return {ch(r), ch(g), ch(b)};
}

}  // namespace mapping
