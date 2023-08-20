#include "initialize.hpp"

#include <shlwapi.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    UNREFERENCED_PARAMETER(lpReserved);

    if (!::er::gModule) {
        ::er::gModule = hModule;
        GetModuleFileNameW(hModule, ::er::gModulePath, MAX_PATH);
        PathRemoveFileSpecW(::er::gModulePath);
    }

    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)init, gModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        gKillSwitch = true;
        break;
    }

    return TRUE;
}
