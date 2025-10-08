#include "data.hpp"

#include "api.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

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
    csMenuManImp_ = api->getGameAddresses().csMenuManImp;
    if (api->getGameVersion() < 0x0002000100000000ULL) {
        // 1.02 ~ 1.10.1
        locationOffset_ = 0x248;
    } else {
        // 1.12+
        locationOffset_ = 0x250;
    }

    for (auto i = 0; i < 3; ++i) {
        bonfires_[i].clear();
        bonfiresAround_[i].clear();
    }
    auto path = std::filesystem::path(api->getModulePath());
    path /= "data";
    path /= "bonfires.json";
    std::ifstream ifs(path);
    if (!ifs) {
        fwprintf(stderr, L"Unable to open %ls\n", path.wstring().c_str());
        return;
    }
    auto json = nlohmann::json::parse(ifs);
    for (auto &p: json.items()) {
        auto& val = p.value();
        auto &g = bonfires_[val["layer"].get<int32_t>()].emplace_back();
        g.id = std::stoi(p.key());
        g.textId = val["textId"];
        g.eventFlagId = val["eventFlagId"];
        g.layer = val["layer"];
        g.x = val["worldX"];
        g.y = val["worldZ"];
        int areaX = (int32_t)std::floor(g.x) / 1024;
        int areaY = (int32_t)std::floor(g.y) / 1024;
        g.localX = g.x - areaX * 1024;
        g.localY = g.y - areaY * 1024;
        g.sortKey = areaY * 10 + areaX;
    }
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
}

void Data::update() {
    auto addr = *(uintptr_t*)csMenuManImp_;
    if (addr == 0) return;
    onGUI_ = api->screenState() != 0 || (*reinterpret_cast<uint8_t*>(addr + 0xAC) & 1u) == 1u || (*reinterpret_cast<uint8_t*>(addr + 0xCD) & 1u) == 1u;

    addr = *(uintptr_t*)(addr + 0x80);
    if (addr == 0) return;
    addr = *(uintptr_t*)(addr + locationOffset_);
    if (addr == 0) return;
    location_ = *(Location*)(addr + 0x24);
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
