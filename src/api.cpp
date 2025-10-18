#include "api.h"

#include "config.hpp"
#include "global.hpp"
#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "params/paraminternal.hpp"
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
            static std::string value;
            value = er::gConfig[name];
            return value.c_str();
        },
        [](const char *name, const wchar_t *defValue) {
            static std::wstring value;
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

        [] {
            return GameAddresses {er::gHooking->csMenuManImp_, er::gHooking->gameDataMan_, er::gHooking->eventFlagMan_, er::gHooking->fieldArea_};
        },
        [](uint32_t flagId, uint8_t *bits) {
            auto eventFlagMan = er::gHooking->eventFlagMan_;
            if (!eventFlagMan) {
                *bits = 0;
                return uintptr_t(0);
            }
            auto addr = util::MemoryHandle(eventFlagMan).as<uintptr_t &>();
            if (addr == 0) {
                *bits = 0;
                return uintptr_t(0);
            }
            auto divisor = util::MemoryHandle(addr + 0x1C).as<uint32_t &>();
            if (divisor == 0) {
                *bits = 0;
                return uintptr_t(0);
            }
            auto flagAddress = util::MemoryHandle(addr + 0x28).as<uintptr_t &>();
            if (flagAddress == 0) {
                *bits = 0;
                return uintptr_t(0);
            }
            auto category = flagId / divisor;
            auto least_significant_digits = flagId - category * divisor;
            auto current_element = util::MemoryHandle(addr + 0x38).as<uintptr_t &>();
            auto current_sub_element = util::MemoryHandle(current_element + 0x08).as<uintptr_t &>();
            while (*(uint8_t *)(current_sub_element + 0x19) == 0) {
                if (*(uint32_t *)(current_sub_element + 0x20) < category)
                    current_sub_element = util::MemoryHandle(current_sub_element + 0x10).as<uintptr_t &>();
                else {
                    current_element = current_sub_element;
                    current_sub_element = util::MemoryHandle(current_sub_element).as<uintptr_t &>();
                }
            }
            if (current_element == current_sub_element) {
                *bits = 0;
                return uintptr_t(0);
            }
            auto mystery_value = *(uint32_t *)(current_element + 0x28) - 1;
            uintptr_t calculated_pointer = 0;
            if (mystery_value != 0) {
                if (mystery_value != 1)
                    calculated_pointer = util::MemoryHandle(current_element + 0x30).as<uintptr_t &>();
                else {
                    *bits = 0;
                    return uintptr_t(0);
                }
            } else {
                calculated_pointer = (*(uint32_t *)(addr + 0x20)) * (*(uint32_t *)(current_element + 0x30)) + flagAddress;
            }

            if (calculated_pointer == 0) {
                *bits = 0;
                return uintptr_t(0);
            }
            auto thing = 7 - (least_significant_digits & 7);
            auto shifted = least_significant_digits >> 3;
            *bits = 1 << thing;
            return calculated_pointer + shifted;
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
        }
        };
        return &api;
    }
};
}

EROverlayAPI *getEROverlayAPI() {
    return er::EROverlayAPIWrapper::get();
}
