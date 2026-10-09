#include "config.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "log.h"

namespace {

const char kDefaultConfig[] =
    "; icue-evga-sync: mirrors the lighting iCUE renders onto the EVGA GPU.\r\n"
    "; Edits are picked up within a second; no restart needed.\r\n"
    ";\r\n"
    ";   icue-evga-syncctl --list    Corsair devices and LED counts iCUE reports\r\n"
    ";   icue-evga-syncctl --probe   the card's zones as the hardware reports them\r\n"
    "\r\n"
    "[sync]\r\n"
    "; Updates per second pushed to the card (1-60). Each zone update costs ~1 ms of\r\n"
    "; CPU in the NVIDIA driver, so this is the knob for CPU use.\r\n"
    "fps=20\r\n"
    "; Card-side brightness 0-255. iCUE's own brightness is already in the colors.\r\n"
    "brightness=255\r\n"
    "; Hand the card back its own lighting when iCUE goes away or this program exits.\r\n"
    "restore=1\r\n"
    "\r\n"
    "; One section per GPU zone:\r\n"
    ";   zone1 Front Logo, zone2 End Plate Logo, zone3 Back Logo, zone4 Addressable Header\r\n"
    ";\r\n"
    "; enabled = auto | 1 | 0   auto drives the zone if the card reports LEDs on it\r\n"
    "; device  = the Corsair device to copy from:\r\n"
    ";             (empty)        first case device: cooler, fan hub, LED hub, RAM, ...\r\n"
    ";             type:<kind>    cooler, fan, ledcontroller, memory, motherboard,\r\n"
    ";                            keyboard, mouse, mousemat, headset, headsetstand\r\n"
    ";             anything else  case-insensitive part of the model name, e.g. LL120\r\n"
    "; leds    = auto | all | a list such as 0-7,12 (LED indexes on that device)\r\n"
    ";             auto splits the device's LEDs evenly across the zones using it,\r\n"
    ";             so an effect sweeping across the device sweeps across the card.\r\n"
    "\r\n"
    "[zone1]\r\nenabled=auto\r\ndevice=\r\nleds=auto\r\n\r\n"
    "[zone2]\r\nenabled=auto\r\ndevice=\r\nleds=auto\r\n\r\n"
    "[zone3]\r\nenabled=auto\r\ndevice=\r\nleds=auto\r\n\r\n"
    "; The header reports 60 LEDs whether or not anything is plugged in, so it is\r\n"
    "; off by default. Set enabled=1 if an ARGB strip is connected to the card.\r\n"
    "[zone4]\r\nenabled=0\r\ndevice=\r\nleds=auto\r\n";

std::string ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512];
    GetPrivateProfileStringW(section, key, def, buf, DWORD(std::size(buf)), path.c_str());
    return Narrow(buf);
}

int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int def, int lo, int hi) {
    int v = int(GetPrivateProfileIntW(section, key, def, path.c_str()));
    return std::clamp(v, lo, hi);
}

int ParseEnabled(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (s == "1" || s == "true" || s == "yes" || s == "on") return 1;
    if (s == "0" || s == "false" || s == "no" || s == "off") return 0;
    return -1;
}

}  // namespace

std::wstring DataDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : std::wstring(L".");
    dir += L"\\icue-evga-sync";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring ConfigPath() {
    return DataDir() + L"\\icue-evga-sync.ini";
}

Config LoadConfig(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
            fwrite(kDefaultConfig, 1, sizeof kDefaultConfig - 1, f);
            fclose(f);
            Log("wrote default config %s", Narrow(path).c_str());
        }
    }

    Config cfg;
    cfg.fps = ReadInt(path, L"sync", L"fps", cfg.fps, 1, 60);
    cfg.brightness = ReadInt(path, L"sync", L"brightness", cfg.brightness, 0, 255);
    cfg.restore = ReadInt(path, L"sync", L"restore", 1, 0, 1) != 0;
    for (int z = 0; z < evga::kZoneCount; ++z) {
        std::wstring section = L"zone" + std::to_wstring(z + 1);
        auto& zc = cfg.zones[z];
        zc.enabled = ParseEnabled(ReadString(path, section.c_str(), L"enabled", L"auto"));
        zc.device = ReadString(path, section.c_str(), L"device", L"");
        zc.leds = ReadString(path, section.c_str(), L"leds", L"auto");
    }
    return cfg;
}
