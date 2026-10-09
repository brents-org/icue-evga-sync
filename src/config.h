#pragma once

#include <string>

#include "evga_v3.h"

struct ZoneConfig {
    int enabled = -1;  // -1 = auto: drive the zone when the card reports LEDs on it
    std::string device;
    std::string leds = "auto";
};

struct Config {
    int fps = 20;
    int brightness = 255;
    bool restore = true;
    ZoneConfig zones[evga::kZoneCount];
};

// %LOCALAPPDATA%\icue-evga-sync, created on first use.
std::wstring DataDir();
std::wstring ConfigPath();

// Writes the commented default file first if none exists.
Config LoadConfig(const std::wstring& path);
