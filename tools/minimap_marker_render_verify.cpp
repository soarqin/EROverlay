// Replay read-only game captures through the production Data, Resources and
// Renderer, and inspect the actual atlas quads and digit glyphs in ImGui.
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
#include <limits>
#include <unordered_map>
#include <vector>

#include "minimap/render.hpp"
#include "util/mapstate.hpp"

EROverlayAPI *api = nullptr;
namespace {
struct Record {
    int32_t id;
    float x, y;
    uint8_t map, icon;
    uint16_t padding;
};
static_assert(sizeof(Record) == 16);
std::array<uint8_t, 1200> view{}, menu{}, field{}, cameraOwner{}, camera{};
std::array<uint8_t, 80> save{};
std::array<Record, 10> records{}, capturedRecords{};
std::array<uint8_t, 1200> capturedView{};
std::array<uint8_t, 80> capturedSave{};
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data()), fieldPointer = reinterpret_cast<uintptr_t>(field.data());
uint64_t generation = 1, nextTexture = 100;
unsigned reads = 0;
bool valid = true, enabled = true, offscreenUsed = false;
enum class Invalidation { None, Generation, View, RawMap, Layer };
Invalidation invalidation = Invalidation::None;
const wchar_t *shape = L"rect", *rotate = L"0", *alpha = L"1";
std::unordered_map<uint64_t, ERTextureView> textures;
std::array<std::vector<uint8_t>, 4> atlasBytes;

template<typename T, size_t N>
void put(std::array<uint8_t, N> &bytes, size_t offset, const T &value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
std::vector<uint8_t> file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return {};
    auto size = stream.tellg();
    if (size <= 0)
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()), size);
    return bytes;
}
void *allocate(size_t bytes, void *) { return std::malloc(bytes); }
void deallocate(void *pointer, void *) { std::free(pointer); }
void syncCount() {
    put(save, 0x40, uint64_t(std::count_if(records.begin(), records.end(), [](const auto &record) { return record.id >= 0; })));
}
void restoreCapture() {
    view = capturedView;
    save = capturedSave;
    records = capturedRecords;
    put(view, 0x338, reinterpret_cast<uintptr_t>(save.data()));
    put(save, 8, reinterpret_cast<uintptr_t>(records.data()));
    shape = L"rect";
    rotate = L"0";
    alpha = L"1";
    enabled = valid = true;
    invalidation = Invalidation::None;
    menu[0x1C] = 0;
}
bool closeTo(float a, float b, float tolerance = .03f) { return std::abs(a - b) <= tolerance; }

