#pragma once

// Set by CMake (-DICUE_EVGA_SYNC_VERSION=X.Y.Z); release.yml passes the release version.
#ifndef ICUE_EVGA_SYNC_VERSION
#define ICUE_EVGA_SYNC_VERSION "0.0.0-dev"
#endif

inline constexpr const char kVersion[] = ICUE_EVGA_SYNC_VERSION;
