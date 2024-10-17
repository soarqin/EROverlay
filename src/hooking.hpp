#pragma once

#include <memory>
#include <cstdint>

namespace er {
class Hooking {
public:
    explicit Hooking();
    ~Hooking() noexcept;
    Hooking(Hooking const &) = delete;
    Hooking(Hooking &&) = delete;
    Hooking &operator=(Hooking const &) = delete;
    Hooking &operator=(Hooking &&) = delete;

    static void hook();
    static void unhook();
    void findHooks();

    void showMouseCursor(bool show);
    [[nodiscard]] bool menuLoaded();
    [[nodiscard]] int screenState();

private:
    uintptr_t csMenuManImp_ = 0;
};

extern std::unique_ptr<Hooking> gHooking;

}
