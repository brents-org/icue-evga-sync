#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {

constexpr DWORD kMaxLogBytes = 1u << 20;

FILE* g_file = nullptr;
bool g_console = false;
std::string* g_sink = nullptr;
std::mutex g_mutex;

}  // namespace

void LogInit(const std::wstring& path, bool console) {
    g_console = console;
    if (path.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA attr;
    bool tooBig = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr) &&
                  (attr.nFileSizeHigh != 0 || attr.nFileSizeLow > kMaxLogBytes);
    g_file = _wfsopen(path.c_str(), tooBig ? L"w" : L"a", _SH_DENYNO);
}

void LogSetSink(std::string* sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = sink;
}

void Log(const char* fmt, ...) {
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fprintf(g_file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                t.wSecond, t.wMilliseconds, msg);
        fflush(g_file);
    }
    if (g_sink) {
        if (!g_sink->empty()) *g_sink += "\n";
        *g_sink += msg;
    }
    if (g_console) {
        printf("%s\n", msg);
        fflush(stdout);
    }
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
