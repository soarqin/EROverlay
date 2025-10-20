#include "config.hpp"
#include "d3drenderer.hpp"
#include "global.hpp"
#include "hooking.hpp"
#include "plugin.hpp"
#include "util/steam.hpp"
#include "proxy/winhttp.h"

#include <chrono>
#include <thread>
#include <shlwapi.h>

void init();
void mainThread();

#define OVERLAY_ATOM_NAME L"EROVERLAY_BY_SOARQIN"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    UNREFERENCED_PARAMETER(lpReserved);

    static ATOM processAtom = 0;

    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            if (FindAtomW(OVERLAY_ATOM_NAME))
                return FALSE;
            load_winhttp_proxy();
            processAtom = AddAtomW(OVERLAY_ATOM_NAME);
            if (!::er::gModule) {
                ::er::gModule = hModule;
            }
            DisableThreadLibraryCalls(hModule);
            CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)init, er::gModule, 0, nullptr);
            break;
        case DLL_PROCESS_DETACH:
            DeleteAtom(processAtom);
            break;
        default:
            break;
    }

    return TRUE;
}

void checkGameVersion() {
    wchar_t exepath[MAX_PATH];
    GetModuleFileNameW(nullptr, exepath, MAX_PATH);
    DWORD len = GetFileVersionInfoSizeW(exepath, nullptr);
    if (len == 0) return;
    BYTE *pVersionResource = new BYTE[len];
    if (!GetFileVersionInfoW(exepath, 0, len, pVersionResource)) {
        delete[] pVersionResource;
        return;
    }
    UINT uLen;
    VS_FIXEDFILEINFO *ptFixedFileInfo;
    if (VerQueryValueW(pVersionResource, L"\\", reinterpret_cast<LPVOID *>(&ptFixedFileInfo), &uLen) && uLen > 0) {
        er::gGameVersion = (uint64_t(ptFixedFileInfo->dwFileVersionMS) << 32) | uint64_t(ptFixedFileInfo->dwFileVersionLS);
    }
    delete[] pVersionResource;
}

void init() {
    checkGameVersion();
    GetModuleFileNameW(::er::gModule, ::er::gModulePath, MAX_PATH);
    PathRemoveFileSpecW(::er::gModulePath);
    er::gConfig.loadFile(L"EROverlay.ini");
    er::gConfig.loadDir(L"configs");
    bool enableConsole = false;
    if (er::gConfig.enabled("common.console")) {
        enableConsole = true;
        AllocConsole();
        freopen("CONIN$", "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1000ms);
    er::gIsDLC01Installed = er::util::isDLCInstalled(2778580) || er::util::isDLCInstalled(2778590);
    fwprintf(stderr, L"DLC \"Shadow of the Erdtree\" is %ls\n", er::gIsDLC01Installed ? L"installed" : L"not installed");
    er::gHooking = std::make_unique<er::Hooking>();

    er::pluginsInit();

    /* wait for menu loaded */
    while (!er::gHooking->menuLoaded()) {
        std::this_thread::sleep_for(100ms);
    }
    /* delay for another 1s */
    std::this_thread::sleep_for(1000ms);
    /* do not hook on game loading, high risk of crash */
    while (er::gHooking->screenState() == 1) {
        std::this_thread::sleep_for(100ms);
    }

    er::gD3DRenderer = std::make_unique<er::D3DRenderer>();
    er::Hooking::hook();

    ImGuiMemAllocFunc allocFunc;
    ImGuiMemFreeFunc freeFunc;
    void *userData;
    ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
    er::pluginsLoadRenderers(ImGui::GetCurrentContext(), (void*)allocFunc, (void*)freeFunc, userData);

    mainThread();

    er::Hooking::unhook();
    std::this_thread::sleep_for(500ms);
    if (enableConsole) {
        FreeConsole();
    }
    FreeLibraryAndExitThread(er::gModule, 0);
}

void mainThread() {
    auto unloadKey = er::gConfig.getImGuiKey("input.unload", 0);

    er::gShowMenu = false;

    er::pluginsUpdate();
    while (er::gRunning) {
        if (unloadKey != 0 && ImGui::IsKeyChordPressed(unloadKey)) {
            er::gShowMenu = false;
            er::gRunning = false;
            er::gHooking->showMouseCursor(false);
            break;
        }
        std::this_thread::yield();
        using namespace std::chrono_literals;
        static auto tick = 1000000us / 60;
        std::this_thread::sleep_for(tick);

        er::pluginsUpdate();
    }
}
