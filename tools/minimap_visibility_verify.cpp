// Exercise production display-state reads and the real resource-status window
// before any map snapshot/resources exist. No game or external fixtures needed.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui.h>

#include "minimap/render.hpp"
#include "util/mapstate.hpp"

EROverlayAPI *api = nullptr;
namespace {
std::array<uint8_t, 4096> menu{};
std::array<uint8_t, 128> gameData{};
std::array<uint8_t, 1024> owner{};
std::array<uint8_t, 128> view{};
std::array<uint8_t, 2048> image{};
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data());
uintptr_t dataPointer = reinterpret_cast<uintptr_t>(gameData.data());
uintptr_t rootVtable = 0;
ERGameLayout layout{sizeof(ERGameLayout), 0x80, 0x250, 0x720, 0x730};
bool stateAvailable = true;
int legacyLoading = 0;
template<size_t N, typename T>
void put(std::array<uint8_t, N> &buffer, size_t offset, const T &value) {
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}
void *allocate(size_t size, void *) { return std::malloc(size); }
void deallocate(void *pointer, void *) { std::free(pointer); }

uintptr_t makeType(size_t slot, const char *name) {
    auto base = reinterpret_cast<uintptr_t>(image.data());
    uint32_t start = uint32_t(slot * 512);
    put(image, start + 0x20, std::array<uint32_t, 6>{1, 0, 0, start + 0x80, 0, start + 0x20});
    put(image, start + 0x100, base + start + 0x20);
    std::memcpy(image.data() + start + 0x90, name, std::strlen(name) + 1);
    return base + start + 0x108;
}

bool frame(er::minimap::Renderer &renderer, const char *condition, bool hidden) {
    // Deliberately do not call gData.update(): state changes must take effect
    // on this render, rather than depend on a 60-Hz snapshot catching up.
    ImGui::NewFrame();
    renderer.render();
    ImGui::Render();
    int vertices = ImGui::GetDrawData()->TotalVtxCount;
    bool ok = hidden ? vertices == 0 : vertices > 0;
    if (!ok)
        std::printf("FAIL: %s vertices=%d expected hidden=%d\n", condition, vertices, hidden);
    return ok;
}
} // namespace

