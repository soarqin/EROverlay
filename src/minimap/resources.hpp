#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gfx.hpp"
#include "nativeapi.h"

namespace er::minimap {

extern const EROverlayNativeAPI *nativeApi;
struct AtlasRegion {
    uint32_t atlas;
    uint32_t x, y, width, height;
};
struct TileKey {
    uint8_t map = 0, level = 0;
    uint16_t x = 0, y = 0;
    uint32_t variant = 0;
    [[nodiscard]] uint64_t value() const { return (uint64_t(map) << 56) | (uint64_t(level) << 52) | (uint64_t(x) << 44) | (uint64_t(y) << 36) | variant; }
};
struct TileView {
    uint64_t texture = 0;
    float left, top, right, bottom;
    ERTextureView view{};
};

class Resources {
public:
    Resources() = default;
    ~Resources() noexcept;
    Resources(const Resources &) = delete;
    Resources &operator=(const Resources &) = delete;
    Resources(Resources &&) = delete;
    Resources &operator=(Resources &&) = delete;
    void update();
    void prepareTextures();
    void resetTextures();
    void stop();
    void beginFrame(uint64_t generation, uint32_t map, const uint32_t (&masks)[3], uint8_t level);
    [[nodiscard]] bool tile(uint32_t map, uint16_t x, uint16_t y, TileView &view);
    void endFrame();
    [[nodiscard]] const IconRecipe *icon(uint32_t id) const;
    [[nodiscard]] const IconRecipe *special(const std::string &name) const;
    [[nodiscard]] const TextLayout *playerMarkerText() const;
    [[nodiscard]] bool layerView(const IconLayer &layer, AtlasRegion &region, ERTextureView &view);
    [[nodiscard]] const char *status() const { return status_.load(std::memory_order_acquire); }
    [[nodiscard]] uint64_t frameTime() const { return now_; } // Render thread only.
    [[nodiscard]] bool ready() const { return definitionsReady_.load(std::memory_order_acquire) && directoryReady_.load(std::memory_order_acquire); }
    [[nodiscard]] bool loadDefinitions(util::Bytes gfx, util::Bytes layouts);
    [[nodiscard]] bool loadDirectory(util::Bytes index, util::Bytes masks);
    [[nodiscard]] size_t iconCount() const { return icons_.size(); }
    [[nodiscard]] size_t atlasCount() const { return definitionsReady_.load(std::memory_order_acquire) ? atlases_.size() : 0; }

private:
    friend struct ResourcePerformanceVerifier;
    struct File {
        uint64_t token = 0;
        uint64_t retry = 0;
        bool complete = false;
    };
    enum class JobState : uint8_t { Pending, Ready, Failed };
    struct TextureJob {
        File file; // Update thread only.
        std::wstring path;
        util::Bytes source; // Immutable atlas bytes or a live file's DDS view.
        uint64_t texture = 0;
        size_t gpuBytes = 0;
        uint64_t retry = 0;
        bool tile = false;
        std::atomic_bool cancelled{false};
        std::atomic<JobState> state{JobState::Pending};
    };
    struct Atlas {
        std::wstring name;
        std::wstring path;
        bool required = false;
        std::vector<uint8_t> dds;
        uint64_t texture = 0;
        uint32_t width = 0, height = 0;
        uint64_t retry = 0;
        uint64_t touched = 0, polled = UINT64_MAX;
        ERTextureView view{};
        ERTextureStatus state = ER_TEXTURE_PENDING;
        std::shared_ptr<TextureJob> job;
        // Update publishes CPU bytes once; only render changes GPU state.
        std::atomic_bool ready{false};
    };
    struct AtlasFile {
        File file;
        std::wstring path;
        std::vector<uint32_t> atlases;
    };
    struct Tile {
        std::shared_ptr<TextureJob> job;
        uint64_t texture = 0;
        uint64_t touched = 0;
        uint64_t frame = 0;
        uint64_t retry = 0;
        size_t gpuBytes = 0;
        TileKey key;
        bool allocationKnown = false;
        std::list<uint64_t>::iterator lru;
    };
    struct Mask {
        uint32_t allowed = 0;
        bool exists = false;
    };
    bool request(File &file, const wchar_t *path, uint32_t flags, const std::vector<const wchar_t *> &names = {});
    [[nodiscard]] bool poll(File &file, util::Bytes &bytes);
    void release(File &file);
    void clearTiles();
    void queueJob(const std::shared_ptr<TextureJob> &job);
    void updateJobs();
    void cancelJob(std::shared_ptr<TextureJob> &job);
    void collectCancelledJobs();
    [[nodiscard]] bool finishJob(std::shared_ptr<TextureJob> &job, uint64_t &texture, size_t &gpuBytes);
    void eraseTile(std::unordered_map<uint64_t, Tile>::iterator tile);
    std::vector<std::unique_ptr<Atlas>> atlases_;
    std::vector<AtlasFile> atlasFiles_;
    std::array<File, 4> files_;
    std::array<std::vector<uint8_t>, 4> metadata_;
    std::unordered_map<std::string, AtlasRegion> regions_;
    std::vector<AtlasRegion> resolvedRegions_;
    std::unordered_map<uint32_t, IconRecipe> icons_;
    std::unordered_map<std::string, IconRecipe> specials_;
    TextLayout playerMarkerText_;
    std::unordered_map<uint64_t, std::wstring> tilePaths_;
    std::array<std::unordered_map<uint32_t, Mask>, 3> masks_;
    std::unordered_map<uint64_t, Tile> tiles_;
    std::list<uint64_t> tileLru_;
    size_t tileBytes_ = 0;
    std::atomic_size_t pendingTiles_{0};
    std::atomic_bool jobsQueued_{false};
    std::vector<uint64_t> tileJobs_; // Render tracks at most eight active requests.
    std::mutex jobMutex_;
    std::vector<std::shared_ptr<TextureJob>> queuedJobs_, activeJobs_, cancelledJobs_;
    std::vector<std::shared_ptr<TextureJob>> incomingJobs_; // Update reuses queue storage.
    uint64_t now_ = 0, atlasFrame_ = 0, cacheTrim_ = 0;
    size_t frameUploadBytes_ = 0;
    unsigned frameUploads_ = 0;
    uint64_t generation_ = 0, frame_ = 0;
    uint32_t map_ = 0;
    std::array<uint32_t, 3> activeMasks_{};
    uint8_t level_ = 0;
    uint32_t mapMask_ = 0;
    std::atomic_bool definitionsReady_{false}, directoryReady_{false};
    std::atomic<const char *> status_{"正在等待游戏资源"};
};

extern Resources gResources;

} // namespace er::minimap
