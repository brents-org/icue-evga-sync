#include "install.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>

#include <cstdio>
#include <string>

#include "log.h"
#include "version.h"

const wchar_t kInstanceMutexName[] = L"Local\\icue-evga-sync.instance";
const wchar_t kStopEventName[] = L"Local\\icue-evga-sync.stop";
const wchar_t* const kInstalledFiles[3] = {L"icue-evga-sync.exe", L"icue-evga-syncctl.exe", L"iCUESDK.x64_2019.dll"};

namespace {

const wchar_t kTaskName[] = L"icue-evga-sync";
const wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\icue-evga-sync";

// Empty when %LOCALAPPDATA% is unset or longer than MAX_PATH; callers refuse to run.
std::wstring InstallDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::wstring(buf, n) + L"\\Programs\\icue-evga-sync";
}

std::wstring CurrentUser() {
    wchar_t buf[256];
    ULONG n = ULONG(std::size(buf));
    return GetUserNameExW(NameSamCompatible, buf, &n) ? std::wstring(buf) : std::wstring();
}

std::wstring XmlEscape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

int RunHidden(const std::wstring& exe, const std::wstring& args, DWORD waitMs) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi))
        return -1;
    DWORD code = 0;
    if (waitMs) {
        WaitForSingleObject(pi.hProcess, waitMs);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return int(code);
}

int RunSchtasks(const std::wstring& args) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    return RunHidden(std::wstring(sys) + L"\\schtasks.exe", args, 30000);
}

bool WriteTaskXml(const std::wstring& path, const std::wstring& user, const std::wstring& exe) {
    // Logon trigger, interactive token, least privilege, no 72h execution cap.
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>Mirrors iCUE lighting onto the EVGA GPU "
        L"(github.com/brents-org/icue-evga-sync).</Description></RegistrationInfo>\r\n"
        L"  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + XmlEscape(user) +
        L"</UserId><Delay>PT10S</Delay></LogonTrigger></Triggers>\r\n"
        L"  <Principals><Principal id=\"Author\"><UserId>" + XmlEscape(user) +
        L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
        L"    <RestartOnFailure><Interval>PT1M</Interval><Count>99</Count></RestartOnFailure>\r\n"
        L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
        L"    <Priority>6</Priority>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\"><Exec><Command>" + XmlEscape(exe) + L"</Command></Exec></Actions>\r\n"
        L"</Task>\r\n";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const wchar_t bom = 0xFEFF;
    fwrite(&bom, sizeof bom, 1, f);
    fwrite(xml.data(), sizeof(wchar_t), xml.size(), f);
    fclose(f);
    return true;
}

DWORD InstalledSizeKb(const std::wstring& dir) {
    ULONGLONG total = 0;
    for (const wchar_t* f : kInstalledFiles) {
        WIN32_FILE_ATTRIBUTE_DATA attr;
        if (GetFileAttributesExW((dir + L"\\" + f).c_str(), GetFileExInfoStandard, &attr))
            total += (ULONGLONG(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
    }
    return DWORD(total / 1024);
}

// HKCU, so it shows under Settings > Apps for this user without elevation.
bool RegisterUninstallEntry(const std::wstring& dir) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    auto str = [key](const wchar_t* name, const std::wstring& value) {
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       DWORD((value.size() + 1) * sizeof(wchar_t)));
    };
    auto dword = [key](const wchar_t* name, DWORD value) {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value);
    };
    std::wstring daemon = dir + L"\\icue-evga-sync.exe";
    str(L"DisplayName", L"icue-evga-sync (iCUE lighting on the EVGA GPU)");
    str(L"DisplayVersion", Widen(kVersion));
    str(L"Publisher", L"brents-org");
    str(L"InstallLocation", dir);
    str(L"DisplayIcon", daemon);
    str(L"UninstallString", L"\"" + daemon + L"\" --uninstall");
    str(L"QuietUninstallString", L"\"" + daemon + L"\" --uninstall --quiet");
    dword(L"NoModify", 1);
    dword(L"NoRepair", 1);
    dword(L"EstimatedSize", InstalledSizeKb(dir));
    RegCloseKey(key);
    return true;
}

bool DeleteWithRetry(const std::wstring& path) {
    // The process that launched this uninstall may still be exiting from here.
    for (int i = 0; i < 50; ++i) {
        if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        Sleep(100);
    }
    return false;
}

// The daemon releases its instance mutex just before it exits, so StopRunning()
// can return while its image is still mapped and the overwrite fails with
// ERROR_SHARING_VIOLATION (seen upgrading a daemon that had run for hours).
bool CopyWithRetry(const std::wstring& from, const std::wstring& to) {
    for (int i = 0; i < 50; ++i) {
        if (CopyFileW(from.c_str(), to.c_str(), FALSE)) return true;
        DWORD err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED && err != ERROR_LOCK_VIOLATION) break;
        Sleep(100);
    }
    return false;
}