bool frame(const char *name, const std::vector<uint8_t> &numbers, bool expectOffscreen) {
    reads = 0;
    offscreenUsed = false;
    er::minimap::gData.update();
    auto snapshot = er::minimap::gData.snapshot();
    er::minimap::Renderer renderer;
    renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(allocate), reinterpret_cast<void *>(deallocate), nullptr);
    ImGui::NewFrame();
    std::array<ImFontGlyph, 5> glyphs;
    auto *baked = ImGui::GetFont()->GetFontBaked(18.f * .675f);
    for (size_t i = 0; i < glyphs.size(); ++i)
        glyphs[i] = *baked->FindGlyph(ImWchar('1' + i));
    bool cursor = renderer.render();
    ImGui::Render();
    const auto *recipe = er::minimap::gResources.special("marker");
    er::minimap::AtlasRegion region{};
    ERTextureView texture{};
    if (!recipe || recipe->layers.size() != 1 || !er::minimap::gResources.layerView(recipe->layers[0], region, texture))
        return false;
    float u = float(region.x) / texture.width, v = float(region.y) / texture.height;
    std::vector<ImVec2> arrows;
    std::array<unsigned, 5> digits{};
    bool upright = true, composite = false;
    for (auto *list: ImGui::GetDrawData()->CmdLists) {
        std::vector<bool> visited(list->VtxBuffer.Size);
        for (const auto &command: list->CmdBuffer) {
            if (command.UserCallback)
                continue;
            for (unsigned i = command.IdxOffset; i < command.IdxOffset + command.ElemCount; ++i) {
                size_t index = list->IdxBuffer[i] + command.VtxOffset;
                if (visited[index])
                    continue;
                visited[index] = true;
                const auto &vertex = list->VtxBuffer[index];
                if (command.GetTexID() == reinterpret_cast<ImTextureID>(texture.gpuHandle) && closeTo(vertex.uv.x, u, .00001f) && closeTo(vertex.uv.y, v, .00001f)) {
                    arrows.push_back(vertex.pos);
                    if (index + 3 >= size_t(list->VtxBuffer.Size))
                        return false;
                    upright &= closeTo(vertex.pos.y, list->VtxBuffer[index + 1].pos.y) && closeTo(vertex.pos.x, list->VtxBuffer[index + 3].pos.x);
                    upright &= list->VtxBuffer[index + 1].pos.x > vertex.pos.x && list->VtxBuffer[index + 3].pos.y > vertex.pos.y;
                }
                if (command.GetTexID() == ImTextureID{1} && vertex.col == IM_COL32(240, 240, 240, 255)) {
                    for (uint8_t number = 1; number <= 5; ++number) {
                        const auto &glyph = glyphs[number - 1];
                        if (!closeTo(vertex.uv.x, glyph.U0, .00001f) || !closeTo(vertex.uv.y, glyph.V0, .00001f))
                            continue;
                        ++digits[number - 1];
                        if (index + 3 >= size_t(list->VtxBuffer.Size))
                            return false;
                        upright &= closeTo(vertex.pos.y, list->VtxBuffer[index + 1].pos.y) && closeTo(vertex.pos.x, list->VtxBuffer[index + 3].pos.x);
                    }
                }
                if (command.GetTexID() == ImTextureID{88} && alpha == std::wstring_view(L"0.6"))
                    composite |= (vertex.col >> 24) == 153;
            }
        }
    }
    if (cursor || offscreenUsed != expectOffscreen || !upright || arrows.size() != numbers.size() || (alpha == std::wstring_view(L"0.6") && !composite)) {
        std::fprintf(stderr, "FAIL %s: arrows=%zu expected=%zu offscreen=%d upright=%d composite=%d\n", name, arrows.size(), numbers.size(), offscreenUsed, upright, composite);
        return false;
    }
    for (uint8_t number = 1; number <= 5; ++number) {
        unsigned expected = std::count(numbers.begin(), numbers.end(), number);
        if (digits[number - 1] != expected) {
            std::fprintf(stderr, "FAIL %s: digit %u glyphs=%u expected=%u\n", name, unsigned(number), digits[number - 1], expected);
            return false;
        }
    }
    if (!numbers.empty()) {
        auto *window = ImGui::FindWindowByName("##minimap_window");
        float cosine = rotate == std::wstring_view(L"1") ? 0.f : 1.f, sine = rotate == std::wstring_view(L"1") ? 1.f : 0.f;
        for (size_t i = 0; i < numbers.size(); ++i) {
            const auto &record = records[numbers[i] - 1];
            float x = (record.x - snapshot.state.x) * .75f, y = (record.y - snapshot.state.y) * .75f;
            // Known GFX pivot (-11.6,-91.35), with the same size as the death
            // marker: .75 map scale * 2 * .45 player size. Rotation affects
            // only the marker position, so the down arrow and digit stay upright.
            ImVec2 expected = window->Pos + ImVec2(window->Size.x * .5f + x * cosine - y * sine - 11.6f * .675f, window->Size.y * .5f + x * sine + y * cosine - 91.35f * .675f);
            if (!closeTo(arrows[i].x, expected.x) || !closeTo(arrows[i].y, expected.y)) {
                std::fprintf(stderr, "FAIL %s: marker %u position %.3f,%.3f expected %.3f,%.3f\n", name, unsigned(numbers[i]), arrows[i].x, arrows[i].y, expected.x, expected.y);
                return false;
            }
        }
    }
    std::printf("PASS %s: %zu arrow quads, matching digit glyphs and positions\n", name, numbers.size());
    return true;
}
} // namespace

