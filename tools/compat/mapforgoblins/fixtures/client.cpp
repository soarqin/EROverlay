#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <cstdio>

#include "mfg_map_api.h"

int wmain(int argc, wchar_t **argv) {
    assert(argc == 2);
    HMODULE module = LoadLibraryW(argv[1]);
    assert(module);
    const auto get = reinterpret_cast<MfgGetMapAPIFn>(GetProcAddress(module, "MfgGetMapAPI"));
    const auto scenario = reinterpret_cast<void (*)(uint32_t)>(GetProcAddress(module, "setTestScenario"));
    assert(get && scenario && !get(0) && !get(2));
    const auto *api = get(1);
    assert(api && api->size == sizeof(*api) && api->abi_version == 1 && api->point_size == sizeof(MfgMapPoint) && api->icon_size == sizeof(MfgMapIcon));
    MfgMapPoint points[8]{};
    uint32_t count = 0;
    points[0].id = 123;
    assert(api->copy_points(0, nullptr, 0, &count) == MFG_MAP_OK && count == 0);
    scenario(0);
    assert(api->copy_points(0, nullptr, 0, &count) == MFG_MAP_SMALL_BUFFER && count == 3);
    assert(api->copy_points(0, points, 1, &count) == MFG_MAP_SMALL_BUFFER && count == 3 && points[0].id == 123);
    for (uint32_t layer = 0; layer < 3; ++layer) {
        assert(api->copy_points(layer, points, 8, &count) == MFG_MAP_OK && count == 3);
        assert(points[0].id == 20 && points[0].raw_map_id == (layer == 2 ? 0x3D1C4000 : 0x3C1C4000) && points[0].icon_id == 6 && points[0].x == 141 && points[0].z == 129);
        assert(points[1].id == (20 | (uint64_t{1} << 63)) && points[1].icon_id == 8);
        assert(points[2].id == (uint64_t{1} << 62) && points[2].icon_id == 26);
        assert(points[0].draw_order < points[1].draw_order && points[1].draw_order < points[2].draw_order);
    }
    assert(api->copy_points(3, points, 8, &count) == MFG_MAP_INVALID && count == 0);
    assert(api->copy_points(0, nullptr, 1, &count) == MFG_MAP_INVALID && count == 0);
    assert(api->copy_points(0, points, 8, nullptr) == MFG_MAP_INVALID);
    for (uint32_t i = 0; i < api->icon_count; ++i) {
        MfgMapIcon icon{};
        assert(api->get_icon(i, &icon) == MFG_MAP_OK && icon.lossless2 && icon.lossless2_size > 8 && icon.matrix && icon.matrix_size);
    }
    MfgMapIcon icon{};
    assert(api->get_icon(api->icon_count, &icon) == MFG_MAP_INVALID && !icon.lossless2);
    scenario(1);
    assert(api->copy_points(0, points, 8, &count) == MFG_MAP_OK && count == 0);
    scenario(2);
    assert(api->copy_points(0, points, 8, &count) == MFG_MAP_FAILED && count == 0);
    scenario(4);
    assert(api->copy_points(0, points, 8, &count) == MFG_MAP_OK && points[1].draw_order < points[0].draw_order);
    scenario(0);
    assert(api->copy_points(0, points, 8, &count) == MFG_MAP_OK && points[1].draw_order < points[0].draw_order);
    FreeLibrary(module);
    std::puts(
        "PASS: provider/client across static/dynamic CRT; ABI rejection; capacities; visibility; badges/rings; layers; icon catalog; actual child depths; exception boundary.");
}
