# Building EROverlay

## Prerequisites

- **Windows** (required — this is a Windows DLL mod)
- **Visual Studio 2022** with the "Desktop development with C++" workload
- **CMake** 3.13 or later
- **C++23** support (provided by MSVC in VS 2022)

---

## Quick Start

### 1. Clone the repository

```bat
git clone https://github.com/soarqin/EROverlay.git
cd EROverlay
```

### 2. Configure with CMake

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DUSE_STATIC_CRT=ON .
```

### 3. Build

**Debug:**
```bat
cmake --build build --config Debug
```

**Release:**
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -T v142 -DRELEASE_USE_STATIC_CRT=OFF -DRELEASE_USE_LTO=ON .
cmake --build build --config Release
```

---

## Output Locations

| Artifact | Path |
|---|---|
| Main loader DLL | `build/bin/EROverlay.dll` |
| Boss overlay | `build/bin/overlays/Boss.dll` |
| Achievements overlay | `build/bin/overlays/Achievements.dll` |
| Minimap overlay | `build/bin/overlays/Minimap.dll` |
| Injector executable | `build/bin/injector.exe` |

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `RELEASE_STRIP_BINARY` | `ON` | Strip debug symbols from Release binaries |
| `RELEASE_USE_LTO` | `OFF` | Enable link-time optimization in Release |
| `RELEASE_USE_STATIC_CRT` | `ON` | Link MSVC runtime statically |

---

## Distribution Build

To produce distributable zip archives (`dist/Boss.zip` and `dist/Minimap.zip`):

```bat
dist.bat
```

This script builds in Release mode and packages all required files.

---

## Opening in Visual Studio

After running the CMake configure step, open `build/EROverlay.sln` in Visual Studio 2022. All projects and their dependencies will be available in the Solution Explorer.

---

## Notes

- There is **no automated test suite**. Testing is done by injecting the built DLL into Elden Ring and verifying behavior in-game.
- The project requires MSVC — GCC and Clang are not officially supported on Windows for this project.
- Always build as **x64**; 32-bit builds are not supported.