int main(int argc, char **argv) {
    std::filesystem::path capture = argc > 1 ? argv[1] : "build/ida/player-marker-live";
    auto viewBytes = file(capture / "view.bin"), saveBytes = file(capture / "save.bin"), slotBytes = file(capture / "slots.bin");
    if (viewBytes.size() != view.size() || saveBytes.size() != save.size() || slotBytes.size() != sizeof(records)) {
        std::fprintf(stderr, "Capture missing: run python3 tools/minimap_marker_capture.py --pid <PID> --output %s\n", capture.string().c_str());
        return 1;
    }
    // Preserve captured position/death fields. Replace all game-owned pointer
    // chains with fixture buffers; the verifier does not access the game.
    std::copy_n(viewBytes.begin(), 0xB8, capturedView.begin());
    std::copy(saveBytes.begin(), saveBytes.end(), capturedSave.begin());
    std::memcpy(capturedRecords.data(), slotBytes.data(), sizeof(records));
    restoreCapture();
    put(field, 0x20, reinterpret_cast<uintptr_t>(cameraOwner.data()));
    put(cameraOwner, 0x18, reinterpret_cast<uintptr_t>(camera.data()));
    put(camera, 0x10, er::minimap::Camera{0, 0, 1, 0});

    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = io.LogFilename = nullptr;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(ImTextureID{1});
    EROverlayAPI legacy{};
    legacy.screenState = [] { return 0; };
    legacy.getGameAddresses = [] { return GameAddresses{reinterpret_cast<uintptr_t>(&menuPointer), 0, 0, reinterpret_cast<uintptr_t>(&fieldPointer), 0}; };
    legacy.configGetInt = [](const char *key, int fallback) { return !std::strcmp(key, "minimap.player_markers") ? int(enabled) : fallback; };
    legacy.configGetVirtualKey = [](const char *, int) { return 0; };
    legacy.configGetString = [](const char *key, const wchar_t *fallback) {
        if (!std::strcmp(key, "minimap.alpha"))
            return alpha;
        if (!std::strcmp(key, "minimap.shape"))
            return shape;
        if (!std::strcmp(key, "minimap.rotate"))
            return rotate;
        if (!std::strcmp(key, "minimap.scale"))
            return L"0.75";
        return fallback;
    };
    legacy.createOffscreen = [] { return reinterpret_cast<void *>(1); };
    legacy.destroyOffscreen = [](void *) {};
    legacy.endOffscreen = [](void *) { return reinterpret_cast<void *>(88); };
    legacy.inputIsKeyPressed = [](int) { return false; };
    api = &legacy;
    EROverlayNativeAPI native{};
    native.readMapState = [](ERMapState *out) {
        *out = {};
        out->generation = generation;
        if (!valid || !er::util::readWorldMapView(reinterpret_cast<uintptr_t>(view.data()), *out))
            return false;
        if (++reads >= 2) {
            if (invalidation == Invalidation::Generation)
                ++out->generation;
            if (invalidation == Invalidation::View)
                ++out->viewModel;
            if (invalidation == Invalidation::RawMap)
                ++out->rawMapId;
            if (invalidation == Invalidation::Layer)
                out->underground = 1;
        }
        return true;
    };
    native.findParamTable = [](uint32_t) -> uintptr_t { return 0; };
    native.readEventFlag = [](uint32_t) { return false; };
    native.createDdsTexture = [](const void *data, uint64_t bytes) -> uint64_t {
        er::util::DdsImage image;
        if (!er::util::parseDds({static_cast<const uint8_t *>(data), static_cast<size_t>(bytes)}, image))
            return 0;
        uint64_t id = ++nextTexture;
        textures[id] = {reinterpret_cast<void *>(uintptr_t(id)), image.width, image.height};
        return id;
    };
    native.pollTexture = [](uint64_t id, ERTextureView *out) {
        auto found = textures.find(id);
        if (found == textures.end())
            return ER_TEXTURE_INVALID;
        *out = found->second;
        return ER_TEXTURE_READY;
    };
    native.retireTexture = [](uint64_t id) { textures.erase(id); };
    native.beginOffscreen = [](void *) {
        offscreenUsed = true;
        return true;
    };
    er::minimap::nativeApi = &native;
    auto gfx = file("build/ida/sprite-probe/run-24636-34293203/worldmap.gfx");
    auto layouts = file("build/ida/probes/run-26836-32694765/common-layouts.bin");
    auto tpf = file("build/ida/probes/run-26836-32694765/common-atlas.tpf");
    if (!er::minimap::gResources.loadDefinitions(gfx, layouts))
        return 2;
    const auto *text = er::minimap::gResources.playerMarkerText();
    const auto *arrow = er::minimap::gResources.special("marker");
    if (!text || text->fontClass != "MenuFont_01" || text->align != 2 || text->color != IM_COL32(240, 240, 240, 255) || !closeTo(text->fontHeight, 18) ||
        !closeTo(text->matrix[4], -6.15f) || !closeTo(text->matrix[5], -7.05f) || !arrow || arrow->layers.size() != 1 || arrow->layers[0].image != "MENU_MAP_Marker" ||
        arrow->layers[0].width != 64 || arrow->layers[0].height != 324)
        return 3;
    const wchar_t *names[]{L"SB_MapCursor", L"SB_MapCursor_02", L"SB_MapCursor_03_dlc", L"SB_Chara"};
    for (size_t i = 0; i < 4; ++i) {
        er::util::Bytes dds;
        if (!er::util::findTpfDds(tpf, names[i], dds))
            return 4;
        atlasBytes[i].assign(dds.begin(), dds.end());
    }
    native.requestFile = [](const ERFileRequest *request) -> uint64_t { return request->tpfNameCount ? 100 : 0; };
    native.pollFile = [](uint64_t token, const wchar_t *part, ERFileData *out) {
        if (token != 100 || !part)
            return ER_FILE_PENDING;
        const wchar_t *names[]{L"SB_MapCursor", L"SB_MapCursor_02", L"SB_MapCursor_03_dlc", L"SB_Chara"};
        for (size_t i = 0; i < 4; ++i)
            if (!std::wcscmp(part, names[i])) {
                *out = {atlasBytes[i].data(), atlasBytes[i].size(), 1};
                return ER_FILE_SUCCEEDED;
            }
        return ER_FILE_INVALID;
    };
    native.releaseFile = [](uint64_t) {};
    er::minimap::gResources.update();
    er::minimap::gResources.prepareTextures();

    if (!frame("captured two beacons", {1, 2}, false))
        return 10;
    auto first = er::minimap::gData.snapshot();
    if (!first.valid || first.playerMarkers.size() != 2 || first.playerMarkers[0].id != records[0].id || first.playerMarkers[1].id != records[1].id ||
        first.playerMarkers[0].x != records[0].x || first.playerMarkers[1].y != records[1].y)
        return 11;
    shape = L"circle";
    rotate = L"1";
    if (!frame("rotated circle with upright beacons", {1, 2}, true))
        return 12;
    rotate = L"0";
    shape = L"rounded";
    alpha = L"0.6";
    if (!frame("transparent rounded composition", {1, 2}, true))
        return 13;
    restoreCapture();
    records[0].id = -1;
    syncCount();
    if (!frame("immediate removal preserves number 2", {2}, false))
        return 14;
    records[1].x += 25;
    records[1].y -= 18;
    if (!frame("position changes without cached beacons", {2}, false))
        return 15;
    records[0] = capturedRecords[0];
    records[4] = {25, records[0].x + 30, records[0].y - 12, 0, 1, 0};
    syncCount();
    if (!frame("non-contiguous slots 1 2 5", {1, 2, 5}, false))
        return 16;
    for (size_t i = 0; i < 5; ++i)
        records[i] = {int32_t(30 + i), capturedRecords[0].x + i * 20.f, capturedRecords[0].y - i * 12.f, 0, 1, 0};
    records[5] = {1000, records[0].x, records[0].y, 0, 1, 0};
    syncCount();
    if (!frame("five numbered slots and bounded reserved slots", {1, 2, 3, 4, 5}, false))
        return 17;
    records[5].id = -1;
    syncCount();
    view[0x30] = 1;
    for (auto &record: records)
        record.map = 1;
    if (!frame("underground beacons", {1, 2, 3, 4, 5}, false))
        return 18;
    records[0].map = 0;
    if (!frame("different map layer hidden", {2, 3, 4, 5}, false))
        return 19;
    put(view, 0x24, int32_t{10});
    view[0x30] = 0;
    for (auto &record: records)
        record.map = 10;
    if (!frame("DLC map category", {1, 2, 3, 4, 5}, false))
        return 20;
    restoreCapture();
    enabled = false;
    if (!frame("configuration hides player beacons", {}, false))
        return 21;
    restoreCapture();
    menu[0x1C] = 1;
    if (!frame("menu hides and clears the snapshot", {}, false) || !er::minimap::gData.snapshot().playerMarkers.empty())
        return 22;
    restoreCapture();
    for (auto &record: records)
        record.id = -1;
    syncCount();
    if (!frame("cleared save records", {}, false) || !er::minimap::gData.snapshot().playerMarkers.empty())
        return 23;
    restoreCapture();
    put(save, 0x10, uint64_t{11});
    if (!frame("invalid capacity clears old beacons", {}, false))
        return 24;
    restoreCapture();
    put(save, 8, uintptr_t{1});
    if (!frame("unreadable slots clear old beacons", {}, false))
        return 25;
    restoreCapture();
    put(save, 0x40, uint64_t{0});
    if (!frame("inconsistent active count discards sample", {}, false))
        return 26;
    restoreCapture();
    records[0].x = std::numeric_limits<float>::quiet_NaN();
    if (!frame("invalid coordinates are not rendered", {2}, false))
        return 27;
    restoreCapture();
    for (auto kind: {Invalidation::Generation, Invalidation::View, Invalidation::RawMap, Invalidation::Layer}) {
        invalidation = kind;
        if (!frame("context changed during update", {}, false) || er::minimap::gData.snapshot().valid || !er::minimap::gData.snapshot().playerMarkers.empty())
            return 28;
    }
    restoreCapture();
    valid = false;
    if (!frame("failed map read clears old beacons", {}, false))
        return 29;
    restoreCapture();
    if (!frame("valid snapshot recovers on next update", {1, 2}, false))
        return 30;
    er::minimap::gResources.stop();
    er::minimap::nativeApi = nullptr;
    ImGui::DestroyContext();
    std::puts("PASS: captured game beacons enter the production ImGui draw list with native arrow UVs and digits 1-5; update, layers, rotation, alpha and invalidation verified.");
}
