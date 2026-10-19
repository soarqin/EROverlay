#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <vector>

#include "goblins.hpp"
#include "resources.hpp"

namespace er::minimap {
Goblins gGoblins;
namespace {
void log(const char *message) {
    if (nativeApi && nativeApi->log)
        nativeApi->log(message);
}
} // namespace

bool Goblins::connect() {
    if (module_ || GetTickCount64() < retry_)
        return api_ != nullptr;
    retry_ = GetTickCount64() + 1000;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(0, L"MapForGoblins.dll", &module))
        return false;
    module_ = module; // Keep the provider loaded through callbacks, point views and icon import.
    auto getAPI = reinterpret_cast<MfgGetMapAPIFn>(GetProcAddress(module, "MfgGetMapAPI"));
    const auto *bridge = getAPI ? getAPI(MFG_MAP_ABI) : nullptr;
    if (!bridge || bridge->size < sizeof(MfgMapAPI) || bridge->abi_version != MFG_MAP_ABI || bridge->point_size != sizeof(MfgMapPoint) || bridge->icon_size != sizeof(MfgMapIcon) ||
        !bridge->view_points || !bridge->get_icon || !bridge->icon_count || bridge->icon_count > MFG_MAP_MAX_ICONS) {
        log("minimap-goblins: MapForGoblins.dll has no compatible read-only map API\n");
        return false;
    }
    std::vector<RuntimeIcon> icons;
    icons.reserve(bridge->icon_count);
    for (uint32_t i = 0; i < bridge->icon_count; ++i) {
        MfgMapIcon icon{};
        if (bridge->get_icon(i, &icon) != MFG_MAP_OK || !icon.lossless2 || !icon.lossless2_size || icon.lossless2_size > 1024 * 1024 || !icon.matrix || !icon.matrix_size ||
            icon.matrix_size > 64) {
            log("minimap-goblins: invalid icon catalog\n");
            return false;
        }
        icons.push_back({icon.source_icon_id, {icon.lossless2, icon.lossless2_size}, {icon.matrix, icon.matrix_size}});
    }
    if (!gResources.loadRuntimeIcons(icons)) {
        log("minimap-goblins: cannot decode icon catalog\n");
        return false;
    }
    api_ = bridge;
    log("minimap-goblins: connected to MapForGoblins read-only map API\n");
    return true;
}

std::span<const MfgMapPoint> Goblins::viewPoints(uint32_t layer) {
    if (layer > 2 || !connect())
        return {};
    const MfgMapPoint *points = nullptr;
    uint32_t count = 0;
    if (api_->view_points(layer, &points, &count) != MFG_MAP_OK || count > MFG_MAP_MAX_POINTS || (count && !points))
        return {};
    return {points, count};
}

void Goblins::stop() {
    api_ = nullptr;
    if (module_)
        FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
    retry_ = 0;
}

} // namespace er::minimap
