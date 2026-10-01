#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <unordered_map>

#include "data.hpp"
#include "defs/BonfireWarpParam.h"
#include "defs/WorldMapPointParam.h"
#include "params/param.hpp"
#include "resources.hpp"

extern EROverlayAPI *api;
namespace er::minimap {
Data gData;
static_assert(sizeof(WorldMapPointParam) == 256 && sizeof(BonfireWarpParam) == 236);
namespace {
template<typename T>
bool read(uintptr_t address, T &value) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), &value, sizeof(value), &copied) && copied == sizeof(value);
}
uintptr_t pointer(uintptr_t address) {
    uintptr_t value = 0;
    return read(address, value) ? value : 0;
}
bool flag(uint32_t id) { return nativeApi && nativeApi->readEventFlag(id); }
bool enabled(uint32_t id) { return !id || id == UINT32_MAX || flag(id); }
template<typename T>
uint32_t value(const T &row, size_t offset) {
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t *>(&row) + offset, 4);
    return v;
}
template<typename T>
bool alternate(const T &row, size_t secondEnable, size_t secondDisable) {
    for (size_t i = 0; i < 8; ++i) {
        int32_t text = static_cast<int32_t>(value(row, 48 + i * 12));
        const auto type = reinterpret_cast<const uint8_t *>(&row)[144 + i];
        if (text < 0 || type != 1)
            continue;
        auto enable1 = value(row, 52 + i * 12), enable2 = value(row, secondEnable + i * 4);
        auto disable1 = value(row, 56 + i * 12), disable2 = value(row, secondDisable + i * 4);
        if (!enabled(enable1) || !enabled(enable2))
            continue;
        unsigned activeDisable = 0;
        bool all = true;
        for (auto id: {disable1, disable2}) {
            if (!id || id == UINT32_MAX)
                continue;
            if (!flag(id))
                all = false;
            else
                ++activeDisable;
        }
        if (!all || !activeDisable)
            return true;
    }
    return false;
}
template<typename T, typename F>
void rows(const wchar_t *name, F function) {
    auto table = nativeApi->findParamTable(wcscmp(name, L"BonfireWarpParam") == 0 ? 43 : 87);
    uint16_t count;
    if (!table || !read(table + 0xA, count) || count > 20000)
        return;
    for (uint16_t i = 0; i < count; ++i) {
        params::ParamEntryOffset entry;
        T row;
        if (!read(table + 0x40 + size_t(i) * 24, entry) || entry.offset <= 0 || entry.offset > 0x10000000 || !read(table + entry.offset, row))
            continue;
        function(entry.paramId, row);
    }
}
// Read the game's already-built legacy-conversion trees. No Scaleform calls
// or allocations are required on the overlay update thread.
bool convert(uintptr_t view, uint32_t raw, float x, float y, float z, float &mapX, float &mapY) {
    uint64_t count = 0;
    if (!read(view + 0x280, count) || !count || count > 8)
        return false;
    for (uint64_t i = 0; i < count; ++i) {
        uintptr_t converter = view + 0xF8 + i * 48;
        uint8_t origin[4];
        if (!read(converter + 8, origin))
            continue;
        uint32_t target = raw;
        float px = x, py = y, pz = z;
        if ((raw >> 24) != 60 && (raw >> 24) != 61) {
            uintptr_t conversion = pointer(converter + 40);
            auto head = conversion ? pointer(conversion + 16) : 0;
            auto node = head ? pointer(head + 8) : 0, candidate = head;
            for (unsigned depth = 0; node && depth < 64; ++depth) {
                uint8_t nil = 1;
                uint32_t key;
                if (!read(node + 25, nil) || nil || !read(node + 28, key))
                    break;
                if (key >= raw) {
                    candidate = node;
                    node = pointer(node);
                } else
                    node = pointer(node + 16);
            }
            uint32_t key = 0;
            float offset[3];
            if (!candidate || candidate == head || !read(candidate + 28, key) || key != raw || !read(candidate + 32, target) || !read(candidate + 36, offset))
                continue;
            px += offset[0];
            py += offset[1];
            pz += offset[2];
        }
        if ((target >> 24) != origin[3])
            continue;
        float settings[6];
        if (!read(converter + 12, settings))
            continue;
        mapX = (px + (int((target >> 16) & 255) - int(origin[2])) * 256.f - settings[0]) * settings[5] + settings[3];
        mapY = -(pz + (int((target >> 8) & 255) - int(origin[1])) * 256.f - settings[2]) * settings[5] + settings[4];
        return std::isfinite(mapX) && std::isfinite(mapY);
    }
    return false;
}
uint32_t rawMap(uint8_t area, uint8_t x, uint8_t z) { return (uint32_t(area) << 24) | (uint32_t(x) << 16) | (uint32_t(z) << 8); }
} // namespace

