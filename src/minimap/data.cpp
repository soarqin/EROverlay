#include "data.hpp"

#include "defs/BonfireWarpParam.h"
#include "defs/WorldMapLegacyConvParam.h"
#include "defs/WorldMapPointParam.h"

#include "api.h"
#include "params/param.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern EROverlayAPI *api;

namespace er::minimap {

Data gData;
Atlas gAtlas;

void Atlas::load(const wchar_t *basePath) {
    std::wstring path = std::wstring(basePath) + L"\\data\\map\\atlas.json";

    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    auto size = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);
    std::vector<char> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);

    std::string content(buf.data(), size);
    auto j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.contains("atlases") || !j["atlases"].is_array()) return;

    atlases_.clear();
    for (auto &ja : j["atlases"]) {
        auto &atlas = atlases_.emplace_back();
        auto filename = ja.value("file", "");
        atlas.filePath = std::wstring(basePath) + L"\\data\\map\\" + std::wstring(filename.begin(), filename.end());
        atlas.width = ja.value("width", 0);
        atlas.height = ja.value("height", 0);

        if (ja.contains("sprites") && ja["sprites"].is_array()) {
            for (auto &js : ja["sprites"]) {
                auto &sprite = atlas.sprites.emplace_back();
                sprite.name = js.value("name", "");
                sprite.x = js.value("x", 0);
                sprite.y = js.value("y", 0);
                sprite.width = js.value("width", 0);
                sprite.height = js.value("height", 0);
                sprite.u0 = sprite.x / (float)atlas.width;
                sprite.v0 = sprite.y / (float)atlas.height;
                sprite.u1 = (sprite.x + sprite.width) / (float)atlas.width;
                sprite.v1 = (sprite.y + sprite.height) / (float)atlas.height;
                sprite.centerX = js.contains("centerX") ? js["centerX"].get<float>() : sprite.width / 2.0f;
                sprite.centerY = js.contains("centerY") ? js["centerY"].get<float>() : sprite.height / 2.0f;
                sprite.texture = nullptr;
                atlas.spriteIndex[sprite.name] = atlas.sprites.size() - 1;
            }
        }
    }
}

void Atlas::loadTextures() {
    if (texturesLoaded_) return;
    texturesLoaded_ = true;
    for (auto &atlas : atlases_) {
        if (atlas.texture.loaded) continue;
        atlas.texture = api->loadTexture(atlas.filePath.c_str());
        for (auto &sprite : atlas.sprites) {
            sprite.texture = &atlas.texture;
        }
    }
}

const SpriteInfo *Atlas::findSprite(const std::string &name) const {
    for (auto &atlas : atlases_) {
        auto it = atlas.spriteIndex.find(name);
        if (it != atlas.spriteIndex.end()) {
            return &atlas.sprites[it->second];
        }
    }
    return nullptr;
}

bool DecorationInfo::isUnlocked() const {
    if (eventFlagAddress == 0) {
        eventFlagAddress = api->resolveFlagAddress(eventFlagId, &eventFlagBits);
        if (eventFlagAddress == 0) {
            eventFlagAddress = (uintptr_t)-1;
            return false;
        }
    } else if (eventFlagAddress == (uintptr_t)-1) {
        return false;
    }
    return (*(uint8_t *)(eventFlagAddress) & eventFlagBits) != 0;
}

