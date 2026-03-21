# Writing an Overlay Plugin

EROverlay supports third-party overlay plugins as individual DLLs placed in the `overlays/` folder. This document explains how to create one.

---

## Overview

A plugin is a standard Windows DLL that exports a single function `getExports()`, which returns a pointer to a `PluginExports` struct. The loader calls the functions in this struct at the appropriate times during the overlay lifecycle.

---

## Plugin Lifecycle

```
DLL loaded → init() → [game running loop] → uninit()
                           │
                    ┌──────┴──────┐
                 update()      createRenderer()
                 (60 Hz)            │
                              render() (per frame)
                                   │
                            destroyRenderer()
```

- **`init()`** — Called once when the plugin is loaded. Receive the API pointer here.
- **`update()`** — Called at approximately 60 Hz in a dedicated thread. Use for game state polling.
- **`createRenderer()`** — Called when the DX12 renderer is ready. Set up ImGui context here.
- **`render()`** — Called every frame from the DX12 Present hook thread. Draw ImGui widgets here.
- **`destroyRenderer()`** — Called when the renderer is being torn down.
- **`uninit()`** — Called when the plugin is unloaded.

---

## Step-by-Step Guide

### 1. Set up your CMakeLists.txt

Create a new directory under `src/` (e.g., `src/myplugin/`) and add a `CMakeLists.txt`:

```cmake
add_project(myplugin SHARED
    dllmain.cpp

    LANGUAGES CXX
    NO_PREFIX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME MyPlugin
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL)
```

Then add `add_subdirectory(myplugin)` to `src/CMakeLists.txt`.

### 2. Include the API header

```cpp
#include "api.h"
```

This header is in `src/` and is automatically on the include path when you link against `EROverlayDLL`.

### 3. Implement the plugin functions

```cpp
#include "api.h"

static EROverlayAPI *api = nullptr;

// Called once on load. Return 0 for success, non-zero to abort loading.
int init(EROverlayAPI *erOverlayAPI) {
    api = erOverlayAPI;
    // Read config, load data, resolve memory addresses, etc.
    return 0;
}

// Called on unload.
void uninit() {
    // Clean up resources.
}

// Called at ~60 Hz in a dedicated thread.
void update() {
    // Poll game state, update internal data structures.
}

// Called when the DX12 renderer is ready.
// Return the render priority: lower value = rendered earlier (lower ImGui layer).
int createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext *>(context));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<ImGuiMemAllocFunc>(allocFunc),
        reinterpret_cast<ImGuiMemFreeFunc>(freeFunc),
        userData);
    // Initialize your renderer here.
    return 100; // priority
}

// Called when the renderer is torn down.
void destroyRenderer() {
    // Release renderer resources.
}

// Called every frame. Return true if your plugin wants to show the mouse cursor.
bool render() {
    ImGui::Begin("My Plugin");
    ImGui::Text("Hello from MyPlugin!");
    ImGui::End();
    return false;
}

// Export the plugin entry point.
static PluginExports exports = { init, uninit, update, createRenderer, destroyRenderer, render };
PLUGIN_DEFINE(exports)
```

---

## The `EROverlayAPI` Interface

The `EROverlayAPI` struct (defined in `src/api.h`) provides access to game data and loader utilities. All functions are available after `init()` is called.

### Global

| Function | Signature | Description |
|---|---|---|
| `getGameVersion` | `uint64_t ()` | Returns the running game version as a packed integer. |
| `getModulePath` | `const wchar_t *()` | Returns the absolute path to the directory containing `EROverlay.dll`. |

### Game Status

| Function | Signature | Description |
|---|---|---|
| `menuLoaded` | `bool ()` | Returns `true` when the main game menu is loaded. |
| `screenState` | `int ()` | Returns the current screen state (loading, in-game, etc.). |

### Steam

| Function | Signature | Description |
|---|---|---|
| `getGameLanguage` | `const wchar_t *()` | Returns the Steam game language string (e.g., `"enUS"`, `"zhCN"`). |
| `isDLC01Installed` | `bool ()` | Returns `true` if the "Shadow of the Erdtree" DLC is installed. |

### Config

> **Note:** Config functions are not efficient for per-frame use. Call them only during `init()` or data loading.

Config keys use dot notation: `"section.key"` (e.g., `"boss.data_file"`, `"common.font_size"`).

| Function | Signature | Description |
|---|---|---|
| `configGet` | `const char *(name)` | Returns the raw string value for a config key, or `nullptr`. |
| `configGetString` | `const wchar_t *(name, defValue)` | Returns the value as a wide string, or `defValue` if not set. |
| `configGetInt` | `int (name, defValue)` | Returns the value as an integer, or `defValue` if not set. |
| `configGetFloat` | `float (name, defValue)` | Returns the value as a float, or `defValue` if not set. |
| `configEnabled` | `bool (name)` | Returns `true` if the value is `"true"`, `"1"`, or `"yes"`. |
| `configGetImGuiKey` | `int (name, defValue)` | Returns the value parsed as an ImGui key code. |

### Game Addresses

| Function | Signature | Description |
|---|---|---|
| `getGameAddresses` | `GameAddresses ()` | Returns a struct with pointers to key game manager objects. |
| `resolveFlagAddress` | `uintptr_t (flagId, bits)` | Resolves the memory address and bit mask for a game event flag. |

The `GameAddresses` struct contains:

```c
typedef struct {
    uintptr_t csMenuManImp;   // Menu manager
    uintptr_t gameDataMan;    // Game data manager
    uintptr_t eventFlagMan;   // Event flag manager
    uintptr_t fieldArea;      // Field area manager
    uintptr_t gameBase;       // Game base pointer
} GameAddresses;
```

### Params

| Function | Signature | Description |
|---|---|---|
| `paramFindTable` | `const void *(name)` | Finds a game param table by name (e.g., `L"NpcParam"`). |

### Rendering / Textures

| Function | Signature | Description |
|---|---|---|
| `loadTexture` | `TextureContext (filename)` | Loads a texture from the `data/` folder (or an absolute path) for use in ImGui. |
| `destroyTexture` | `void (TextureContext *)` | Releases a previously loaded texture. |

The `TextureContext` struct:

```c
typedef struct {
    void *texture;    // ID3D12Resource pointer
    int   width;
    int   height;
    void *cpuHandle;  // D3D12_CPU_DESCRIPTOR_HANDLE
    void *gpuHandle;  // D3D12_GPU_DESCRIPTOR_HANDLE
    bool  loaded;     // true even if loading failed (prevents retry)
} TextureContext;
```

---

## Threading Notes

- `update()` runs in a **dedicated overlay thread** at ~60 Hz.
- `render()` runs in the **DX12 Present hook thread**.
- If you share state between `update()` and `render()`, protect it with a `std::mutex`.
- Use `std::atomic_bool` for simple cross-thread flags.

---

## Config File Convention

If your plugin needs user-configurable settings, create a `.ini` file in the `configs/` folder (e.g., `configs/myplugin.ini`). The loader automatically reads all `.ini` files in that folder. Access your keys via `api->configGetInt("myplugin.my_key", defaultValue)`.

---

## Reference Implementation

See [`src/boss/dllmain.cpp`](../src/boss/dllmain.cpp) for a complete, production-quality example of a plugin that uses the full API: config loading, memory address resolution, threaded data updates, and ImGui rendering.
