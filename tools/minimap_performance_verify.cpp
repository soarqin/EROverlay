// Verify production cache/jobs and disabled logging with owned fixtures.
// No game process, injected DLL, debugger or synthetic renderer is required.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "minimap/data.hpp"
#include "minimap/resources.hpp"
#include "util/nativelog.hpp"

EROverlayAPI *api = nullptr;
namespace {
std::vector<uint8_t> readFile(const char *path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    auto size = stream.tellg();
    if (size <= 0)
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()), size);
    return bytes;
}
std::vector<uint8_t> tileBytes;
std::unordered_map<uint64_t, ERTextureView> textures;
uint64_t nextFile = 0, nextTexture = 0;
unsigned requests = 0, filePolls = 0, creates = 0, texturePolls = 0, releases = 0, retires = 0;
bool holdFile = false;
DWORD renderThread = 0;
unsigned renderFileCalls = 0, renderQueueCalls = 0;
} // namespace

namespace er::minimap {
struct ResourcePerformanceVerifier {
    static bool run(Resources &resources) {
        auto index = readFile("build/ida/probes/run-26836-32694765/map-index.bin");
        auto masks = readFile("build/ida/probes/run-26836-32694765/map-masks.bin");
        if (!resources.loadDirectory(index, masks))
            return false;
        uint32_t progress[3]{0x8000, 8, 0};
        TileView view;
        renderThread = GetCurrentThreadId();
        resources.prepareTextures();
        resources.beginFrame(1, 0, progress, 0);
        if (resources.tile(0, 20, 20, view) || requests || creates)
            return false;
        // Heavy CPU preparation actually runs on another thread. Render only
        // queues a job and receives its published token on the next frame.
        std::thread update([&] { resources.update(); });
        update.join();
        resources.prepareTextures();
        if (!resources.tile(0, 20, 20, view) || !view.view.gpuHandle || creates != 1 || resources.pendingTiles_ != 0 || renderFileCalls || renderQueueCalls)
            return false;
        unsigned beforeRequests = requests, beforePolls = filePolls, beforeCreates = creates;
        const auto token = view.texture;
        auto start = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < 1000; ++frame) {
            resources.prepareTextures();
            resources.beginFrame(1, 0, progress, 0);
            if (!resources.tile(0, 20, 20, view) || view.texture != token)
                return false;
            resources.endFrame();
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        if (requests != beforeRequests || filePolls != beforePolls || creates != beforeCreates || texturePolls != 1001)
            return false;
        // An unrelated fragment bit and a map switch preserve this texture.
        progress[0] |= 0x80000000;
        resources.beginFrame(1, 1, progress, 0);
        if (!resources.tile(0, 20, 20, view) || view.texture != token || requests != beforeRequests)
            return false;
        holdFile = true;
        (void)resources.tile(1, 20, 20, view);
        std::thread pending([&] { resources.update(); });
        pending.join();
        if (resources.pendingTiles_ != 1)
            return false;
        auto beforeRetires = retires, beforeReleases = releases;
        resources.beginFrame(2, 0, progress, 0);
        std::thread cancel([&] { resources.update(); });
        cancel.join();
        resources.prepareTextures();
        if (resources.pendingTiles_ || !resources.tiles_.empty() || retires != beforeRetires + 1 || releases != beforeReleases + 1 || !resources.cancelledJobs_.empty())
            return false;
        holdFile = false;
        // Populate real cache records through the production erase path with
        // varied committed-size estimates. Enforce bytes and LRU, not just count.
        auto add = [&](uint64_t id, size_t bytes, uint64_t frame) {
            auto [it, inserted] = resources.tiles_.try_emplace(id);
            it->second.texture = ++nextTexture;
            textures[nextTexture] = {reinterpret_cast<void *>(nextTexture), 2048, 2048};
            it->second.gpuBytes = bytes;
            it->second.frame = frame;
            it->second.touched = resources.now_;
            resources.tileLru_.push_back(id);
            it->second.lru = std::prev(resources.tileLru_.end());
            resources.tileBytes_ += bytes;
        };
        for (uint64_t id = 0; id < 40; ++id)
            add(id, 2 * 1024 * 1024, resources.frame_ - 1);
        resources.endFrame();
        if (resources.tileBytes_ != 64 * 1024 * 1024 || resources.tiles_.size() != 32 || resources.tileLru_.front() != 8)
            return false;
        add(40, 2 * 1024 * 1024, resources.frame_);
        resources.endFrame();
        if (!resources.tiles_.contains(40) || resources.tileLru_.front() != 9 || resources.tileBytes_ > 64 * 1024 * 1024)
            return false;
        for (auto &[id, tile]: resources.tiles_)
            tile.touched = 0;
        resources.cacheTrim_ = 0;
        resources.prepareTextures();
        if (!resources.tiles_.empty() || resources.tileBytes_ || !textures.empty())
            return false;
        std::printf("PASS: 1000 warm tile frames: %lld us total, zero file polls/requests/creates, one GPU view poll per frame.\n", static_cast<long long>(elapsed));
        std::puts("PASS: update-thread CPU loading; fragment/map reuse; generation cancellation; 64 MiB LRU budget, visible protection and idle retirement.");
        return true;
    }
};
} // namespace er::minimap

