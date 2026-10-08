// Exercise the production parser and revival path against controlled memory.
// This executable does not attach to the game or touch its files.
#define NOMINMAX

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "../src/boss/data.cpp"
#include "util/eventflag.hpp"

namespace {
using Json = nlohmann::ordered_json;
using er::bosses::BossData;
using er::bosses::BossDataSet;
using er::bosses::RenderState;

std::wstring modulePath = L"build\\native-checks\\boss-revival";
std::map<uint32_t, std::array<uint8_t, 125>> storage;
alignas(uintptr_t) std::array<uint8_t, 0xB0> gameData{};
uintptr_t gameDataPointer = reinterpret_cast<uintptr_t>(gameData.data());
uintptr_t managerPointer = 1;
uintptr_t fieldPointer = 0;
uint32_t missingFlag = 0, switchManagerAtFlag = 0;
bool allowRevive = true;
int screen = 0;
std::vector<uint32_t> resolved;

bool check(bool result, const char *name) {
    if (!result)
        std::printf("FAIL: %s\n", name);
    return result;
}

uint8_t mask(uint32_t flag) { return static_cast<uint8_t>(0x80u >> (flag % 8)); }

void setFlag(uint32_t flag, bool value) {
    auto &byte = storage[flag / 1000][flag % 1000 / 8];
    if (value)
        byte |= mask(flag);
    else
        byte &= static_cast<uint8_t>(~mask(flag));
}

uintptr_t resolve(uint32_t flag, uint8_t *bits) {
    resolved.push_back(flag);
    *bits = 0;
    if (flag == missingFlag)
        return 0;
    if (flag == switchManagerAtFlag)
        managerPointer = 2;
    *bits = mask(flag);
    return reinterpret_cast<uintptr_t>(&storage[flag / 1000][flag % 1000 / 8]);
}

GameAddresses addresses() {
    return {0, reinterpret_cast<uintptr_t>(&gameDataPointer), reinterpret_cast<uintptr_t>(&managerPointer), reinterpret_cast<uintptr_t>(&fieldPointer), 0};
}

EROverlayAPI fakeAPI{};

void initAPI() {
    fakeAPI.getGameVersion = [] { return uint64_t{0x0002000700010000}; };
    fakeAPI.getModulePath = [] { return modulePath.c_str(); };
    fakeAPI.getGameLanguage = [] { return L"engus"; };
    fakeAPI.getGameAddresses = addresses;
    fakeAPI.screenState = [] { return screen; };
    fakeAPI.configEnabled = [](const char *name) { return std::string(name) == "boss.allow_revive" && allowRevive; };
    fakeAPI.configGetInt = [](const char *, int value) { return value; };
    fakeAPI.configGetVirtualKey = [](const char *, int value) { return value; };
    fakeAPI.configGetString = [](const char *, const wchar_t *value) { return value; };
    fakeAPI.resolveFlagAddress = resolve;
    *reinterpret_cast<int *>(gameData.data() + er::bosses::offsets::kInGameTime) = 100;
}

void fixture(const Json &reviveFlags, const Json &primary = 10000800) {
    std::filesystem::create_directories(modulePath + L"\\data\\engus");
    Json boss = {{"boss", "Fixture"}, {"place", "Arena"}, {"flag_id", primary}, {"revive_flags", reviveFlags}};
    Json region = {{"region_name", "Region"}, {"regions", {10000}}, {"bosses", {boss}}};
    std::ofstream(std::filesystem::path(modulePath) / L"data/engus/bosses.json") << Json::array({region}).dump();
}

bool parserCheck() {
    uint32_t flag = 0;
    for (const auto &bad: Json::array({0, -1, 1.5, true, "100", 4294967296ULL}))
        if (!check(!er::bosses::readFlagId(bad, flag), "reject invalid flag ID"))
            return false;
    if (!check(er::bosses::readFlagId(Json(4294967295ULL), flag) && flag == UINT32_MAX, "accept uint32 max"))
        return false;
    for (const auto &bad:
         Json::array({nullptr, 7, Json::object(), Json::array({-1}), Json::array({0}), Json::array({1.5}), Json::array({Json{{"flag_id", 10000801}, {"value", 1}}}),
                      Json::array({10000801, Json{{"flag_id", 10000801}, {"value", true}}}), Json::array({Json{{"flag_id", 10000800}, {"value", true}}})})) {
        BossData boss;
        boss.flagId = 10000800;
        if (!check(!er::bosses::loadReviveFlags(Json{{"revive_flags", bad}}, boss), "reject malformed revival recipe"))
            return false;
    }
    BossData boss;
    boss.flagId = 10000800;
    if (!check(er::bosses::loadReviveFlags(Json::object(), boss) && boss.reviveFlags.empty(), "legacy primary-only data"))
        return false;
    Json valid = {{"revive_flags", {10000800, 10000801, 10000801, Json{{"flag_id", 9411}, {"value", true}}}}};
    return check(er::bosses::loadReviveFlags(valid, boss) && boss.reviveFlags.size() == 2 && !boss.reviveFlags[0].value && boss.reviveFlags[1].value,
                 "deduplicate equal targets and preserve set-to-one target");
}

template<typename T>
void put(std::array<uint8_t, 0x80> &bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

bool resolverCheck() {
    std::array<uint8_t, 0x80> manager{}, sentinel{}, node{};
    std::array<uint8_t, 250> packed{}, direct{};
    auto ptr = [](auto &bytes) { return reinterpret_cast<uintptr_t>(bytes.data()); };
    put(manager, 0x1C, uint32_t{1000});
    put(manager, 0x20, uint32_t{125});
    put(manager, 0x28, ptr(packed));
    put(manager, 0x38, ptr(sentinel));
    put(sentinel, 0x08, ptr(node));
    sentinel[0x19] = 1;
    put(node, 0x00, ptr(sentinel));
    put(node, 0x10, ptr(sentinel));
    put(node, 0x20, uint32_t{10000});
    put(node, 0x28, uint32_t{1});
    put(node, 0x30, uint32_t{1});
    uint8_t bits = 0;
    auto resolveFlag = [&](uint32_t id) { return er::util::resolveEventFlag(ptr(manager), id, &bits); };
    if (!check(resolveFlag(10000800) == ptr(packed) + 225 && bits == 0x80, "packed category index and MSB-first bits"))
        return false;
    if (!check(resolveFlag(10000807) == ptr(packed) + 225 && bits == 0x01, "last bit in byte"))
        return false;
    if (!check(resolveFlag(10001800) == 0 && bits == 0 && resolveFlag(9999800) == 0 && bits == 0, "exact category; reject adjacent category"))
        return false;
    put(node, 0x28, uint32_t{2});
    put(node, 0x30, ptr(direct));
    put(manager, 0x28, uintptr_t{0});
    if (!check(resolveFlag(10000800) == ptr(direct) + 100 && bits == 0x80, "direct category needs no packed buffer"))
        return false;
    put(node, 0x28, uint32_t{3});
    if (!check(resolveFlag(10000800) == 0 && bits == 0, "unsupported storage type"))
        return false;
    put(node, 0x28, uint32_t{2});
    put(node, 0x00, ptr(node));
    if (!check(resolveFlag(10000800) == 0 && bits == 0, "bounded corrupt tree traversal"))
        return false;
    return check(er::util::resolveEventFlag(0, 10000800, &bits) == 0 && bits == 0 && er::util::resolveEventFlag(ptr(manager), 10000800, nullptr) == 0,
                 "unavailable manager and null output");
}

bool revivalCheck() {
    fixture(Json::array({10000801, 10000802, Json{{"flag_id", 9411}, {"value", true}}}));
    BossDataSet data;
    data.load(true);
    data.initMemoryAddresses();
    storage.clear();
    setFlag(10000800, true);
    setFlag(10000801, true);
    setFlag(10000802, true);
    setFlag(10000803, true);
    setFlag(9411, false);
    data.update();
    RenderState state;
    data.fillRenderState(state);
    if (!check(state.count == 1, "primary flag counts defeated boss"))
        return false;
    auto before = storage;
    missingFlag = 9411;
    if (!check(!data.revive(0) && storage == before, "unavailable extra flag causes no partial writes"))
        return false;
    missingFlag = 0;
    switchManagerAtFlag = 10000800;
    managerPointer = 1;
    if (!check(!data.revive(0) && storage == before, "save-manager change aborts before writes"))
        return false;
    switchManagerAtFlag = 0;
    managerPointer = 1;
    screen = 1;
    if (!check(!data.revive(0) && storage == before, "loading screen aborts revival"))
        return false;
    screen = 0;
    if (!check(!data.revive(-1) && !data.revive(1) && storage == before, "invalid index writes nothing"))
        return false;
    resolved.clear();
    if (!check(data.revive(0), "successful multi-flag revival"))
        return false;
    data.fillRenderState(state);
    if (!check(storage[10000][100] == 0x10 && (storage[9][51] & mask(9411)) != 0 && state.count == 0 && state.dead[0] == 0 && state.regionCounts[0] == 0,
               "clear extra flags, set inverse flag, preserve adjacent bit, refresh counts"))
        return false;
    if (!check(resolved.back() == 10000800, "preflight primary flag last"))
        return false;
    before = storage;
    if (!check(!data.revive(0) && storage == before, "live boss is not rewritten"))
        return false;
    data.update();
    data.fillRenderState(state);
    if (!check(state.count == 0, "extra flags do not contribute to counts"))
        return false;
    // Replace the mocked save allocation after tracking has cached addresses.
    auto previousSave = std::move(storage);
    storage.clear();
    setFlag(10000800, true);
    setFlag(10000801, true);
    if (!check(data.revive(0) && previousSave[10000][100] == 0x10 && storage[10000][100] == 0, "resolve fresh addresses after save switch"))
        return false;
    missingFlag = 10000800;
    data.update();
    missingFlag = 0;
    setFlag(10000800, true);
    data.update();
    data.fillRenderState(state);
    if (!check(state.count == 1, "retry unavailable primary address"))
        return false;
    allowRevive = false;
    BossDataSet disabled;
    disabled.load(true);
    disabled.initMemoryAddresses();
    before = storage;
    if (!check(!disabled.revive(0) && storage == before, "allow_revive=false enforced in data layer"))
        return false;
    allowRevive = true;
    for (const auto &primary: Json::array({nullptr, 0, -1, 1.5, true, "10000800", 4294967296ULL})) {
        fixture(Json::array(), primary);
        BossDataSet invalidPrimary;
        invalidPrimary.load(true);
        invalidPrimary.initMemoryAddresses();
        if (!check(invalidPrimary.total() == 1 && invalidPrimary.bosses()[0].flagId == 0 && !invalidPrimary.revive(0) && storage == before,
                   "invalid primary ID is rejected without conversion or writes"))
            return false;
    }
    fixture(Json::array({10000801, Json{{"flag_id", 10000801}, {"value", true}}}));
    BossDataSet invalid;
    invalid.load(true);
    invalid.initMemoryAddresses();
    return check(!invalid.revive(0) && storage == before, "malformed recipe disables revival");
}

bool distributedDataCheck() {
    modulePath = L"src\\boss";
    BossDataSet data;
    data.load(true);
    data.initMemoryAddresses();
    if (!check(data.total() == 207, "all distributed boss entries loaded"))
        return false;
    for (const auto &boss: data.bosses()) {
        storage.clear();
        setFlag(boss.flagId, true);
        for (const auto &flag: boss.reviveFlags)
            setFlag(flag.flagId, !flag.value);
        auto expected = storage;
        for (const auto &flag: boss.reviveFlags)
            setFlag(flag.flagId, flag.value);
        setFlag(boss.flagId, false);
        expected.swap(storage);
        if (!check(data.revive(static_cast<int>(boss.index)) && storage == expected, "distributed revival recipe executes exactly its flags"))
            return false;
    }
    BossDataSet base;
    base.load(false);
    return check(base.total() == 165, "DLC filtering preserved");
}
} // namespace

EROverlayAPI *api = &fakeAPI;

int main() {
    initAPI();
    if (!parserCheck() || !resolverCheck() || !revivalCheck() || !distributedDataCheck())
        return 1;
    std::puts("PASS: production parser, native flag resolver, preflight failures, save changes, bit preservation, counting and all 207 revival recipes.");
}
