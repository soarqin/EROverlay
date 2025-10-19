#include "data.hpp"

#include "defs/BonfireWarpParam.h"
#include "defs/WorldMapLegacyConvParam.h"

#include "api.h"
#include "params/param.hpp"
#include "util/string.hpp"

#include <imgui.h>

#include <thread>
#include <cmath>

extern EROverlayAPI *api;

namespace er::minimap {

Data gData;

bool BonfireInfo::isUnlocked() const {
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
                if (convTable.find(key) == convTable.end()) {
                    if (convTable.find(key2) != convTable.end()) {
                        auto &c = convTable[key2];
                        convTable[key] = { c.u, c.v, c.w, c.x + wmlcp->dstPosX - wmlcp->srcPosX, c.y + wmlcp->dstPosY - wmlcp->srcPosY, c.z + wmlcp->dstPosZ - wmlcp->srcPosZ };
                    }
                }
                if (convTable.find(key2) == convTable.end()) {
                    if (convTable.find(key) != convTable.end()) {
                        auto &c = convTable[key];
                        convTable[key2] = { c.u, c.v, c.w, c.x + wmlcp->srcPosX - wmlcp->dstPosX, c.y + wmlcp->srcPosY - wmlcp->dstPosY, c.z + wmlcp->srcPosZ - wmlcp->dstPosZ };
                    }
                }
            }
        } paramTableIterateEnd();

        for (auto i = 0; i < 3; ++i) {
            bonfires_[i].clear();
            bonfiresAround_[i].clear();
        }
        t = api->paramFindTable(L"BonfireWarpParam");
        if (t == nullptr) {
            fwprintf(stderr, L"Unable to find BonfireWarpParam\n");
            return;
        }
        paramTableIterateBegin(t, BonfireWarpParam, bwp) {
            int32_t layer = bwp->dispMask00 ? 0 : bwp->dispMask01 ? 1 : bwp->dispMask02 ? 2 : -1;
            if (layer == -1) continue;
            auto u0 = bwp->areaNo;
            float worldX, worldZ;
            if (u0 == 60 || u0 == 61) {
                worldX = (bwp->gridXNo - 28) * 256.0f + 128.0f + bwp->posX;
                worldZ = (64 - bwp->gridZNo) * 256.0f + 128.0f - bwp->posZ;
                if (layer == 2) {
                    worldX -= 3035.0f;
                    worldZ -= 1864.0f;
                }
            } else {
                auto key = buildKey(u0, bwp->gridXNo, bwp->gridZNo);
                if (convTable.find(key) != convTable.end()) {
                    auto &c = convTable[key];
                    worldX = (c.v - 28) * 256.0f + 128.0f + bwp->posX + c.x;
                    worldZ = (64 - c.w) * 256.0f + 128.0f - bwp->posZ - c.z;
                    if (layer == 2) {
                        worldX -= 3035.0f;
                        worldZ -= 1864.0f;
                    }
                } else {
                    continue;
                }
            }
            auto &g = bonfires_[layer].emplace_back();
            g.x = worldX;
            g.y = worldZ;
            g.id = entry->paramId;
            g.textId = bwp->textId1;
            g.eventFlagId = bwp->eventflagId;
            g.layer = layer;
            int areaX = (int32_t)std::floor(g.x) / 1024;
            int areaY = (int32_t)std::floor(g.y) / 1024;
            g.localX = g.x - areaX * 1024;
            g.localY = g.y - areaY * 1024;
            g.sortKey = areaY * 10 + areaX;
        } paramTableIterateEnd();
        for (auto i = 0; i < 3; ++i) {
            auto &l = bonfires_[i];
            std::sort(l.begin(), l.end(), [](const auto &a, const auto &b) {
                if (a.sortKey == b.sortKey) {
                    return a.id < b.id;
                }
                return a.sortKey < b.sortKey;
            });
            auto sz = l.size();
            auto lastSortKey = -1;
            const auto *lastBonfire = &l.front();
            auto &garound = bonfiresAround_[i];
            for (auto &g: l) {
                if (g.sortKey != lastSortKey) {
                    if (lastSortKey != -1) {
                        garound[lastSortKey] = { lastBonfire, &g };
                    }
                    lastSortKey = g.sortKey;
                    lastBonfire = &g;
                }
            }
            if (lastSortKey != -1) {
                garound[lastSortKey] = { lastBonfire, &l.front() + sz };
            }
        }
        paramsLoaded_ = true;
    });
    th.detach();
    csMenuManImp_ = api->getGameAddresses().csMenuManImp;
    if (api->getGameVersion() < 0x0002000100000000ULL) {
        // 1.02 ~ 1.10.1
        locationOffset_ = 0x248;
    } else {
        // 1.12+
        locationOffset_ = 0x250;
    }

    toggleKey_ = api->configGetImGuiKey("minimap.toggle_key", ImGuiKey_M);
    scaleKey_ = api->configGetImGuiKey("minimap.scale_key", ImGuiKey_N);
    widthRatios_ = util::strSplitToFloatVec(api->configGetString("minimap.width_ratio", L"30%,40%"));
    heightRatios_ = util::strSplitToFloatVec(api->configGetString("minimap.height_ratio", L"30%,40%"));
    scales_ = util::strSplitToFloatVec(api->configGetString("minimap.scale", L"0.75,1"));
    auto maxCount = std::max(widthRatios_.size(), std::max(heightRatios_.size(), scales_.size()));
    if (maxCount == 0) {
        maxCount = 1;
        widthRatios_.push_back(0.3f);
        heightRatios_.push_back(0.3f);
        scales_.push_back(0.75f);
    } else {
        while (widthRatios_.size() < maxCount) {
            widthRatios_.push_back(widthRatios_.back());
        }
        while (heightRatios_.size() < maxCount) {
            heightRatios_.push_back(heightRatios_.back());
        }
        while (scales_.size() < maxCount) {
            scales_.push_back(scales_.back());
        }
    }
    if (toggleKey_ == scaleKey_) {
        scales_.push_back(0.f);
        widthRatios_.push_back(widthRatios_.back());
        heightRatios_.push_back(heightRatios_.back());
    }
    currentWidthRatio_ = widthRatios_[0];
    currentHeightRatio_ = heightRatios_[0];
    currentScale_ = scales_[0];
    alpha_ = api->configGetFloat("minimap.alpha", 0.8f);
}

