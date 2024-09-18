#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "memory.hpp"

#include <MinHook.h>

namespace er {

void Hooking::findHooks() {
    Signature sig("48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8");
    MemoryHandle addr = sig.scan();
    csMenuManImp_ = addr.add(addr.add(3).as<uint32_t &>() + 7).as<uintptr_t>();
}

Hooking::Hooking() {
    MH_Initialize();
}

Hooking::~Hooking() noexcept {
    unhook();
    MH_Uninitialize();
}

void Hooking::hook() {
    gD3DRenderer->hook();
}

void Hooking::unhook() {
    gD3DRenderer->unhook();
}

void Hooking::showMouseCursor(bool show) {
    if (csMenuManImp_ == 0) {
        findHooks();
        if (csMenuManImp_ == 0) return;
    }
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return;
    auto *ptr = (uint8_t *)(addr + 0xAC);
    if (show)
        *ptr |= 1u;
    else
        *ptr &= ~1u;
}

bool Hooking::menuLoaded() {
    if (csMenuManImp_ == 0) {
        findHooks();
        if (csMenuManImp_ == 0) return false;
    }
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return false;
    return *(uint8_t *)(addr + 0x21) == 1;
}

int Hooking::screenState() {
    if (csMenuManImp_ == 0) {
        findHooks();
        if (csMenuManImp_ == 0) return 1;
    }
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return false;
    return *(int *)(addr + 0x730);
}

}