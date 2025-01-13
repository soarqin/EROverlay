#pragma once

#include <memory>
#include <cstdint>

namespace er {
class Hooking {
    friend class EROverlayAPIWrapper;
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

    void showMouseCursor(bool show) const;
    [[nodiscard]] bool menuLoaded() const;
    [[nodiscard]] int screenState() const;

private:
    uintptr_t csMenuManImp_ = 0;
    uintptr_t gameDataMan_ = 0;
    uintptr_t eventFlagMan_ = 0;
    uintptr_t fieldArea_ = 0;
    int menuInfoOffset_ = 0;
};

extern std::unique_ptr<Hooking> gHooking;

}
