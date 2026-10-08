#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <unordered_map>

#include "data.hpp"
#include "defs/BonfireWarpParam.h"
#include "defs/WorldMapPointParam.h"
#include "layout.hpp"
#include "resources.hpp"
#include "util/paramreader.hpp"

extern EROverlayAPI *api;
namespace er::minimap {
Data gData;
static_assert(sizeof(WorldMapPointParam) == 256 && sizeof(BonfireWarpParam) == 236);
namespace {
bool readBytes(uintptr_t address, void *value, size_t bytes) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), value, bytes, &copied) && copied == bytes;
}
template<typename T>
bool read(uintptr_t address, T &value) {
    return readBytes(address, &value, sizeof(value));
}
uintptr_t pointer(uintptr_t address) {
    uintptr_t value = 0;
    return read(address, value) ? value : 0;
}
// CSMenuMarkersSaveData reserves ten records (RVA 0x81A550), while the
// ordinary placement path limits active numbered beacons to five (0x887440).
constexpr size_t PLAYER_MARKER_LIMIT = 5;
constexpr size_t PLAYER_MARKER_CAPACITY = 10;
struct SavedPlayerMarker {
    int32_t id;
    float x, y;
    uint8_t map, icon;
    uint16_t padding;
};
static_assert(sizeof(SavedPlayerMarker) == 16 && offsetof(SavedPlayerMarker, map) == 12 && offsetof(SavedPlayerMarker, icon) == 13);
[[nodiscard]] bool readPlayerMarkers(uintptr_t view, PlayerMarkers &markers) {
    markers.clear();
    if (!view)
        return false;
    struct Header {
        uintptr_t slots;
        uint64_t capacity;
    } first{}, after{};
    auto save = pointer(view + 0x338); // WorldMapMarkerDataList +0x38.
    uint64_t count = 0, afterCount = 0;
    std::array<SavedPlayerMarker, PLAYER_MARKER_CAPACITY> records{}, afterRecords{};
    if (!save || !read(save + 8, first) || !first.slots || !first.capacity || first.capacity > records.size() || !read(save + 0x40, count) || count > first.capacity)
        return false;
    size_t bytes = static_cast<size_t>(first.capacity) * sizeof(SavedPlayerMarker);
    // Copy values, never retain game-owned slots. A concurrent insertion,
    // deletion or load discards this sample and is retried on the next update.
    if (!readBytes(first.slots, records.data(), bytes) || !readBytes(first.slots, afterRecords.data(), bytes) || !read(save + 8, after) || !read(save + 0x40, afterCount) ||
        pointer(view + 0x338) != save || first.slots != after.slots || first.capacity != after.capacity || count != afterCount ||
        std::memcmp(records.data(), afterRecords.data(), bytes) ||
        static_cast<uint64_t>(std::count_if(records.begin(), records.begin() + first.capacity, [](const auto &record) { return record.id >= 0; })) != count)
        return false;
    for (size_t slot = 0; slot < std::min<size_t>(first.capacity, PLAYER_MARKER_LIMIT); ++slot) {
        const auto &record = records[slot];
        if (record.id < 0 || !std::isfinite(record.x) || !std::isfinite(record.y) || (record.map != 0 && record.map != 1 && record.map != 10) || record.icon != 1)
            continue;
        // 0x879330 formats Text_0 with slot+1, independent of the save ID
        // and of the active list's ordering. These are already map coordinates.
        markers.push_back({record.id, record.x, record.y, static_cast<uint8_t>(slot + 1), record.map});
    }
    return true;
}
bool flag(uint32_t id) { return nativeApi && nativeApi->readEventFlag(id); }
// WorldMapPinData::SetTo (RVA 0x87BE10) shows Cleared only for an active,
// nonzero completion event. Native constructors normalize -1 to zero.
bool cleared(uint32_t id) { return id && id != UINT32_MAX && flag(id); }
bool enabled(uint32_t id) { return !id || id == UINT32_MAX || flag(id); }
template<typename T>
uint32_t value(const T &row, size_t offset) {
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t *>(&row) + offset, 4);
    return v;
}
template<typename T>
bool textActive(const T &row, size_t index, size_t secondEnable, size_t secondDisable) {
    if (static_cast<int32_t>(value(row, 48 + index * 12)) < 0)
        return false;
    auto enable1 = value(row, 52 + index * 12), enable2 = value(row, secondEnable + index * 4);
    if (!enabled(enable1) || !enabled(enable2))
        return false;
    unsigned activeDisable = 0;
    for (auto id: {value(row, 56 + index * 12), value(row, secondDisable + index * 4)}) {
        if (!id || id == UINT32_MAX)
            continue;
        if (!flag(id))
            return true;
        ++activeDisable;
    }
    return !activeDisable;
}
template<typename T>
bool alternate(const T &row, size_t secondEnable, size_t secondDisable) {
    for (size_t i = 0; i < 8; ++i) {
        const auto type = reinterpret_cast<const uint8_t *>(&row)[144 + i];
        if (type == 1 && textActive(row, i, secondEnable, secondDisable))
            return true;
    }
    return false;
}
bool hasText(const WorldMapPointParam &row) {
    for (size_t i = 0; i < 8; ++i)
        if (textActive(row, i, 192, 224))
            return true;
    return false;
}
template<typename T, typename F>
void rows(uint32_t group, size_t minimum, F function) {
    auto table = nativeApi->findParamTable(group);
    util::ParamRows reader;
    if (!table)
        return;
    if (!reader.open(table)) {
        if (nativeApi->log) {
            char line[120];
            std::snprintf(line, sizeof(line), "minimap-param group=%u invalid-directory\n", group);
            nativeApi->log(line);
        }
        return;
    }
    for (const auto &entry: reader.rows()) {
        T row{};
        if (!reader.read(entry, row, minimum)) {
            if (nativeApi->log) {
                char line[150];
                std::snprintf(line, sizeof(line), "minimap-param group=%u row=%u bytes=%zu minimum=%zu unreadable-or-short-row\n", group, entry.id, entry.size, minimum);
                nativeApi->log(line);
            }
            continue;
        }
        function(entry.id, row, entry.size);
    }
}
// Read the game's already-built legacy-conversion trees. No Scaleform calls
// or allocations are required on the overlay update thread. Converter headers
// are read once per refresh instead of once per marker; tree lookups are kept
// across refreshes, keyed by the tree head that a rebuilt tree would replace.
class MapConverter {
public:
    MapConverter(uintptr_t view, uint32_t mapMask, MapConversionCache &cache) : cache_(cache) {
        uint64_t count = 0;
        if (!read(view + 0x280, count) || !count || count > 8)
            return;
        for (uint64_t i = 0; i < count; ++i) {
            uintptr_t address = view + 0xF8 + i * 48;
            Converter converter{};
            if (!read(address + 8, converter.origin) || (converter.origin[3] != 60 && (converter.origin[3] != 61 || !(mapMask & 4))) ||
                !read(address + 12, converter.settings))
                continue;
            auto conversion = pointer(address + 40);
            converter.head = conversion ? pointer(conversion + 16) : 0;
            converters_.push_back(converter);
        }
    }
    bool convert(uint32_t raw, float x, float z, float &mapX, float &mapY) {
        for (const auto &converter: converters_) {
            uint32_t target = raw;
            float px = x, pz = z;
            if ((raw >> 24) != 60 && (raw >> 24) != 61) {
                const auto *conversion = lookup(converter.head, raw);
                if (!conversion)
                    continue;
                target = conversion->target;
                px += conversion->offset[0];
                pz += conversion->offset[2];
            }
            const auto &origin = converter.origin;
            if ((target >> 24) != origin[3])
                continue;
            const auto &settings = converter.settings;
            mapX = (px + (int((target >> 16) & 255) - int(origin[2])) * 256.f - settings[0]) * settings[5] + settings[3];
            mapY = -(pz + (int((target >> 8) & 255) - int(origin[1])) * 256.f - settings[2]) * settings[5] + settings[4];
            return std::isfinite(mapX) && std::isfinite(mapY);
        }
        return false;
    }

private:
    struct Converter {
        uint8_t origin[4];
        float settings[6];
        uintptr_t head;
    };
    const MapConversion *lookup(uintptr_t head, uint32_t raw) {
        if (!head)
            return nullptr;
        auto found = cache_.find({head, raw});
        if (found == cache_.end()) {
            MapConversion conversion;
            // An unreadable tree is not cached; the next refresh retries it.
            if (!find(head, raw, conversion))
                return nullptr;
            found = cache_.emplace(std::pair{head, raw}, conversion).first;
        }
        return found->second.found ? &found->second : nullptr;
    }
    // Native map lower_bound: +0 left, +16 right, +25 nil, +28 key, +32 target, +36 offset.
    static bool find(uintptr_t head, uint32_t raw, MapConversion &conversion) {
        uintptr_t node = pointer(head + 8), candidate = head;
        for (unsigned depth = 0;; ++depth) {
            uint8_t nil = 1;
            uint32_t key = 0;
            if (!node || depth >= 64 || !read(node + 25, nil))
                return false;
            if (nil)
                break;
            if (!read(node + 28, key))
                return false;
            if (key >= raw) {
                candidate = node;
                node = pointer(node);
            } else
                node = pointer(node + 16);
        }
        uint32_t key = 0;
        if (candidate == head)
            return true;
        if (!read(candidate + 28, key))
            return false;
        if (key != raw)
            return true;
        conversion.found = read(candidate + 32, conversion.target) && read(candidate + 36, conversion.offset);
        return conversion.found;
    }
    MapConversionCache &cache_;
    std::vector<Converter> converters_;
};
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
    if (next.onGUI) {
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(next);
        return;
    }
    auto camera = pointer(addresses.fieldArea);
    camera = camera ? pointer(camera + 0x20) : 0;
    camera = camera ? pointer(camera + 0x18) : 0;
    if (camera)
        read(camera + 0x10, next.camera);
    if (!std::isfinite(next.camera.yawCos) || !std::isfinite(next.camera.yawSin))
        next.camera = {};
    auto publish = [&] {
        if (next.valid)
            (void)readPlayerMarkers(next.state.viewModel, next.playerMarkers);
        ERMapState after{};
        if (!nativeApi->readMapState(&after) || after.generation != next.state.generation || after.viewModel != next.state.viewModel || after.rawMapId != next.state.rawMapId ||
            after.mapId != next.state.mapId || after.underground != next.state.underground) {
            next.valid = false;
            next.state.deathValid = false;
            next.decorations = {};
            next.decorationStorage_.reset();
            next.playerMarkers.clear();
        }
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(next);
    };
    bool cached = false;
    {
        std::lock_guard lock(mutex_);
        if (GetTickCount64() < markerRefresh_ && snapshot_.valid && snapshot_.state.generation == next.state.generation && snapshot_.state.rawMapId == next.state.rawMapId &&
            snapshot_.state.viewModel == next.state.viewModel) {
            next.roundtable = snapshot_.roundtable;
            next.homeIcon = snapshot_.homeIcon;
            next.decorations = snapshot_.decorations;
            next.decorationStorage_ = snapshot_.decorationStorage_;
            cached = true;
        }
    }
    if (cached) {
        publish();
        return;
    }
    markerRefresh_ = GetTickCount64() + 200;
    auto decorations = std::make_shared<std::vector<DecorationInfo>>();
    size_t previousCount = 0;
    {
        std::lock_guard lock(mutex_);
        previousCount = snapshot_.decorations.size();
    }
    decorations->reserve(previousCount);
    const auto layout = gameLayout();
    uint32_t homeId = 0;
    auto common = nativeApi->findParamTable(141);
    util::ParamRows commonRows;
    if (commonRows.open(common))
        for (const auto &entry: commonRows.rows())
            if (entry.id == 0) {
                (void)commonRows.field(entry, 0x278, homeId);
                break;
            }
    struct GraceIcons {
        uint32_t base = 0, alternate = 0;
    };
    std::unordered_map<uint64_t, GraceIcons> nativeGraceIcons;
    auto view = next.state.viewModel;
    if (conversionGeneration_ != next.state.generation || conversions_.size() > 4096) {
        conversions_.clear();
        conversionGeneration_ = next.state.generation;
    }
    MapConverter converter(view, layout.mapMask, conversions_);
    uintptr_t graces = pointer(view + 0x2E8), end = pointer(view + 0x2F0);
    if (graces && end >= graces && layout.graceStride && (end - graces) % layout.graceStride == 0 && (end - graces) / layout.graceStride < 2000) {
        // Copy the native grace array once; fields past its end, or an
        // unreadable copy, fall back to individual reads.
        std::vector<uint8_t> graceBytes(end - graces);
        bool copied = readBytes(graces, graceBytes.data(), graceBytes.size());
        auto field = [&](uintptr_t address, auto &value) {
            size_t offset = address - graces;
            if (copied && offset <= graceBytes.size() && sizeof(value) <= graceBytes.size() - offset) {
                std::memcpy(&value, graceBytes.data() + offset, sizeof(value));
                return true;
            }
            return read(address, value);
        };
        for (auto entry = graces; entry < end; entry += layout.graceStride) {
            uint32_t id;
            uint8_t normal = 1;
            if (!field(entry + 0x238, id) || !field(entry + layout.graceNormalOffset, normal))
                continue;
            GraceIcons icons{};
            if (field(entry + (normal ? 0x248 : 0x288), icons.base)) {
                if (layout.alternateIcons)
                    (void)field(entry + (normal ? 0x2C8 : 0x308), icons.alternate);
                nativeGraceIcons[id] = icons;
            }
        }
    }
    rows<BonfireWarpParam>(43, 48, [&](uint64_t id, const BonfireWarpParam &row, size_t size) {
        // Only the home grace and discovered graces need an icon, so skip
        // the alternate-text flag reads for every undiscovered grace.
        bool home = id == homeId, discovered = flag(row.eventflagId);
        if (!home && !discovered)
            return;
        uint32_t raw = rawMap(row.areaNo, row.gridXNo, row.gridZNo);
        uint32_t selected = row.iconId;
        auto game = nativeGraceIcons.find(id);
        bool alt = layout.alternateIcons && size >= sizeof(row) && alternate(row, 168, 200);
        if (game != nativeGraceIcons.end())
            selected = alt ? game->second.alternate : game->second.base;
        else if (alt && row.altIconId)
            selected = row.altIconId;
        if (home) {
            next.roundtable = next.state.rawMapId == raw;
            next.homeIcon = selected;
        }
        if (!discovered)
            return;
        DecorationInfo marker;
        marker.id = id;
        marker.iconId = selected;
        marker.source = DecorationSource::Grace;
        marker.maps = (row.dispMask00 ? 1 : 0) | (row.dispMask01 ? 2 : 0) | (row.dispMask02 ? 4 : 0);
        marker.maps &= layout.mapMask;
        if (!marker.maps || !selected || home || !converter.convert(raw, row.posX, row.posZ, marker.x, marker.y))
            return;
        marker.cleared = cleared(row.clearedEventFlagId);
        decorations->push_back(marker);
    });
    rows<WorldMapPointParam>(87, offsetof(WorldMapPointParam, posZ_forDistViewMark) + sizeof(float), [&](uint64_t id, const WorldMapPointParam &row, size_t size) {
        if (id < 78500 || !row.iconId || row.iconId == 80)
            return;
        bool opened = flag(row.eventFlagId), distant = !opened && flag(row.distViewEventFlagId);
        if (!opened && !distant)
            return;
        // WorldMapPinData::_Update (0x87BF90): ordinary points require an
        // active text slot. Mod completion points disable theirs when the
        // colocated grace activates, while area/no-text points remain visible.
        if (!row.isAreaIcon && !row.isEnableNoText && !hasText(row))
            return;
        DecorationInfo marker;
        marker.id = id;
        marker.iconId = row.iconId;
        marker.source = DecorationSource::Landmark;
        marker.areaIcon = row.isAreaIcon;
        marker.maps = (row.dispMask00 ? 1 : 0) | (row.dispMask01 ? 2 : 0) | (row.dispMask02 ? 4 : 0);
        marker.maps &= layout.mapMask;
        if (!opened && distant && row.distViewIconId)
            marker.iconId = row.distViewIconId;
        else if (layout.alternateIcons && size >= sizeof(row) && alternate(row, 192, 224) && row.altIconId)
            marker.iconId = row.altIconId;
        uint32_t raw = rawMap(row.areaNo, row.gridXNo, row.gridZNo);
        float x = row.posX, z = row.posZ;
        if (distant && row.isOverrideDistViewMarkPos) {
            raw = rawMap(row.areaNo_forDistViewMark, row.gridXNo_forDistViewMark, row.gridZNo_forDistViewMark);
            x = row.posX_forDistViewMark;
            z = row.posZ_forDistViewMark;
        }
        if (!marker.maps || !converter.convert(raw, x, z, marker.x, marker.y))
            return;
        marker.cleared = cleared(row.clearedEventFlagId);
        marker.rotationRad = row.angle * std::numbers::pi_v<float> / 180.f;
        decorations->push_back(marker);
    });
    next.decorationStorage_ = std::move(decorations);
    next.decorations = *next.decorationStorage_;
    publish();
}
MapSnapshot Data::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
} // namespace er::minimap