int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(ImTextureID{1});
    EROverlayAPI legacy{};
    legacy.screenState = [] { return legacyLoading; };
    legacy.configGetString = [](const char *key, const wchar_t *fallback) {
        if (!std::strcmp(key, "minimap.presets.order"))
            return L"compact";
        if (!std::strcmp(key, "minimap.preset.compact.width") || !std::strcmp(key, "minimap.preset.compact.height"))
            return L"25%";
        return fallback;
    };
    legacy.configGetInt = [](const char *, int fallback) { return fallback; };
    legacy.configGetVirtualKey = [](const char *, int) { return 0; };
    legacy.inputIsKeyPressed = [](int) { return false; };
    legacy.createOffscreen = []() -> void * { return nullptr; };
    legacy.destroyOffscreen = [](void *) {};
    api = &legacy;
    EROverlayNativeAPI native{};
    native.size = sizeof(native);
    native.readDisplayState = [](ERDisplayState *state) {
        return stateAvailable && er::util::readDisplayState(reinterpret_cast<uintptr_t>(&menuPointer), reinterpret_cast<uintptr_t>(&dataPointer), layout, *state);
    };
    er::minimap::nativeApi = &native;
    // No snapshot is valid and resource loading has not even started.
    if (er::minimap::gData.snapshot().valid || !*er::minimap::gResources.status())
        return 1;
    bool ok = true;
    {
        er::minimap::Renderer renderer;
        renderer.init(ImGui::GetCurrentContext(), reinterpret_cast<void *>(allocate), reinterpret_cast<void *>(deallocate), nullptr);
        ok &= frame(renderer, "boot without character", true);
        put(gameData, 0x58, reinterpret_cast<uintptr_t>(gameData.data() + 0x60));
        // Reproduce the reported black-screen boot: pointers and memory are
        // readable, but zero loading flags do not mean the UI has initialized.
        ok &= frame(renderer, "allocated player and zero-filled menu", true);
        const auto menuType = makeType(0, ".?AVCSMenuManImp@CS@@");
        const auto loadingType = makeType(1, ".?AVLoadingScreenData@CS@@");
        put(menu, 0, menuType);
        ok &= frame(renderer, "LoadingScreenData constructor not reached", true);
        put(menu, layout.menuInfoOffset, loadingType);
        ok &= frame(renderer, "menu owner not published", true);
        put(menu, layout.menuOwnerOffset, reinterpret_cast<uintptr_t>(owner.data()));
        ok &= frame(renderer, "menu view not published", true);
        put(owner, layout.ownerViewOffset, reinterpret_cast<uintptr_t>(view.data()));
        ok &= frame(renderer, "gameplay waiting for resources", false);
        // Ordinary menu mask/control flags do not hide the minimap.
        put(menu, 0x1c, uint32_t{0xfffffffe});
        for (unsigned id: {28, 29, 31, 33, 36, 37, 38, 44, 46, 48, 49, 50, 60, 64, 65})
            menu[0x90 + id] = 1;
        ok &= frame(renderer, "ordinary menus", false);
        menu[0x90 + 61] = 1;
        ok &= frame(renderer, "full-screen map", true);
        menu[0x90 + 61] = 2;
        ok &= frame(renderer, "inactive map secondary flag", false);
        menu[0x90 + 61] = 0;
        put(menu, 0x730, uint16_t{1});
        ok &= frame(renderer, "LoadingScreenData", true);
        put(menu, 0x730, uint16_t{0});
        for (unsigned id: {6, 21}) {
            menu[0x90 + id] = 1;
            ok &= frame(renderer, id == 6 ? "LoadingScreen" : "NowLoading", true);
            menu[0x90 + id] = 2;
            ok &= frame(renderer, "inactive loading secondary flag", false);
            menu[0x90 + id] = 0;
        }
        // A real MSVC RTTI chain with a relocated base, not a hard-coded VA.
        rootVtable = makeType(2, ".?AVLogoMenu@@");
        put(menu, 0x30, reinterpret_cast<uintptr_t>(&rootVtable));
        put(menu, 0x78, uint64_t{1});
        ok &= frame(renderer, "LogoMenu with character already allocated", true);
        rootVtable = 0;
        ok &= frame(renderer, "root menu constructor in progress", true);
        rootVtable = makeType(3, ".?AVTitleMenu@@");
        ok &= frame(renderer, "non-logo root menu", false);
        put(image, 3 * 512 + 0x24, uint32_t{16});
        ok &= frame(renderer, "non-logo secondary-base root menu", false);
        rootVtable = makeType(2, ".?AVLogoMenu@@");
        put(image, 2 * 512 + 0x24, uint32_t{16});
        ok &= frame(renderer, "LogoMenu secondary-base root", true);
        put(menu, 0x78, uint64_t{0});
        put(menu, layout.menuInfoOffset, uintptr_t{0});
        ok &= frame(renderer, "loading data invalidated after gameplay", true);
        put(menu, layout.menuInfoOffset, loadingType);
        ok &= frame(renderer, "display state reinitialized", false);
        stateAvailable = false;
        ok &= frame(renderer, "unavailable display-state read", true);
        stateAvailable = true;
        menuPointer = 0;
        ok &= frame(renderer, "menu initialization", true);
        menuPointer = reinterpret_cast<uintptr_t>(menu.data());
        native.size = offsetof(EROverlayNativeAPI, readDisplayState);
        ok &= frame(renderer, "older core size-gated API", false);
        legacyLoading = 1;
        ok &= frame(renderer, "older core loading", true);
        native.size = sizeof(native);
        legacyLoading = 0;
        // The startup guard must follow the supplied historical layout,
        // including MenuInfo and root-count positions, without fixed RVAs.
        for (auto historical: {ERGameLayout{sizeof(ERGameLayout), 0x78, 0x248, 0x708, 0x718}, ERGameLayout{sizeof(ERGameLayout), 0x80, 0x248, 0x718, 0x728},
                               ERGameLayout{sizeof(ERGameLayout), 0x80, 0x250, 0x720, 0x730}}) {
            layout = historical;
            menu.fill(0);
            owner.fill(0);
            ok &= frame(renderer, "historical zero-filled UI with allocated player", true);
            put(menu, 0, menuType);
            put(menu, layout.menuInfoOffset, loadingType);
            put(menu, layout.menuOwnerOffset, reinterpret_cast<uintptr_t>(owner.data()));
            ok &= frame(renderer, "historical partially published UI", true);
            put(owner, layout.ownerViewOffset, reinterpret_cast<uintptr_t>(view.data()));
            ok &= frame(renderer, "historical initialized UI waiting for resources", false);
            rootVtable = makeType(2, ".?AVLogoMenu@@");
            put(menu, 0x30, reinterpret_cast<uintptr_t>(&rootVtable));
            put(menu, layout.menuOwnerOffset - sizeof(uintptr_t), uint64_t{1});
            ok &= frame(renderer, "historical initialized LogoMenu", true);
            put(menu, layout.menuOwnerOffset - sizeof(uintptr_t), uint64_t{0});
            menu[layout.menuOwnerOffset + 0x10 + 61] = 1;
            ok &= frame(renderer, "historical full-screen map", true);
        }
    }
    er::minimap::nativeApi = nullptr;
    ImGui::DestroyContext();
    if (ok)
        std::puts("PASS: startup zero/partial/reset UI state produces no resource text or border; initialized waiting state and ordinary menus stay visible; logo/loading/map, "
                  "relocated RTTI, secondary roots, three historical layouts and optional API size gate work.");
    return ok ? 0 : 2;
}
