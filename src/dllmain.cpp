#include <shlwapi.h>
#include <thread>
#include <chrono>

#include "d3drenderer.hpp"
#include "hooking.hpp"
#include "global.hpp"
#include "config.hpp"
#include "steamapi.hpp"

#include "bosses/data.hpp"
#include "config.hpp"
#include "bosses/render.hpp"

using namespace std::chrono_literals;

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

void init() {
    GetModuleFileNameW(::er::gModule, ::er::gModulePath, MAX_PATH);
    PathRemoveFileSpecW(::er::gModulePath);
    er::gConfig.load(std::wstring(::er::gModulePath) + L"\\EROverlay.ini");
    bool enableConsole = false;
    if (er::gConfig.enabled("common.console")) {
        enableConsole = true;
        AllocConsole();
        freopen("CONIN$", "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    std::this_thread::sleep_for(2000ms);
    er::initSteamAPI();
    bool dlcInstalled = er::isDLCInstalled(2778580) || er::isDLCInstalled(2778590);
    fwprintf(stderr, L"DLC \"Shadow of the Erdtree\" is %ls\n", dlcInstalled ? L"installed" : L"not installed");
    er::bosses::gBossDataSet.load(dlcInstalled);
    er::bosses::gBossDataSet.initMemoryAddresses();

    er::gHooking = std::make_unique<er::Hooking>();
    //  WAIT FOR USER INPUT
    while (!er::gHooking->menuLoaded()) {
        std::this_thread::sleep_for(1000ms);
    }
    std::this_thread::sleep_for(2000ms);
    /* do not hook on game loading, high risk of crash */
    while (er::gHooking->screenState() == 1) {
        std::this_thread::sleep_for(100ms);
    }

    er::gD3DRenderer = std::make_unique<er::D3DRenderer>();
    er::gD3DRenderer->registerWindow<er::bosses::Render>();

    er::gHooking->hook();

    mainThread();

    er::gHooking->unhook();
    std::this_thread::sleep_for(500ms);
    if (enableConsole) {
        FreeConsole();
    }
    FreeLibraryAndExitThread(er::gModule, 0);
}

void mainThread() {
    auto unloadKey = er::gConfig.getVirtualKey("input.unload", {VK_OEM_MINUS});
    auto toggleFullKey = er::gConfig.getVirtualKey("input.toggle_full_mode", {VK_OEM_PLUS});

    er::showMenu = false;
    int counter = 0x1F;
    er::bosses::gBossDataSet.update();
    while (er::gRunning) {
        if (er::gD3DRenderer->isForeground()) {
            if (!toggleFullKey.empty()) {
                for (auto &vk: toggleFullKey) {
                    if (!(GetAsyncKeyState(vk & 0x7FFF) & ((vk & 0x8000) != 0 ? 0x8000 : 1))) {
                        goto noToggleFull;
                    }
                }
                er::showMenu = !er::showMenu;
            }
            noToggleFull:
            if (!unloadKey.empty()) {
                for (auto &vk: unloadKey) {
                    if (!(GetAsyncKeyState(vk & 0x7FFF) & ((vk & 0x8000) != 0 ? 0x8000 : 1))) {
                        goto noUnload;
                    }
                }
                er::showMenu = false;
                er::gRunning = false;
            }
        }
        noUnload:

        std::this_thread::yield();
        std::this_thread::sleep_for(20ms);

        if (er::gHooking->screenState() != 0) {
            continue;
        }

        counter = (counter + 1) & 0x1F;
        if (counter == 0) {
            er::bosses::gBossDataSet.update();
        }
    }
}
