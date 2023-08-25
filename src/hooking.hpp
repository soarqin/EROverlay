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

    void hook();
    void unhook();
    void findHooks();

    void showMouseCursor(bool show) const;
    [[nodiscard]] bool menuLoaded() const;

private:
    uintptr_t csMenuManImp_ = 0;
};

inline std::unique_ptr<Hooking> gHooking;
}
