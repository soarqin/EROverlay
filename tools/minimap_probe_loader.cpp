// Explicit PID loader for a sample-specific ordinary mod probe; no debugger API.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return 1;
    wchar_t *end = nullptr;
    DWORD pid = wcstoul(argv[1], &end, 10);
    if (!pid || !end || *end)
        return 1;
    wchar_t dllPath[MAX_PATH];
    DWORD length = GetFullPathNameW(argv[2], MAX_PATH, dllPath, nullptr);
    if (!length || length >= MAX_PATH)
        return 2;
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        wprintf(L"OpenProcess=%lu\n", GetLastError());
        return 3;
    }
    wchar_t exe[MAX_PATH];
    DWORD exeLength = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, exe, &exeLength) || !wcsrchr(exe, 92) || _wcsicmp(wcsrchr(exe, 92) + 1, L"eldenring.exe")) {
        wprintf(L"Target is not eldenring.exe\n");
        CloseHandle(process);
        return 3;
    }
    SIZE_T size = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void *remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SIZE_T written = 0;
    if (!remote || !WriteProcessMemory(process, remote, dllPath, size, &written) || written != size) {
        if (remote)
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 4;
    }
    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = load ? CreateRemoteThread(process, nullptr, 0, load, remote, 0, nullptr) : nullptr;
    if (!thread) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 5;
    }
    DWORD wait = WaitForSingleObject(thread, 30000);
    DWORD code = 0;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeThread(thread, &code);
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    }
    wprintf(L"pid=%lu wait=%lu LoadLibraryW-low32=%lx; inspect probe events.log for actual completion\n", pid, wait, code);
    CloseHandle(thread);
    CloseHandle(process);
    // The 32-bit thread exit code does not prove a 64-bit module handle is nonzero.
    return wait == WAIT_OBJECT_0 ? 0 : 6;
}
