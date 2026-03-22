#pragma once

#include "api.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace er::minimap {

struct Location {
    int32_t mapId;
    float x;
    float y;
    int32_t underground;
    float rad;
};

struct SpriteInfo;

struct DecorationInfo {
    uint64_t id = 0;
    uint32_t eventFlagId = 0;
    int32_t layer = 0;
    float x = 0.f;
    float y = 0.f;
    float localX = 0.f;
    float localY = 0.f;
    float rotationRad = 0.f;
    int32_t sortKey = 0;
    mutable uintptr_t eventFlagAddress = 0;
    mutable uint8_t eventFlagBits = 0;
    const SpriteInfo *sprite = nullptr;

    [[nodiscard]] bool isUnlocked() const;
};

class Data {
public:
    void load();
    void update();

    [[nodiscard]] inline bool onGUI() const { return onGUI_; }
    [[nodiscard]] inline bool paramsLoaded() const { return paramsLoaded_; }
    [[nodiscard]] inline const Location &location() const { return location_; }
    [[nodiscard]] inline const std::vector<DecorationInfo> &decorations(int32_t layer) const { return decorations_[layer]; }
    [[nodiscard]] std::tuple<const DecorationInfo *, const DecorationInfo *> decorationsAround(int32_t layer, int u, int v) const;

private:
    uintptr_t csMenuManImp_ = 0;
    size_t locationOffset_ = 0;
    Location location_ = {};
    bool onGUI_ = false;
    std::atomic_bool paramsLoaded_ = false;

    std::vector<DecorationInfo> decorations_[3];
    std::vector<std::tuple<const DecorationInfo *, const DecorationInfo *>> decorationsAround_[3];
};

extern Data gData;

struct SpriteInfo {
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float u0 = 0.f;
    float v0 = 0.f;
    float u1 = 1.f;
    float v1 = 1.f;
    float centerX = 0.f;
    float centerY = 0.f;
    TextureContext *texture = nullptr;
};

struct AtlasInfo {
    std::wstring filePath;
    int width = 0;
    int height = 0;
    std::vector<SpriteInfo> sprites;
    std::unordered_map<std::string, size_t> spriteIndex; // name -> index in sprites
    TextureContext texture = {};
};

class Atlas {
public:
    void load(const wchar_t *basePath);
    void loadTextures();
    [[nodiscard]] inline bool texturesLoaded() const { return texturesLoaded_; }
    [[nodiscard]] const SpriteInfo *findSprite(const std::string &name) const;
    [[nodiscard]] const std::vector<AtlasInfo> &atlases() const { return atlases_; }

private:
    std::vector<AtlasInfo> atlases_;
    bool texturesLoaded_ = false;
};

extern Atlas gAtlas;

}
