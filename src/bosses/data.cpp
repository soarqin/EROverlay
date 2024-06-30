#include "../memory.hpp"
#include "data.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <cstdio>

namespace er::bosses {

BossDataSet gBossDataSet;
static std::vector<bool> deadSwapTmp;
static std::vector<int> deadByRegionSwapTmp;

void BossDataSet::load(const std::wstring &filename) {
    nlohmann::ordered_json j;
    std::ifstream ifs(filename.c_str());
    if (!ifs) {
        fwprintf(stderr, L"Unable to open %ls\n", filename.c_str());
        return;
    }
    j = nlohmann::ordered_json::parse(ifs);
    for (auto &p: j.items()) {
        auto regionIndex = regions_.size();
        auto &rd = regions_.emplace_back();
        rd.name = p.key();
        for (auto &n: p.value()["regions"]) {
            regionMap_[n.get<uint32_t>()] = (int)regionIndex;
        }
        for (auto &n: p.value()["bosses"]) {
            auto index = bosses_.size();
            auto &bd = bosses_.emplace_back();
            bd.region = p.key();
            bd.boss = n["boss"];
            bd.place = n["place"];
            bd.offset = std::strtoull(n["offset"].get<std::string>().c_str(), nullptr, 16);
            bd.bits = 1 << std::strtoul(n["bit"].get<std::string>().c_str(), nullptr, 10);
            bd.index = index;
            std::string s = n["boss_display"];
            if (s.empty()) {
                bd.displayName = bd.boss;
            } else {
                bd.displayName = s;
            }
            s = n["place_display"];
            if (!s.empty()) {
                bd.tip = bd.displayName + ": " + s;
            } else if (!bd.place.empty()) {
                bd.tip = bd.displayName + ": " + bd.place;
            }
            rd.bosses.push_back(index);
            bd.regionIndex = regionIndex;
        }
    }
    dead_.resize(bosses_.size());
    deadSwapTmp.resize(bosses_.size());
    deadByRegionSwapTmp.resize(regions_.size());
}

void BossDataSet::initMemoryAddresses() {
    {
        Signature sig("48 8B 3D ?? ?? ?? ?? 48 85 FF ?? ?? 32 C0 E9");
        auto res = sig.scan();
        if (res) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            eventFlagMan_ = addr.as<uintptr_t>();
        }
    }
    {
        Signature sig("48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0");
        auto res = sig.scan();
        if (res) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            fieldArea_ = addr.as<uintptr_t>();
        }
    }
}

void BossDataSet::update() {
    if (flagAddress_ == 0) {
        auto addr1 = MemoryHandle(eventFlagMan_).as<uintptr_t &>();
        if (addr1 == 0) {
            return;
        }
        auto addr2 = MemoryHandle(addr1 + 0x28).as<uintptr_t &>();
        if (addr2 == 0) {
            return;
        }
        flagAddress_ = addr2;
    }
    int cnt = 0;
    std::fill(deadByRegionSwapTmp.begin(), deadByRegionSwapTmp.end(), 0);
    size_t sz = bosses_.size();
    for (size_t i = 0; i < sz; i++) {
        auto &b = bosses_[i];
        bool al = (*(uint8_t *)(flagAddress_ + b.offset) & b.bits) != 0;
        deadSwapTmp[i] = al;
        if (al) {
            cnt++;
            deadByRegionSwapTmp[b.regionIndex]++;
        }
    }
    std::unique_lock lock(mutex_);
    sz = regions_.size();
    for (size_t i = 0; i < sz; i++) {
        regions_[i].count = deadByRegionSwapTmp[i];
    }
    dead_.swap(deadSwapTmp);
    count_ = cnt;
    auto addr1 = *(uintptr_t *)fieldArea_;
    if (addr1 == 0) {
        return;
    }
    auto mapId = *(uint32_t *)(addr1 + 0xE4);
    if (mapId == 0 || mapId == mapId_) return;
    mapId_ = mapId;
    auto ite = regionMap_.find(mapId < 100000 ? mapId / 10 : (mapId < 1000000 ? mapId / 100 : mapId / 1000));
    if (ite == regionMap_.end()) return;
    regionIndex_ = ite->second;
}

void BossDataSet::revive(int index) {
    if (flagAddress_ == 0) {
        return;
    }
    auto &b = bosses_[index];
    *(uint8_t *)(flagAddress_ + b.offset) &= ~b.bits;
}

}
