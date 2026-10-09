// icue-evga-sync: iCUE renders, the EVGA card follows.
//
// Built twice from this file: icue-evga-sync.exe (windows subsystem, the
// background daemon the logon task starts) and icue-evga-syncctl.exe (console
// subsystem, for --list/--probe/--install and foreground debugging).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "card.h"
#include "config.h"
#include "icue_source.h"
#include "install.h"
#include "log.h"
#include "mapping.h"
#include "nvapi_i2c.h"
#include "sync_engine.h"
#include "version.h"

namespace {

SyncEngine* g_engine = nullptr;
HANDLE g_stop = nullptr;

const char kUsage[] =
    "usage: icue-evga-syncctl [command]\n"
    "  run          sync in the foreground until Ctrl+C (the default for icue-evga-sync.exe)\n"
    "  --list       Corsair devices iCUE exposes, with LED counts and current colors\n"
    "  --probe      NVIDIA GPUs, and the EVGA card's firmware and zones (read-only)\n"
    "  --install    copy to %LOCALAPPDATA%\\Programs\\icue-evga-sync and start at logon\n"
    "  --uninstall  stop, remove the logon task, Apps & features entry and binaries\n"
    "  --stop       ask a running daemon to restore the card and exit\n"
    "  --version    print the version\n"
    "config: %LOCALAPPDATA%\\icue-evga-sync\\icue-evga-sync.ini (created on first run)\n";

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_POWERBROADCAST:
            if ((wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) && g_engine) g_engine->OnResume();
            return TRUE;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            // The process may be torn down as soon as this returns.
            if (wp && g_engine) g_engine->Shutdown();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL WINAPI ConsoleHandler(DWORD) {
    if (g_stop) SetEvent(g_stop);
    return TRUE;
}

// A hidden top-level window: message-only windows never see the
// power and end-session broadcasts this needs.
HWND CreateMessageWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"icue-evga-sync";
    RegisterClassW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"icue-evga-sync", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                           wc.hInstance, nullptr);
}

bool OpenCard(nv::Api& api, Card& card) {
    if (!api.Load()) {
        Log("NvAPI unavailable (status %d); is the NVIDIA driver installed?", api.LastStatus());
        return false;
    }
    std::string why;
    if (!Card::Find(api, card, why)) {
        Log("no supported EVGA card: %s", why.c_str());
        return false;
    }
    uint8_t major = 0, minor = 0;
    card.ReadFirmware(major, minor);
    Log("found %s (firmware 1.%02u.%02u)", card.Name(), major, minor);
    return true;
}

bool WaitForIcue(icue::Source& source, unsigned timeoutMs) {
    std::string err;
    if (!source.Connect(err)) {
        Log("%s", err.c_str());
        return false;
    }
    for (unsigned waited = 0; waited < timeoutMs && !source.Connected(); waited += 100) Sleep(100);
    if (!source.Connected()) Log("iCUE SDK session: %s", icue::StateName(source.State()));
    return source.Connected();
}

// EcoQoS: a background daemon has no business on a P-core at full clocks.
void RequestEfficiencyMode() {
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
}

int RunDaemon() {
    HANDLE instance = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Log("already running");
        return 0;
    }
    g_stop = CreateEventW(nullptr, TRUE, FALSE, kStopEventName);
    ResetEvent(g_stop);

    nv::Api api;
    Card card;
    if (!OpenCard(api, card)) return 2;

    icue::Source source;
    std::string err;
    if (!source.Connect(err)) {
        Log("%s", err.c_str());
        return 3;
    }
    SyncEngine engine(card, source, ConfigPath());
    if (!engine.Start(err)) {
        Log("%s", err.c_str());
        return 2;
    }
    g_engine = &engine;
    RequestEfficiencyMode();
    CreateMessageWindow();
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    Log("running; config %s", Narrow(ConfigPath()).c_str());

    bool quit = false;
    while (!quit) {
        DWORD wait = engine.Tick();
        if (MsgWaitForMultipleObjects(1, &g_stop, FALSE, wait, QS_ALLINPUT) == WAIT_OBJECT_0) break;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = true;
            DispatchMessageW(&msg);
        }
    }
    engine.Shutdown();
    g_engine = nullptr;
    source.Disconnect();
    Log("stopped");
    CloseHandle(instance);
    return 0;
}

