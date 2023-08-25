#include <Windows.h>
#include <TlHelp32.h>
#include <stdio.h>

/**
 * Function wich find the process id of the specified process.
 * \param lpProcessName : name of the target process.
 * \return : the process id if the process is found else -1.
 */
DWORD GetProcessByName(const wchar_t *lpProcessName) {
    PROCESSENTRY32W ProcList = {};
    ProcList.dwSize = sizeof(ProcList);

    HANDLE hProcList = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcList == INVALID_HANDLE_VALUE)
        return -1;

    if (!Process32FirstW(hProcList, &ProcList))
        return -1;

    do {
        if (lstrcmpiW(ProcList.szExeFile, lpProcessName) == 0)
            return ProcList.th32ProcessID;
    } while (Process32NextW(hProcList, &ProcList));

    return -1;
}

int WINAPI wWinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd) {
    const wchar_t *lpDLLName = L"EROverlay.dll";
    const wchar_t *lpProcessName = L"eldenring.exe";
    wchar_t lpFullDLLPath[MAX_PATH];

#ifndef NDEBUG
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif

    DWORD dwProcessID = GetProcessByName(lpProcessName);
    if (dwProcessID == (DWORD)-1) {
        wprintf(L"An error is occured when trying to find the target process.\n");
        return -1;
    }

    wprintf(L"[DLL Injector]\n");
    wprintf(L"Process : %ls\n", lpProcessName);
    wprintf(L"Process ID : %i\n\n", (int)dwProcessID);

    DWORD dwFullPathResult = GetFullPathNameW(lpDLLName, MAX_PATH, lpFullDLLPath, NULL);
    if (dwFullPathResult == 0) {
        wprintf(L"An error is occured when trying to get the full path of the DLL.\n");
        return -1;
    }

    HANDLE hTargetProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessID);
    if (hTargetProcess == INVALID_HANDLE_VALUE) {
        wprintf(L"An error is occured when trying to open the target process.\n");
        return -1;
    }

    wprintf(L"[PROCESS INJECTION]\n");
    wprintf(L"Process opened successfully.\n");

    LPVOID lpPathAddress = VirtualAllocEx(hTargetProcess,
                                          NULL,
                                          sizeof(wchar_t) * (lstrlenW(lpFullDLLPath) + 1),
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
    if (lpPathAddress == NULL) {
        wprintf(L"An error is occured when trying to allocate memory in the target process.\n");
        return -1;
    }

    wprintf(L"Memory allocate at 0x%X\n", (UINT)(uintptr_t)lpPathAddress);

    const DWORD dwWriteResult = WriteProcessMemory(hTargetProcess,
                                                   lpPathAddress,
                                                   lpFullDLLPath,
                                                   sizeof(wchar_t) * (lstrlenW(lpFullDLLPath) + 1),
                                                   NULL);
    if (dwWriteResult == 0) {
        wprintf(L"An error is occured when trying to write the DLL path in the target process.\n");
        return -1;
    }

    printf("DLL path writen successfully.\n");

    HMODULE hModule = GetModuleHandleA("kernel32.dll");
    if (hModule == INVALID_HANDLE_VALUE || hModule == NULL)
        return -1;

    FARPROC lpFunctionAddress = GetProcAddress(hModule, "LoadLibraryW");
    if (lpFunctionAddress == NULL) {
        wprintf(L"An error is occured when trying to get \"LoadLibraryW\" address.\n");
        return -1;
    }

    wprintf(L"LoadLibraryW address at 0x%X\n", (UINT)(uintptr_t)lpFunctionAddress);

    HANDLE hThreadCreationResult =
        CreateRemoteThread(hTargetProcess, NULL, 0, (LPTHREAD_START_ROUTINE)lpFunctionAddress, lpPathAddress, 0, NULL);
    if (hThreadCreationResult == INVALID_HANDLE_VALUE) {
        wprintf(L"An error is occured when trying to create the thread in the target process.\n");
        return -1;
    }

    wprintf(L"DLL Injected !\n");

    return 0;
}