void Data::load() {
    std::thread th([this]() {
        const void *t = api->paramFindTable(L"WorldMapLegacyConvParam");
        if (t == nullptr) {
            fwprintf(stderr, L"Unable to find WorldMapLegacyConvParam\n");
            return;
        }
        struct ConvValue {
            uint8_t u;
            uint8_t v;
            uint8_t w;
            float x;
            float y;
            float z;
        };
        auto buildKey = [](uint8_t area, uint8_t gridX, uint8_t gridZ) {
            return (uint32_t)area * 10000 + (uint32_t)gridX * 100 + (uint32_t)gridZ;
        };
        std::unordered_map<uint32_t, ConvValue> convTable;
        paramTableIterateBegin(t, WorldMapLegacyConvParam, wmlcp) {
            auto u0 = wmlcp->srcAreaNo;
            auto u1 = wmlcp->dstAreaNo;
            if (u0 != 60 && u0 != 61 && (u1 == 60 || u1 == 61)) {
                auto key = buildKey(u0, wmlcp->srcGridXNo, wmlcp->srcGridZNo);
                convTable[key] = { u1, wmlcp->dstGridXNo, wmlcp->dstGridZNo, wmlcp->dstPosX - wmlcp->srcPosX, wmlcp->dstPosY - wmlcp->srcPosY, wmlcp->dstPosZ - wmlcp->srcPosZ };
            }
        } paramTableIterateEnd();
        paramTableIterateBegin(t, WorldMapLegacyConvParam, wmlcp) {
            auto u0 = wmlcp->srcAreaNo;
            auto u1 = wmlcp->dstAreaNo;
            if (u0 != 60 && u0 != 61 && u1 != 60 && u1 != 61) {
                auto key = buildKey(u0, wmlcp->srcGridXNo, wmlcp->srcGridZNo);
                auto key2 = buildKey(u1, wmlcp->dstGridXNo, wmlcp->dstGridZNo);
                auto it1 = convTable.find(key);
                auto it2 = convTable.find(key2);
                if (it1 == convTable.end() && it2 != convTable.end()) {
                    auto &c = it2->second;
                    convTable[key] = {c.u, c.v, c.w, c.x + wmlcp->dstPosX - wmlcp->srcPosX, c.y + wmlcp->dstPosY - wmlcp->srcPosY, c.z + wmlcp->dstPosZ - wmlcp->srcPosZ};
                }
                if (it2 == convTable.end() && it1 != convTable.end()) {
                    auto &c = it1->second;
                    convTable[key2] = {c.u, c.v, c.w, c.x + wmlcp->srcPosX - wmlcp->dstPosX, c.y + wmlcp->srcPosY - wmlcp->dstPosY, c.z + wmlcp->srcPosZ - wmlcp->dstPosZ};
                }
            }
        } paramTableIterateEnd();

        for (auto i = 0; i < 3; ++i) {
            decorations_[i].clear();
            decorationsAround_[i].clear();
            decorationsAround_[i].resize(100);
        }
        auto addDecoration = [&](uint64_t id, int32_t layer, uint16_t iconId, uint32_t eventFlagId, uint8_t areaNo, uint8_t gridXNo, uint8_t gridZNo, float posX, float posY, float posZ, float rotationDeg, DecorationSource source) {
            if (layer == -1) return;
            if (iconId == 0) return;
            char iconIdStr[16];
            sprintf(iconIdStr, "%02d", iconId);
            auto *sprite = gAtlas.findSprite(iconIdStr);
            if (sprite == nullptr) return;
            float worldX, worldZ;
            if (areaNo == 60 || areaNo == 61) {
                worldX = (gridXNo - 28) * 256.0f + 128.0f + posX;
                worldZ = (64 - gridZNo) * 256.0f + 128.0f - posZ;
                if (layer == 2) {
                    worldX -= 3035.0f;
                    worldZ -= 1864.0f;
                }
            } else {
                auto key = buildKey(areaNo, gridXNo, gridZNo);
                auto convIt = convTable.find(key);
                if (convIt != convTable.end()) {
                    auto &c = convIt->second;
                    worldX = (c.v - 28) * 256.0f + 128.0f + posX + c.x;
                    worldZ = (64 - c.w) * 256.0f + 128.0f - posZ - c.z;
                    if (layer == 2) {
                        worldX -= 3035.0f;
                        worldZ -= 1864.0f;
                    }
                } else {
                    return;
                }
            }
            auto &g = decorations_[layer].emplace_back();
            g.x = worldX;
            g.y = worldZ;
            g.id = id;
            g.eventFlagId = eventFlagId;
            g.layer = layer;
            g.source = source;
            int areaX = (int32_t)std::floor(g.x) / 1024;
            int areaY = (int32_t)std::floor(g.y) / 1024;
            g.localX = g.x - areaX * 1024;
            g.localY = g.y - areaY * 1024;
            g.rotationRad = rotationDeg == 0.f ? 0.f : static_cast<float>((rotationDeg + 180.0) * std::numbers::pi / 180.0);
            g.sortKey = areaY * 10 + areaX;
            g.sprite = sprite;
        };
        t = api->paramFindTable(L"BonfireWarpParam");
        if (t == nullptr) {
            fwprintf(stderr, L"Unable to find BonfireWarpParam\n");
            return;
        }
        paramTableIterateBegin(t, BonfireWarpParam, bwp) {
            int32_t layer = bwp->dispMask00 ? 0 : bwp->dispMask01 ? 1 : bwp->dispMask02 ? 2 : -1;
            addDecoration(entry->paramId, layer, bwp->iconId, bwp->eventflagId, bwp->areaNo, bwp->gridXNo, bwp->gridZNo, bwp->posX, bwp->posY, bwp->posZ, 0.f, DecorationSource::Grace);
        } paramTableIterateEnd();
        t = api->paramFindTable(L"WorldMapPointParam");
        if (t == nullptr) {
            fwprintf(stderr, L"Unable to find WorldMapPointParam\n");
            return;
        }
        paramTableIterateBegin(t, WorldMapPointParam, wmpp) {
            /* 80 is the icon id for the NPCs, I don't want to show them as now, because its condition fields are a bit silly */
            if (wmpp->iconId == 80) continue;
            int32_t layer = wmpp->dispMask00 ? 0 : wmpp->dispMask01 ? 1 : wmpp->dispMask02 ? 2 : -1;
            addDecoration(entry->paramId, layer, wmpp->iconId, wmpp->eventFlagId, wmpp->areaNo, wmpp->gridXNo, wmpp->gridZNo, wmpp->posX, wmpp->posY, wmpp->posZ, wmpp->angle, DecorationSource::Landmark);
        } paramTableIterateEnd();
        for (auto i = 0; i < 3; ++i) {
            auto &l = decorations_[i];
            if (l.empty()) continue;
            std::sort(l.begin(), l.end(), [](const auto &a, const auto &b) {
                if (a.sortKey == b.sortKey) {
                    if (a.source == b.source) {
                        return a.id > b.id;
                    }
                    return a.source > b.source;
                }
                return a.sortKey < b.sortKey;
            });
            auto sz = l.size();
            auto lastSortKey = -1;
            const auto *lastDecoration = &l.front();
            auto &garound = decorationsAround_[i];
            for (auto &g : l) {
                if (g.sortKey != lastSortKey) {
                    if (lastSortKey != -1) {
                        if ((size_t)lastSortKey >= garound.size()) {
                            garound.resize((size_t)(lastSortKey + 1));
                        }
                        garound[lastSortKey] = {lastDecoration, &g};
                    }
                    lastSortKey = g.sortKey;
                    lastDecoration = &g;
                }
            }
            if (lastSortKey != -1) {
                if ((size_t)lastSortKey >= garound.size()) {
                    garound.resize((size_t)(lastSortKey + 1));
                }
                garound[lastSortKey] = {lastDecoration, l.data() + sz};
            }
        }
        paramsLoaded_ = true;
    });
    th.detach();
    auto gameAddresses = api->getGameAddresses();
    csMenuManImp_ = gameAddresses.csMenuManImp;
    fieldArea_ = gameAddresses.fieldArea;
    if (api->getGameVersion() < 0x0002000100000000ULL) {
        // 1.02 ~ 1.10.1
        locationOffset_ = 0x248;
    } else {
        // 1.12+
        locationOffset_ = 0x250;
    }
}

