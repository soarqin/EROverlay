// Exercise production flag reads and tile requests with real map metadata.
// Synthetic memory is owned by this process; no debugger or game writes.
#define NOMINMAX

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "minimap/resources.hpp"
#include "params/param.hpp"
#include "util/gameflags.hpp"

namespace {
template<size_t N, typename T>
void put(std::array<uint8_t, N> &bytes, size_t offset, const T &value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
template<size_t N>
uintptr_t address(std::array<uint8_t, N> &bytes) {
    return reinterpret_cast<uintptr_t>(bytes.data());
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
std::vector<std::wstring> requested;
uint64_t nextToken = 1;
} // namespace

int main() {
    using namespace er::util;
    std::array<uint8_t, 128> manager{}, sentinel{}, node{};
    std::array<uint8_t, 64> packed{}, direct{};
    std::array<uint8_t, 2048> table{};
    put(manager, 0x1C, uint32_t{100});
    put(manager, 0x20, uint32_t{16});
    put(manager, 0x28, address(packed));
    put(manager, 0x38, address(sentinel));
    put(sentinel, 8, address(node));
    sentinel[0x19] = 1;
    put(node, 0, address(sentinel));
    put(node, 0x10, address(sentinel));
    put(node, 0x20, uint32_t{1});
    put(node, 0x28, uint32_t{2});
    put(node, 0x30, address(direct));
    direct[0] = 0x80;
    direct[1] = 0x2E;
    bool value = false;
    if (!readGameEventFlag(address(manager), 100, value) || !value || !readGameEventFlag(address(manager), 110, value) || !value ||
        !readGameEventFlag(address(manager), 111, value) || value || !readGameEventFlag(address(manager), 210, value) || value)
        return 1;
    // Same flag category in packed-index form must give the same result.
    put(node, 0x28, uint32_t{1});
    put(node, 0x30, uint32_t{1});
    packed[16] = 0x80;
    packed[17] = 0x2E;
    if (!readGameEventFlag(address(manager), 110, value) || !value)
        return 2;
    put(node, 0x28, uint32_t{0});
    put(node, 0x30, address(direct));
    if (!readGameEventFlag(address(manager), 110, value) || value)
        return 3;
    put(node, 0x28, uint32_t{2});
    put(node, 0x30, uintptr_t{1});
    if (readGameEventFlag(address(manager), 110, value))
        return 4;
    put(node, 0x30, address(direct));

    constexpr uint64_t ids[] = {0, 15, 31, 103, 1000, 1001, 1070};
    constexpr int32_t events[] = {-1, 110, 111, 112, 113, 114, 115};
    table[0x2D] = 4;
    table[0x2E] = 2;
    put(table, 0, uint32_t{512 + std::size(ids) * 24});
    put(table, 0xA, uint16_t{7});
    for (size_t i = 0; i < std::size(ids); ++i) {
        er::params::ParamEntryOffset entry{ids[i], static_cast<intptr_t>(512 + i * 24), 0};
        put(table, 0x40 + i * sizeof(entry), entry);
        put(table, entry.offset + 4, events[i]);
    }
    uint32_t masks[3]{};
    if (!readMapPieceMasks(address(table), address(manager), false, masks) || masks[0] != 0x8000 || masks[1] != 8 || masks[2] != 3)
        return 5;
    direct[1] |= 0x10;
    if (!readMapPieceMasks(address(table), address(manager), false, masks) || masks[0] != 0x80008000)
        return 6;
    direct[1] &= ~0x10;
    if (!readMapPieceMasks(address(table), address(manager), false, masks))
        return 7;

    EROverlayNativeAPI api{};
    api.requestFile = [](const ERFileRequest *request) {
        requested.emplace_back(request->path);
        return nextToken++;
    };
    api.pollFile = [](uint64_t, const wchar_t *, ERFileData *) { return ER_FILE_PENDING; };
    api.releaseFile = [](uint64_t) {};
    er::minimap::nativeApi = &api;
    {
        er::minimap::Resources resources;
        auto index = file("build/ida/probes/run-26836-32694765/map-index.bin");
        auto metadata = file("build/ida/probes/run-26836-32694765/map-masks.bin");
        if (!resources.loadDirectory(index, metadata))
            return 8;
        er::minimap::TileView view;
        resources.prepareTextures();
        resources.beginFrame(1, 0, masks, 0);
        bool ready = resources.tile(0, 20, 20, view);
        resources.prepareTextures();
        if (ready || requested.size() != 1 || !requested.back().ends_with(L"M00_L0_20_20_00008000.tpf.dcx"))
            return 9;
        // The two maps must have independent variants in the same frame.
        resources.beginFrame(1, 1, masks, 0);
        ready = resources.tile(0, 20, 20, view);
        ready |= resources.tile(1, 20, 20, view);
        resources.prepareTextures();
        if (ready || requested.size() != 2 || !requested[1].ends_with(L"M01_L0_20_20_00000008.tpf.dcx"))
            return 10;
        direct[1] = 0;
        if (!readMapPieceMasks(address(table), address(manager), false, masks) || masks[0] || masks[1] || masks[2])
            return 11;
        resources.beginFrame(1, 0, masks, 0);
        ready = resources.tile(0, 20, 20, view);
        resources.prepareTextures();
        if (ready || !requested.back().ends_with(L"M00_L0_20_20_00000000.tpf.dcx"))
            return 12;
        if (!readMapPieceMasks(0, 0, true, masks) || masks[0] != UINT32_MAX || masks[1] != UINT32_MAX || masks[2] != UINT32_MAX)
            return 13;
        if (readMapPieceMasks(address(table), 0, false, masks) || masks[0] || masks[1] || masks[2])
            return 14;
    }
    er::minimap::nativeApi = nullptr;
    std::puts("PASS: direct/packed flags, native map-piece IDs, bit 31, unexplored/expanded tile requests, underground base/overlay and unreadable-progress rejection.");
}
