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
    }

    return TRUE;
}

void init() {
    er::bosses::gBossDataSet.load(std::wstring(er::gModulePath) + L"\\data\\bosses.json");
    er::bosses::gBossDataSet.initMemoryAddresses();

    //  WAIT FOR USER INPUT
    while (!(GetAsyncKeyState(VK_OEM_PLUS) & 1))
        Sleep(60);
    er::bosses::gBossDataSet.update();

    er::gD3DRenderer = std::make_unique<er::D3DRenderer>();
    er::gD3DRenderer->registerWindow<er::bosses::Render>();

    er::gHooking = std::make_unique<er::Hooking>();
    er::gHooking->hook();

    mainThread();

    er::gHooking->unhook();
    std::this_thread::sleep_for(500ms);
    FreeLibraryAndExitThread(er::gModule, 0);
}

void mainThread() {
    er::showMenu = true;
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

        std::this_thread::sleep_for(3ms);
        std::this_thread::yield();
    }
}
