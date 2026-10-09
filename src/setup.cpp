// icue-evga-sync-setup.exe: a single-file, per-user installer. The daemon, the
// console tool and Corsair's SDK DLL ride along as RCDATA resources; they are
// unpacked to a temp folder and handed to the same Install() that
// `icue-evga-syncctl --install` runs. No admin rights, no MSI.
//
//   icue-evga-sync-setup.exe          ask, install, report
//   icue-evga-sync-setup.exe --quiet  install without dialogs (exit code says how it went)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

#include "install.h"
#include "log.h"
#include "setup_payload.h"
#include "version.h"

namespace {

const wchar_t kTitle[] = L"icue-evga-sync setup";

bool Unpack(int resourceId, const std::wstring& path) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL data = res ? LoadResource(nullptr, res) : nullptr;
    const void* bytes = data ? LockResource(data) : nullptr;
    DWORD size = res ? SizeofResource(nullptr, res) : 0;
    if (!bytes || size == 0) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    bool ok = fwrite(bytes, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool quiet = false;
    for (int i = 1; i < argc; ++i)
        if (_wcsicmp(argv[i], L"--quiet") == 0 || _wcsicmp(argv[i], L"/S") == 0) quiet = true;
    LocalFree(argv);

    if (!quiet) {
        std::wstring ask = L"Install icue-evga-sync " + Widen(kVersion) +
                           L"?\n\nIt mirrors the lighting iCUE shows on your Corsair devices onto the EVGA "
                           L"graphics card, and starts with Windows (no admin rights needed).";
        if (MessageBoxW(nullptr, ask.c_str(), kTitle, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 1;
    }

    std::string report;
    LogSetSink(&report);
    // A --quiet run has no dialog, so the outcome also goes to a file.
    wchar_t appData[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring logDir = std::wstring(appData, n) + L"\\icue-evga-sync";
        CreateDirectoryW(logDir.c_str(), nullptr);
        LogInit(logDir + L"\\setup.log", false);
    }
    Log("setup %s%s", kVersion, quiet ? " (--quiet)" : "");

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"icue-evga-sync-setup-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);

    int rc = 0;
    for (int i = 0; i < kPayloadCount && rc == 0; ++i) {
        if (!Unpack(kPayloadFirstId + i, dir + L"\\" + kInstalledFiles[i])) {
            Log("Setup is damaged: could not unpack %s.", Narrow(kInstalledFiles[i]).c_str());
            rc = 1;
        }
    }
    if (rc == 0) rc = Install(dir);

    for (const wchar_t* f : kInstalledFiles) DeleteFileW((dir + L"\\" + f).c_str());
    RemoveDirectoryW(dir.c_str());

    if (!quiet)
        MessageBoxW(nullptr, Widen(report).c_str(), kTitle, rc == 0 ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
    return rc;
}
