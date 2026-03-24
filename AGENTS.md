# AGENTS.md — EROverlay Codebase Guide

## Project Overview

**EROverlay** is a Windows DLL overlay mod for Elden Ring, written in **C++23**. It hooks into the game's DirectX 12 rendering pipeline via `minhook` and renders ImGui overlays. The project is structured as a loader DLL (`EROverlay.dll`) plus individual overlay plugin DLLs (Boss, Achievements, Minimap) loaded from the `overlays/` folder.

Key libraries: `imgui`, `minhook`, `nlohmann_json`, `fmt`, `Pattern16`, `stb`.

---

## Build System

**CMake** (minimum 3.13) with **Visual Studio 2022** (MSVC) as the primary toolchain. C++23 is required.

### Configure & Build (Debug)
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DUSE_STATIC_CRT=ON .
cmake --build build --config Debug
```

### Configure & Build (Release)
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -T v142 -DRELEASE_USE_STATIC_CRT=OFF -DRELEASE_USE_LTO=ON .
cmake --build build --config Release
```

### Distribution Build
```bat
dist.bat
```
Produces `dist/Boss.zip` and `dist/Minimap.zip` with all required files.

### Output Locations
- DLLs: `build/bin/` (e.g., `EROverlay.dll`, `overlays/Boss.dll`)
- Libs: `build/lib/`

### CMake Options
| Option | Default | Description |
|---|---|---|
| `RELEASE_STRIP_BINARY` | ON | Strip binaries in Release |
| `RELEASE_USE_LTO` | OFF | Link-time optimization |
| `RELEASE_USE_STATIC_CRT` | ON | Static MSVC runtime |

### No Test Suite
There is no automated test suite. Testing is done by injecting the built DLL into Elden Ring and verifying behavior in-game.

---

## Code Style

### Formatting — `.clang-format`
- **Based on**: LLVM style
- **Indent**: 4 spaces (no tabs)
- **Column limit**: 180 characters
- **Brace style**: Custom (K&R-like — braces on same line for all constructs)
- **Template declarations**: Always break before `<`
- **Constructor initializers**: Break after colon, one per line
- **Include order**: System headers (`<...>`) first, then local (`"..."`)
- **Case labels**: Indented inside `switch`
- **Trailing newline**: Always inserted at EOF

Run formatter: `clang-format -i <file>` or configure your editor to use `.clang-format`.

### Language Standard
- **C++23** (`CMAKE_CXX_STANDARD 23`)
- MSVC `/utf-8` flag is always set — source files must be UTF-8 encoded

### Namespaces
- All core overlay code lives in the `er` namespace
- Sub-namespaces for modules: `er::bosses`, `er::util`
- Plugin DLL entry points (`init`, `update`, `render`, etc.) are in global scope with C linkage via `PLUGIN_DEFINE()`

### Naming Conventions
| Element | Convention | Example |
|---|---|---|
| Classes/Structs | `PascalCase` | `BossDataSet`, `MemoryHandle` |
| Functions/Methods | `camelCase` | `loadConfig()`, `initMemoryAddresses()` |
| Private members | `camelCase_` (trailing underscore) | `bosses_`, `flagResolved_` |
| Global variables | `gPascalCase` | `gModule`, `gHooking`, `gBossDataSet` |
| Constants/Macros | `UPPER_SNAKE_CASE` | `OVERLAY_ATOM_NAME`, `WIN32_LEAN_AND_MEAN` |
| Template params | Single uppercase letter or `PascalCase` | `T`, `TReturn` |
| Namespaces | `lowercase` | `er`, `er::util`, `er::bosses` |

### Header Guards
Use `#pragma once` — never use `#ifndef` include guards.

### Include Order (per `.clang-format`)
1. System/platform headers: `<windows.h>`, `<d3d12.h>`, `<cstdint>`, etc.
2. Third-party library headers: `<imgui.h>`, `<fmt/format.h>`, `<nlohmann/json.hpp>`
3. Local project headers: `"config.hpp"`, `"util/memory.hpp"`

