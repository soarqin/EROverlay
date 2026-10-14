// Replay actual mod PARAM, GFX, layouts and DDS through Data/Resources/Renderer.
// All game state is owned by this process; no debugger or game writes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "minimap/defs/BonfireWarpParam.h"
#include "minimap/defs/WorldMapPointParam.h"
#include "minimap/goblins.hpp"
#include "minimap/render.hpp"
#include "util/mapstate.hpp"
#include "util/paramreader.hpp"

EROverlayAPI *api = nullptr;
namespace {
using namespace er::minimap;
std::array<uint8_t, 1200> menu{}, view{};
std::array<uint8_t, 64> conversion{}, head{}, node{};
std::array<uint8_t, 0x350> nativeGrace{};
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data());
std::vector<uint8_t> graces, points, tpf;
std::unordered_set<uint32_t> flags;
std::unordered_map<std::wstring, er::util::Bytes> images;
std::unordered_map<uint64_t, ERTextureView> textures;
std::vector<std::vector<uint8_t>> coloredSprites;
uint64_t generation = 1, nextTexture = 0;
const wchar_t *shape = L"rect", *rotate = L"false";
bool showGraces = true, showLandmarks = true, showOthers = false;
bool vanillaCompletion = false;
bool warpAboveItems = true;

std::vector<uint8_t> readFile(const char *path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() <= 0)
        return {};
    auto size = stream.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    return stream.read(reinterpret_cast<char *>(bytes.data()), size) ? bytes : std::vector<uint8_t>{};
}
std::vector<uint8_t> paramFile(const char *path) {
    auto bytes = readFile(path);
    if (bytes.empty() || bytes.size() > UINT32_MAX)
        return {};
    // Native PARAM allocations retain their original file length at table[-16].
    std::vector<uint8_t> storage(bytes.size() + 16);
    auto length = static_cast<uint32_t>(bytes.size());
    std::memcpy(storage.data(), &length, sizeof(length));
    std::memcpy(storage.data() + 16, bytes.data(), bytes.size());
    return storage;
}
template<typename T>
void put(uint8_t *bytes, size_t offset, const T &value) {
    std::memcpy(bytes + offset, &value, sizeof(value));
}
void configure(EROverlayAPI &legacy, EROverlayNativeAPI &native) {
    legacy.screenState = [] { return 0; };
    legacy.getGameAddresses = [] { return GameAddresses{reinterpret_cast<uintptr_t>(&menuPointer), 0, 0, 0, 0}; };
    legacy.configGetInt = [](const char *, int fallback) { return fallback; };
    legacy.configGetVirtualKey = [](const char *, int) { return 0; };
    legacy.configGetString = [](const char *key, const wchar_t *fallback) {
        if (!std::strcmp(key, "minimap.presets.order"))
            return L"compact";
        if (!std::strcmp(key, "minimap.preset.compact.shape"))
            return shape;
        if (!std::strcmp(key, "minimap.preset.compact.rotate"))
            return rotate;
        if (!std::strcmp(key, "minimap.preset.compact.opacity"))
            return L"0.75";
        if (!std::strcmp(key, "minimap.preset.compact.zoom"))
            return L"0.75";
        if (!std::strcmp(key, "minimap.markers.graces"))
            return showGraces ? L"true" : L"false";
        if (!std::strcmp(key, "minimap.markers.landmarks"))
            return showLandmarks ? L"true" : L"false";
        if (!std::strcmp(key, "minimap.markers.others") && showOthers)
            return L"true";
        return fallback;
    };
    legacy.inputIsKeyPressed = [](int) { return false; };
    legacy.createOffscreen = [] { return reinterpret_cast<void *>(1); };
    legacy.destroyOffscreen = [](void *) {};
    legacy.endOffscreen = [](void *) { return reinterpret_cast<void *>(99); };
    api = &legacy;
    native.size = sizeof(native);
    native.readMapState = [](ERMapState *state) {
        *state = {};
        state->generation = generation;
        return er::util::readWorldMapView(reinterpret_cast<uintptr_t>(view.data()), *state);
    };
    native.findParamTable = [](uint32_t group) -> uintptr_t {
        return group == 43 && !graces.empty()   ? reinterpret_cast<uintptr_t>(graces.data() + 16)
               : group == 87 && !points.empty() ? reinterpret_cast<uintptr_t>(points.data() + 16)
                                                : 0;
    };
    native.readEventFlag = [](uint32_t id) { return flags.contains(id); };
    native.requestFile = [](const ERFileRequest *request) -> uint64_t { return request->tpfNameCount ? 1 : 0; };
    native.pollFile = [](uint64_t, const wchar_t *name, ERFileData *data) {
        *data = {};
        auto found = name ? images.find(name) : images.end();
        if (found == images.end())
            return ER_FILE_FAILED;
        *data = {found->second.data(), found->second.size(), 1};
        return ER_FILE_SUCCEEDED;
    };
    native.releaseFile = [](uint64_t) {};
    native.queueDdsTexture = [](const void *data, uint64_t size) -> uint64_t {
        er::util::DdsImage image;
        if (!er::util::parseDds({static_cast<const uint8_t *>(data), static_cast<size_t>(size)}, image))
            return 0;
        if (image.format == 28 || image.format == 29) {
            const auto *bytes = static_cast<const uint8_t *>(data);
            std::vector<uint8_t> sprite(bytes, bytes + size);
            if (std::find(coloredSprites.begin(), coloredSprites.end(), sprite) == coloredSprites.end())
                coloredSprites.push_back(std::move(sprite));
        }
        auto token = ++nextTexture;
        textures.emplace(token, ERTextureView{reinterpret_cast<void *>(uintptr_t(token)), image.width, image.height});
        return token;
    };
    native.pollTexture = [](uint64_t token, ERTextureView *data) {
        auto found = textures.find(token);
        if (found == textures.end())
            return ER_TEXTURE_INVALID;
        *data = found->second;
        return ER_TEXTURE_READY;
    };
    native.retireTexture = [](uint64_t token) { textures.erase(token); };
    native.beginOffscreen = [](void *) { return true; };
    nativeApi = &native;
}
uint64_t drawIndex(const IconLayer &layer, const ImVec2 *position = nullptr) {
    AtlasRegion region{};
    ERTextureView texture{};
    if (!gResources.layerView(layer, region, texture))
        return UINT64_MAX;
    uint64_t offset = 0;
    for (const auto *list: ImGui::GetDrawData()->CmdLists) {
        for (const auto &command: list->CmdBuffer) {
            if (command.UserCallback || command.GetTexID() != reinterpret_cast<ImTextureID>(texture.gpuHandle))
                continue;
            for (unsigned index = command.IdxOffset; index < command.IdxOffset + command.ElemCount; ++index) {
                const auto &vertex = list->VtxBuffer[list->IdxBuffer[index] + command.VtxOffset];
                if (std::abs(vertex.uv.x - float(region.x) / texture.width) < .00001f && std::abs(vertex.uv.y - float(region.y) / texture.height) < .00001f &&
                    (!position || (std::abs(vertex.pos.x - position->x) < .02f && std::abs(vertex.pos.y - position->y) < .02f)))
                    return offset + index;
            }
        }
        offset += list->IdxBuffer.Size;
    }
    return UINT64_MAX;
}
bool drawn(const IconLayer &layer, const ImVec2 *position = nullptr) { return drawIndex(layer, position) != UINT64_MAX; }
bool startupDefinitions(er::util::Bytes gfx, er::util::Bytes layouts) {
    auto *previous = nativeApi;
    EROverlayNativeAPI gateway = *previous;
    static er::util::Bytes startupGfx, startupLayouts;
    startupGfx = gfx;
    startupLayouts = layouts;
    static bool requestedMod = false;
    requestedMod = false;
    // Forward paths unchanged, as older cores did. A mod VFS lookup must
    // receive the canonical lowercase GFX key directly from Resources.
    gateway.readMapState = [](ERMapState *state) {
        *state = {};
        return true;
    };
    gateway.requestFile = [](const ERFileRequest *request) -> uint64_t {
        if (request->tpfNameCount)
            return 1;
        if (!std::wcscmp(request->path, L"menu:/02_120_worldmap.gfx")) {
            requestedMod = true;
            return 2;
        }
        if (!std::wcscmp(request->path, L"menu:/hi/01_common.sblytbnd.dcx"))
            return 3;
        return 4;
    };
    gateway.pollFile = [](uint64_t token, const wchar_t *name, ERFileData *data) {
        *data = {};
        if (token == 1) {
            auto found = name ? images.find(name) : images.end();
            if (found == images.end())
                return ER_FILE_FAILED;
            *data = {found->second.data(), found->second.size(), 1};
        } else if (token == 2 || token == 3) {
            auto bytes = token == 2 ? startupGfx : startupLayouts;
            *data = {bytes.data(), bytes.size(), 1};
        } else
            return ER_FILE_FAILED;
        return ER_FILE_SUCCEEDED;
    };
    nativeApi = &gateway;
    bool ok;
    {
        Resources resources;
        resources.update();
        GfxMovie movie;
        ok = requestedMod && movie.parse(gfx) && resources.iconCount() > 0 && resources.markerLayers().size() == 7;
        if (ok && movie.iconFrameCount() == 376)
            ok = resources.icon(349) && resources.icon(366) && resources.icon(376);
    }
    nativeApi = previous;
    if (ok)
        std::puts("PASS: actual GFX definitions and mod-only frames load through an unmodified VFS request path");
    else
        std::fputs("FAIL: canonical mod GFX request or startup definitions\n", stderr);
    return ok;
}
template<typename T>
std::vector<uint8_t> paramRows(std::initializer_list<std::pair<uint32_t, T>> rows, bool reversed = false) {
    size_t start = 0x40 + rows.size() * 24, size = start + rows.size() * sizeof(T);
    std::vector<uint8_t> storage(16 + size);
    put(storage.data(), 0, static_cast<uint32_t>(size));
    auto *table = storage.data() + 16;
    put(table, 0, static_cast<uint32_t>(size));
    put(table, 0xA, static_cast<uint16_t>(rows.size()));
    table[0x2D] = 4;
    table[0x2E] = 2;
    size_t index = 0;
    for (const auto &[id, row]: rows) {
        size_t destination = reversed ? rows.size() - 1 - index : index;
        put(table, 0x40 + destination * 24, id);
        put(table, 0x48 + destination * 24, static_cast<uint64_t>(start + destination * sizeof(T)));
        put(table, start + destination * sizeof(T), row);
        ++index;
    }
    return storage;
}
bool overlappingMarkers() {
    // Use real GFX/atlas recipes with several simultaneous pins. Per-icon
    // presence checks cannot detect an opaque landmark covering a boss/grace.
    const auto *mine = gResources.icon(25), *grace = gResources.icon(1), *boss = gResources.icon(366);
    if (!boss)
        boss = gResources.icon(80); // Vanilla fixture still exercises the same overlap.
    if (!mine || !grace || !boss)
        return false;
    std::array expected{MapLayer::Area, MapLayer::Item, MapLayer::Warp, MapLayer::Memo, MapLayer::Marker, MapLayer::Dead, MapLayer::Player};
    if (!warpAboveItems)
        std::swap(expected[1], expected[2]);
    auto layers = gResources.markerLayers();
    if (layers.size() != expected.size() || !std::equal(layers.begin(), layers.end(), expected.begin())) {
        std::fputs("FAIL: marker containers do not follow this fixture's GFX depths\n", stderr);
        return false;
    }
    auto savedGraces = std::move(graces), savedPoints = std::move(points);
    WorldMapPointParam landmark{};
    landmark.iconId = 25;
    landmark.areaNo = 60;
    landmark.gridXNo = 28;
    landmark.gridZNo = 64;
    landmark.dispMask00 = 1;
    for (size_t text = 1; text < 8; ++text)
        put(reinterpret_cast<uint8_t *>(&landmark), 48 + text * 12, int32_t{-1});
    auto bossPoint = landmark;
    bossPoint.iconId = gResources.icon(366) ? 366 : 80;
    BonfireWarpParam bonfire{};
    bonfire.eventflagId = 1;
    bonfire.iconId = 1;
    bonfire.areaNo = 60;
    bonfire.gridXNo = 28;
    bonfire.gridZNo = 64;
    bonfire.dispMask00 = 1;
    graces = paramRows<BonfireWarpParam>({{500000, bonfire}});
    view.fill(0);
    put(view.data(), 0x14, uint32_t{0x3C1C4000});
    put(view.data(), 0x28, 128.f);
    put(view.data(), 0x2C, 128.f);
    put(view.data(), 0x280, uint64_t{1});
    const uint8_t origin[]{0, 64, 28, 60};
    const float settings[]{0, 0, 0, 128, 128, 1};
    std::memcpy(view.data() + 0x100, origin, sizeof(origin));
    std::memcpy(view.data() + 0x104, settings, sizeof(settings));
    flags = {1};
    showOthers = showLandmarks = true;
    bool ok = true;
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        const bool bossFirst = !(scenario & 1), reversed = scenario & 2;
        // Swap the IDs without changing the artwork: the game's list order
        // can put either icon on top. Boss art has no special priority.
        points = bossFirst ? paramRows<WorldMapPointParam>({{100, bossPoint}, {27783, landmark}, {61433100, landmark}}, reversed)
                           : paramRows<WorldMapPointParam>({{100, landmark}, {27783, landmark}, {61433100, bossPoint}}, reversed);
        for (bool showGrace: {false, true}) {
            showGraces = showGrace;
            Renderer renderer;
            renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(+[](size_t size, void *) { return std::malloc(size); }),
                          reinterpret_cast<void *>(+[](void *data, void *) { std::free(data); }), nullptr);
            ++generation;
            gData.update();
            for (unsigned i = 0; i < 8; ++i) {
                ImGui::NewFrame();
                renderer.render();
                ImGui::Render();
                gResources.update();
            }
            auto *window = ImGui::FindWindowByName("##minimap_window");
            if (!window) {
                std::fputs("FAIL: overlapping marker window was not rendered\n", stderr);
                ok = false;
                break;
            }
            auto at = [&](const IconLayer &layer) {
                Point local = transform(layer.matrix, {0, 0});
                ImVec2 position = window->Pos + window->Size * .5f + ImVec2(local.x, local.y) * .375f;
                return drawIndex(layer, &position);
            };
            uint64_t mineIndex = at(mine->layers.front()), bossIndex = at(boss->layers.front()), graceIndex = at(grace->layers.front());
            if (mineIndex == UINT64_MAX || bossIndex == UINT64_MAX || ((mineIndex < bossIndex) != bossFirst) ||
                (showGrace && (graceIndex == UINT64_MAX || (warpAboveItems ? graceIndex <= std::max(mineIndex, bossIndex) : graceIndex >= std::min(mineIndex, bossIndex)))) ||
                (!showGrace && graceIndex != UINT64_MAX)) {
                std::fprintf(stderr, "FAIL: overlapping landmark/mod boss/grace draw order: scenario=%u mine=%llu boss=%llu grace=%llu enabled=%d\n", scenario,
                             static_cast<unsigned long long>(mineIndex), static_cast<unsigned long long>(bossIndex), static_cast<unsigned long long>(graceIndex), showGrace);
                ok = false;
                break;
            }
        }
    }
    graces = std::move(savedGraces);
    points = std::move(savedPoints);
    showGraces = showLandmarks = false;
    ++generation;
    if (ok)
        std::puts("PASS: native container and point order, including boss art below a landmark when the game's IDs require it");
    return ok;
}
bool goblinsDLL(const char *path) {
    const auto dllPath = std::filesystem::u8path(path);
    auto module = LoadLibraryW(dllPath.c_str());
    const auto setScenario = module ? reinterpret_cast<void (*)(uint32_t)>(GetProcAddress(module, "setTestScenario")) : nullptr;
    const auto calls = module ? reinterpret_cast<uint32_t (*)()>(GetProcAddress(module, "testCalls")) : nullptr;
    const auto get = module ? reinterpret_cast<MfgGetMapAPIFn>(GetProcAddress(module, "MfgGetMapAPI")) : nullptr;
    if (!setScenario || !calls || !get) {
        std::fputs("FAIL: provide the test provider, not the shipping mod DLL\n", stderr);
        if (module)
            FreeLibrary(module);
        return false;
    }
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = io.LogFilename = nullptr;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    showOthers = showLandmarks = showGraces = true;
    const auto *nativeSix = gResources.icon(6);
    auto containers = gResources.markerLayers();
    const bool warpAbove = std::find(containers.begin(), containers.end(), MapLayer::Warp) > std::find(containers.begin(), containers.end(), MapLayer::Item);
    bool ok = true;
    for (uint32_t layer = 0; layer < 3; ++layer) {
        const uint8_t area = layer == 2 ? 61 : 60;
        WorldMapPointParam mine{};
        mine.iconId = 25;
        mine.areaNo = area;
        mine.gridXNo = 28;
        mine.gridZNo = 64;
        mine.dispMask00 = mine.dispMask01 = mine.dispMask02 = 1;
        mine.posX = 141;
        mine.posZ = 129;
        BonfireWarpParam grace{};
        grace.eventflagId = 1;
        grace.iconId = 1;
        grace.areaNo = area;
        grace.gridXNo = 28;
        grace.gridZNo = 64;
        grace.dispMask00 = grace.dispMask01 = grace.dispMask02 = 1;
        grace.posX = 141;
        grace.posZ = 129;
        auto masked = mine;
        masked.dispMask00 = masked.dispMask01 = masked.dispMask02 = 0;
        masked.iconId = 60000; // Runtime remapped frame must never be used as a source ID.
        points = paramRows<WorldMapPointParam>({{20, masked}, {78501, mine}});
        graces = paramRows<BonfireWarpParam>({{500000, grace}});
        flags = {1};
        view.fill(0);
        put(view.data(), 0x14, (uint32_t(area) << 24) | 0x1C4000);
        put(view.data(), 0x24, layer == 2 ? 10u : 0u);
        put(view.data(), 0x30, static_cast<uint8_t>(layer == 1));
        put(view.data(), 0x28, 269.f);
        put(view.data(), 0x2C, -1.f);
        put(view.data(), 0x280, uint64_t{1});
        const uint8_t origin[]{0, 64, 28, area};
        const float settings[]{0, 0, 0, 128, 128, 1};
        std::memcpy(view.data() + 0x100, origin, sizeof(origin));
        std::memcpy(view.data() + 0x104, settings, sizeof(settings));
        for (uint32_t scenario: {0u, 1u, 2u, 3u, 4u}) {
            setScenario(scenario);
            ++generation;
            gData.setShowOthers(true);
            gData.update();
            auto snapshot = gData.snapshot();
            size_t runtimeCount = 0;
            for (const auto &marker: snapshot.decorations)
                if (marker.runtimeIcon) {
                    ++runtimeCount;
                    ok &= marker.source == DecorationSource::Other && marker.maps == (1u << layer) && marker.x == 269 && marker.y == -1;
                }
            const size_t expected = scenario == 1 || scenario == 2 ? 0 : scenario == 3 ? 603 : 3;
            ok &= snapshot.valid && runtimeCount == expected && snapshot.decorations.size() == expected + 2;
            if (scenario == 0 || scenario == 4) {
                Renderer renderer;
                renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(+[](size_t size, void *) { return std::malloc(size); }),
                              reinterpret_cast<void *>(+[](void *data, void *) { std::free(data); }), nullptr);
                for (unsigned i = 0; i < 8; ++i) {
                    ImGui::NewFrame();
                    renderer.render();
                    ImGui::Render();
                    gResources.update();
                }
                const auto *base = gResources.runtimeIcon(6), *badge = gResources.runtimeIcon(8), *ring = gResources.runtimeIcon(26);
                if (!base || !badge || !ring) {
                    ok = false;
                    break;
                }
                auto baseIndex = drawIndex(base->layers.front()), badgeIndex = drawIndex(badge->layers.front()), ringIndex = drawIndex(ring->layers.front());
                auto mineIndex = drawIndex(gResources.icon(25)->layers.front()), graceIndex = drawIndex(gResources.icon(1)->layers.front());
                ok &= baseIndex != UINT64_MAX && badgeIndex != UINT64_MAX && ringIndex != UINT64_MAX && mineIndex < std::min(baseIndex, badgeIndex) &&
                      std::max(baseIndex, badgeIndex) < ringIndex && (warpAbove ? ringIndex < graceIndex : graceIndex < mineIndex);
                // The provider can report badge-before-base; never override it to improve Boss art.
                ok &= scenario == 4 ? badgeIndex < baseIndex : true;
            }
        }
        const auto before = calls();
        gData.setShowOthers(false);
        ++generation;
        gData.update();
        auto disabled = gData.snapshot();
        ok &= calls() == before && std::none_of(disabled.decorations.begin(), disabled.decorations.end(), [](const auto &m) { return m.runtimeIcon; });
    }
    const auto *bridge = get(1);
    for (uint32_t index = 0; index < bridge->icon_count; ++index) {
        MfgMapIcon icon{};
        ok &= bridge->get_icon(index, &icon) == MFG_MAP_OK;
        const auto *recipe = gResources.runtimeIcon(icon.source_icon_id);
        ok &= recipe && recipe->layers.size() == 1 && recipe->bounds.left < 0 && recipe->bounds.top < 0 && recipe->bounds.right > 0 && recipe->bounds.bottom > 0;
    }
    ok &= gResources.icon(6) == nativeSix && gResources.runtimeIcon(6) != nativeSix;
    gResources.stop();
    FreeLibrary(module);
    ok &= GetModuleHandleW(L"MapForGoblins.dll") != nullptr;
    gGoblins.stop();
    ok &= GetModuleHandleW(L"MapForGoblins.dll") == nullptr && textures.empty();
    ImGui::DestroyContext();
    std::puts(ok ? "PASS: real DLL bridge through Data/Resources/ImGui; all embedded icons; native layers; source ID collisions; masks; buffer growth; provider "
                   "visibility/failure; optional switch; module/texture lifetime."
                 : "FAIL: DLL bridge replay");
    return ok;
}
bool otherMarkers() {
    struct Sample {
        uint32_t id;
        WorldMapPointParam row;
        size_t text;
    };
    er::util::ParamRows params;
    if (!params.open(reinterpret_cast<uintptr_t>(points.data() + 16)))
        return false;
    std::map<uint32_t, Sample> samples;
    auto field = [](const WorldMapPointParam &row, size_t offset) {
        uint32_t value;
        std::memcpy(&value, reinterpret_cast<const uint8_t *>(&row) + offset, sizeof(value));
        return value;
    };
    for (const auto &entry: params.rows()) {
        WorldMapPointParam row{};
        if (!params.read(entry, row) || !(row.dispMask00 || row.dispMask01 || row.dispMask02) ||
            !(row.iconId == 80 || row.iconId == 87 || row.iconId == 88 || (entry.id < 78500 && row.iconId >= 349 && row.iconId <= 376)))
            continue;
        size_t text = 0;
        while (text < 8 && static_cast<int32_t>(field(row, 48 + text * 12)) < 0)
            ++text;
        if (text == 8)
            continue;
        auto found = samples.find(row.iconId);
        if (found == samples.end() || (found->second.row.areaNo < 60 && row.areaNo >= 60))
            samples.insert_or_assign(row.iconId, Sample{entry.id, row, text});
    }
    if (!samples.contains(80) || !samples.contains(87) || !samples.contains(88)) {
        std::fputs("FAIL: fixture lacks NPC and quest markers\n", stderr);
        return false;
    }
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = io.LogFilename = nullptr;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    showGraces = showLandmarks = false;
    unsigned layers = 0, removals = 0, quests = 0;
    bool ok = true;
    auto setFlag = [](uint32_t flag) {
        if (flag && flag != UINT32_MAX)
            flags.insert(flag);
    };
    for (const auto &[baseIcon, sample]: samples) {
        const auto &row = sample.row;
        uint32_t raw = (uint32_t(row.areaNo) << 24) | (uint32_t(row.gridXNo) << 16) | (uint32_t(row.gridZNo) << 8);
        bool world = row.areaNo == 60 || row.areaNo == 61;
        uint8_t origin[]{0, world ? row.gridZNo : uint8_t{64}, world ? row.gridXNo : uint8_t{28}, world ? row.areaNo : uint8_t{60}};
        uint32_t map = row.dispMask00 ? 0 : row.dispMask01 ? 1 : 10;
        put(view.data(), 0x14, raw);
        put(view.data(), 0x24, map == 1 ? uint32_t{0} : map);
        put(view.data(), 0x30, static_cast<uint8_t>(map == 1));
        put(view.data(), 0x28, row.posX + 128.f);
        put(view.data(), 0x2C, 128.f - row.posZ);
        put(view.data(), 0x280, uint64_t{1});
        std::memcpy(view.data() + 0x100, origin, sizeof(origin));
        const float settings[]{0, 0, 0, 128, 128, 1};
        std::memcpy(view.data() + 0x104, settings, sizeof(settings));
        put(view.data(), 0xF8 + 40, reinterpret_cast<uintptr_t>(conversion.data()));
        put(conversion.data(), 16, reinterpret_cast<uintptr_t>(head.data()));
        put(head.data(), 8, reinterpret_cast<uintptr_t>(node.data()));
        head[25] = 1;
        put(node.data(), 0, reinterpret_cast<uintptr_t>(head.data()));
        put(node.data(), 16, reinterpret_cast<uintptr_t>(head.data()));
        put(node.data(), 28, raw);
        put(node.data(), 32, uint32_t{0x3C1C4000});
        flags.clear();
        setFlag(row.eventFlagId);
        setFlag(field(row, 52 + sample.text * 12));
        setFlag(field(row, 192 + sample.text * 4));
        uint32_t icon = reinterpret_cast<const uint8_t *>(&row)[144 + sample.text] == 1 && row.altIconId ? row.altIconId : row.iconId;
        const auto *recipe = gResources.icon(icon);
        if (!recipe || recipe->layers.empty()) {
            std::fprintf(stderr, "FAIL: actual icon %u has no resolved recipe\n", icon);
            ok = false;
            break;
        }
        if (sample.id >= 78500 && baseIcon != 80) {
            // High-ID quest pins (87/88) stay ordinary landmarks, independent
            // of the other switch; low-ID mod points use the other group.
            gData.setShowOthers(false);
            ++generation;
            gData.update();
            auto snapshot = gData.snapshot();
            if (std::none_of(snapshot.decorations.begin(), snapshot.decorations.end(),
                             [&](const auto &entry) { return entry.source == DecorationSource::Landmark && entry.id == sample.id && entry.iconId == icon; })) {
                std::fprintf(stderr, "FAIL: quest point %u / icon %u is not an ordinary landmark\n", sample.id, icon);
                ok = false;
                break;
            }
            ++quests;
            std::printf("PASS: quest point %u / icon %u stays an ordinary landmark\n", sample.id, icon);
            continue;
        }
        auto frame = [&](bool expected, bool update = true) {
            Renderer renderer;
            renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(+[](size_t size, void *) { return std::malloc(size); }),
                          reinterpret_cast<void *>(+[](void *data, void *) { std::free(data); }), nullptr);
            if (update) {
                ++generation;
                gData.update();
            }
            auto snapshot = gData.snapshot();
            auto marker = std::find_if(snapshot.decorations.begin(), snapshot.decorations.end(),
                                       [&](const auto &entry) { return entry.source == DecorationSource::Other && entry.id == sample.id; });
            if (update && ((marker != snapshot.decorations.end()) != expected || (expected && marker->iconId != icon))) {
                std::fprintf(stderr, "FAIL: other point %u / icon %u visibility=%d expected=%d\n", sample.id, icon, marker != snapshot.decorations.end(), expected);
                return false;
            }
            for (unsigned i = 0; i < 8; ++i) {
                ImGui::NewFrame();
                renderer.render();
                ImGui::Render();
                // Warm hidden recipes too, so absent drawing cannot be caused
                // by an unavailable texture. All layers use actual mod assets.
                for (const auto &layer: recipe->layers) {
                    AtlasRegion region{};
                    ERTextureView texture{};
                    (void)gResources.layerView(layer, region, texture);
                }
                gResources.update();
            }
            gResources.prepareTextures();
            ImGui::NewFrame();
            renderer.render();
            ImGui::Render();
            auto *window = ImGui::FindWindowByName("##minimap_window");
            const float angle = row.angle * 3.14159265358979323846f / 180.f;
            for (const auto &layer: recipe->layers) {
                if (!layer.bitmap())
                    continue;
                Point local = transform(layer.matrix, {0, 0});
                ImVec2 position =
                    window->Pos + window->Size * .5f + ImVec2(local.x * std::cos(angle) - local.y * std::sin(angle), local.x * std::sin(angle) + local.y * std::cos(angle)) * .375f;
                if (drawn(layer, &position) != expected) {
                    std::fprintf(stderr, "FAIL: point %u / icon %u layer %s drawing, expected=%d\n", sample.id, icon, layer.image.c_str(), expected);
                    return false;
                }
                layers += expected;
            }
            return true;
        };
        showOthers = false;
        ok = frame(false);
        showOthers = true;
        ok = ok && frame(true);
        showOthers = false;
        ok = ok && frame(false, false); // Also hide a previously published snapshot.
        showOthers = true;
        uint32_t category = field(row, 192 + sample.text * 4);
        if (!category || category == UINT32_MAX)
            category = field(row, 52 + sample.text * 12);
        if (category && category != UINT32_MAX) {
            flags.erase(category);
            ok = ok && frame(false);
            flags.insert(category);
        }
        bool removable = false;
        for (size_t text = 0; text < 8; ++text)
            for (auto flag: {field(row, 56 + text * 12), field(row, 224 + text * 4)})
                if (flag && flag != UINT32_MAX) {
                    setFlag(flag);
                    removable = true;
                }
        if (removable) {
            ok = ok && frame(false);
            ++removals;
        }
        if (!ok)
            break;
        std::printf("PASS: point %u / icon %u, other switch, independent drawing and native text conditions\n", sample.id, icon);
    }
    if (ok && samples.contains(349)) {
        const auto *mapItem = gResources.icon(349), *material = gResources.icon(376);
        if (!mapItem || !material || mapItem->layers.front().color.add[1] != 100 || material->layers.front().color.add[1] != 150 || coloredSprites.empty()) {
            std::fprintf(stderr, "FAIL: actual additive green backgrounds were not preserved (%zu converted images)\n", coloredSprites.size());
            ok = false;
        } else {
            CreateDirectoryW(L"build/native-checks/colored-sprites", nullptr);
            for (size_t index = 0; index < coloredSprites.size(); ++index) {
                std::ofstream stream("build/native-checks/colored-sprites/" + std::to_string(index) + ".dds", std::ios::binary);
                stream.write(reinterpret_cast<const char *>(coloredSprites[index].data()), coloredSprites[index].size());
            }
        }
    }
    if (ok)
        ok = overlappingMarkers();
    // Saved stamps use a separate list, map layer and one-based GFX timeline.
    std::array<uint8_t, 96> save{};
    std::array<uint8_t, 1600> slots{};
    for (size_t slot = 0; slot < 100; ++slot)
        put(slots.data(), slot * 16, int32_t{-1});
    put(view.data(), 0x24, uint32_t{0});
    put(view.data(), 0x30, uint8_t{0});
    put(view.data(), 0x378, reinterpret_cast<uintptr_t>(save.data()));
    put(save.data(), 8, reinterpret_cast<uintptr_t>(slots.data()));
    put(save.data(), 16, uint64_t{100});
    put(save.data(), 0x40, uint64_t{1});
    put(slots.data(), 0, int32_t{123});
    float playerX, playerY;
    std::memcpy(&playerX, view.data() + 0x28, 4);
    std::memcpy(&playerY, view.data() + 0x2C, 4);
    put(slots.data(), 4, playerX + 16);
    put(slots.data(), 8, playerY + 10);
    flags.clear();
    showOthers = true;
    for (uint8_t icon = 1; ok && icon <= 10; ++icon) {
        const auto *recipe = gResources.memoIcon(icon);
        if (!recipe || recipe->layers.empty()) {
            ok = false;
            break;
        }
        put(slots.data(), 13, icon);
        Renderer renderer;
        renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(+[](size_t size, void *) { return std::malloc(size); }),
                      reinterpret_cast<void *>(+[](void *data, void *) { std::free(data); }), nullptr);
        gData.update();
        if (gData.snapshot().memoMarkers.size() != 1 || gData.snapshot().memoMarkers[0].icon != icon) {
            ok = false;
            break;
        }
        for (unsigned i = 0; i < 8; ++i) {
            ImGui::NewFrame();
            renderer.render();
            ImGui::Render();
            gResources.update();
        }
        auto *window = ImGui::FindWindowByName("##minimap_window");
        for (const auto &layer: recipe->layers) {
            Point local = transform(layer.matrix, {0, 0});
            ImVec2 position = window->Pos + window->Size * .5f + ImVec2(12, 7.5f) + ImVec2(local.x, local.y) * .375f;
            if (!drawn(layer, &position)) {
                std::fprintf(stderr, "FAIL: stamp frame %u actual bitmap drawing\n", icon);
                ok = false;
            }
        }
    }
    if (ok) {
        // Removal is sampled every update, including while points are cached.
        put(slots.data(), 0, int32_t{-1});
        put(save.data(), 0x40, uint64_t{0});
        gData.update();
        ok = gData.snapshot().memoMarkers.empty();
    }
    if (ok)
        std::puts("PASS: all 10 native stamp frames, saved coordinates and removal");
    gResources.stop();
    ImGui::DestroyContext();
    std::printf("Other marker verification: %zu icon groups, %u drawn bitmap layers, %u removal cases, %u quest landmarks\n", samples.size() - quests, layers, removals, quests);
    return ok;
}
bool scenario(const char *name, std::initializer_list<uint32_t> enabled, uint32_t graceIcon, uint32_t bossIcon, bool graceCleared = false, bool bossCleared = false) {
    flags = enabled;
    ++generation;
    gData.update();
    auto snapshot = gData.snapshot();
    auto grace =
        std::find_if(snapshot.decorations.begin(), snapshot.decorations.end(), [](const auto &entry) { return entry.source == DecorationSource::Grace && entry.id == 210001; });
    auto boss =
        std::find_if(snapshot.decorations.begin(), snapshot.decorations.end(), [](const auto &entry) { return entry.source == DecorationSource::Landmark && entry.id == 210008; });
    if (!snapshot.valid || (grace != snapshot.decorations.end()) != (graceIcon != 0) || (boss != snapshot.decorations.end()) != (bossIcon != 0) ||
        (graceIcon && (grace->iconId != graceIcon || grace->cleared != graceCleared)) || (bossIcon && (boss->iconId != bossIcon || boss->cleared != bossCleared))) {
        std::fprintf(stderr, "FAIL: %s selected icons or completion state: grace=%u boss=%u\n", name, grace == snapshot.decorations.end() ? 0 : grace->iconId,
                     boss == snapshot.decorations.end() ? 0 : boss->iconId);
        return false;
    }
    Renderer renderer;
    renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(+[](size_t size, void *) { return std::malloc(size); }),
                  reinterpret_cast<void *>(+[](void *data, void *) { std::free(data); }), nullptr);
    for (unsigned frame = 0; frame < 8; ++frame) {
        ImGui::NewFrame();
        renderer.render();
        ImGui::Render();
        gResources.update();
    }
    // Warm the atlas even when this scenario hides markers; then compare the
    // actual draw list, not only whether CPU metadata contains the recipe.
    for (auto id: {499u, 500u})
        if (auto recipe = gResources.icon(id))
            for (const auto &layer: recipe->layers)
                if (layer.bitmap()) {
                    gResources.prepareTextures();
                    AtlasRegion region{};
                    ERTextureView texture{};
                    (void)gResources.layerView(layer, region, texture);
                    gResources.update();
                }
    for (const auto &layer: gResources.special("cleared")->layers) {
        if (!layer.bitmap())
            continue;
        gResources.prepareTextures();
        AtlasRegion region{};
        ERTextureView texture{};
        (void)gResources.layerView(layer, region, texture);
        gResources.update();
    }
    gResources.prepareTextures();
    ImGui::NewFrame();
    renderer.render();
    ImGui::Render();
    auto worldBoss = gResources.icon(499), alive = gResources.icon(500);
    const auto &dead = gResources.special("cleared")->layers.front();
    bool expectedBoss = showLandmarks && bossIcon;
    bool expectedAlive = (showLandmarks && bossIcon == 500) || (showGraces && (graceIcon == 501 || graceIcon == 502));
    bool expectedDead = (showGraces && graceIcon && graceCleared) || (showLandmarks && bossIcon && bossCleared);
    bool actualDead = false;
    if (vanillaCompletion) {
        // The vanilla overlay is a filled yellow dot at a nonzero local offset.
        // Verify triangle coverage at its center, not just boundary vertices.
        auto *window = ImGui::FindWindowByName("##minimap_window");
        Point local = transform(dead.matrix, {0, 0});
        auto center = window->Pos + window->Size * .5f + ImVec2(local.x, local.y) * .375f;
        auto color = dead.shape.fills.front().color;
        auto cross = [](ImVec2 a, ImVec2 b) { return a.x * b.y - a.y * b.x; };
        for (const auto *list: ImGui::GetDrawData()->CmdLists)
            for (const auto &command: list->CmdBuffer) {
                if (command.UserCallback || command.GetTexID() != ImTextureID{1})
                    continue;
                for (unsigned i = command.IdxOffset; i + 2 < command.IdxOffset + command.ElemCount; i += 3) {
                    const auto &a = list->VtxBuffer[list->IdxBuffer[i] + command.VtxOffset];
                    const auto &b = list->VtxBuffer[list->IdxBuffer[i + 1] + command.VtxOffset];
                    const auto &c = list->VtxBuffer[list->IdxBuffer[i + 2] + command.VtxOffset];
                    if (a.col != color || b.col != color || c.col != color)
                        continue;
                    float ab = cross(b.pos - a.pos, center - a.pos), bc = cross(c.pos - b.pos, center - b.pos), ca = cross(a.pos - c.pos, center - c.pos);
                    actualDead |= (ab >= 0 && bc >= 0 && ca >= 0) || (ab <= 0 && bc <= 0 && ca <= 0);
                }
            }
    } else
        actualDead = drawn(dead);
    bool actualBoss = worldBoss && drawn(worldBoss->layers.front()), actualAlive = alive && drawn(alive->layers.back());
    if (actualBoss != expectedBoss || actualAlive != expectedAlive || actualDead != expectedDead) {
        std::fprintf(stderr, "FAIL: %s draw boss=%d alive=%d dead=%d expected=%d/%d/%d\n", name, actualBoss, actualAlive, actualDead, expectedBoss, expectedAlive, expectedDead);
        return false;
    }
    std::printf("PASS: %s grace=%u boss=%u; base/alive/dead DDS layers enter ImGui draw list as expected.\n", name, graceIcon, bossIcon);
    return true;
}
} // namespace
int main(int argc, char **argv) {
    if ((argc != 6 && argc != 7 && argc != 8) || (argc == 7 && std::strcmp(argv[6], "--vanilla-completion") && std::strcmp(argv[6], "--other-markers")) ||
        (argc == 8 && std::strcmp(argv[6], "--goblins-dll") && (std::strcmp(argv[6], "--other-markers") || std::strcmp(argv[7], "--grace-below-items")))) {
        std::fputs("Usage: minimap_mod_marker_verify.exe worldmap.gfx layouts.bnd atlases.tpf BonfireWarpParam.param WorldMapPointParam.param "
                   "[--vanilla-completion|--other-markers [--grace-below-items]|--goblins-dll provider-fixture.dll]\n",
                   stderr);
        return 1;
    }
    vanillaCompletion = argc == 7 && !std::strcmp(argv[6], "--vanilla-completion");
    warpAboveItems = argc != 8 || !std::strcmp(argv[6], "--goblins-dll");
    showLandmarks = !vanillaCompletion;
    auto gfx = readFile(argv[1]), layouts = readFile(argv[2]);
    tpf = readFile(argv[3]);
    graces = paramFile(argv[4]);
    points = paramFile(argv[5]);
    std::vector<er::util::TpfEntry> entries;
    if (graces.empty() || points.empty() || !er::util::parseTpf(tpf, entries))
        return 2;
    for (const auto &entry: entries)
        images.emplace(entry.name, entry.dds);
    EROverlayAPI legacy{};
    EROverlayNativeAPI native{};
    configure(legacy, native);
    if (!startupDefinitions(gfx, layouts))
        return 9;
    if (!gResources.loadDefinitions(gfx, layouts))
        return 3;
    if (argc == 8 && !std::strcmp(argv[6], "--goblins-dll"))
        return goblinsDLL(argv[7]) ? 0 : 10;
    if (argc >= 7 && !std::strcmp(argv[6], "--other-markers"))
        return otherMarkers() ? 0 : 8;
    auto cleared = gResources.special("cleared");
    if (!cleared || cleared->layers.size() != 1 || (!vanillaCompletion && cleared->layers.front().image != "MENU_MAP_Boss_Dead") ||
        (vanillaCompletion && (cleared->layers.front().bitmap() || cleared->layers.front().shape.fills.empty()))) {
        std::fputs("FAIL: missing WorldMapItem/Cleared completion glyph from the actual mod GFX\n", stderr);
        return 4;
    }
    if (!vanillaCompletion)
        for (auto id: {499u, 500u, 501u, 502u, 600u, 748u, 901u})
            if (!gResources.icon(id))
                return 4;
    gResources.update();
    put(view.data(), 0x14, uint32_t{0x15000000});
    put(view.data(), 0x24, int32_t{10});
    put(view.data(), 0x28, 5373.9f);
    put(view.data(), 0x2C, 4501.46f);
    put(view.data(), 0x280, uint64_t{1});
    const uint8_t origin[]{0, 64, 28, 61};
    std::memcpy(view.data() + 0x100, origin, sizeof(origin));
    const float settings[]{0, 0, 0, 128, 128, 1};
    std::memcpy(view.data() + 0x104, settings, sizeof(settings));
    put(view.data(), 0xF8 + 40, reinterpret_cast<uintptr_t>(conversion.data()));
    put(conversion.data(), 16, reinterpret_cast<uintptr_t>(head.data()));
    put(head.data(), 8, reinterpret_cast<uintptr_t>(node.data()));
    head[25] = 1;
    put(node.data(), 0, reinterpret_cast<uintptr_t>(head.data()));
    put(node.data(), 16, reinterpret_cast<uintptr_t>(head.data()));
    put(node.data(), 28, uint32_t{0x15000000});
    put(node.data(), 32, uint32_t{0x3D302F00});
    const float delta[]{50.25f, 431.f, -153.f};
    std::memcpy(node.data() + 36, delta, sizeof(delta));
    // Golden Hippopotamus uses actual mod rows 210001/210008 at one position.
    // Populate the native grace choices the same way the game's ctor does.
    er::util::ParamRows params;
    BonfireWarpParam grace{};
    if (!params.open(reinterpret_cast<uintptr_t>(graces.data() + 16)))
        return 5;
    bool found = false;
    for (const auto &row: params.rows())
        if (row.id == 210001)
            found = params.read(row, grace);
    if (!found || grace.altIconId != 501 || grace.altForbiddenIconId != 502 || grace.clearedEventFlagId != 21000850)
        return 6;
    put(nativeGrace.data(), 0x238, uint32_t{210001});
    put(nativeGrace.data(), 0x248, uint32_t{grace.iconId});
    put(nativeGrace.data(), 0x288, uint32_t{grace.forbiddenIconId});
    put(nativeGrace.data(), 0x2C8, uint32_t{grace.altIconId});
    put(nativeGrace.data(), 0x308, uint32_t{grace.altForbiddenIconId});
    nativeGrace[0x348] = 1;
    put(view.data(), 0x2E8, reinterpret_cast<uintptr_t>(nativeGrace.data()));
    put(view.data(), 0x2F0, reinterpret_cast<uintptr_t>(nativeGrace.data()) + nativeGrace.size());
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = io.LogFilename = nullptr;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    bool ok;
    if (vanillaCompletion) {
        ok = scenario("vanilla completion dot fill and pivot", {21009850, 21009851, 21009852, 21000850, 72101}, 1, 0, true);
        showGraces = false;
        ok &= scenario("vanilla completion hidden with grace group", {21009850, 21009851, 21009852, 21000850, 72101}, 1, 0, true);
    } else
        ok = scenario("undiscovered boss", {}, 0, 0) && scenario("discovered living boss", {21009850}, 0, 500) &&
             scenario("defeated boss before grace activation", {21009850, 21009851, 21000850}, 0, 499) &&
             scenario("standalone completed boss", {21009850, 21009851, 21009852, 21000850}, 0, 499, false, true) &&
             scenario("boss grace activated", {21009850, 21009851, 21009852, 21000850, 72101}, 1, 0, true);
    if (ok && !vanillaCompletion) {
        shape = L"circle";
        rotate = L"true";
        ok = scenario("rotated transparent circle", {21009850, 21009851, 21009852, 21000850, 72101}, 1, 0, true);
        showLandmarks = false;
        ok &= scenario("completed grace with landmarks hidden", {21009850, 21009851, 21009852, 21000850, 72101}, 1, 0, true);
        showGraces = false;
        ok &= scenario("both marker groups hidden", {21009850, 21009851, 21009852, 21000850, 72101}, 1, 0, true);
        showLandmarks = true;
        ok &= scenario("completed landmark with graces hidden", {21009850, 21009851, 21009852, 21000850}, 0, 499, false, true);
        ok &= scenario("activated grace does not leave a duplicate landmark", {21009850, 21009851, 21009852, 21000850, 72101}, 1, 0, true);
        showGraces = true;
        nativeGrace[0x348] = 0;
        ok &= scenario("alternate forbidden boss grace", {21009850, 72101}, 502, 500);
    }
    gResources.stop();
    nativeApi = nullptr;
    ImGui::DestroyContext();
    return ok && textures.empty() ? 0 : 7;
}
