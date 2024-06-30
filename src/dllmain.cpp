#include <shlwapi.h>
#include <thread>
#include <chrono>

#include "d3drenderer.hpp"
#include "hooking.hpp"
#include "global.hpp"
#include "config.hpp"

#include "bosses/data.hpp"
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

    er::bosses::gBossDataSet
        .load(std::wstring(er::gModulePath) + L"\\data\\" + er::gConfig.getw("boss.data", L"engus.json"));
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
    er::showMenu = false;
    int counter = 0x1F;
    er::bosses::gBossDataSet.update();
    while (er::gRunning) {
        if (er::gD3DRenderer->isForeground()) {
            if (GetAsyncKeyState(VK_OEM_PLUS) & 1) {
                er::showMenu = !er::showMenu;
                //gHooking->showMouseCursor(showMenu);
            }

            if (GetAsyncKeyState(VK_OEM_MINUS) & 1) {
                er::showMenu = false;
                //gHooking->showMouseCursor(false);
                er::gRunning = false;
                break;
            }
        }

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
