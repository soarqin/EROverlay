#include "common.hpp"
#include "d3drenderer.hpp"
#include "hooking.hpp"
#include "console.hpp"

using namespace ER;

void mainThread();

void init() {
    ///  STRUCTS, HOOKS & VARIABLES
    g_Console = std::make_unique<Console>("ELDEN RING Overlay - DEBUG");

    g_Console->printdbg("[+] ELDEN RING Overlay (PREVIEW)\n", Console::Colors::yellow);
    g_Console->printdbg("[+] BUILD DATE: %s %s\n", Console::Colors::yellow, __DATE__, __TIME__);
    g_Console->printdbg("[+] Created by Soar Qin\n\n", Console::Colors::yellow);
    g_Console->printdbg("[!] PLEASE DON'T INJECT UNTIL YOU HAVE REACHED THE MAIN MENU\n\n", Console::Colors::red);
    g_Console->printdbg("[+] PRESS [INSERT] TO SHOW/HIDE MENU\n\n", Console::Colors::DEFAULT);

    // g_GameFunctions->FMVSkip(g_GameVariables->m_ModuleBase);
    // g_GameFunctions->UnlockFPS(g_GameVariables->m_ModuleBase);

    //  WAIT FOR USER INPUT
    while (!(GetAsyncKeyState(VK_INSERT) & 1))
        Sleep(60);
    system("cls");

    gD3DRenderer = std::make_unique<D3DRenderer>();
    gStyles = std::make_unique<Styles>();
    gHooking = std::make_unique<Hooking>();
    gHooking->hook();

    mainThread();

    g_Console.reset();
    gHooking->unhook();
    std::this_thread::sleep_for(500ms);
    FreeLibraryAndExitThread(g_Module, 0);
}

void mainThread() {
    //  CREATE WorldCharMan Update Thread
    /*
    std::thread WCMUpdate(UpdateThread);
    std::thread BGWorker(BackgroundWorker);
    */

    showMenu = true;
    //  MAIN LOOP
    while (g_Running) {
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            showMenu = !showMenu;
        }

        if (GetAsyncKeyState(VK_DELETE) & 1) {
            showMenu = FALSE;
            g_Running = FALSE;
        }

        std::this_thread::sleep_for(3ms);
        std::this_thread::yield();
    }

    /*
    BGWorker.join();
    WCMUpdate.join();
    */
}