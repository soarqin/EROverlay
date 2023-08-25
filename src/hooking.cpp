#include <cstdio>
#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "memory.hpp"

namespace er {

void Hooking::findHooks() {
    Signature sig("48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8 ?? ?? ?? ??");
    auto addr = sig.scan();
    csMenuManImp_ = addr.add(addr.add(3).as<uint32_t&>() + 7).as<uintptr_t>();
}

Hooking::Hooking() {
    findHooks();
}

Hooking::~Hooking() noexcept {
    unhook();
}

void Hooking::hook() {
    gD3DRenderer->hook();
}

void Hooking::unhook() {
    gD3DRenderer->unhook();
}

void Hooking::showMouseCursor(bool show) const {
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return;
    auto *ptr = (uint8_t *)(addr + 0xAC);
    if (show)
        *ptr |= 1u;
    else
        *ptr &= ~1u;
}

bool Hooking::menuLoaded() const {
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return false;
    return *(uint8_t *)(addr + 0x21) == 1;
}

}