void Data::update() {
    auto addr = *(uintptr_t*)csMenuManImp_;
    if (addr == 0) {
        onGUI_ = true;
        return;
    }
    onGUI_ = api->screenState() != 0 || (*reinterpret_cast<uint8_t*>(addr + 0xAC) & 1u) == 1u || (*reinterpret_cast<uint8_t*>(addr + 0xCD) & 1u) == 1u;

    addr = *(uintptr_t*)(addr + 0x80);
    if (addr == 0) return;
    addr = *(uintptr_t*)(addr + locationOffset_);
    if (addr == 0) return;
    location_ = *(Location*)(addr + 0x24);
}

void Data::updateInput() {
    if (onGUI_) return;
    if (toggleKey_ != 0 && toggleKey_ != scaleKey_ && ImGui::IsKeyChordPressed(static_cast<ImGuiKey>(toggleKey_))) {
        show_ = !show_;
    }
    if (scaleKey_ != 0 && show_ && ImGui::IsKeyChordPressed(static_cast<ImGuiKey>(scaleKey_))) {
        currentScaleIndex_ = (currentScaleIndex_ + 1) % scales_.size();
        currentScale_ = scales_[currentScaleIndex_];
        currentWidthRatio_ = widthRatios_[currentScaleIndex_];
        currentHeightRatio_ = heightRatios_[currentScaleIndex_];
    }
}

std::tuple<const BonfireInfo *, const BonfireInfo *> Data::bonfiresAround(int32_t layer, int u, int v) const {
    auto sortKey = v * 10 + u;
    auto &bonfires = bonfiresAround_[layer];
    auto it = bonfires.find(sortKey);
    if (it == bonfires.end()) {
        return { nullptr, nullptr };
    }
    return it->second;
}

}