int main() {
    tileBytes = readFile("build/ida/probes/run-26836-32694765/surface-v8000.tpf");
    if (tileBytes.empty())
        return 1;
    EROverlayNativeAPI native{};
    native.size = sizeof(native);
    native.requestFile = [](const ERFileRequest *request) -> uint64_t {
        if (!std::wstring_view(request->path).starts_with(L"menutpfbnd:"))
            return 0;
        ++requests;
        renderFileCalls += GetCurrentThreadId() == renderThread;
        return ++nextFile;
    };
    native.pollFile = [](uint64_t, const wchar_t *, ERFileData *out) {
        ++filePolls;
        renderFileCalls += GetCurrentThreadId() == renderThread;
        *out = {tileBytes.data(), tileBytes.size(), 1};
        return holdFile ? ER_FILE_PENDING : ER_FILE_SUCCEEDED;
    };
    native.releaseFile = [](uint64_t) { ++releases; };
    native.queueDdsTexture = [](const void *data, uint64_t size) -> uint64_t {
        renderQueueCalls += GetCurrentThreadId() == renderThread;
        er::util::DdsImage image;
        if (!er::util::parseDds({static_cast<const uint8_t *>(data), static_cast<size_t>(size)}, image))
            return 0;
        ++creates;
        auto token = ++nextTexture;
        textures[token] = {reinterpret_cast<void *>(token), image.width, image.height};
        return token;
    };
    native.pollTexture = [](uint64_t token, ERTextureView *out) {
        ++texturePolls;
        auto found = textures.find(token);
        if (found == textures.end())
            return ER_TEXTURE_INVALID;
        *out = found->second;
        return ER_TEXTURE_READY;
    };
    native.retireTexture = [](uint64_t token) { retires += textures.erase(token); };
    er::minimap::nativeApi = &native;
    {
        er::minimap::Resources resources;
        if (!er::minimap::ResourcePerformanceVerifier::run(resources))
            return 2;
    }
    er::minimap::nativeApi = nullptr;
    // Hold the logger's mutex while a different thread invokes disabled
    // logging. It must return without locking, formatting or writing.
    std::atomic_bool done{false};
    std::unique_lock lock(er::util::nativeLogMutex);
    std::thread disabled([&] {
        for (unsigned i = 0; i < 100000; ++i)
            er::util::nativeLog("%u\n", i);
        er::util::flushNativeLog();
        done.store(true, std::memory_order_release);
    });
    for (unsigned i = 0; i < 100 && !done.load(std::memory_order_acquire); ++i)
        Sleep(1);
    bool skipped = done.load(std::memory_order_acquire);
    lock.unlock();
    disabled.join();
    if (!skipped)
        return 3;
    FILE *file = std::tmpfile();
    if (!file)
        return 4;
    er::util::nativeLogFile.store(file);
    er::util::nativeLog("enabled=%d\n", 1);
    er::util::flushNativeLog();
    std::rewind(file);
    char message[32]{};
    bool written = std::fgets(message, sizeof(message), file) && std::string_view(message) == "enabled=1\n";
    er::util::closeNativeLog();
    if (!written || er::util::nativeLogEnabled())
        return 5;
    std::puts("PASS: disabled logger bypasses a held mutex for 100000 calls; enabled log flush/close preserves messages.");
}
