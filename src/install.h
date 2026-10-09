#pragma once

#include <string>

// Per-user install: copies the binaries to %LOCALAPPDATA%\Programs\icue-evga-sync,
// registers a logon task that runs the daemon unelevated in the user's session
// (iCUE's SDK server and NvAPI both live there), and adds an Apps & features
// entry whose Uninstall runs Uninstall() below.
int Install(const std::wstring& sourceDir);
int Uninstall();
// Call last, after any dialog: if this process is Uninstall()'s temp copy,
// schedules that file's deletion for just after the process exits.
void CleanUpUninstallCopy();

// Signals a running daemon to hand the card back and exit; waits for it.
bool StopRunning(unsigned timeoutMs);

std::wstring ExeDir();

extern const wchar_t kInstanceMutexName[];
extern const wchar_t kStopEventName[];
extern const wchar_t* const kInstalledFiles[3];
