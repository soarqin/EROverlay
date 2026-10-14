#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>

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
    module_ = module; // Own a reference through every callback and resource copy.
    auto getAPI = reinterpret_cast<MfgGetMapAPIFn>(GetProcAddress(module, "MfgGetMapAPI"));
    const auto *bridge = getAPI ? getAPI(MFG_MAP_ABI) : nullptr;
    if (!bridge || bridge->size < sizeof(MfgMapAPI) || bridge->abi_version != MFG_MAP_ABI || bridge->point_size != sizeof(MfgMapPoint) || bridge->icon_size != sizeof(MfgMapIcon) ||
        !bridge->copy_points || !bridge->get_icon || !bridge->icon_count || bridge->icon_count > MFG_MAP_MAX_ICONS) {
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
    buffer_.resize(256);
    log("minimap-goblins: connected to MapForGoblins read-only map API v1\n");
    return true;
}

std::span<const MfgMapPoint> Goblins::points(uint32_t layer) {
    if (layer > 2 || !connect())
        return {};
    // Visibility may change between the sizing and copying calls. Bound retries
    // and allocation; a rapidly changing snapshot is retried next update.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint32_t count = 0;
        int result = api_->copy_points(layer, buffer_.data(), static_cast<uint32_t>(buffer_.size()), &count);
        if (count > MFG_MAP_MAX_POINTS)
            return {};
        if (result == MFG_MAP_SMALL_BUFFER && count > buffer_.size()) {
            buffer_.resize(count);
            continue;
        }
        if (result != MFG_MAP_OK || count > buffer_.size())
            return {};
        auto snapshot = std::span(buffer_).first(count);
        if (!std::ranges::is_sorted(snapshot, {}, &MfgMapPoint::draw_order))
            std::stable_sort(snapshot.begin(), snapshot.end(), [](const auto &a, const auto &b) { return a.draw_order < b.draw_order; });
        return snapshot;
    }
    return {};
}

void Goblins::stop() {
    api_ = nullptr;
    buffer_.clear();
    if (module_)
        FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
    retry_ = 0;
}

} // namespace er::minimap
