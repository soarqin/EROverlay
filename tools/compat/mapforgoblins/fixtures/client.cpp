#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

#include "mfg_map_api.h"

int wmain(int argc, wchar_t **argv) {
    assert(argc == 2);
    HMODULE module = LoadLibraryW(argv[1]);
    assert(module);
    const auto get = reinterpret_cast<MfgGetMapAPIFn>(GetProcAddress(module, "MfgGetMapAPI"));
    const auto scenario = reinterpret_cast<void (*)(uint32_t)>(GetProcAddress(module, "setTestScenario"));
    assert(get && scenario && !get(0) && !get(MFG_MAP_ABI + 1));
    static_assert(sizeof(MfgMapAPI) == 40 && sizeof(MfgMapPoint) == 32 && sizeof(MfgMapIcon) == 32);
    const auto *api = get(MFG_MAP_ABI);
    assert(api && api->size == sizeof(*api) && api->abi_version == MFG_MAP_ABI && api->point_size == sizeof(MfgMapPoint) && api->icon_size == sizeof(MfgMapIcon));
    const MfgMapPoint *points = nullptr;
    uint32_t count = 0;
    assert(api->view_points(0, &points, &count) == MFG_MAP_OK && !points && !count);
    scenario(0);
    for (uint32_t layer = 0; layer < 3; ++layer) {
        assert(api->view_points(layer, &points, &count) == MFG_MAP_OK && count == 3);
        assert(points[0].id == 20 && points[0].raw_map_id == (layer == 2 ? 0x3D1C4000 : 0x3C1C4000) && points[0].icon_id == 6 && points[0].x == 141 && points[0].z == 129);
        assert(points[1].id == (20 | (uint64_t{1} << 63)) && points[1].icon_id == 8);
        assert(points[2].id == (uint64_t{1} << 62) && points[2].icon_id == 26);
        assert(points[0].draw_order < points[1].draw_order && points[1].draw_order < points[2].draw_order);
    }
    for (uint32_t i = 0; i < api->icon_count; ++i) {
        MfgMapIcon icon{};
        assert(api->get_icon(i, &icon) == MFG_MAP_OK && icon.lossless2 && icon.lossless2_size > 8 && icon.matrix && icon.matrix_size);
    }
    MfgMapIcon icon{};
    assert(api->get_icon(api->icon_count, &icon) == MFG_MAP_INVALID && !icon.lossless2);
    const MfgMapPoint *view = points;
    assert(api->view_points(3, &view, &count) == MFG_MAP_INVALID && !view && !count);
    assert(api->view_points(0, nullptr, &count) == MFG_MAP_INVALID && !count);
    assert(api->view_points(0, &view, nullptr) == MFG_MAP_INVALID && !view);
    for (uint32_t value: {1u, 2u, 5u}) {
        scenario(value);
        assert(api->view_points(0, &view, &count) == (value == 1 ? MFG_MAP_OK : MFG_MAP_FAILED) && !view && !count);
    }
    scenario(0);
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 3);
    const MfgMapPoint saved[] = {view[0], view[1], view[2]};
    // Another caller can grow/change its view and exit without touching ours.
    std::thread other([&] {
        scenario(3);
        const MfgMapPoint *otherView = nullptr;
        uint32_t otherCount = 0;
        assert(api->view_points(2, &otherView, &otherCount) == MFG_MAP_OK && otherCount == 603 && otherView != view);
        assert(otherView[0].raw_map_id == 0x3D1C4000);
    });
    other.join();
    assert(!std::memcmp(view, saved, sizeof(saved)));
    scenario(3);
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 603);
    const auto *storage = view;
    scenario(0);
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 3 && view == storage);
    scenario(4);
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 3 && view[0].icon_id == 8 && view[1].icon_id == 6);
    scenario(0);
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && view[0].icon_id == 8 && view[0].draw_order < view[1].draw_order);
    FreeLibrary(module);
    std::puts("PASS: read-only views across CRTs; ABI validation; thread-isolated lifetime; storage reuse; limits/failures; visibility; native child depths; icons.");
}
