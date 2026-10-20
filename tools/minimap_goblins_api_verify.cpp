#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

#include "fixtures/minimap_goblins.hpp"
#include "mfg_map_api.h"

int wmain(int argc, wchar_t **argv) {
    using er::minimap::test::GoblinsScenario;
    assert(argc == 2);
    HMODULE module = LoadLibraryW(argv[1]);
    assert(module);
    const auto get = reinterpret_cast<MfgGetMapAPIFn>(GetProcAddress(module, "MfgGetMapAPI"));
    const auto scenario = reinterpret_cast<void (*)(uint32_t, GoblinsScenario)>(GetProcAddress(module, "setTestScenario"));
    const auto poll = reinterpret_cast<bool (*)(uint32_t)>(GetProcAddress(module, "testPollLayer"));
    const auto calls = reinterpret_cast<uint32_t (*)()>(GetProcAddress(module, "testCalls"));
    assert(get && scenario && poll && calls && !get(0) && !get(MFG_MAP_ABI + 1));
    static_assert(sizeof(MfgMapAPI) == 40 && sizeof(MfgMapPoint) == 32 && sizeof(MfgMapIcon) == 32);
    const auto *api = get(MFG_MAP_ABI);
    assert(api && api->size == sizeof(*api) && api->abi_version == MFG_MAP_ABI && api->point_size == sizeof(MfgMapPoint) && api->icon_size == sizeof(MfgMapIcon));
    const MfgMapPoint *points = nullptr;
    uint32_t count = 0;
    assert(api->view_points(0, &points, &count) == MFG_MAP_OK && !points && !count);
    for (uint32_t layer = 0; layer < 3; ++layer) {
        scenario(layer, GoblinsScenario::Visible);
        assert(api->view_points(layer, &points, &count) == MFG_MAP_OK && !points && !count);
        assert(poll(layer));
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

    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 3);
    const MfgMapPoint saved[] = {view[0], view[1], view[2]};
    // Reading other layers and publishing a replacement on a background thread keep this view alive.
    assert(api->view_points(1, &points, &count) == MFG_MAP_OK && count == 3);
    const MfgMapPoint otherSaved[] = {points[0], points[1], points[2]};
    scenario(0, GoblinsScenario::ManyPoints);
    assert(poll(0));
    assert(!std::memcmp(view, saved, sizeof(saved)) && !std::memcmp(points, otherSaved, sizeof(otherSaved)));
    const MfgMapPoint *shared = nullptr;
    std::thread other([&] {
        uint32_t otherCount = 0;
        assert(api->view_points(0, &shared, &otherCount) == MFG_MAP_OK && otherCount == 603 && shared != view);
    });
    other.join();
    assert(!std::memcmp(view, saved, sizeof(saved)));
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 603 && view == shared);
    const auto before = calls();
    for (unsigned i = 0; i < 1000; ++i) {
        assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 603 && view == shared);
    }
    assert(calls() == before);
    // A failed background build keeps the last successful snapshot available.
    scenario(0, GoblinsScenario::FailedBuild);
    assert(poll(0));
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 603 && view == shared);
    scenario(0, GoblinsScenario::Overflow);
    assert(poll(0));
    assert(api->view_points(0, &view, &count) == MFG_MAP_FAILED && !view && !count);
    scenario(0, GoblinsScenario::Hidden);
    assert(poll(0));
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && !view && !count);
    scenario(0, GoblinsScenario::NativeDepths);
    assert(poll(0));
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 3 && view[0].icon_id == 8 && view[1].icon_id == 6);
    scenario(0, GoblinsScenario::NewChild);
    assert(poll(0));
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 4 && view[3].id == 30 && view[2].draw_order < view[3].draw_order);
    scenario(0, GoblinsScenario::Visible);
    assert(poll(0));
    assert(api->view_points(0, &view, &count) == MFG_MAP_OK && count == 3 && view[0].icon_id == 8 && view[0].draw_order < view[1].draw_order);
    FreeLibrary(module);
    std::puts("PASS: official background snapshots across CRTs; ABI validation; cross-layer/thread lifetime; shared pointers; reads without rebuilds; limits; failed-build "
              "retention; native child depths; icons.");
}
