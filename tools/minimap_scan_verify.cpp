// Runs the exact runtime scanner on a mapped EXE without executing game code.
#define NOMINMAX
#include <chrono>
#include <cstdio>
#include <iostream>

#include <nlohmann/json.hpp>

#include "fixtures/pe_image.hpp"
#include "util/gamescan.hpp"

namespace {
using er::util::GameBindings;
using er::util::PeImage;
using er::util::gamescan::resolve;

nlohmann::json values(const GameBindings &b) {
    const auto &l = b.layout;
    return {{"submit", b.submit},
            {"flush", b.flush},
            {"enqueue", b.enqueue},
            {"cancel", b.cancel},
            {"manager", b.manager},
            {"allocator", b.allocator},
            {"retirePoolOwner", b.retirePoolOwner},
            {"menu", b.menu},
            {"gameData", b.gameData},
            {"eventFlags", b.eventFlags},
            {"field", b.field},
            {"repository", b.repository},
            {"reveal", b.reveal},
            {"layout",
             {l.menuOwnerOffset, l.ownerViewOffset, l.menuInfoOffset, l.screenStateOffset, l.graceStride, l.graceNormalOffset, l.alternateIcons, l.mapMask, l.repositoryGroups}}};
}

bool negatives(er::test::MappedPe &mapped, const PeImage &image, const GameBindings &bindings) {
    bool ok = true;
    std::string error;
    auto rejects = [&](const char *name, std::string_view reason) {
        GameBindings ignored = bindings;
        bool rejected = !resolve(image, ignored, error) && values(ignored) == values(GameBindings{}) && !ignored.layout.size && error.find(reason) != std::string::npos;
        std::fprintf(stderr, "%s: %s (%s)\n", name, rejected ? "PASS" : "FAIL", error.c_str());
        ok &= rejected;
    };
    // An unrecognized file hash must not change address/ABI resolution.
    mapped.bytes[0x30] ^= 1;
    GameBindings same{};
    bool changedHash = resolve(image, same, error) && values(same) == values(bindings);
    std::fprintf(stderr, "changed file hash: %s\n", changedHash ? "PASS" : "FAIL");
    ok &= changedHash;
    mapped.bytes[0x30] ^= 1;
    uint32_t pdataRva = 0;
    auto pdata = image.section(".pdata", pdataRva);
    uint32_t duplicate = 0;
    size_t bodySize = 0;
    using namespace er::util::gamescan::detail;
    const Rule *submitRule = nullptr;
    for (const auto &rule: RULES)
        if (rule.role == Role::Submit && rule.body && image.matches(bindings.submit, rule.body)) {
            submitRule = &rule;
            bodySize = er::util::Signature(rule.body).size();
            break;
        }
    if (!submitRule)
        return false;
    for (size_t offset = 0; offset + sizeof(RUNTIME_FUNCTION) <= pdata.size(); offset += sizeof(RUNTIME_FUNCTION)) {
        RUNTIME_FUNCTION entry{};
        std::memcpy(&entry, pdata.data() + offset, sizeof(entry));
        if (entry.EndAddress > entry.BeginAddress && entry.EndAddress - entry.BeginAddress >= bodySize && entry.BeginAddress != bindings.submit) {
            duplicate = entry.BeginAddress;
            break;
        }
    }
    if (!duplicate)
        return false;
    std::vector<uint8_t> saved(mapped.bytes.begin() + duplicate, mapped.bytes.begin() + duplicate + bodySize);
    std::memcpy(mapped.bytes.data() + duplicate, mapped.bytes.data() + bindings.submit, bodySize);
    rejects("duplicate native submit", "submit (candidates=2)");
    std::memcpy(mapped.bytes.data() + duplicate, saved.data(), saved.size());
    // Calls are relocatable, but cannot be redirected to an unrelated function.
    if (!submitRule->callCount)
        return false;
    auto call = bindings.submit + submitRule->calls[0] + 1;
    mapped.bytes[call] ^= 1;
    rejects("redirected submit dependency", "submit -> allocate");
    mapped.bytes[call] ^= 1;
    auto allocate = image.relative(call - 1, 1, 5);
    auto factory = image.relative(allocate + 137, 1, 5);
    if (!factory)
        return false;
    mapped.bytes[factory + 0x5A] ^= 1;
    rejects("redirected request initializer", "factory/initializer contract changed");
    mapped.bytes[factory + 0x5A] ^= 1;
    int32_t factoryAllocator = 0;
    if (!image.read(factory + 0x1F, factoryAllocator))
        return false;
    int32_t invalidAllocator = int32_t(bindings.submit) - int32_t(factory + 0x23);
    std::memcpy(mapped.bytes.data() + factory + 0x1F, &invalidAllocator, sizeof(invalidAllocator));
    rejects("request allocator outside data", "factory allocator is outside the data section");
    std::memcpy(mapped.bytes.data() + factory + 0x1F, &factoryAllocator, sizeof(factoryAllocator));
    // Redirect the real marker call and put E8/rel32 inside a MOV immediate.
    // A raw E8 search would accept this unrelated operand as the call proof.
    auto record = image.unique("88 54 24 44 49 8B 00 48 89 44 24 3C 44 88 4C 24 45 48 8D 54 24 38 E8 ?? ?? ?? ?? 8B F0 85 C0");
    auto markerCall = record + 22;
    auto markerAdd = image.relative(markerCall, 1, 5);
    if (!record || !markerAdd)
        return false;
    auto fake = markerCall + 11;
    std::array<uint8_t, 10> fakeInstruction{0x48, 0xB8, 0xE8};
    int32_t displacement = int32_t(markerAdd) - int32_t(fake + 2 + 5);
    std::memcpy(fakeInstruction.data() + 3, &displacement, sizeof(displacement));
    saved.assign(mapped.bytes.begin() + fake, mapped.bytes.begin() + fake + fakeInstruction.size());
    std::memcpy(mapped.bytes.data() + fake, fakeInstruction.data(), fakeInstruction.size());
    mapped.bytes[markerCall + 1] ^= 1;
    rejects("marker call hidden in an immediate", "beacon list/add call changed");
    mapped.bytes[markerCall + 1] ^= 1;
    std::memcpy(mapped.bytes.data() + fake, saved.data(), saved.size());
    // Keep the two anchors while invalidating a whole-body ABI instruction.
    auto original = mapped.bytes[bindings.submit];
    mapped.bytes[bindings.submit] ^= 1;
    rejects("changed request ABI", "submit (candidates=0)");
    mapped.bytes[bindings.submit] = original;
    uint32_t dataRva = 0;
    auto data = image.section(".data", dataRva);
    constexpr std::string_view type = ".?AVWorldMapViewModel@CS@@";
    auto hit = std::search(data.begin(), data.end(), type.begin(), type.end());
    if (hit == data.end())
        return false;
    auto typeRva = dataRva + size_t(hit - data.begin());
    mapped.bytes[typeRva] ^= 1;
    rejects("missing native RTTI", "constructor RTTI disagrees");
    mapped.bytes[typeRva] ^= 1;
    PeImage truncated(std::span<const uint8_t>(mapped.bytes.data(), 64), mapped.header.OptionalHeader.ImageBase);
    GameBindings ignored{};
    bool shortImage = !resolve(truncated, ignored, error) && !ignored.submit;
    std::fprintf(stderr, "truncated PE: %s\n", shortImage ? "PASS" : "FAIL");
    ok &= shortImage;
    constexpr uint64_t delta = 0x200000000ULL;
    if (!mapped.relocate(delta))
        return false;
    PeImage relocated(mapped.bytes, mapped.header.OptionalHeader.ImageBase + delta);
    bool aslr = resolve(relocated, same, error) && values(same) == values(bindings);
    std::fprintf(stderr, "ASLR with relocated RTTI: %s\n", aslr ? "PASS" : "FAIL");
    ok &= aslr;
    return ok;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2)
        return 1;
    er::test::MappedPe mapped;
    if (!mapped.load(argv[1]))
        return 1;
    PeImage image(mapped.bytes, mapped.header.OptionalHeader.ImageBase);
    GameBindings bindings{};
    std::string error;
    auto start = std::chrono::steady_clock::now();
    bool ok = resolve(image, bindings, error);
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!ok) {
        std::cerr << error << '\n';
        return 2;
    }
    std::cout << nlohmann::json{{"bindings", values(bindings)}, {"seconds", seconds}}.dump() << '\n';
    return argc > 2 && std::wstring_view(argv[2]) == L"--negative" && !negatives(mapped, image, bindings) ? 3 : 0;
}