Always define `WIN32_LEAN_AND_MEAN` before including `<windows.h>`.

### Class Design
- Non-copyable, non-movable classes use explicitly deleted copy/move constructors and assignment operators
- Use `[[nodiscard]]` on all getter methods and functions whose return value must not be ignored
- Use `noexcept` on destructors
- Prefer `explicit` constructors
- Private members at the bottom of the class, after public interface

### Memory Management
- Prefer RAII and smart pointers (`std::unique_ptr`) for owned resources
- Raw `new`/`delete` is acceptable for plugin-managed objects (e.g., renderer lifetime)
- Use `MemoryHandle` / `MemoryRegion` / `Module` wrappers (in `util/memory.hpp`) for raw pointer arithmetic

### Error Handling
- No exceptions are used in this codebase (game mod context — exceptions are unreliable in injected DLLs)
- Functions return `bool` or check return values explicitly
- Use `UNREFERENCED_PARAMETER()` for intentionally unused parameters
- Null-check pointers before use; return early on failure

### Windows API Usage
- Use wide-character (`W`) variants of Win32 APIs: `GetModuleFileNameW`, `FindAtomW`, etc.
- `wchar_t` for paths and Windows strings; `std::string` / `std::wstring` for internal data
- `uintptr_t` for raw memory addresses

### Plugin API Pattern
Plugins export a `PluginExports` struct via `PLUGIN_DEFINE(exports)` macro. The struct contains function pointers:
```cpp
static PluginExports exports = { init, uninit, update, createRenderer, destroyRenderer, render };
PLUGIN_DEFINE(exports)
```
`init()` must not be null. All other fields are optional (null = not called).

### Config Access
Use `er::gConfig` (global `Config` instance). Config keys use dot notation: `"common.console"`, `"input.unload"`. Call config functions only during init/load — they are not efficient for per-frame use.

### Threading
- Main overlay logic runs in a dedicated thread spawned from `DllMain`
- Plugin `update()` is called at ~60 Hz in the main overlay thread
- Plugin `render()` is called from the DX12 Present hook thread
- Use `std::mutex` to protect shared state between update and render threads (see `BossDataSet::mutex_`)
- Use `std::atomic_bool` for cross-thread flags (e.g., `gRunning`)

---

## Project Structure

```
EROverlay/
├── src/
│   ├── dllmain.cpp          # DLL entry point, init/main thread
│   ├── api.h / api.cpp      # Public plugin API (C-compatible)
│   ├── config.hpp / .cpp    # INI config loader
│   ├── d3drenderer.hpp/.cpp # DX12 hook + ImGui rendering
│   ├── hooking.hpp / .cpp   # Game memory scanning & hooks
│   ├── global.hpp / .cpp    # Global state variables
│   ├── plugin.hpp / .cpp    # Plugin loader/manager
│   ├── util/                # Utility headers (memory, string, file, steam, etc.)
│   ├── params/              # Game param table access
│   ├── proxy/               # winhttp.dll proxy (DLL injection method)
│   ├── boss/                # Boss overlay plugin
│   ├── achievements/        # Achievements overlay plugin
│   └── minimap/             # Minimap overlay plugin
├── deps/                    # Third-party dependencies
├── cmake/                   # CMake helper macros
├── configs/                 # Default .ini config files
├── injector/                # Standalone injector executable
├── .clang-format            # Clang-format style config
└── dist.bat                 # Distribution build script
```

---

## Adding a New Overlay Plugin

1. Create a new subdirectory under `src/` (e.g., `src/myplugin/`)
2. Add `add_subdirectory(myplugin)` to `src/CMakeLists.txt`
3. Write a `CMakeLists.txt` using `add_project(MyPlugin SHARED ...)` macro
4. Implement `dllmain.cpp` with `init()`, optional callbacks, and `PLUGIN_DEFINE(exports)`
5. Link against `EROverlay` (for `api.h`) and any needed libraries
6. The loader will automatically discover and load DLLs from the `overlays/` folder

See `src/boss/dllmain.cpp` as the canonical example.
