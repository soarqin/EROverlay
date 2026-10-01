// Run the real view reader, update snapshot and ImGui renderer without a game
// or D3D device. Inspect the actual DropSoul quad, not a copied predicate.
#define NOMINMAX
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <vector>

#include "minimap/render.hpp"
#include "util/mapstate.hpp"

EROverlayAPI *api = nullptr;
namespace {
std::array<uint8_t, 1200> view{}, menu{};
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data());
ERMapState state{};
const wchar_t *shape = L"rect", *rotate = L"0";
bool offscreenUsed = false;
template<typename T>
void put(size_t offset, const T &value) {
    std::memcpy(view.data() + offset, &value, sizeof(value));
}
std::vector<uint8_t> file(const char *path) {
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

bool frame(bool expectDeath, bool expectOffscreen) {
    offscreenUsed = false;
    er::minimap::gData.update();
    er::minimap::Renderer renderer;
    renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(allocate), reinterpret_cast<void *>(deallocate), nullptr);
    ImGui::NewFrame();
    bool cursor = renderer.render();
    ImGui::Render();
    if (cursor || offscreenUsed != expectOffscreen)
        return false;
    auto *data = ImGui::GetDrawData();
    bool found = false;
    // DropSoul region: atlas SB_MapCursor_02, x=800 y=1921 w=86 h=92.
    // Its quad starts at this exact UV in the production renderer.
    float u = 800.f / 2048, v = 1921.f / 2048;
    for (auto *list: data->CmdLists)
        for (const auto &command: list->CmdBuffer) {
            if (command.UserCallback || command.GetTexID() != ImTextureID{77})
                continue;
            for (unsigned i = command.IdxOffset; i < command.IdxOffset + command.ElemCount; ++i) {
                auto index = list->IdxBuffer[i] + command.VtxOffset;
                const auto &vertex = list->VtxBuffer[index];
                if (std::abs(vertex.uv.x - u) < .00001f && std::abs(vertex.uv.y - v) < .00001f)
                    found = true;
            }
        }
    return found == expectDeath;
}
} // namespace

int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(ImTextureID{1});

    EROverlayAPI legacy{};
    legacy.screenState = [] { return 0; };
    legacy.getGameAddresses = [] { return GameAddresses{reinterpret_cast<uintptr_t>(&menuPointer), 0, 0, 0, 0}; };
    legacy.configGetInt = [](const char *, int fallback) { return fallback; };
    legacy.configGetVirtualKey = [](const char *, int) { return 0; };
    legacy.configGetString = [](const char *key, const wchar_t *fallback) {
        if (!std::strcmp(key, "minimap.alpha"))
            return L"1";
        if (!std::strcmp(key, "minimap.shape"))
            return shape;
        if (!std::strcmp(key, "minimap.rotate"))
            return rotate;
        return fallback;
    };
    legacy.createOffscreen = [] { return reinterpret_cast<void *>(1); };
    legacy.destroyOffscreen = [](void *) {};
    legacy.endOffscreen = [](void *) { return reinterpret_cast<void *>(88); };
    legacy.inputIsKeyPressed = [](int) { return false; };
    api = &legacy;
    EROverlayNativeAPI native{};
    native.readMapState = [](ERMapState *out) {
        *out = state;
        return er::util::readWorldMapView(reinterpret_cast<uintptr_t>(view.data()), *out);
    };
    native.findParamTable = [](uint32_t) -> uintptr_t { return 0; };
    native.readEventFlag = [](uint32_t) { return false; };
    native.pollTexture = [](uint64_t, ERTextureView *out) {
        *out = {reinterpret_cast<void *>(77), 2048, 2048};
        return ER_TEXTURE_READY;
    };
    native.createDdsTexture = [](const void *, uint64_t) -> uint64_t { return 1; };
    native.retireTexture = [](uint64_t) {};
    native.beginOffscreen = [](void *) {
        offscreenUsed = true;
        return true;
    };
    er::minimap::nativeApi = &native;
    auto gfx = file("build/ida/sprite-probe/run-24636-34293203/worldmap.gfx");
    auto layouts = file("build/ida/probes/run-26836-32694765/common-layouts.bin");
    if (!er::minimap::gResources.loadDefinitions(gfx, layouts))
        return 1;

    state.generation = 1;
    put(0x14, uint32_t{0x0A010000});
    put(0x24, int32_t{0});
    put(0x28, 2807.2265625f);
    put(0x2C, 6776.87939453125f);
    put(0x30, uint32_t{0x32A66000});
    put(0x34, 248.1034698486328f);
    put(0xA9, uint8_t{1});
    put(0xAC, 2802.62890625f);
    put(0xB0, 6778.857421875f);
    put(0xB4, int32_t{0});
    // Feed four local DDS to the resources through the regular file interface.
    auto tpf = file("build/ida/probes/run-26836-32694765/common-atlas.tpf");
    std::vector<er::util::TpfEntry> entries;
    if (!er::util::parseTpf(tpf, entries))
        return 2;
    static std::array<std::vector<uint8_t>, 4> atlasBytes;
    const wchar_t *names[] = {L"SB_MapCursor", L"SB_MapCursor_02", L"SB_MapCursor_03_dlc", L"SB_Chara"};
    for (size_t i = 0; i < 4; ++i) {
        er::util::Bytes dds;
        if (!er::util::findTpfDds(tpf, names[i], dds))
            return 3;
        atlasBytes[i].assign(dds.begin(), dds.end());
    }
    native.requestFile = [](const ERFileRequest *request) -> uint64_t { return request->tpfNameCount ? 100 : 0; };
    native.pollFile = [](uint64_t token, const wchar_t *part, ERFileData *data) {
        if (token != 100 || !part)
            return ER_FILE_PENDING;
        const wchar_t *names[] = {L"SB_MapCursor", L"SB_MapCursor_02", L"SB_MapCursor_03_dlc", L"SB_Chara"};
        for (size_t i = 0; i < 4; ++i)
            if (!std::wcscmp(part, names[i])) {
                *data = {atlasBytes[i].data(), atlasBytes[i].size(), 1};
                return ER_FILE_SUCCEEDED;
            }
        return ER_FILE_INVALID;
    };
    native.releaseFile = [](uint64_t) {};
    er::minimap::gResources.update();
    er::minimap::gResources.prepareTextures();
    if (!frame(true, false))
        return 4;
    shape = L"circle";
    rotate = L"1";
    if (!frame(true, true))
        return 5;
    shape = L"rect";
    rotate = L"0";
    view[0x30] = 1;
    put(0xB4, int32_t{1});
    if (!frame(true, false))
        return 6;
    put(0x24, int32_t{10});
    view[0x30] = 0;
    put(0xB4, int32_t{10});
    if (!frame(true, false))
        return 7;
    view[0xA9] = 0;
    if (!frame(false, false))
        return 8;
    view[0xA9] = 1;
    put(0xB4, int32_t{0});
    if (!frame(false, false))
        return 9;
    er::minimap::gResources.stop();
    er::minimap::nativeApi = nullptr;
    ImGui::DestroyContext();
    std::puts("PASS: real DropSoul ImGui quad from captured view bytes; rect/rotated-circle, surface/underground/DLC, cleared and different-map deaths.");
}
