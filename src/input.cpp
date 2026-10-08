#include "input.hpp"

#include "d3drenderer.hpp"
#include "global.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <map>
#include <mutex>

namespace er::input {
namespace {

struct KeyChordState {
    bool wasDown = false;
    bool pressed = false;
    uint64_t frame = 0;
};

std::mutex gKeyChordStatesMutex;
std::map<int, KeyChordState> gRenderKeyChordStates;
std::map<int, KeyChordState> gNonRenderKeyChordStates;
uint64_t gInputFrame = 0;
DWORD gRenderThreadId = 0;

[[nodiscard]] int fixupKeyChord(int keyChord) {
    const auto vk = keyChord & KEY_CODE_MASK;
    switch (vk) {
        case VK_LCONTROL:
        case VK_RCONTROL:
            return keyChord | KEY_MOD_CTRL;
        case VK_LSHIFT:
        case VK_RSHIFT:
            return keyChord | KEY_MOD_SHIFT;
        case VK_LMENU:
        case VK_RMENU:
            return keyChord | KEY_MOD_ALT;
        case VK_LWIN:
        case VK_RWIN:
            return keyChord | KEY_MOD_SUPER;
        default:
            return keyChord;
    }
}

[[nodiscard]] bool isVirtualKeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

[[nodiscard]] bool isGameWindowForeground() {
    if (gD3DRenderer) return gD3DRenderer->isForeground();
    return true;
}

[[nodiscard]] bool isWin32KeyChordDown(int keyChord) {
    const auto fixedKeyChord = fixupKeyChord(keyChord);
    const auto mods = fixedKeyChord & KEY_MOD_MASK;
    const auto vk = fixedKeyChord & KEY_CODE_MASK;
    // Hotkeys are polled every frame and are almost always up: test the main
    // key before querying the modifiers.
    if (vk != 0 && !isVirtualKeyDown(vk)) return false;
    if (((mods & KEY_MOD_CTRL) != 0) != isVirtualKeyDown(VK_CONTROL)) return false;
    if (((mods & KEY_MOD_SHIFT) != 0) != isVirtualKeyDown(VK_SHIFT)) return false;
    if (((mods & KEY_MOD_ALT) != 0) != isVirtualKeyDown(VK_MENU)) return false;
    if (((mods & KEY_MOD_SUPER) != 0) != (isVirtualKeyDown(VK_LWIN) || isVirtualKeyDown(VK_RWIN))) return false;
    return vk != 0 || mods != 0;
}

[[nodiscard]] bool isRenderThread() {
    return gRenderThreadId != 0 && GetCurrentThreadId() == gRenderThreadId;
}

[[nodiscard]] KeyChordState &stateForCurrentThread(int keyChord, uint64_t &frame) {
    if (isRenderThread()) {
        frame = gInputFrame;
        return gRenderKeyChordStates[keyChord];
    }

    frame = 0;
    return gNonRenderKeyChordStates[keyChord];
}

}

void beginFrame() {
    std::lock_guard lock(gKeyChordStatesMutex);
    gRenderThreadId = GetCurrentThreadId();
    ++gInputFrame;
}

bool isKeyChordDown(int keyChord) {
    return keyChord != 0 && isWin32KeyChordDown(keyChord) && isGameWindowForeground();
}

bool isKeyChordPressed(int keyChord) {
    if (keyChord == 0) return false;

    std::lock_guard lock(gKeyChordStatesMutex);
    uint64_t frame = 0;
    auto &state = stateForCurrentThread(keyChord, frame);
    if (frame != 0 && state.frame == frame) return state.pressed;

    const bool down = isKeyChordDown(keyChord);
    state.pressed = down && !state.wasDown;
    state.wasDown = down;
    state.frame = frame;
    return state.pressed;
}

void resetKeyChordStates() {
    std::lock_guard lock(gKeyChordStatesMutex);
    gRenderKeyChordStates.clear();
    gNonRenderKeyChordStates.clear();
    gInputFrame = 0;
    gRenderThreadId = 0;
}

}
