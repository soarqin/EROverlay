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
#include <initializer_list>
#include <string_view>
#include <unordered_map>
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
bool verifyPresets = false, separateKeys = false;
bool verifyMargins = false;
std::unordered_map<std::string, const wchar_t *> marginSettings;
int pressedKey = 0;
unsigned configReads = 0;
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

bool presetFrame(er::minimap::Renderer &renderer, int key, ImVec2 size, ImVec2 position, bool offscreen, ImVec2 viewportPosition = {0, 0}) {
    pressedKey = key;
    offscreenUsed = false;
    unsigned before = configReads;
    er::minimap::gData.update();
    ImGui::NewFrame();
    auto *viewport = ImGui::GetMainViewport();
    viewport->Pos = viewportPosition;
    viewport->WorkPos = viewportPosition;
    renderer.render();
    ImGui::Render();
    if (configReads != before || offscreenUsed != offscreen)
        return false;
    if (!size.x)
        return ImGui::GetDrawData()->TotalVtxCount == 0;
    auto *window = ImGui::FindWindowByName("##minimap_window");
    if (!window || window->Size.x != size.x || window->Size.y != size.y || window->Pos.x != position.x || window->Pos.y != position.y) {
        if (window)
            std::fprintf(stderr, "Window size %.0f,%.0f / position %.0f,%.0f; expected %.0f,%.0f / %.0f,%.0f\n", window->Size.x, window->Size.y, window->Pos.x, window->Pos.y,
                         size.x, size.y, position.x, position.y);
        return false;
    }
    if (offscreen) {
        bool compositeFound = false;
        for (const auto *list: ImGui::GetDrawData()->CmdLists)
            for (const auto &command: list->CmdBuffer) {
                if (command.UserCallback || command.GetTexID() != ImTextureID{88})
                    continue;
                for (unsigned i = command.IdxOffset; i < command.IdxOffset + command.ElemCount; ++i) {
                    const auto &vertex = list->VtxBuffer[list->IdxBuffer[i] + command.VtxOffset];
                    if (std::fabs(vertex.uv.x - (vertex.pos.x - viewportPosition.x) / viewport->Size.x) > .00001f ||
                        std::fabs(vertex.uv.y - (vertex.pos.y - viewportPosition.y) / viewport->Size.y) > .00001f)
                        return false;
                    compositeFound = true;
                }
            }
        if (!compositeFound)
            return false;
    }
    return true;
}

