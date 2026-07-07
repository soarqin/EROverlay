#pragma once

namespace er::input {

inline constexpr int KEY_MOD_CTRL = 1 << 12;
inline constexpr int KEY_MOD_SHIFT = 1 << 13;
inline constexpr int KEY_MOD_ALT = 1 << 14;
inline constexpr int KEY_MOD_SUPER = 1 << 15;
inline constexpr int KEY_MOD_MASK = KEY_MOD_CTRL | KEY_MOD_SHIFT | KEY_MOD_ALT | KEY_MOD_SUPER;
inline constexpr int KEY_CODE_MASK = ~KEY_MOD_MASK;

void beginFrame();
[[nodiscard]] bool isKeyChordDown(int keyChord);
[[nodiscard]] bool isKeyChordPressed(int keyChord);
void resetKeyChordStates();

}
