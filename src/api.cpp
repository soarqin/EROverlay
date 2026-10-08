#include "api.h"
#include "nativeapi.h"
#include "gamefiles.hpp"
#include "util/nativelog.hpp"

#include "config.hpp"
#include "global.hpp"
#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "input.hpp"
#include "params/paraminternal.hpp"
#include "util/eventflag.hpp"
#include "util/memory.hpp"
#include "util/steam.hpp"

namespace er {
class EROverlayAPIWrapper {
public:
    static EROverlayAPI *get() {
        static EROverlayAPI api = {[]{
            return er::gGameVersion;
        }, [] {
            return (const wchar_t *)er::gModulePath;
        }, [] {
            return er::gHooking->menuLoaded();
        }, [] {
            return er::gHooking->screenState();
        }, [] {
            return er::util::getGameLanguage();
        }, [] {
            return er::gIsDLC01Installed;
        },

        [](const char *name) {
            thread_local std::string value;
            value = er::gConfig[name];
            return value.c_str();
        },
        [](const char *name, const wchar_t *defValue) {
            thread_local std::wstring value;
            value = er::gConfig.getw(name, defValue);
            return value.c_str();
        },
        [](const char *name, int defValue) {
            return er::gConfig.get(name, defValue);
        },
        [](const char *name, float defValue) {
            return er::gConfig.get(name, defValue);
        },
        [](const char *name) {
            return er::gConfig.enabled(name);
        },
        [](const char *name, int defValue) {
            return er::gConfig.getVirtualKey(name, defValue);
        },
        [] {
            return GameAddresses {
                er::gHooking->csMenuManImp_,
                er::gHooking->gameDataMan_,
                er::gHooking->eventFlagMan_,
                er::gHooking->fieldArea_,
                er::util::Module(nullptr).base().as<uintptr_t>(),
            };
        },
        [](uint32_t flagId, uint8_t *bits) {
            auto eventFlagMan = er::gHooking == nullptr ? 0 : er::gHooking->eventFlagMan_;
            auto manager = eventFlagMan == 0 ? 0 : util::MemoryHandle(eventFlagMan).as<uintptr_t &>();
            return util::resolveEventFlag(manager, flagId, bits);
        },
        [](const wchar_t *name) {
            return (const void *)er::params::paramFindTable(name);
        },
        [](const wchar_t *filename)->TextureContext {
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
            er::gD3DRenderer->HeapDescriptorAlloc(&cpuHandle, &gpuHandle);
            TextureContext texture = {};
            texture.cpuHandle = (void *)cpuHandle.ptr;
            texture.gpuHandle = (void *)gpuHandle.ptr;
            texture.loaded = true;
            if (er::gD3DRenderer->LoadTextureFromFile(filename, cpuHandle, (ID3D12Resource **)&texture.texture, &texture.width, &texture.height)) {
                return texture;
            }
            er::gD3DRenderer->HeapDescriptorFree(cpuHandle, gpuHandle);
            return {};
        },
        [](TextureContext *texture) {
            if (texture->texture == nullptr) {
                return;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
            cpuHandle.ptr = (uintptr_t)texture->cpuHandle;
            gpuHandle.ptr = (uintptr_t)texture->gpuHandle;
            er::gD3DRenderer->DestroyTexture((ID3D12Resource **)&texture->texture, cpuHandle, gpuHandle);
        },
        []() -> void * {
            return er::gD3DRenderer->CreateOffscreen();
        },
        [](void *offscreen) {
            er::gD3DRenderer->DestroyOffscreen((er::OffscreenContext *)offscreen);
        },
        [](void *offscreen) {
            (void)er::gD3DRenderer->BeginOffscreen((er::OffscreenContext *)offscreen);
        },
        [](void *offscreen) -> void * {
            return er::gD3DRenderer->EndOffscreen((er::OffscreenContext *)offscreen);
        },
        [](int keyChord) {
            return er::input::isKeyChordDown(keyChord);
        },
        [](int keyChord) {
            return er::input::isKeyChordPressed(keyChord);
        }
        };
        return &api;
    }
};

}

EROverlayAPI *getEROverlayAPI() {
    return er::EROverlayAPIWrapper::get();
}

const EROverlayNativeAPI *getEROverlayNativeAPI(uint32_t version) {
    if (version != 1)
        return nullptr;
    static const EROverlayNativeAPI native = {
        sizeof(EROverlayNativeAPI), 1,
        [] { return er::gGameFiles && er::gGameFiles->compatible(); },
        [](const ERFileRequest *request) -> uint64_t { return er::gGameFiles && request ? er::gGameFiles->request(*request) : 0; },
        [](uint64_t token, const wchar_t *part, ERFileData *data) {
            if (!data || !er::gGameFiles) return ER_FILE_INVALID;
            return er::gGameFiles->poll(token, part, *data);
        },
        [](uint64_t token) { if (er::gGameFiles) er::gGameFiles->release(token); },
        [](const void *bytes, uint64_t size) -> uint64_t { return er::gD3DRenderer ? er::gD3DRenderer->createDdsTexture(bytes, size) : 0; },
        [](uint64_t token, ERTextureView *view) {
            if (!view || !er::gD3DRenderer) return ER_TEXTURE_INVALID;
            return er::gD3DRenderer->pollTexture(token, *view);
        },
        [](uint64_t token) { if (er::gD3DRenderer) er::gD3DRenderer->retireTexture(token); },
        [](ERMapState *state) { return state && er::gGameFiles && er::gGameFiles->readMapState(*state); },
        [](void *offscreen) { return er::gD3DRenderer && er::gD3DRenderer->BeginOffscreen(static_cast<er::OffscreenContext *>(offscreen)); },
        [](uint32_t group) -> uintptr_t { return er::gGameFiles ? er::gGameFiles->findParamTable(group) : 0; },
        er::util::nativeLogEnabled() ? +[](const char *message) { if (message) er::util::nativeLog("%s", message); } : nullptr,
        [](uint32_t id) { return er::gGameFiles && er::gGameFiles->readEventFlag(id); },
        [](ERGameLayout *layout) { return layout && er::gGameFiles && er::gGameFiles->readGameLayout(*layout); },
        [](const void *bytes, uint64_t size) -> uint64_t { return er::gD3DRenderer ? er::gD3DRenderer->createDdsTexture(bytes, size) : 0; },
        [](void *offscreen, float x, float y, float width, float height) {
            return er::gD3DRenderer && er::gD3DRenderer->BeginOffscreenRegion(static_cast<er::OffscreenContext *>(offscreen), x, y, width, height);
        },
        [](uint64_t token) -> uint64_t { return er::gD3DRenderer ? er::gD3DRenderer->textureMemoryBytes(token) : 0; }
    };
    return &native;
}
