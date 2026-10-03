// Twelve native-style atlases through production Resources/Data/Renderer.
// Local fixtures and mock I/O only; no game process or debugger is involved.
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
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "minimap/defs/WorldMapPointParam.h"
#include "minimap/render.hpp"
#include "params/param.hpp"
#include "util/mapstate.hpp"

EROverlayAPI *api = nullptr;
namespace {
std::array<uint8_t, 2048> view{}, menu{};
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data());
std::vector<uint8_t> pointTable;
std::map<std::wstring, std::vector<uint8_t>> images;
struct Request {
    std::wstring path;
    std::vector<std::wstring> names;
};
std::unordered_map<uint64_t, Request> files;
std::unordered_map<uint64_t, ERTextureView> textures;
std::unordered_map<uint64_t, std::wstring> textureNames;
std::vector<uint32_t> usedIcons;
std::vector<er::minimap::IconLayer> layers;
std::vector<std::wstring> requestedTiles;
uint64_t nextFile = 0, nextTexture = 100;
unsigned maxNames = 0, created = 0, retired = 0, polls = 0;
int failTexture = -1;
bool fullMap = false, earlierGame = false;
std::vector<uint8_t> readFile(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    auto size = file.tellg();
    std::vector<uint8_t> result(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(result.data()), size);
    return result;
}
template<typename T>
void put(uint8_t *data, size_t offset, const T &value) {
    std::memcpy(data + offset, &value, sizeof(value));
}
bool setImages(const char *path) {
    auto tpf = readFile(path);
    std::vector<er::util::TpfEntry> entries;
    if (!er::util::parseTpf(tpf, entries)) {
        std::fprintf(stderr, "FAIL fixture=%s bytes=%zu TPF parse\n", path, tpf.size());
        return false;
    }
    images.clear();
    for (const auto &entry: entries)
        images.emplace(entry.name, std::vector<uint8_t>(entry.dds.begin(), entry.dds.end()));
    return true;
}
void configure(EROverlayAPI &legacy, EROverlayNativeAPI &native) {
    legacy.screenState = [] { return 0; };
    legacy.getGameAddresses = [] { return GameAddresses{reinterpret_cast<uintptr_t>(&menuPointer), 0, 0, 0, 0}; };
    legacy.configGetInt = [](const char *, int fallback) { return fallback; };
    legacy.configGetVirtualKey = [](const char *, int) { return 0; };
    legacy.configGetString = [](const char *key, const wchar_t *fallback) {
        if (!std::strcmp(key, "minimap.presets.order"))
            return L"compact";
        if (!std::strcmp(key, "minimap.map.full_map"))
            return fullMap ? L"true" : L"false";
        if (!std::strcmp(key, "minimap.preset.compact.shape"))
            return L"rect";
        if (!std::strcmp(key, "minimap.preset.compact.rotate"))
            return L"0";
        if (!std::strcmp(key, "minimap.preset.compact.opacity"))
            return L"1";
        if (!std::strcmp(key, "minimap.preset.compact.zoom"))
            return L"0.75";
        return fallback;
    };
    legacy.createOffscreen = [] { return reinterpret_cast<void *>(1); };
    legacy.destroyOffscreen = [](void *) {};
    legacy.endOffscreen = [](void *) { return reinterpret_cast<void *>(99); };
    legacy.inputIsKeyPressed = [](int) { return false; };
    api = &legacy;
    native.size = sizeof(native);
    native.version = 1;
    native.readGameLayout = [](ERGameLayout *layout) {
        *layout = {sizeof(ERGameLayout), 0x80, 0x250, 0x720, 0x730, 0x350, 0x348, 1, earlierGame ? 3u : 7u, 194};
        return true;
    };
    native.readMapState = [](ERMapState *state) {
        *state = {};
        state->generation = 1;
        return er::util::readWorldMapView(reinterpret_cast<uintptr_t>(view.data()), *state);
    };
    native.findParamTable = [](uint32_t group) -> uintptr_t { return group == 87 && !pointTable.empty() ? reinterpret_cast<uintptr_t>(pointTable.data()) : 0; };
    native.readEventFlag = [](uint32_t id) { return id == 1; };
    native.requestFile = [](const ERFileRequest *request) -> uint64_t {
        if (!request->tpfNameCount) {
            if (std::wstring_view(request->path).starts_with(L"menutpfbnd:"))
                requestedTiles.emplace_back(request->path);
            return 0;
        }
        Request stored;
        stored.path = request->path;
        maxNames = std::max(maxNames, request->tpfNameCount);
        for (uint32_t i = 0; i < request->tpfNameCount; ++i)
            stored.names.emplace_back(request->tpfNames[i]);
        files.emplace(++nextFile, std::move(stored));
        return nextFile;
    };
    native.pollFile = [](uint64_t token, const wchar_t *part, ERFileData *out) {
        *out = {};
        auto request = files.find(token);
        if (request == files.end() || !part || std::find(request->second.names.begin(), request->second.names.end(), part) == request->second.names.end())
            return ER_FILE_INVALID;
        auto found = images.find(part);
        if (found == images.end())
            return ER_FILE_FAILED;
        *out = {found->second.data(), found->second.size(), 1};
        return ER_FILE_SUCCEEDED;
    };
    native.releaseFile = [](uint64_t token) { files.erase(token); };
    native.createDdsTexture = [](const void *data, uint64_t size) -> uint64_t {
        er::util::DdsImage parsed;
        er::util::Bytes bytes(static_cast<const uint8_t *>(data), static_cast<size_t>(size));
        if (!er::util::parseDds(bytes, parsed))
            return 0;
        std::wstring name;
        for (const auto &[key, dds]: images)
            if (dds.size() == size && std::equal(bytes.begin(), bytes.end(), dds.begin())) {
                name = key;
                break;
            }
        if (name.empty())
            return 0;
        uint64_t token = ++nextTexture;
        textures[token] = {reinterpret_cast<void *>(uintptr_t(token)), parsed.width, parsed.height};
        textureNames[token] = name;
        ++created;
        return token;
    };
    native.pollTexture = [](uint64_t token, ERTextureView *out) {
        ++polls;
        auto found = textures.find(token);
        if (found == textures.end())
            return ER_TEXTURE_INVALID;
        if (failTexture >= 0 && textureNames[token] == L"SB_ModMap_" + (failTexture < 10 ? std::wstring(L"0") : std::wstring()) + std::to_wstring(failTexture))
            return ER_TEXTURE_FAILED;
        *out = found->second;
        return ER_TEXTURE_READY;
    };
    native.retireTexture = [](uint64_t token) {
        if (textures.erase(token))
            ++retired;
        textureNames.erase(token);
    };
    native.beginOffscreen = [](void *) { return true; };
    native.queueDdsTexture = native.createDdsTexture;
    er::minimap::nativeApi = &native;
}
bool collectLayers(er::minimap::Resources &resources) {
    layers.clear();
    usedIcons.clear();
    // Visible recipes request their atlases. Drive update/render publication
    // instead of assuming every defined atlas is uploaded at startup.
    for (unsigned frame = 0; frame < 16; ++frame) {
        resources.prepareTextures();
        for (uint32_t id = 1; id <= 348; ++id)
            if (const auto *recipe = resources.icon(id))
                for (const auto &layer: recipe->layers) {
                    er::minimap::AtlasRegion region;
                    ERTextureView texture;
                    (void)resources.layerView(layer, region, texture);
                }
        resources.update();
    }
    resources.prepareTextures();
    std::array<bool, 12> used{};
    for (uint32_t id = 1; id <= 348; ++id) {
        if (id == 80) // The game/production point reader treats 80 as hidden.
            continue;
        const auto *recipe = resources.icon(id);
        if (!recipe)
            continue;
        for (const auto &layer: recipe->layers) {
            if (!layer.bitmap())
                continue;
            er::minimap::AtlasRegion region;
            ERTextureView texture;
            if (!resources.layerView(layer, region, texture) || region.atlas >= used.size())
                return false;
            used[region.atlas] = true;
            layers.push_back(layer);
        }
        usedIcons.push_back(id);
    }
    return std::all_of(used.begin(), used.end(), [](bool value) { return value; });
}
bool drawAllLayers() {
    using namespace er::minimap;
    pointTable.assign(4096 + usedIcons.size() * sizeof(WorldMapPointParam), 0);
    put(pointTable.data(), 0, static_cast<uint32_t>(pointTable.size()));
    put(pointTable.data(), 0xA, static_cast<uint16_t>(usedIcons.size()));
    pointTable[0x2D] = 4;
    pointTable[0x2E] = 2;
    for (size_t i = 0; i < usedIcons.size(); ++i) {
        er::params::ParamEntryOffset entry{90000 + i, static_cast<intptr_t>(4096 + i * sizeof(WorldMapPointParam)), 0};
        put(pointTable.data(), 0x40 + i * 24, entry);
        WorldMapPointParam row{};
        row.eventFlagId = 1;
        row.iconId = static_cast<uint16_t>(usedIcons[i]);
        row.areaNo = 60;
        row.gridXNo = 28;
        row.gridZNo = 64;
        row.dispMask00 = 1;
        row.posX = float(i % 10) * 4;
        row.posZ = float(i / 10) * 4;
        put(pointTable.data(), entry.offset, row);
    }
    put(view.data(), 0x14, uint32_t{0x3C1C4000});
    put(view.data(), 0x24, int32_t{0});
    put(view.data(), 0x28, 128.f);
    put(view.data(), 0x2C, 128.f);
    put(view.data(), 0x280, uint64_t{1});
    uint8_t origin[]{0, 64, 28, 60};
    std::memcpy(view.data() + 0x100, origin, 4);
    float settings[]{0, 0, 0, 128, 128, 1};
    std::memcpy(view.data() + 0x104, settings, sizeof(settings));
    gData.update();
    if (gData.snapshot().decorations.size() != usedIcons.size()) {
        std::fprintf(stderr, "FAIL decorations=%zu expected=%zu\n", gData.snapshot().decorations.size(), usedIcons.size());
        return false;
    }
    auto retained = gData.snapshot();
    auto *decorations = retained.decorations.data();
    for (unsigned sample = 0; sample < 1000; ++sample)
        if (gData.snapshot().decorations.data() != decorations)
            return false;
    gData.update();
    if (gData.snapshot().decorations.data() != decorations || retained.decorations.size() != usedIcons.size())
        return false;
    Renderer renderer;
    renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(+[](size_t size, void *) { return std::malloc(size); }),
                  reinterpret_cast<void *>(+[](void *value, void *) { std::free(value); }), nullptr);
    ImGui::NewFrame();
    renderer.render();
    ImGui::Render();
    std::array<bool, 12> drawn{};
    for (const auto &layer: layers) {
        AtlasRegion region;
        ERTextureView texture;
        if (!gResources.layerView(layer, region, texture))
            return false;
        bool found = false;
        for (const auto *list: ImGui::GetDrawData()->CmdLists)
            for (const auto &command: list->CmdBuffer) {
                if (command.UserCallback || command.GetTexID() != reinterpret_cast<ImTextureID>(texture.gpuHandle))
                    continue;
                for (unsigned i = command.IdxOffset; i < command.IdxOffset + command.ElemCount; ++i) {
                    const auto &vertex = list->VtxBuffer[list->IdxBuffer[i] + command.VtxOffset];
                    if (std::abs(vertex.uv.x - float(region.x) / texture.width) < .00001f && std::abs(vertex.uv.y - float(region.y) / texture.height) < .00001f)
                        found = true;
                }
            }
        if (!found) {
            std::fprintf(stderr, "FAIL draw image=%s atlas=%u\n", layer.image.c_str(), region.atlas);
            return false;
        }
        drawn[region.atlas] = true;
    }
    return std::all_of(drawn.begin(), drawn.end(), [](bool value) { return value; });
}
} // namespace

