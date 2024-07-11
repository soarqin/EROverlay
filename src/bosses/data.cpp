#include "data.hpp"

#include "../memory.hpp"
#include "../global.hpp"
#include "../config.hpp"
#include "../steamapi.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <cstdio>

namespace er::bosses {

BossDataSet gBossDataSet;
static std::vector<bool> deadSwapTmp;
static std::vector<int> deadByRegionSwapTmp;

void BossDataSet::load(bool hasDLC) {
    nlohmann::ordered_json j;
    std::wstring name = er::gConfig.getw("boss.data_file", L"bosses.json");
    std::wstring lang = er::gConfig.getw("common.language", L"engUS");
    if (lang.empty()) {
        lang = getGameLanguage();
    }
    std::wstring filename = std::wstring(er::gModulePath) + L"\\data\\" + lang + L"\\" + name;
    std::ifstream ifs(filename.c_str());
    if (!ifs) {
        /* fallback to use english */
        lang = L"engUS";
        filename = std::wstring(er::gModulePath) + L"\\data\\" + lang + L"\\" + name;
        ifs.open(filename.c_str());
        if (!ifs) {
            fwprintf(stderr, L"Unable to open %ls\n", filename.c_str());
            return;
        }
    }
    j = nlohmann::ordered_json::parse(ifs);
    for (auto &p: j.items()) {
        if (!hasDLC && p.value()["dlc"] == 1) {
            continue;
        }
        auto regionIndex = regions_.size();
        auto &rd = regions_.emplace_back();
        rd.name = p.value()["region_name"];
        for (auto &n: p.value()["regions"]) {
            regionMap_[n.get<uint32_t>()] = (int)regionIndex;
        }
        for (auto &n: p.value()["bosses"]) {
            auto index = bosses_.size();
            auto &bd = bosses_.emplace_back();
            bd.boss = n["boss"];
            bd.place = n["place"];
            bd.flagId = n["flag_id"].get<uint32_t>();
            bd.index = index;
            bd.tip = bd.boss + ": " + bd.place;
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
    if (!flagResolved_) {
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
        for (auto &b: bosses_) {
            resolveFlag(b.flagId, b.offset, b.bits);
#if !defined(NDEBUG)
            std::wstring ws;
            ws.resize(MultiByteToWideChar(CP_UTF8, 0, b.boss.c_str(), -1, nullptr, 0));
            MultiByteToWideChar(CP_UTF8, 0, b.boss.c_str(), -1, ws.data(), (int)ws.size());
            while (!ws.empty() && ws.back() == 0) ws.pop_back();
            fwprintf(stderr, L"%ls: %zX %X\n", ws.c_str(), b.offset - flagAddress_, b.bits);
#endif
        }
        flagResolved_ = true;
    }
    int cnt = 0;
    std::fill(deadByRegionSwapTmp.begin(), deadByRegionSwapTmp.end(), 0);
    size_t sz = bosses_.size();
    for (size_t i = 0; i < sz; i++) {
        auto &b = bosses_[i];
        bool al = (*(uint8_t *)(b.offset) & b.bits) != 0;
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
    /* TODO: this is 0xE4 for older version than DLC,
     *       need to check game version */
    auto mapId = *(uint32_t *)(addr1 + (er::gGameVersion < 0x0002000200000000ULL ? 0xE4 : 0xE8));
    if (mapId == 0 || mapId == mapId_) return;
    mapId_ = mapId;
    auto ite = regionMap_.find(mapId / 1000);
    if (ite == regionMap_.end()) return;
    regionIndex_ = ite->second;
}

void BossDataSet::revive(int index) {
    if (flagAddress_ == 0) {
        return;
    }
    auto &b = bosses_[index];
    *(uint8_t *)(b.offset) &= ~b.bits;
}

void BossDataSet::resolveFlag(uint32_t flagId, uintptr_t &offset, uint8_t &bits) const {
    auto addr = MemoryHandle(eventFlagMan_).as<uintptr_t &>();
    if (addr == 0) {
        return;
    }
    auto divisor = *(uint32_t *)(addr + 0x1C);
    if (divisor == 0) {
        return;
    }
    auto category = flagId / divisor;
    auto least_significant_digits = flagId - category * divisor;
    auto current_element = MemoryHandle(addr + 0x38).as<uintptr_t &>();
    auto current_sub_element = MemoryHandle(current_element + 0x08).as<uintptr_t &>();
    while (*(uint8_t *)(current_sub_element + 0x19) == 0) {
        if (*(uint32_t *)(current_sub_element + 0x20) < category)
            current_sub_element = MemoryHandle(current_sub_element + 0x10).as<uintptr_t &>();
        else {
            current_element = current_sub_element;
            current_sub_element = MemoryHandle(current_sub_element).as<uintptr_t &>();
        }
    }
    if (current_element == current_sub_element) {
        offset = 0;
        bits = 0;
        return;
    }
    auto mystery_value = *(uint32_t *)(current_element + 0x28) - 1;
    uintptr_t calculated_pointer = 0;
    if (mystery_value != 0) {
        if (mystery_value != 1)
            calculated_pointer = MemoryHandle(current_element + 0x30).as<uintptr_t &>();
        else {
            offset = 0;
            bits = 0;
            return;
        }
    } else {
        calculated_pointer = (*(uint32_t *)(addr + 0x20)) * (*(uint32_t *)(current_element + 0x30)) + flagAddress_;
    }

    if (calculated_pointer == 0) {
        offset = 0;
        bits = 0;
        return;
    }
    auto thing = 7 - (least_significant_digits & 7);
    auto shifted = least_significant_digits >> 3;
    offset = calculated_pointer + shifted;
    bits = 1 << thing;
}

}
