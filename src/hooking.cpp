#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "util/signature.hpp"

#include <MinHook.h>

#include "global.hpp"

namespace er {

std::unique_ptr<Hooking> gHooking;

void Hooking::findHooks() {
    {
        util::Signature sig("48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8");
        if (auto res = sig.scan()) {
            csMenuManImp_ = res.add(res.add(3).as<uint32_t &>() + 7).as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05 48 8B 40 58 C3 C3");
        if (auto res = sig.scan()) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            gameDataMan_ = addr.as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 3D ?? ?? ?? ?? 48 85 FF ?? ?? 32 C0 E9");
        if (auto res = sig.scan()) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            eventFlagMan_ = addr.as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0");
        if (auto res = sig.scan()) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            fieldArea_ = addr.as<uintptr_t>();
        }
    }
    if (gGameVersion < 0x0001000300000000ULL) {
        // 1.02 ~ 1.02.3
        menuInfoOffset_ = 0x708;
    } else if (gGameVersion < 0x0002000100000000ULL) {
        // 1.03 ~ 1.10.1
        menuInfoOffset_ = 0x718;
    } else
        // 1.12+
        menuInfoOffset_ = 0x720;
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
    auto addr = *reinterpret_cast<uintptr_t*>(csMenuManImp_);
    if (addr == 0) return;
    auto *ptr = reinterpret_cast<uint8_t*>(addr + 0xAC);
    if (show)
        *ptr |= 1u;
    else
        *ptr &= ~1u;
}

bool Hooking::menuLoaded() const {
    if (csMenuManImp_ == 0) return false;
    auto addr = *reinterpret_cast<uintptr_t*>(csMenuManImp_);
    if (addr == 0) return false;
    return *reinterpret_cast<float*>(addr + menuInfoOffset_ + 0x24) > 0.0f;
}

int Hooking::screenState() const {
    if (csMenuManImp_ == 0) return 1;
    auto addr = *reinterpret_cast<uintptr_t*>(csMenuManImp_);
    if (addr == 0) return false;
    return *reinterpret_cast<int*>(addr + menuInfoOffset_ + 0x10);
}

}