int main() {
    auto gfx = readFile("build/ida/sprite-probe/run-24636-34293203/worldmap.gfx");
    auto layouts = readFile("build/native-checks/mod-fixture/layouts.bin");
    if (!setImages("build/native-checks/mod-fixture/atlases.tpf"))
        return 1;
    EROverlayAPI legacy{};
    EROverlayNativeAPI native{};
    configure(legacy, native);
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = io.LogFilename = nullptr;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(ImTextureID{1});
    using namespace er::minimap;
    if (!gResources.loadDefinitions(gfx, layouts) || gResources.atlasCount() != 12 || gResources.iconCount() != 118)
        return 2;
    gResources.update();
    gResources.prepareTextures();
    if (maxNames != 12 || !files.empty() || !textures.empty())
        return 3;
    // Repeating one visible atlas uploads only that atlas and polls it once
    // in each frame, even if hundreds of icons refer to it.
    const auto *player = gResources.special("player");
    if (!player || player->layers.empty())
        return 18;
    for (unsigned frame = 0; frame < 3; ++frame) {
        gResources.prepareTextures();
        unsigned before = polls;
        for (unsigned icon = 0; icon < 500; ++icon) {
            AtlasRegion region;
            ERTextureView texture;
            (void)gResources.layerView(player->layers.front(), region, texture);
        }
        if (polls - before > 1)
            return 19;
        gResources.update();
    }
    if (textures.size() != 1 || !collectLayers(gResources) || textures.size() != 12)
        return 20;
    if (!drawAllLayers() || !textures.empty()) // Renderer destruction retires all.
        return 4;
    gResources.prepareTextures();
    if (!textures.empty() || !collectLayers(gResources) || textures.size() != 12)
        return 5;
    gResources.stop();
    if (!files.empty() || !textures.empty() || created != retired)
        return 6;
    // Missing and malformed DDS parts leave eleven other atlases usable.
    for (const char *file: {"build/native-checks/mod-fixture/partial-atlases.tpf", "build/native-checks/mod-fixture/malformed-atlases.tpf"}) {
        Resources partial;
        if (!setImages(file) || !partial.loadDefinitions(gfx, layouts))
            return 7;
        partial.update();
        partial.prepareTextures();
        for (unsigned frame = 0; frame < 16; ++frame) {
            partial.prepareTextures();
            for (const auto &layer: layers) {
                AtlasRegion region;
                ERTextureView texture;
                (void)partial.layerView(layer, region, texture);
            }
            partial.update();
        }
        if (textures.size() != 11 || !files.empty())
            return 8;
        partial.stop();
        if (!textures.empty())
            return 9;
    }
    // Asynchronous upload failure is isolated, then recreated on a reset.
    if (!setImages("build/native-checks/mod-fixture/atlases.tpf"))
        return 10;
    {
        Resources recovery;
        if (!recovery.loadDefinitions(gfx, layouts))
            return 11;
        recovery.update();
        if (!collectLayers(recovery))
            return 21;
        failTexture = 0;
        recovery.prepareTextures();
        bool rejected = false, sibling = false;
        for (const auto &layer: layers) {
            AtlasRegion region;
            ERTextureView texture;
            bool ready = recovery.layerView(layer, region, texture);
            rejected |= region.atlas == 0 && !ready;
            sibling |= region.atlas != 0 && ready;
        }
        if (!rejected || !sibling || textures.size() != 11)
            return 12;
        failTexture = -1;
        recovery.resetTextures();
        recovery.prepareTextures();
        if (!collectLayers(recovery) || textures.size() != 12)
            return 13;
        recovery.stop();
    }
    earlierGame = true;
    {
        Resources old;
        auto oldLayouts = readFile("build/native-checks/mod-fixture/pre-dlc-layouts.bin");
        auto oldMasks = readFile("build/native-checks/mod-fixture/pre-dlc-masks.bin");
        auto index = readFile("build/ida/probes/run-26836-32694765/map-index.bin");
        if (!old.loadDefinitions(gfx, oldLayouts) || old.atlasCount() != 3 || !old.special("death") || !old.special("marker") || !old.loadDirectory(index, oldMasks))
            return 14;
        uint32_t masks[3]{UINT32_MAX, UINT32_MAX, UINT32_MAX};
        TileView tile;
        old.beginFrame(1, 1, masks, 0);
        requestedTiles.clear();
        (void)old.tile(0, 20, 20, tile);
        (void)old.tile(1, 20, 20, tile);
        old.update();
        for (auto suffix: {L"M00_L0_20_20_00008400.tpf.dcx", L"M01_L0_20_20_00000008.tpf.dcx"})
            if (std::none_of(requestedTiles.begin(), requestedTiles.end(), [&](const auto &path) { return path.ends_with(suffix); }))
                return 17;
        old.beginFrame(1, 2, masks, 0);
        if (old.tile(2, 20, 20, tile))
            return 15;
    }
    gResources.stop();
    if (!files.empty() || !textures.empty() || created != retired)
        return 16;
    nativeApi = nullptr;
    ImGui::DestroyContext();
    std::puts("PASS: 12 referenced atlases in one request; 117 visible recipes draw correct texture/UVs; unused/missing/malformed atlases, GPU failure and reset/unload isolation; "
              "pre-DLC three-atlas resources without M10.");
    std::puts("PASS: no startup GPU atlas allocations; one visible atlas uses one texture and at most one status poll per frame for 500 references.");
}
