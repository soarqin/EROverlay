// Exercise the production native completion callback with fixture TPF bytes.
// The ordinary verifier EXE is unknown: its worker must refuse game calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

#include "gamefiles.hpp"
#include "minimap/resources.hpp"
// Access the exact production callback and pool logic in this verifier TU.
#include "../src/gamefiles.cpp"

namespace er {
struct NativeFileVerifier {
    static inline unsigned freed = 0;
    static inline GameFiles *bridge = nullptr;
    static inline util::Bytes container;
    static inline uint64_t copiedBytes = 0;
    static inline uint32_t requestedNames = 0;
    static inline uint64_t nextTexture = 0;
    static inline std::unordered_map<uint64_t, ERTextureView> textureViews;
    static inline std::unordered_map<std::wstring, util::Bytes> metadataSources;
    static inline uint32_t metadataRequests = 0;
    static inline uint32_t mapStateReads = 0;
    static inline bool mapReady = false;
    static inline bool gameCompatible = true;
    static void freeBuffer(void *, void *bytes) {
        ++freed;
        HeapFree(GetProcessHeap(), 0, bytes);
    }
    static std::vector<uint8_t> readFile(const char *path) {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            return {};
        auto size = stream.tellg();
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char *>(bytes.data()), size);
        return bytes;
    }
    static bool completeBuffer(GameFiles::Impl::Item &item, util::Bytes bytes, uint32_t status = 1) {
        std::array<uintptr_t, 14> vtable{};
        vtable[13] = reinterpret_cast<uintptr_t>(freeBuffer);
        struct Allocator {
            uintptr_t *vtable;
        } allocator{vtable.data()};
        item.allocator = &allocator;
        auto *buffer = HeapAlloc(GetProcessHeap(), 0, bytes.size());
        if (!buffer)
            return false;
        std::memcpy(buffer, bytes.data(), bytes.size());
        unsigned before = freed;
        GameFiles::Impl::complete(status, &item, buffer, bytes.size());
        item.allocator = nullptr;
        return freed == before + 1;
    }
    static bool completeFixture(GameFiles &files, const char *path, bool partial, uint64_t maxBytes = 256ull * 1024 * 1024, size_t expectedParts = 12) {
        auto tpf = readFile(path);
        if (tpf.empty())
            return false;
        auto item = std::make_shared<GameFiles::Impl::Item>();
        for (unsigned i = 0; i < 12; ++i)
            item->names.push_back(std::wstring(L"SB_ModMap_") + (i < 10 ? L"0" : L"") + std::to_wstring(i));
        item->parts.resize(item->names.size());
        item->maxBytes = maxBytes;
        if (!completeBuffer(*item, tpf) || item->status != ER_FILE_SUCCEEDED) {
            std::fprintf(stderr, "FAIL: production completion fixture=%s input=%zu copy-budget=%llu status=%d\n", path, tpf.size(), static_cast<unsigned long long>(maxBytes),
                         int(item->status.load()));
            return false;
        }
        uint64_t token;
        {
            std::lock_guard lock(files.impl_->mutex);
            token = files.impl_->nextToken++;
            files.impl_->items.emplace(token, item);
        }
        for (size_t i = 0; i < item->names.size(); ++i) {
            ERFileData data{};
            auto status = files.poll(token, item->names[i].c_str(), data);
            util::Bytes expected;
            bool found = util::findTpfDds(tpf, item->names[i], expected);
            if (!found || i >= expectedParts) {
                if ((!found && !partial) || status != ER_FILE_FAILED || data.data || data.size)
                    return false;
            } else if (status != ER_FILE_SUCCEEDED || data.size != expected.size() || !std::equal(expected.begin(), expected.end(), static_cast<const uint8_t *>(data.data)))
                return false;
        }
        ERFileData data{};
        if (files.poll(token, L"unknown-part", data) != ER_FILE_INVALID)
            return false;
        files.release(token);
        return files.poll(token, item->names.front().c_str(), data) == ER_FILE_INVALID;
    }
    static bool verifyRawLimits() {
        auto bytes = readFile("build/native-checks/mod-fixture/atlases.tpf");
        if (bytes.empty())
            return false;
        for (uint32_t status: {1u, 2u, 0u}) {
            for (size_t maxBytes: {bytes.size(), bytes.size() - 1}) {
                GameFiles::Impl::Item item;
                item.parts.resize(1);
                item.maxBytes = maxBytes;
                if (!completeBuffer(item, bytes, status))
                    return false;
                bool success = status == 1 && maxBytes == bytes.size();
                auto expected = status == 2 ? ER_FILE_CANCELLED : success ? ER_FILE_SUCCEEDED : ER_FILE_FAILED;
                if (item.status != expected || item.parts[0].valid != success || (!success && item.parts[0].size))
                    return false;
                if (success && (item.parts[0].size != bytes.size() || !std::equal(bytes.begin(), bytes.end(), item.parts[0].bytes.get())))
                    return false;
            }
        }
        return true;
    }
    static bool verifyResources(const char *tpfPath = "build/native-checks/mod-fixture/large-atlases.tpf",
                                const char *gfxPath = "build/ida/sprite-probe/run-24636-34293203/worldmap.gfx",
                                const char *layoutsPath = "build/native-checks/mod-fixture/layouts.bin", uint32_t expectedNames = 12,
                                uint64_t expectedBytes = 12ull * (2 * 1024 * 1024 + 128)) {
        // Stop the EXE-checking worker, then deliver owned fixture buffers in
        // place of game I/O. Request, completion, poll and release are production.
        GameFiles files;
        files.stop();
        files.impl_->stopping = false;
        bridge = &files;
        auto bytes = readFile(tpfPath);
        container = bytes;
        requestedNames = 0;
        copiedBytes = 0;
        EROverlayNativeAPI native{};
        native.size = sizeof(native);
        native.gameCompatible = [] { return gameCompatible; };
        native.requestFile = [](const ERFileRequest *request) -> uint64_t {
            auto token = bridge->request(*request);
            if (!token)
                return 0;
            auto item = bridge->impl_->items.at(token);
            if (!request->tpfNameCount) {
                auto source = metadataSources.find(item->path);
                if (source == metadataSources.end() || !completeBuffer(*item, source->second)) {
                    bridge->release(token);
                    return 0;
                }
                ++metadataRequests;
                return token;
            }
            if (item->path != L"menu:/hi/01_common.tpf.dcx") {
                bridge->release(token);
                return 0;
            }
            if (!completeBuffer(*item, container)) {
                bridge->release(token);
                return 0;
            }
            requestedNames = request->tpfNameCount;
            for (const auto &part: item->parts)
                copiedBytes += part.size;
            return token;
        };
        native.pollFile = [](uint64_t token, const wchar_t *part, ERFileData *data) { return bridge->poll(token, part, *data); };
        native.releaseFile = [](uint64_t token) { bridge->release(token); };
        // Only GPU submission is mocked; actual drawing/UVs have a separate
        // atlas verifier. Ensure CPU readiness reaches the visible recipes.
        native.queueDdsTexture = [](const void *data, uint64_t size) -> uint64_t {
            util::DdsImage image;
            if (!util::parseDds({static_cast<const uint8_t *>(data), static_cast<size_t>(size)}, image))
                return 0;
            auto token = ++nextTexture;
            textureViews.emplace(token, ERTextureView{reinterpret_cast<void *>(uintptr_t(token)), image.width, image.height});
            return token;
        };
        native.pollTexture = [](uint64_t token, ERTextureView *view) {
            auto found = textureViews.find(token);
            if (found == textureViews.end())
                return ER_TEXTURE_INVALID;
            *view = found->second;
            return ER_TEXTURE_READY;
        };
        native.retireTexture = [](uint64_t token) { textureViews.erase(token); };
        native.readMapState = [](ERMapState *state) {
            *state = {};
            ++mapStateReads;
            return mapReady;
        };
        minimap::nativeApi = &native;
        auto gfx = readFile(gfxPath);
        auto layouts = readFile(layoutsPath);
        auto index = readFile("build/ida/probes/run-26836-32694765/map-index.bin");
        auto masks = readFile("build/ida/probes/run-26836-32694765/map-masks.bin");
        auto startupGfx = readFile("build/ida/sprite-probe/run-24636-34293203/worldmap.gfx");
        metadataSources = {{L"menu:/02_120_worldmap.gfx", startupGfx},
                           {L"menu:/hi/01_common.sblytbnd.dcx", layouts},
                           {L"menu:/71_maptile.tpfbhd", index},
                           {L"menu:/71_maptile.mtmskbnd.dcx", masks}};
        metadataRequests = 0;
        mapStateReads = 0;
        mapReady = false;
        gameCompatible = false;
        bool ok = false;
        {
            minimap::Resources resources;
            resources.update();
            if (metadataRequests || resources.ready() || resources.atlasCount() || std::strcmp(resources.status(), "当前游戏版本不支持原生资源")) {
                std::fputs("FAIL: metadata requested/cached before gameplay resources were ready\n", stderr);
                minimap::nativeApi = nullptr;
                return false;
            }
            gameCompatible = true;
            resources.update();
            if (metadataRequests || std::strcmp(resources.status(), "正在等待游戏资源")) {
                std::fputs("FAIL: unsupported startup status did not clear while waiting for gameplay\n", stderr);
                minimap::nativeApi = nullptr;
                return false;
            }
            // The loader's mod mounts become available before gameplay. A
            // title-screen read must not freeze the vanilla 348-frame GFX.
            metadataSources[L"menu:/02_120_worldmap.gfx"] = gfx;
            mapReady = true;
            unsigned before = freed;
            resources.update();
            ok = resources.ready() && !*resources.status() && (!expectedNames || resources.atlasCount() == expectedNames) && metadataRequests == 4 &&
                 requestedNames == resources.atlasCount() && copiedBytes && copiedBytes <= 256ull * 1024 * 1024 && (!expectedBytes || copiedBytes == expectedBytes) &&
                 freed == before + 5;
            minimap::GfxMovie movie;
            ok &= movie.parse(gfx);
            for (auto id: {499u, 500u, 501u, 502u}) {
                minimap::IconRecipe expected;
                if (movie.icon(id, expected))
                    ok &= resources.icon(id) != nullptr;
            }
            for (const char *name: {"player", "death", "marker", "home", "arrow", "bearing"}) {
                auto recipe = resources.special(name);
                if (!recipe) {
                    ok = false;
                    continue;
                }
                for (const auto &layer: recipe->layers) {
                    if (!layer.bitmap())
                        continue;
                    minimap::AtlasRegion region;
                    ERTextureView view;
                    resources.prepareTextures();
                    (void)resources.layerView(layer, region, view);
                    resources.update();
                    resources.prepareTextures();
                    ok &= resources.layerView(layer, region, view);
                }
            }
            unsigned reads = mapStateReads;
            resources.update();
            ok &= mapStateReads == reads;
        }
        minimap::nativeApi = nullptr;
        metadataSources.clear();
        container = {};
        bridge = nullptr;
        ok &= textureViews.empty();
        std::printf("%s: Resources input=%s bytes=%zu requested=%u copied=%llu; special recipes and status.\n", ok ? "PASS" : "FAIL", tpfPath, bytes.size(), requestedNames,
                    static_cast<unsigned long long>(copiedBytes));
        return ok;
    }
    static int run(int argc, char **argv) {
        if (argc != 1 && argc != 4) {
            std::fprintf(stderr, "Usage: minimap_file_verify.exe [decompressed.tpf worldmap.gfx decompressed-layouts.bnd]\n");
            return 7;
        }
        GameFiles files;
        if (!completeFixture(files, "build/native-checks/mod-fixture/atlases.tpf", false) || !completeFixture(files, "build/native-checks/mod-fixture/partial-atlases.tpf", true))
            return 1;
        if (!completeFixture(files, "build/native-checks/mod-fixture/large-atlases.tpf", false))
            return 4;
        // Exactly one 2048x2048 BC1 DDS fits. The other eleven parts must fail
        // independently without copying beyond the requested output budget.
        if (!completeFixture(files, "build/native-checks/mod-fixture/atlases.tpf", false, 2ull * 1024 * 1024 + 128, 1) || !verifyRawLimits())
            return 5;
        std::array<const wchar_t *, 12> names;
        names.fill(L"SB_ModMap_00");
        ERFileRequest request{L"menu:/Hi/01_Common.tpf.dcx", 0x40, names.data(), static_cast<uint32_t>(names.size()), 256ull * 1024 * 1024};
        auto token = files.request(request);
        if (!token)
            return 2;
        {
            std::lock_guard lock(files.impl_->mutex);
            if (files.impl_->items.at(token)->path != L"menu:/hi/01_common.tpf.dcx" || files.impl_->items.at(token)->names.front() != L"SB_ModMap_00") {
                std::fputs("FAIL: virtual path is not canonical, or DDS name case changed\n", stderr);
                return 9;
            }
        }
        for (auto [path, expected]:
             {std::pair{L"MENU:\\Hi\\01_Common.tpf.dcx", L"menu:/hi/01_common.tpf.dcx"}, std::pair{L"D:/Mods/Menu/01_Common.tpf.dcx", L"D:/Mods/Menu/01_Common.tpf.dcx"},
              std::pair{L"D:\\Mods\\Menu\\01_Common.tpf.dcx", L"D:\\Mods\\Menu\\01_Common.tpf.dcx"}, std::pair{L"Mods/Menu/01_Common.tpf.dcx", L"Mods/Menu/01_Common.tpf.dcx"}}) {
            ERFileRequest other{path, 0x40, nullptr, 0, 1024};
            auto otherToken = files.request(other);
            if (!otherToken)
                return 9;
            {
                std::lock_guard lock(files.impl_->mutex);
                if (files.impl_->items.at(otherToken)->path != expected)
                    return 9;
            }
            files.release(otherToken);
        }
        ERFileStatus status = ER_FILE_QUEUED;
        for (unsigned retry = 0; retry < 1000 && status == ER_FILE_QUEUED; ++retry) {
            ERFileData data;
            status = files.poll(token, names.front(), data);
            Sleep(1);
        }
        if (status != ER_FILE_UNSUPPORTED || files.compatible())
            return 3;
        files.release(token);
        files.stop();
        if (!verifyResources())
            return 6;
        if (argc == 4 && !verifyResources(argv[1], argv[2], argv[3], 0, 0))
            return 8;
        std::puts("PASS: production callback extracts 12 DDS, frees native buffer once, isolates missing parts, retires released data, accepts more than eight names and rejects "
                  "unknown EXE calls.");
        std::puts("PASS: >256 MiB / 42-image container returns only 12 requested DDS; named-copy and raw-file budgets remain bounded; cancellation/failure frees buffers.");
        std::puts("PASS: production GameFiles request/completion/poll/release feeds Resources; all six special recipes become renderable with no required-atlas error.");
        std::puts("PASS: first metadata load waits for gameplay and selects late-mounted mod GFX; canonical virtual paths preserve exact DDS names.");
        return 0;
    }
};
} // namespace er

int main(int argc, char **argv) { return er::NativeFileVerifier::run(argc, argv); }