void Data::update() {
    if (csMenuManImp_ == 0) { onGUI_ = true; return; }
    auto addr = *(uintptr_t*)csMenuManImp_;
    if (addr == 0) {
        onGUI_ = true;
        return;
    }
    onGUI_ = api->screenState() != 0 || *reinterpret_cast<uint32_t*>(addr + 0x1C) != 0;

    addr = *(uintptr_t*)(addr + 0x80);
    if (addr != 0) {
        addr = *(uintptr_t*)(addr + locationOffset_);
        if (addr != 0) {
            location_ = *(Location*)(addr + 0x24);
        }
    }
    addr = *(uintptr_t*)(fieldArea_);
    if (addr != 0) {
        addr = *(uintptr_t*)(addr + 0x20);
        if (addr != 0) {
            addr = *(uintptr_t*)(addr + 0x18);
            if (addr != 0) {
                camera_ = *(Camera*)(addr + 0x10);
            }
        }
    }
}

std::tuple<const DecorationInfo *, const DecorationInfo *> Data::decorationsAround(int32_t layer, int u, int v) const {
    size_t sortKey = (size_t)(v * 10 + u);
    auto &decorations = decorationsAround_[layer];
    if (sortKey >= decorations.size()) {
        return { nullptr, nullptr };
    }
    return decorations[sortKey];
}

}