bool marginFrame(const char *label, std::initializer_list<std::pair<const char *, const wchar_t *>> fields, ImVec2 size, ImVec2 position, bool offscreen = false,
                 ImVec2 viewportPosition = {0, 0}) {
    verifyMargins = true;
    marginSettings = {{"height", L"20%"}, {"opacity", L"100%"}};
    for (const auto &[key, value]: fields)
        marginSettings[key] = value;
    er::minimap::Renderer renderer;
    renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(allocate), reinterpret_cast<void *>(deallocate), nullptr);
    bool result = presetFrame(renderer, 0, size, position, offscreen, viewportPosition);
    if (!result)
        std::fprintf(stderr, "FAIL: %s\n", label);
    return result;
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
    legacy.configGetVirtualKey = [](const char *key, int) {
        ++configReads;
        if (!verifyPresets)
            return 0;
        if (!std::strcmp(key, "minimap.controls.toggle"))
            return int('M');
        if (!std::strcmp(key, "minimap.controls.cycle"))
            return separateKeys ? 0x77 : int('M'); // VK_F8
        return 0;
    };
    legacy.configGetString = [](const char *key, const wchar_t *fallback) {
        ++configReads;
        if (verifyMargins) {
            if (!std::strcmp(key, "minimap.presets.order"))
                return L"compact";
            if (!std::strcmp(key, "minimap.controls.toggle") || !std::strcmp(key, "minimap.controls.cycle"))
                return L"";
            std::string_view field(key);
            constexpr std::string_view PREFIX = "minimap.preset.compact.";
            if (field.starts_with(PREFIX)) {
                auto found = marginSettings.find(std::string(field.substr(PREFIX.size())));
                if (found != marginSettings.end())
                    return found->second;
            }
            return fallback;
        }
        if (!std::strcmp(key, "minimap.presets.order"))
            return verifyPresets ? L"compact,rotating,large" : L"compact";
        if (verifyPresets) {
            if (!std::strcmp(key, "minimap.controls.toggle"))
                return L"M";
            if (!std::strcmp(key, "minimap.controls.cycle"))
                return separateKeys ? L"F8" : L"M";
            if (!std::strcmp(key, "minimap.preset.compact.height"))
                return L"20%";
            if (!std::strcmp(key, "minimap.preset.rotating.width") || !std::strcmp(key, "minimap.preset.large.width"))
                return L"60%";
            if (!std::strcmp(key, "minimap.preset.rotating.height") || !std::strcmp(key, "minimap.preset.large.height"))
                return L"40%";
            if (!std::strcmp(key, "minimap.preset.rotating.position") || !std::strcmp(key, "minimap.preset.large.position"))
                return L"center";
            if (!std::strcmp(key, "minimap.preset.rotating.rotate"))
                return L"true";
            if (!std::strcmp(key, "minimap.preset.rotating.opacity") || !std::strcmp(key, "minimap.preset.large.opacity"))
                return L"100%";
        }
        if (!std::strcmp(key, "minimap.preset.compact.opacity"))
            return L"1";
        if (!std::strcmp(key, "minimap.preset.compact.shape"))
            return shape;
        if (!std::strcmp(key, "minimap.preset.compact.rotate"))
            return rotate;
        return fallback;
    };
    legacy.createOffscreen = [] { return reinterpret_cast<void *>(1); };
    legacy.destroyOffscreen = [](void *) {};
    legacy.endOffscreen = [](void *) { return reinterpret_cast<void *>(88); };
    legacy.inputIsKeyPressed = [](int key) { return key == pressedKey; };
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
    verifyPresets = true;
    shape = L"rect";
    rotate = L"0";
    {
        er::minimap::Renderer renderer;
        renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(allocate), reinterpret_cast<void *>(deallocate), nullptr);
        if (!presetFrame(renderer, 0, {324, 216}, {1596, 0}, false) || !presetFrame(renderer, 'M', {432, 432}, {744, 324}, true) ||
            !presetFrame(renderer, 'M', {648, 432}, {636, 324}, false) || !presetFrame(renderer, 'M', {0, 0}, {0, 0}, false) ||
            !presetFrame(renderer, 'M', {324, 216}, {1596, 0}, false))
            return 10;
    }
    separateKeys = true;
    {
        er::minimap::Renderer renderer;
        renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(allocate), reinterpret_cast<void *>(deallocate), nullptr);
        if (!presetFrame(renderer, 'M', {0, 0}, {0, 0}, false) || !presetFrame(renderer, 0x77, {0, 0}, {0, 0}, false) ||
            !presetFrame(renderer, 'M', {324, 216}, {1596, 0}, false) || !presetFrame(renderer, 0x77, {432, 432}, {744, 324}, true) ||
            !presetFrame(renderer, 0x77, {648, 432}, {636, 324}, false) || !presetFrame(renderer, 0x77, {324, 216}, {1596, 0}, false))
            return 11;
    }
    if (!marginFrame("upper right", {{"margin_right", L"24px"}, {"margin_top", L"36"}}, {324, 216}, {1572, 36}) ||
        !marginFrame("upper left", {{"margin_left", L"24px"}, {"margin_top", L"36"}}, {324, 216}, {24, 36}) ||
        !marginFrame("lower right", {{"margin_right", L"24px"}, {"margin_bottom", L"36"}}, {324, 216}, {1572, 828}) ||
        !marginFrame("lower left", {{"margin_left", L"24px"}, {"margin_bottom", L"36"}}, {324, 216}, {24, 828}) ||
        !marginFrame("percent margins use full viewport", {{"margin_left", L"5%"}, {"margin_bottom", L"10%"}}, {324, 216}, {96, 756}) ||
        !marginFrame("mixed units", {{"margin_right", L"2%"}, {"margin_top", L"24px"}}, {324, 216}, {1557, 24}) ||
        !marginFrame("negative margins move outward", {{"margin_right", L"-24"}, {"margin_top", L"-36"}}, {324, 216}, {1620, -36}) ||
        !marginFrame("zero selects lower left", {{"margin_left", L"0"}, {"margin_bottom", L"0"}}, {324, 216}, {0, 864}) ||
        !marginFrame("blank margins are unset", {{"margin_left", L""}, {"margin_right", L"30"}, {"margin_top", L""}, {"margin_bottom", L"0"}}, {324, 216}, {1566, 864}) ||
        !marginFrame("default margins", {}, {324, 216}, {1596, 0}) || !marginFrame("default vertical anchor", {{"margin_left", L"24"}}, {324, 216}, {24, 0}) ||
        !marginFrame("default horizontal anchor", {{"margin_bottom", L"36"}}, {324, 216}, {1596, 828}) ||
        !marginFrame("center ignores margins", {{"position", L"center"}, {"margin_left", L"120"}, {"margin_bottom", L"40%"}}, {324, 216}, {798, 432}) ||
        !marginFrame("opposite margins do not stretch", {{"margin_left", L"0"}, {"margin_right", L"30"}, {"margin_top", L"0"}, {"margin_bottom", L"60"}}, {324, 216}, {0, 0}))
        return 12;
    if (!marginFrame("circle uses final diameter", {{"width", L"60%"}, {"height", L"40%"}, {"shape", L"circle"}, {"margin_right", L"24"}, {"margin_bottom", L"36"}}, {432, 432},
                     {1464, 612}, true) ||
        !marginFrame("rotated circle uses final diameter", {{"width", L"60%"}, {"height", L"40%"}, {"rotate", L"true"}, {"margin_right", L"24"}, {"margin_bottom", L"36"}},
                     {432, 432}, {1464, 612}, true) ||
        !marginFrame("rounded composite respects margins", {{"shape", L"rounded"}, {"margin_right", L"24"}, {"margin_bottom", L"36"}}, {324, 216}, {1572, 828}, true) ||
        !marginFrame("translucent rectangle respects margins", {{"opacity", L"50%"}, {"margin_left", L"24"}, {"margin_top", L"36"}}, {324, 216}, {24, 36}, true) ||
        !marginFrame("viewport origin", {{"margin_right", L"24"}, {"margin_bottom", L"36"}}, {324, 216}, {1672, 878}, false, {100, 50}) ||
        !marginFrame("viewport origin and composite sampling", {{"shape", L"circle"}, {"margin_right", L"24"}, {"margin_bottom", L"36"}}, {216, 216}, {1780, 878}, true, {100, 50}))
        return 13;
    marginSettings = {{"height", L"20%"}, {"opacity", L"100%"}, {"margin_right", L"2%"}, {"margin_bottom", L"10%"}};
    {
        er::minimap::Renderer renderer;
        renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(allocate), reinterpret_cast<void *>(deallocate), nullptr);
        if (!presetFrame(renderer, 0, {324, 216}, {1557, 756}, false))
            return 14;
        io.DisplaySize = {2560, 1440};
        if (!presetFrame(renderer, 0, {432, 288}, {2076, 1008}, false))
            return 15;
        io.DisplaySize = {3440, 1440};
        if (!presetFrame(renderer, 0, {432, 288}, {2939, 1008}, false))
            return 16;
        io.DisplaySize = {1080, 1080};
        if (!presetFrame(renderer, 0, {182, 121}, {876, 851}, false))
            return 17;
    }
    er::minimap::gResources.stop();
    er::minimap::nativeApi = nullptr;
    ImGui::DestroyContext();
    std::puts("PASS: real DropSoul ImGui quad from captured view bytes; rect/rotated-circle, surface/underground/DLC, cleared and different-map deaths.");
    std::puts("PASS: named preset cycling changes actual window sizes/positions; rotation returns to rect; shared-key hidden cycle and separate show/cycle keys; no per-frame "
              "config reads.");
    std::puts("PASS: actual ImGui placement and composite UVs with four corner margins, pixels/percentages/negative/zero/blank values, centered/circle/rounded shapes, viewport "
              "origin and live resizing.");
}
