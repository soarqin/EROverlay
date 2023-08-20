#include "d3drenderer.hpp"
#include "hooking.hpp"
#include "console.hpp"
#include "global.hpp"

#include "bosses/data.hpp"
#include "bosses/render.hpp"

#include <shlwapi.h>
#include <atomic>
#include <thread>
#include <chrono>
using namespace std::chrono_literals;

using namespace er;

void mainThread();

void init() {
    gConsole = std::make_unique<Console>("ELDEN RING Overlay - DEBUG");

    gConsole->printdbg("[+] ELDEN RING Overlay (PREVIEW)\n", Console::Colors::yellow);
    gConsole->printdbg("[+] BUILD DATE: %s %s\n", Console::Colors::yellow, __DATE__, __TIME__);
    gConsole->printdbg("[+] Created by Soar Qin\n\n", Console::Colors::yellow);
    gConsole->printdbg("[!] PLEASE DON'T INJECT UNTIL YOU HAVE REACHED THE MAIN MENU\n\n", Console::Colors::red);
    gConsole->printdbg("[+] PRESS [INSERT] TO SHOW/HIDE MENU\n\n", Console::Colors::DEFAULT);

    bosses::gBossDataSet.load(std::wstring(gModulePath) + L"\\data\\zhocn.json");
    bosses::gBossDataSet.initMemoryAddresses();

    //  WAIT FOR USER INPUT
    while (!(GetAsyncKeyState(VK_INSERT) & 1))
        Sleep(60);
    bosses::gBossDataSet.update();

    gD3DRenderer = std::make_unique<D3DRenderer>();
    gD3DRenderer->registerWindow<bosses::Render>();
    gStyles = std::make_unique<Styles>();

    gHooking = std::make_unique<Hooking>();
    gHooking->hook();

    mainThread();

    gConsole.reset();
    gHooking->unhook();
    std::this_thread::sleep_for(500ms);
    FreeLibraryAndExitThread(gModule, 0);
}

void mainThread() {
    showMenu = true;
    while (gRunning) {
        if (gD3DRenderer->isForeground()) {
            if (GetAsyncKeyState(VK_INSERT) & 1) {
                showMenu = !showMenu;
                //gHooking->showMouseCursor(showMenu);
            }

            if (GetAsyncKeyState(VK_DELETE) & 1) {
                showMenu = false;
                //gHooking->showMouseCursor(false);
                gRunning = false;
            }
        }

        std::this_thread::sleep_for(3ms);
        std::this_thread::yield();
    }
}