void Data::update() {
    MapSnapshot next;
    if (!nativeApi || !nativeApi->readMapState(&next.state)) {
        next.onGUI = api->screenState() != 0;
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(next);
        return;
    }
    auto addresses = api->getGameAddresses();
    auto menu = pointer(addresses.csMenuManImp);
    uint32_t menuState = 1;
    if (menu)
        read(menu + 0x1C, menuState);
    next.onGUI = api->screenState() != 0 || menuState != 0;
    next.valid = !next.onGUI;
    auto camera = pointer(addresses.fieldArea);
    camera = camera ? pointer(camera + 0x20) : 0;
    camera = camera ? pointer(camera + 0x18) : 0;
    if (camera)
        read(camera + 0x10, next.camera);
    if (!std::isfinite(next.camera.yawCos) || !std::isfinite(next.camera.yawSin))
        next.camera = {};
    {
        std::lock_guard lock(mutex_);
        if (GetTickCount64() < markerRefresh_ && snapshot_.valid && snapshot_.state.generation == next.state.generation && snapshot_.state.rawMapId == next.state.rawMapId) {
            next.roundtable = snapshot_.roundtable;
            next.homeIcon = snapshot_.homeIcon;
            next.decorations = snapshot_.decorations;
            snapshot_ = std::move(next);
            return;
        }
    }
    markerRefresh_ = GetTickCount64() + 200;
    static uint64_t lastLog = 0;
    if (GetTickCount64() >= lastLog) {
        lastLog = GetTickCount64() + 5000;
        char line[256];
        std::snprintf(line, sizeof(line), "map raw=%08x map=%d position=%.2f,%.2f masks=%08x,%08x,%08x death=%d %.2f,%.2f/%d\n", next.state.rawMapId, next.state.mapId,
                      next.state.x, next.state.y, next.state.activeMasks[0], next.state.activeMasks[1], next.state.activeMasks[2], int(next.state.deathValid), next.state.deathX,
                      next.state.deathY, next.state.deathMapId);
        if (nativeApi->log)
            nativeApi->log(line);
    }
    uint32_t homeId = 0;
    auto common = nativeApi->findParamTable(141);
    if (common) {
        uint16_t count = 0;
        if (read(common + 0xA, count) && count) {
            params::ParamEntryOffset entry;
            if (read(common + 0x40, entry) && entry.paramId == 0 && entry.offset > 0)
                read(common + entry.offset + 0x278, homeId);
        }
    }
    std::unordered_map<uint64_t, uint32_t> nativeGraceIcons;
    auto view = next.state.viewModel;
    uintptr_t graces = pointer(view + 0x2E8), end = pointer(view + 0x2F0);
    if (graces && end >= graces && (end - graces) % 0x350 == 0 && (end - graces) / 0x350 < 2000) {
        for (auto entry = graces; entry < end; entry += 0x350) {
            uint32_t id;
            uint8_t normal = 1;
            std::array<uint8_t, 236> row{};
            if (!read(entry + 0x238, id) || !read(entry + 0x348, normal))
                continue;
            auto parameter = pointer(entry + 0x240);
            bool alt = parameter && read(parameter, row) && alternate(row, 168, 200);
            uint32_t icon;
            if (read(entry + (alt ? (normal ? 0x2C8 : 0x308) : (normal ? 0x248 : 0x288)), icon))
                nativeGraceIcons[id] = icon;
        }
    }
    rows<BonfireWarpParam>(L"BonfireWarpParam", [&](uint64_t id, const BonfireWarpParam &row) {
        uint32_t raw = rawMap(row.areaNo, row.gridXNo, row.gridZNo);
        uint32_t selected = row.iconId;
        auto game = nativeGraceIcons.find(id);
        if (game != nativeGraceIcons.end())
            selected = game->second;
        else if (alternate(row, 168, 200) && row.altIconId)
            selected = row.altIconId;
        if (id == homeId) {
            next.roundtable = next.state.rawMapId == raw;
            next.homeIcon = selected;
        }
        if (!flag(row.eventflagId))
            return;
        DecorationInfo marker;
        marker.id = id;
        marker.iconId = selected;
        marker.source = DecorationSource::Grace;
        marker.maps = (row.dispMask00 ? 1 : 0) | (row.dispMask01 ? 2 : 0) | (row.dispMask02 ? 4 : 0);
        if (!marker.maps || !selected || id == homeId || !convert(view, raw, row.posX, row.posY, row.posZ, marker.x, marker.y))
            return;
        next.decorations.push_back(marker);
    });
    rows<WorldMapPointParam>(L"WorldMapPointParam", [&](uint64_t id, const WorldMapPointParam &row) {
        if (id < 78500 || !row.iconId || row.iconId == 80)
            return;
        bool opened = flag(row.eventFlagId), distant = !opened && flag(row.distViewEventFlagId);
        if (!opened && !distant)
            return;
        DecorationInfo marker;
        marker.id = id;
        marker.iconId = row.iconId;
        marker.source = DecorationSource::Landmark;
        marker.areaIcon = row.isAreaIcon;
        marker.maps = (row.dispMask00 ? 1 : 0) | (row.dispMask01 ? 2 : 0) | (row.dispMask02 ? 4 : 0);
        if (!opened && distant && row.distViewIconId)
            marker.iconId = row.distViewIconId;
        else if (alternate(row, 192, 224) && row.altIconId)
            marker.iconId = row.altIconId;
        uint32_t raw = rawMap(row.areaNo, row.gridXNo, row.gridZNo);
        float x = row.posX, y = row.posY, z = row.posZ;
        if (distant && row.isOverrideDistViewMarkPos) {
            raw = rawMap(row.areaNo_forDistViewMark, row.gridXNo_forDistViewMark, row.gridZNo_forDistViewMark);
            x = row.posX_forDistViewMark;
            y = row.posY_forDistViewMark;
            z = row.posZ_forDistViewMark;
        }
        if (!marker.maps || !convert(view, raw, x, y, z, marker.x, marker.y))
            return;
        marker.rotationRad = row.angle * std::numbers::pi_v<float> / 180.f;
        next.decorations.push_back(marker);
    });
    ERMapState after;
    if (!nativeApi->readMapState(&after) || after.generation != next.state.generation || after.viewModel != view || after.rawMapId != next.state.rawMapId) {
        next.valid = false;
        next.state.deathValid = false;
        next.decorations.clear();
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(next);
    }
}
MapSnapshot Data::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
} // namespace er::minimap