std::wstring ModulePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::wstring UninstallCopyPath() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + L"icue-evga-sync-uninstall.exe";
}

}  // namespace

std::wstring ExeDir() {
    std::wstring path = ModulePath();
    return path.substr(0, path.find_last_of(L'\\'));
}

bool StopRunning(unsigned timeoutMs) {
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEventName);
    if (!stop) return true;  // nothing running
    SetEvent(stop);
    CloseHandle(stop);
    for (unsigned waited = 0; waited < timeoutMs; waited += 100) {
        HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kInstanceMutexName);
        if (!m) return true;
        CloseHandle(m);
        Sleep(100);
    }
    return false;
}

int Install(const std::wstring& sourceDir) {
    std::wstring dst = InstallDir();
    if (dst.empty()) {
        Log("Could not read %%LOCALAPPDATA%%.");
        return 1;
    }
    std::wstring user = CurrentUser();
    if (user.empty()) {
        Log("Could not resolve the current user.");
        return 1;
    }
    HANDLE running = OpenMutexW(SYNCHRONIZE, FALSE, kInstanceMutexName);
    bool wasRunning = running != nullptr;
    if (running) CloseHandle(running);
    if (!StopRunning(5000)) {
        Log("The running icue-evga-sync did not stop within 5 seconds.");
        return 1;
    }
    // Past this point a failed install must not leave the card unsynced: put
    // back whatever was running (the task still points at the same exe).
    auto fail = [&]() {
        if (wasRunning && RunSchtasks(std::wstring(L"/Run /TN ") + kTaskName) == 0)
            Log("The previously installed version was restarted.");
        return 1;
    };

    if (_wcsicmp(sourceDir.c_str(), dst.c_str()) != 0) {
        CreateDirectoryW(dst.substr(0, dst.find_last_of(L'\\')).c_str(), nullptr);
        CreateDirectoryW(dst.c_str(), nullptr);
        for (const wchar_t* f : kInstalledFiles) {
            if (!CopyWithRetry(sourceDir + L"\\" + f, dst + L"\\" + f)) {
                Log("Copying %s failed (error %lu).", Narrow(f).c_str(), GetLastError());
                return fail();
            }
        }
    }

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring xml = std::wstring(tmp) + L"icue-evga-sync-task.xml";
    if (!WriteTaskXml(xml, user, dst + L"\\icue-evga-sync.exe")) {
        Log("Could not write %s.", Narrow(xml).c_str());
        return fail();
    }
    int rc = RunSchtasks(std::wstring(L"/Create /F /TN ") + kTaskName + L" /XML \"" + xml + L"\"");
    DeleteFileW(xml.c_str());
    if (rc != 0) {
        Log("Registering the logon task failed (schtasks exit %d).", rc);
        return fail();
    }
    if (!RegisterUninstallEntry(dst)) Log("Warning: could not add the Apps & features entry.");
    rc = RunSchtasks(std::wstring(L"/Run /TN ") + kTaskName);
    Log("icue-evga-sync %s installed to %s for %s, starting at every logon%s.", kVersion, Narrow(dst).c_str(),
        Narrow(user).c_str(), rc == 0 ? " (running now)" : "");
    Log("iCUE needs Settings > General > Enable SDK turned on.");
    return 0;
}

int Uninstall() {
    std::wstring dst = InstallDir();
    if (dst.empty()) {
        Log("Could not read %%LOCALAPPDATA%%.");
        return 1;
    }
    if (_wcsicmp(ExeDir().c_str(), dst.c_str()) == 0) {
        // Windows will not delete a running image, so hand off to a temp copy
        // (Apps & features runs the installed exe) and let it finish the job.
        std::wstring copy = UninstallCopyPath();
        std::wstring args = GetCommandLineW();
        bool quiet = args.find(L"--quiet") != std::wstring::npos;
        if (CopyFileW(ModulePath().c_str(), copy.c_str(), FALSE) &&
            RunHidden(copy, quiet ? L"--uninstall --quiet" : L"--uninstall", 0) >= 0)
            return 0;
    }

    StopRunning(5000);
    int rc = RunSchtasks(std::wstring(L"/Delete /F /TN ") + kTaskName);
    bool filesOk = true;
    for (const wchar_t* f : kInstalledFiles) filesOk &= DeleteWithRetry(dst + L"\\" + f);
    RemoveDirectoryW(dst.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    Log("icue-evga-sync uninstalled (logon task %s%s). Settings and log are kept in %%LOCALAPPDATA%%\\icue-evga-sync.",
        rc == 0 ? "removed" : "was not registered", filesOk ? "" : "; some files were in use and remain");
    return filesOk ? 0 : 1;
}

void CleanUpUninstallCopy() {
    std::wstring copy = UninstallCopyPath();
    if (_wcsicmp(ModulePath().c_str(), copy.c_str()) != 0) return;
    // The copy is a running image too, so a detached cmd deletes it once we exit.
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    RunHidden(std::wstring(sys) + L"\\cmd.exe", L"/d /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + copy + L"\"", 0);
}
