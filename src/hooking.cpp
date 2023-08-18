#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "memory.hpp"

namespace ER {

//	MinHook
void Hooking::HookInput(uint64_t a1, uint64_t a2) {
    if (showMenu)
        return;

    reinterpret_cast<decltype(&HookInput)>(gHooking->originalInputHandler_)(a1, a2);
}

void Hooking::findHooks() {
    inputHandler_ = signature(
        "48 8B C4 41 56 48 81 EC ? ? ? ? 48 C7 40 ? ? ? ? ? 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 0F 29 70 E8 0F 29 78 D8 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 50 ?")
        .scan().as<uint64_t>();
}

Hooking::Hooking() {
    MH_Initialize();
}

Hooking::~Hooking() {
    unhook();
}

void Hooking::hook() {
    gD3DRenderer->hook();

    findHooks();

    if (inputHandler_)
        MH_CreateHook((LPVOID)inputHandler_, (LPVOID)&HookInput, (LPVOID *)&originalInputHandler_);
    MH_EnableHook(MH_ALL_HOOKS);
}

void Hooking::unhook() {
    gD3DRenderer->unhook();
    MH_DisableHook(MH_ALL_HOOKS);
}
}