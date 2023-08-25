#include <shlwapi.h>
#include <thread>
#include <chrono>

#include "d3drenderer.hpp"
#include "hooking.hpp"
#include "global.hpp"

#include "bosses/data.hpp"
#include "bosses/render.hpp"

using namespace std::chrono_literals;

void init();
void mainThread();

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
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)init, er::gModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        er::gKillSwitch = true;
        break;
    default:
        break;
    }

    return TRUE;
}

void init() {
#ifndef NDEBUG
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif

    er::bosses::gBossDataSet.load(std::wstring(er::gModulePath) + L"\\data\\bosses.json");
    er::bosses::gBossDataSet.initMemoryAddresses();

    er::gHooking = std::make_unique<er::Hooking>();
    //  WAIT FOR USER INPUT
    while (!er::gHooking->menuLoaded()) {
        Sleep(1000);
    }
    Sleep(5000);
    er::bosses::gBossDataSet.update();

    er::gD3DRenderer = std::make_unique<er::D3DRenderer>();
    er::gD3DRenderer->registerWindow<er::bosses::Render>();

    er::gHooking->hook();

    mainThread();

    er::gHooking->unhook();
    std::this_thread::sleep_for(500ms);
    FreeLibraryAndExitThread(er::gModule, 0);
}

void mainThread() {
    er::showMenu = false;
    int counter = 0x3F;
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
            }
        }

        counter = (counter + 1) & 0x3F;
        if (counter == 0) {
            er::bosses::gBossDataSet.update();
        }
        std::this_thread::sleep_for(10ms);
        std::this_thread::yield();
    }
}
