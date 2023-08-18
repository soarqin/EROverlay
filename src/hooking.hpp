#pragma once
#include "common.hpp"

namespace ER {
class Hooking {
public:
    explicit Hooking();
    ~Hooking() noexcept;
    Hooking(Hooking const &) = delete;
    Hooking(Hooking &&) = delete;
    Hooking &operator=(Hooking const &) = delete;
    Hooking &operator=(Hooking &&) = delete;

    void hook();
    void unhook();
    void findHooks();

    // Hook Pointers
    void *originalInputHandler_ = nullptr;
    uint64_t inputHandler_ = 0;
    static void __fastcall HookInput(uint64_t a1, uint64_t a2);
};

inline std::unique_ptr<Hooking> gHooking;
}
