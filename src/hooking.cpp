#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "util/signature.hpp"

#include <MinHook.h>

namespace er {

std::unique_ptr<Hooking> gHooking;

void Hooking::findHooks() {
    {
        util::Signature sig("48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8");
        auto res = sig.scan();
        if (res) {
            csMenuManImp_ = res.add(res.add(3).as<uint32_t &>() + 7).as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05 48 8B 40 58 C3 C3");
        auto res = sig.scan();
        if (res) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            gameDataMan_ = addr.as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 3D ?? ?? ?? ?? 48 85 FF ?? ?? 32 C0 E9");
        auto res = sig.scan();
        if (res) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            eventFlagMan_ = addr.as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0");
        auto res = sig.scan();
        if (res) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            fieldArea_ = addr.as<uintptr_t>();
        }
    }
}

Hooking::Hooking() {
    MH_Initialize();
    findHooks();
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

void Hooking::showMouseCursor(bool show) const {
    if (csMenuManImp_ == 0) return;
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return;
    auto *ptr = (uint8_t *)(addr + 0xAC);
    if (show)
        *ptr |= 1u;
    else
        *ptr &= ~1u;
}

bool Hooking::menuLoaded() const {
    if (csMenuManImp_ == 0) return false;
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return false;
    return *(uint8_t *)(addr + 0x21) == 1;
}

int Hooking::screenState() const {
    if (csMenuManImp_ == 0) return 1;
    auto addr = *(uintptr_t *)csMenuManImp_;
    if (addr == 0) return false;
    return *(int *)(addr + 0x730);
}

}
