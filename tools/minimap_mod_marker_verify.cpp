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
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "minimap/defs/BonfireWarpParam.h"
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
uint64_t generation = 1, nextTexture = 0;
const wchar_t *shape = L"rect", *rotate = L"false";
bool showGraces = true, showLandmarks = true;
bool vanillaCompletion = false;

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
bool drawn(const IconLayer &layer) {
    AtlasRegion region{};
    ERTextureView texture{};
    if (!gResources.layerView(layer, region, texture))
        return false;
    for (const auto *list: ImGui::GetDrawData()->CmdLists)
        for (const auto &command: list->CmdBuffer) {
            if (command.UserCallback || command.GetTexID() != reinterpret_cast<ImTextureID>(texture.gpuHandle))
                continue;
            for (unsigned index = command.IdxOffset; index < command.IdxOffset + command.ElemCount; ++index) {
                const auto &vertex = list->VtxBuffer[list->IdxBuffer[index] + command.VtxOffset];
                if (std::abs(vertex.uv.x - float(region.x) / texture.width) < .00001f && std::abs(vertex.uv.y - float(region.y) / texture.height) < .00001f)
                    return true;
            }
        }
    return false;
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
    if ((argc != 6 && argc != 7) || (argc == 7 && std::strcmp(argv[6], "--vanilla-completion"))) {
        std::fputs("Usage: minimap_mod_marker_verify.exe worldmap.gfx layouts.bnd atlases.tpf BonfireWarpParam.param WorldMapPointParam.param [--vanilla-completion]\n", stderr);
        return 1;
    }
    vanillaCompletion = argc == 7;
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
    if (!gResources.loadDefinitions(gfx, layouts))
        return 3;
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
