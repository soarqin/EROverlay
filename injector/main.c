#include <Windows.h>
#include <TlHelp32.h>
#include <stdio.h>

DWORD get_process_by_name(const wchar_t *process_name) {
    PROCESSENTRY32W proc_entry = {.dwSize = sizeof(PROCESSENTRY32W)};

    const HANDLE proc_list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (proc_list == INVALID_HANDLE_VALUE)
        return -1;

    if (!Process32FirstW(proc_list, &proc_entry))
        return -1;

    do {
        if (lstrcmpiW(proc_entry.szExeFile, process_name) == 0)
            return proc_entry.th32ProcessID;
    } while (Process32NextW(proc_list, &proc_entry));

    return -1;
}

int WINAPI wWinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    const wchar_t *dll_name = L"EROverlay.dll";
    const wchar_t *process_name = L"eldenring.exe";
    wchar_t full_dll_path[MAX_PATH];

#ifndef NDEBUG
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif

    const DWORD process_id = get_process_by_name(process_name);
    if (process_id == (DWORD)-1) {
        wprintf(L"An error is occurred when trying to find the target process.\n");
        return -1;
    }

    wprintf(L"[DLL Injector]\n");
    wprintf(L"Process : %ls\n", process_name);
    wprintf(L"Process ID : %i\n\n", (int)process_id);

    const DWORD full_path_result = GetFullPathNameW(dll_name, MAX_PATH, full_dll_path, NULL);
    if (full_path_result == 0) {
        wprintf(L"An error is occurred when trying to get the full path of the DLL.\n");
        return -1;
    }

    const HANDLE target_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_id);
    if (target_process == INVALID_HANDLE_VALUE) {
        wprintf(L"An error is occurred when trying to open the target process.\n");
        return -1;
    }

    wprintf(L"[PROCESS INJECTION]\n");
    wprintf(L"Process opened successfully.\n");

    const LPVOID path_address = VirtualAllocEx(target_process,
                                               NULL,
                                               sizeof(wchar_t) * (lstrlenW(full_dll_path) + 1),
                                               MEM_COMMIT | MEM_RESERVE,
                                               PAGE_EXECUTE_READWRITE);
    if (path_address == NULL) {
        wprintf(L"An error is occurred when trying to allocate memory in the target process.\n");
        return -1;
    }

    wprintf(L"Memory allocate at 0x%X\n", (UINT)(uintptr_t)path_address);

    const DWORD write_result = WriteProcessMemory(target_process,
                                                   path_address,
                                                   full_dll_path,
                                                   sizeof(wchar_t) * (lstrlenW(full_dll_path) + 1),
                                                   NULL);
    if (write_result == 0) {
        wprintf(L"An error is occurred when trying to write the DLL path in the target process.\n");
        return -1;
    }

    wprintf(L"DLL path written successfully.\n");

    const HMODULE module_handle = GetModuleHandleA("kernel32.dll");
    if (module_handle == INVALID_HANDLE_VALUE || module_handle == NULL)
        return -1;

    const FARPROC function_address = GetProcAddress(module_handle, "LoadLibraryW");
    if (function_address == NULL) {
        wprintf(L"An error is occurred when trying to get \"LoadLibraryW\" address.\n");
        return -1;
    }

    wprintf(L"LoadLibraryW address at 0x%X\n", (UINT)(uintptr_t)function_address);

    const HANDLE thread_creation_result =
        CreateRemoteThread(target_process, NULL, 0, (LPTHREAD_START_ROUTINE)function_address, path_address, 0, NULL);
    if (thread_creation_result == INVALID_HANDLE_VALUE) {
        wprintf(L"An error is occurred when trying to create the thread in the target process.\n");
        return -1;
    }

    wprintf(L"DLL Injected !\n");

    return 0;
}