int ListDevices() {
    icue::Source source;
    if (!WaitForIcue(source, 5000)) return 3;
    Log("%s", source.Versions().c_str());
    std::vector<mapping::Device> devices;
    std::string err;
    if (!source.ListDevices(devices, err)) {
        Log("%s", err.c_str());
        return 3;
    }
    int pick = mapping::PickDevice(devices, "");
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        std::string colors;
        std::vector<unsigned> ids;
        std::vector<evga::Rgb> rgb;
        if (source.LedIds(d.id, ids, err) && source.Sample(d.id, ids, rgb, err)) {
            for (size_t k = 0; k < rgb.size() && k < 8; ++k) {
                char hex[8];
                snprintf(hex, sizeof hex, " %02X%02X%02X", rgb[k].r, rgb[k].g, rgb[k].b);
                colors += hex;
            }
            if (rgb.size() > 8) colors += " ...";
        }
        Log("%c %-13s %-32s %3d LEDs %s", int(i) == pick ? '*' : ' ', mapping::TypeName(d.type), d.model.c_str(),
            d.ledCount, colors.c_str());
    }
    Log("(* = the default source when a zone's device= is empty)");
    return 0;
}

int Probe() {
    nv::Api api;
    if (!api.Load()) {
        Log("NvAPI unavailable (status %d)", api.LastStatus());
        return 2;
    }
    for (const auto& g : api.EnumerateGpus()) {
        const auto* m = evga::FindSupportedCard(g.vendor, g.device, g.subVendor, g.subDevice);
        Log("GPU %04X:%04X sub %04X:%04X  %s", g.vendor, g.device, g.subVendor, g.subDevice,
            m ? m->name : "(not a known EVGA v3 board)");
    }
    Card card;
    if (!OpenCard(api, card)) return 2;
    evga::ZoneLayout layout;
    if (!card.ReadLayout(layout)) {
        Log("zone layout read failed (status %d)", card.LastStatus());
        return 2;
    }
    Log("zone sync flag: %d", layout.sync ? 1 : 0);
    for (int z = 0; z < evga::kZoneCount; ++z) {
        evga::StaticZone s;
        bool ok = card.ReadStatic(z, s);
        Log("zone %d %-18s mode %u  LEDs %3u  static %s%02X%02X%02X @ %u", z + 1, evga::kZoneNames[z], layout.mode[z],
            layout.ledCount[z], ok ? "#" : "(read failed) #", s.color.r, s.color.g, s.color.b, s.brightness);
    }
    return 0;
}

// --install/--uninstall from the windowless build (Apps & features runs
// `icue-evga-sync.exe --uninstall`) report in a dialog unless --quiet.
int WithDialog(bool console, bool quiet, int (*fn)()) {
    if (console || quiet) return fn();
    std::string report;
    LogSetSink(&report);
    int rc = fn();
    LogSetSink(nullptr);
    // Empty when the work was handed to another process, which reports itself.
    if (!report.empty())
        MessageBoxW(nullptr, Widen(report).c_str(), L"icue-evga-sync",
                    rc == 0 ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
    return rc;
}

int Main(const std::vector<std::wstring>& args, bool console) {
    std::wstring cmd = args.size() > 1 ? args[1] : L"run";
    bool quiet = false;
    for (size_t i = 2; i < args.size(); ++i) quiet |= args[i] == L"--quiet";
    bool daemon = cmd == L"run";
    // Only the daemon writes the log file; one-shot commands just print.
    LogInit(daemon ? DataDir() + L"\\icue-evga-sync.log" : std::wstring(), console);

    if (daemon) return RunDaemon();
    if (cmd == L"--list") return ListDevices();
    if (cmd == L"--probe") return Probe();
    if (cmd == L"--install") return WithDialog(console, quiet, [] { return Install(ExeDir()); });
    if (cmd == L"--uninstall") {
        int rc = WithDialog(console, quiet, [] { return Uninstall(); });
        CleanUpUninstallCopy();
        return rc;
    }
    if (cmd == L"--stop") return StopRunning(5000) ? 0 : 1;
    if (cmd == L"--version") {
        Log("icue-evga-sync %s", kVersion);
        return 0;
    }
    if (console) fputs(kUsage, cmd == L"--help" || cmd == L"-h" ? stdout : stderr);
    return cmd == L"--help" || cmd == L"-h" ? 0 : 64;
}

std::vector<std::wstring> Args(int argc, wchar_t** argv) {
    return std::vector<std::wstring>(argv, argv + argc);
}

}  // namespace

#ifdef ICUE_EVGA_CONSOLE
int wmain(int argc, wchar_t** argv) {
    return Main(Args(argc, argv), true);
}
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc = Main(Args(argc, argv), false);
    LocalFree(argv);
    return rc;
}
#endif
