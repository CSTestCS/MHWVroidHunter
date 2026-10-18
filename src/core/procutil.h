// Windows helpers for the converter helper process (header only, Windows builds).
#pragma once
#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>

namespace vh {

// Ends every running VRoidHunterConverter.exe except the current process (for example a stuck
// run left over from an older version, which would also keep the exe locked against updates).
// Returns how many were ended.
inline int endStaleConverters() {
    int ended = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"VRoidHunterConverter.exe") != 0 || pe.th32ProcessID == GetCurrentProcessId()) continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        if (TerminateProcess(h, 1)) {
            WaitForSingleObject(h, 5000);
            ++ended;
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return ended;
}

} // namespace vh
#endif
