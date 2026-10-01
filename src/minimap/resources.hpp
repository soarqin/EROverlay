#pragma once

#include <array>
#include <atomic>
#include <cstdint>
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
    [[nodiscard]] bool layerView(const IconLayer &layer, AtlasRegion &region, ERTextureView &view);
    [[nodiscard]] const char *status() const { return status_.load(std::memory_order_acquire); }
    [[nodiscard]] bool ready() const { return definitionsReady_.load(std::memory_order_acquire) && directoryReady_.load(std::memory_order_acquire); }
    [[nodiscard]] bool loadDefinitions(util::Bytes gfx, util::Bytes layouts);
    [[nodiscard]] bool loadDirectory(util::Bytes index, util::Bytes masks);
    [[nodiscard]] size_t iconCount() const { return icons_.size(); }

private:
    struct File {
        uint64_t token = 0;
        uint64_t retry = 0;
        bool complete = false;
    };
    struct Atlas {
        std::vector<uint8_t> dds;
        uint64_t texture = 0;
        uint32_t width = 0, height = 0;
        uint64_t retry = 0;
    };
    struct Tile {
        File file;
        uint64_t texture = 0;
        uint64_t touched = 0;
        TileKey key;
    };
    struct Mask {
        uint32_t allowed = 0;
        bool exists = false;
    };
    bool request(File &file, const wchar_t *path, uint32_t flags, bool atlas = false);
    [[nodiscard]] bool poll(File &file, util::Bytes &bytes);
    void release(File &file);
    void clearTiles();
    std::array<Atlas, 4> atlases_;
    std::array<File, 5> files_;
    std::array<std::vector<uint8_t>, 4> metadata_;
    std::unordered_map<std::string, AtlasRegion> regions_;
    std::unordered_map<uint32_t, IconRecipe> icons_;
    std::unordered_map<std::string, IconRecipe> specials_;
    std::unordered_set<std::string> tileNames_;
    std::array<std::unordered_map<uint32_t, Mask>, 3> masks_;
    std::unordered_map<uint64_t, Tile> tiles_;
    uint64_t generation_ = 0, frame_ = 0;
    uint32_t map_ = 0;
    std::array<uint32_t, 3> activeMasks_{};
    uint8_t level_ = 0;
    std::atomic_bool definitionsReady_{false}, directoryReady_{false}, atlasesReady_{false};
    std::atomic<const char *> status_{"正在等待游戏资源"};
};

extern Resources gResources;

} // namespace er::minimap
