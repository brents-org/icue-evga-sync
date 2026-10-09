#pragma once

#include <string>

// Timestamped lines to a log file and, when attached, the console.
void LogInit(const std::wstring& path, bool console);
// Also collect message lines (no timestamps) into *sink, e.g. for a dialog.
void LogSetSink(std::string* sink);
void Log(const char* fmt, ...);

std::string Narrow(const std::wstring& s);
std::wstring Widen(const std::string& s);
