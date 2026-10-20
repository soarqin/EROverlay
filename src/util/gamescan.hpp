#pragma once

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "gamebindings.hpp"
#include "peimage.hpp"

namespace er::util::gamescan {
namespace detail {

#include "minimap_scan_rules.inc"

class Scanner {
public:
    Scanner(const PeImage &image, std::string &error) : image_(image), error_(error) {
        text_ = image_.section(".text", textRva_);
        data_ = image_.section(".data", dataRva_);
    }

    [[nodiscard]] bool resolve(GameBindings &result) {
        if (!loadRanges() || !findFunctions() || !extract(result) || !guardFields())
            return false;
        for (auto address: {result.manager, result.allocator, result.retirePoolOwner, result.menu, result.gameData, result.eventFlags, result.field, result.repository}) {
            if (!dataContains(address, sizeof(uintptr_t)))
                return fail("minimap global is outside the data section");
        }
        return dataContains(result.reveal, 1) || fail("minimap reveal flag is outside the data section");
    }

private:
    struct Range {
        uint32_t start, end, root, unwind;
    };

    bool fail(std::string message) {
        if (error_.empty())
            error_ = std::move(message);
        return false;
    }

    uint32_t function(Role role) const { return functions_[size_t(role)]; }

    bool dataContains(uint32_t address, size_t size) const { return address >= dataRva_ && uint64_t(address - dataRva_) + size <= data_.size(); }

    uint32_t unique(std::vector<uint32_t> values, const char *name) {
        std::ranges::sort(values);
        auto tail = std::ranges::unique(values);
        values.erase(tail.begin(), tail.end());
        if (values.size() != 1 || !values[0]) {
            std::string message = std::string("minimap signature missing or ambiguous: ") + name + " (candidates=" + std::to_string(values.size()) + ")";
            for (size_t i = 0; i < std::min(values.size(), size_t{4}); ++i)
                message += " " + std::to_string(values[i]);
            fail(std::move(message));
            return 0;
        }
        return values[0];
    }

    bool loadRanges() {
        uint32_t pdataRva = 0;
        auto pdata = image_.section(".pdata", pdataRva);
        if (text_.empty() || pdata.empty())
            return fail("minimap PE code/unwind sections are missing");
        for (size_t offset = 0; offset + sizeof(RUNTIME_FUNCTION) <= pdata.size(); offset += sizeof(RUNTIME_FUNCTION)) {
            RUNTIME_FUNCTION entry{};
            std::memcpy(&entry, pdata.data() + offset, sizeof(entry));
            if (!entry.BeginAddress && !entry.EndAddress && !entry.UnwindData)
                continue;
            if (entry.BeginAddress >= entry.EndAddress || !image_.executable(entry.BeginAddress) || !image_.executable(entry.EndAddress - 1))
                continue;
            ranges_.push_back({entry.BeginAddress, entry.EndAddress, entry.BeginAddress, entry.UnwindData});
        }
        std::ranges::sort(ranges_, {}, &Range::start);
        for (size_t i = 1; i < ranges_.size(); ++i)
            if (ranges_[i - 1].end > ranges_[i].start)
                return fail("minimap unwind ranges overlap");
        for (auto &range: ranges_) {
            uint32_t entry = range.start;
            for (unsigned depth = 0;; ++depth) {
                auto current = std::lower_bound(ranges_.begin(), ranges_.end(), entry, [](const Range &range, uint32_t address) { return range.start < address; });
                std::array<uint8_t, 4> header{};
                if (depth == 16 || current == ranges_.end() || current->start != entry || !image_.read(current->unwind, header)) {
                    entry = 0;
                    break;
                }
                if (!(header[0] & (UNW_FLAG_CHAININFO << 3)))
                    break;
                uint64_t chain = uint64_t(current->unwind) + 4 + ((header[2] + 1u) & ~1u) * 2;
                RUNTIME_FUNCTION parent{};
                if (chain > UINT32_MAX || !image_.read(uint32_t(chain), parent) || !parent.BeginAddress || !image_.executable(parent.BeginAddress)) {
                    entry = 0;
                    break;
                }
                if (parent.BeginAddress == entry)
                    break;
                entry = parent.BeginAddress;
            }
            range.root = entry;
        }
        return !ranges_.empty() || fail("minimap unwind ranges are missing");
    }

    uint32_t root(uint32_t address) const {
        auto entry = std::upper_bound(ranges_.begin(), ranges_.end(), address, [](uint32_t value, const Range &range) { return value < range.start; });
        return entry != ranges_.begin() && address < std::prev(entry)->end ? std::prev(entry)->root : 0;
    }

    std::vector<uint32_t> scan(const char *pattern, uint32_t begin, uint32_t end) const {
        std::vector<uint32_t> result;
        if (begin < textRva_ || begin >= end || uint64_t(end - textRva_) > text_.size())
            return result;
        auto pointer = const_cast<uint8_t *>(text_.data()) + begin - textRva_;
        for (auto hit: Signature(pattern).scanAll(MemoryRegion(MemoryHandle(pointer), end - begin)))
            result.push_back(begin + uint32_t(hit.as<const uint8_t *>() - pointer));
        return result;
    }

    std::vector<uint32_t> scan(const char *pattern) const { return scan(pattern, textRva_, textRva_ + uint32_t(text_.size())); }

    std::vector<uint32_t> local(Role role, std::initializer_list<const char *> patterns) const {
        std::vector<uint32_t> result;
        for (const auto &range: ranges_)
            if (range.root == function(role))
                for (auto *pattern: patterns) {
                    auto hits = scan(pattern, range.start, range.end);
                    result.insert(result.end(), hits.begin(), hits.end());
                }
        return result;
    }

    std::vector<uint32_t> required(const char *name, Role role, std::initializer_list<const char *> patterns) {
        auto hits = local(role, patterns);
        if (hits.empty())
            fail(std::string("minimap field contract changed: ") + name);
        return hits;
    }

    uint32_t value(uint32_t address, uint32_t offset, bool byte = false) {
        uint32_t result = 0;
        uint8_t small = 0;
        bool ok = uint64_t(address) + offset <= UINT32_MAX && (byte ? image_.read(address + offset, small) : image_.read(address + offset, result));
        if (!ok)
            fail("minimap instruction operand is truncated");
        return byte ? small : result;
    }

    uint32_t operand(const std::vector<uint32_t> &hits, uint32_t offset, const char *name, bool byte = false) {
        std::vector<uint32_t> values;
        for (auto hit: hits)
            values.push_back(value(hit, offset, byte));
        return unique(std::move(values), name);
    }

    bool findFunctions() {
        for (size_t index = 0; index < size_t(Role::Count); ++index) {
            auto role = Role(index);
            std::vector<uint32_t> candidates;
            for (const auto &rule: RULES) {
                if (rule.role != role)
                    continue;
                auto first = scan(rule.anchors[0].pattern), second = scan(rule.anchors[1].pattern);
                for (auto hit: first) {
                    uint32_t entry = root(hit);
                    if (role == Role::EventStorage) {
                        // This leaf has no unwind entry. Both fixed-position
                        // anchors and the entire body must match its entry.
                        if (rule.anchors[0].offset || std::find(second.begin(), second.end(), hit + rule.anchors[1].offset) == second.end())
                            continue;
                        entry = hit;
                    } else if (!entry || std::none_of(second.begin(), second.end(), [&](uint32_t address) { return root(address) == entry; }))
                        continue;
                    if (rule.body && !image_.matches(entry, rule.body))
                        continue;
                    candidates.push_back(entry);
                }
            }
            if (role == Role::CommonParam || role == Role::PieceParam) {
                auto hits = scan(role == Role::CommonParam ? "48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 45 33 C0 BA 8D 00 00 00 E8 ?? ?? ?? ??"
                                                           : "48 8B 0D ?? ?? ?? ?? 45 33 C0 41 8D 50 58 E8 ?? ?? ?? ??");
                std::erase_if(candidates, [&](uint32_t entry) { return std::none_of(hits.begin(), hits.end(), [&](uint32_t address) { return root(address) == entry; }); });
            }
            functions_[index] = unique(std::move(candidates), ROLE_NAMES[index]);
            if (!functions_[index])
                return false;
        }
        return true;
    }

    bool call(Role caller, Role callee) {
        // Call sites must belong to the already validated body; do not scan
        // raw E8 bytes in arbitrary data and treat them as instructions.
        for (const auto &rule: RULES) {
            if (rule.role != caller || !rule.body || !image_.matches(function(caller), rule.body))
                continue;
            // Generated call offsets come from the IDA instruction boundaries.
            for (uint32_t i = 0; i < rule.callCount; ++i)
                if (image_.relative(function(caller) + rule.calls[i], 1, 5) == function(callee))
                    return true;
        }
        return fail(std::string("minimap call contract changed: ") + ROLE_NAMES[size_t(caller)] + " -> " + ROLE_NAMES[size_t(callee)]);
    }

    bool referencesVtable(uint32_t entry, const char *type) const {
        auto table = image_.vtable(type);
        if (!table)
            return false;
        for (const auto &range: ranges_)
            if (range.root == entry)
                for (auto *pattern: {"48 8D ?? ?? ?? ?? ??", "4C 8D ?? ?? ?? ?? ??"})
                    for (auto hit: scan(pattern, range.start, range.end))
                        if ((valueModRM(hit) & 0xC7) == 0x05 && image_.relativeAddress(hit, 3, 7) == table)
                            return true;
        return false;
    }

    uint8_t valueModRM(uint32_t address) const {
        uint8_t value = 0;
        (void)image_.read(address + 2, value);
        return value;
    }

    bool extract(GameBindings &result) {
        result.submit = function(Role::Submit);
        result.flush = function(Role::Flush);
        result.enqueue = function(Role::Enqueue);
        result.cancel = function(Role::Cancel);
        constexpr const char *hooks[] = {"48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8", "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05 48 8B 40 58 C3 C3",
                                         "48 8B 3D ?? ?? ?? ?? 48 85 FF ?? ?? 32 C0 E9",
                                         "48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0"};
        uint32_t *globals[] = {&result.menu, &result.gameData, &result.eventFlags, &result.field};
        for (size_t i = 0; i < std::size(hooks); ++i) {
            auto hit = unique(scan(hooks[i]), "game global");
            if (!hit || !(*globals[i] = image_.relativeAddress(hit, 3, 7)))
                return fail("minimap game global reference is invalid");
        }
        auto hit = unique(local(Role::Cancel, {"48 8B 15 ?? ?? ?? ?? 48 85 D2 74 ?? 48 8B 82 40 01 00 00"}), "file manager");
        result.manager = image_.relativeAddress(hit, 3, 7);
        hit = unique(local(Role::Complete, {"48 8B 05 ?? ?? ?? ?? 0F B7 D1 4C 8B 00 49 8B 40 50 48 C1 E9 10"}), "pool owner completion");
        result.retirePoolOwner = image_.relativeAddress(hit, 3, 7);
        hit = unique(local(Role::Cleanup, {"48 8B 1D ?? ?? ?? ?? 48 8B D7 48 8B CB E8 ?? ?? ?? ?? 48 8B 57 28 48 8B 0B E8 ?? ?? ?? ??"}), "pool owner cleanup");
        if (!result.retirePoolOwner || image_.relativeAddress(hit, 3, 7) != result.retirePoolOwner || image_.relative(hit + 25, 1, 5) != function(Role::Retire))
            return fail("minimap request retirement contract changed");
        hit = unique(local(Role::AllocatorAnchor, {"4C 8B 05 ?? ?? ?? ?? 4C 89 45 B7 41 8D 54 24 08 B9 B0 00 00 00"}), "allocator first load");
        result.allocator = image_.relativeAddress(hit, 3, 7);
        hit = unique(local(Role::AllocatorAnchor, {"48 8B 0D ?? ?? ?? ?? 48 89 4D 97 4C 89 6D 8F"}), "allocator second load");
        if (!result.allocator || image_.relativeAddress(hit, 3, 7) != result.allocator)
            return fail("minimap allocator references disagree");
        // The factory allocates request objects separately from file buffers.
        // Validate its allocation/release flow and the resolved initializer.
        hit = unique(local(Role::Allocate, {"49 8B CF E8 ?? ?? ?? ?? 48 85 C0 75 12"}), "request factory call");
        auto factory = image_.relative(hit + 3, 1, 5);
        constexpr auto factoryBody =
            "40 57 48 83 EC 30 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 40 48 89 74 24 58 48 8B F1 48 8B 3D ?? ?? ?? ?? 48 89 7C 24 48 4C 8B C7 BA 08 00 00 00 B9 10 01 00 00 "
            "E8 ?? ?? ?? ?? 48 89 44 24 50 48 85 C0 74 0D 48 8B C8 E8 ?? ?? ?? ?? 48 8B D8 EB 02 33 DB 48 8B D6 48 8B CB E8 ?? ?? ?? ?? 84 C0 75 22 48 85 DB 74 19 4C 8B 03 "
            "33 D2 48 8B CB 41 FF 50 30 4C 8B 07 48 8B D3 48 8B CF 41 FF 50 68 33 C0 EB 03 48 8B C3 48 8B 5C 24 40 48 8B 74 24 58 48 83 C4 30 5F C3";
        if (!factory || !image_.matches(factory, factoryBody) || image_.relative(factory + 0x59, 1, 5) != function(Role::RequestInit))
            return fail("minimap request factory/initializer contract changed");
        if (!dataContains(image_.relativeAddress(factory + 0x1C, 3, 7), sizeof(uintptr_t)))
            return fail("minimap request factory allocator is outside the data section");
        auto common = unique(local(Role::CommonParam, {"48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 45 33 C0 BA 8D 00 00 00 E8 ?? ?? ?? ??"}), "common param");
        auto piece = unique(local(Role::PieceParam, {"48 8B 0D ?? ?? ?? ?? 45 33 C0 41 8D 50 58 E8 ?? ?? ?? ??"}), "piece param");
        result.repository = image_.relativeAddress(common, 3, 7);
        auto getter = image_.relative(common + 24, 1, 5);
        if (!result.repository || result.repository != image_.relativeAddress(piece, 3, 7) || !getter || getter != image_.relative(piece + 14, 1, 5) ||
            !image_.matches(getter, "81 FA ?? ?? ?? ?? 7D 24 48 63 D2 48 8D 04 D2 44 3B 84 C1 80 00 00 00 73 13 41 8B C0 48 8D 14 D2 48 03 D0 48 8B 84 D1 88 00 00 00 C3 33 C0 C3"))
            return fail("minimap parameter repository getter contract changed");
        auto &layout = result.layout;
        layout.size = sizeof(layout);
        layout.repositoryGroups = value(getter, 2);
        if (layout.repositoryGroups <= 141 || layout.repositoryGroups > 4096 || local(Role::CommonParam, {"48 8B 80 80 00 00 00 48 8B 90 80 00 00 00"}).empty() ||
            local(Role::PieceParam, {"48 8B 80 80 00 00 00 48 8B 90 80 00 00 00"}).empty())
            return fail("minimap parameter entry layout changed");
        if (!referencesVtable(function(Role::ViewCtor), ".?AVWorldMapViewModel@CS@@") || !referencesVtable(function(Role::GraceCtor), ".?AVWorldMapWarpPinData@CS@@"))
            return fail("minimap constructor RTTI disagrees");
        auto creates = scan("48 83 BB ?? ?? ?? ?? 00 75 ?? 4C 8B 05 ?? ?? ?? ?? 4C 89 44 24 40 BA 08 00 00 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 89 44 24 48 48 85 C0 74 ?? 48 8B 93 "
                            "?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 90 48 89 83 ?? ?? ?? ??");
        std::erase_if(creates, [&](uint32_t address) { return image_.relative(address + 57, 1, 5) != function(Role::ViewCtor); });
        hit = unique(std::move(creates), "map view creation");
        auto create = root(hit);
        layout.ownerViewOffset = value(hit, 3);
        if (!create || layout.ownerViewOffset != value(hit, 66) || !layout.ownerViewOffset || layout.ownerViewOffset > 4096 || value(hit, 28) < 0x3C8)
            return fail("minimap map view allocation contract changed");
        std::vector<uint32_t> owners;
        for (const auto &[pattern, offset]: {std::pair{"48 8B 4B ?? 48 85 C9 74 05 E8 ?? ?? ?? ?? 48 8B 4B 08 E8 ?? ?? ?? ??", 9u},
                                             std::pair{"48 8B 8B ?? ?? ?? ?? 48 85 C9 74 05 E8 ?? ?? ?? ?? 48 8B 4B 08 E8 ?? ?? ?? ??", 12u}})
            for (auto address: scan(pattern))
                if (image_.relative(address + offset, 1, 5) == create)
                    owners.push_back(value(address, 3, offset == 9));
        layout.menuOwnerOffset = unique(std::move(owners), "menu owner member");
        auto menu = scan("40 88 BB ?? ?? ?? ?? 48 8D 8B ?? ?? ?? ?? E8 ?? ?? ?? ?? 90 48 89 BB ?? ?? ?? ??");
        std::erase_if(menu, [&](uint32_t address) { return !referencesVtable(root(address), ".?AVCSMenuManImp@CS@@"); });
        hit = unique(std::move(menu), "loading data construction");
        layout.menuInfoOffset = value(hit, 10);
        auto loadingCtor = image_.relative(hit + 14, 1, 5);
        auto words = scan("66 89 41 ??", loadingCtor, loadingCtor + 49);
        auto words32 = scan("66 89 81 ?? ?? ?? ??", loadingCtor, loadingCtor + 49);
        std::vector<uint32_t> loadingOffsets;
        for (auto address: words)
            loadingOffsets.push_back(value(address, 3, true));
        for (auto address: words32)
            loadingOffsets.push_back(value(address, 3));
        layout.screenStateOffset = layout.menuInfoOffset + unique(std::move(loadingOffsets), "loading state member");
        layout.graceStride = operand(local(Role::ViewCtor, {"48 69 D7 ?? ?? ?? ?? 48 03 53 08", "48 69 D6 ?? ?? ?? ?? 48 03 57 08"}), 3, "grace stride multiply");
        if (layout.graceStride != operand(local(Role::ViewCtor, {"48 81 43 10 ?? ?? ?? ??", "48 81 47 10 ?? ?? ?? ??"}), 4, "grace stride increment"))
            return fail("minimap grace strides disagree");
        auto normal = local(Role::GraceCtor, {"C6 86 ?? ?? ?? ?? 00 C7 86 ?? ?? ?? ?? FF FF FF FF C6 86 ?? ?? ?? ?? 01"});
        std::vector<uint32_t> normalOffsets;
        for (auto address: normal)
            normalOffsets.push_back(value(address, 19));
        for (auto address: local(Role::GraceCtor, {"41 C6 86 ?? ?? ?? ?? 00 41 C7 86 ?? ?? ?? ?? FF FF FF FF 41 C6 86 ?? ?? ?? ?? 01"}))
            normalOffsets.push_back(value(address, 22));
        layout.graceNormalOffset = unique(std::move(normalOffsets), "grace normal member");
        if (layout.graceNormalOffset + 8 != layout.graceStride || layout.graceStride < 0x2D0 || layout.graceStride > 4096)
            return fail("minimap grace terminal field disagrees");
        bool altNormal = !local(Role::GraceCtor, {"89 86 C8 02 00 00"}).empty();
        bool altForbidden = !local(Role::GraceCtor, {"89 86 08 03 00 00"}).empty();
        if (altNormal != altForbidden)
            return fail("minimap alternate grace icon fields disagree");
        layout.alternateIcons = altNormal;
        if (local(Role::ViewCtor, {"BA 3C 00 00 00 44 8D 4A 04 44 8D 42 E0"}).empty())
            return fail("minimap surface converter is missing");
        layout.mapMask = local(Role::ViewCtor, {"BA 3D 00 00 00 44 8D 4A 03 44 8D 42 DF"}).empty() ? 3 : 7;
        hit = unique(local(Role::ViewCtor, {"0F B6 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 07", "0F B6 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 03"}), "map reveal");
        result.reveal = image_.relativeAddress(hit, 3, 7);
        if (image_.relative(hit + 7, 1, 5) != function(Role::PieceMasks))
            return fail("minimap reveal/piece-mask call disagrees");
        if (!call(Role::Submit, Role::Allocate) || !call(Role::Allocate, Role::PendingInsert) || !call(Role::Allocate, Role::Retire) || !call(Role::Flush, Role::Enqueue) ||
            !call(Role::Complete, Role::Cleanup) || !call(Role::Convert, Role::LegacyLookup))
            return false;
        bool eventReaderCalled = false;
        for (const auto &rule: RULES)
            if (rule.role == Role::PieceMasks && rule.body && image_.matches(function(Role::PieceMasks), rule.body))
                for (uint32_t i = 0; i < rule.callCount; ++i) {
                    auto reader = image_.relative(function(Role::PieceMasks) + rule.calls[i], 1, 5);
                    if (reader && image_.matches(reader, "48 83 EC 28 8B 12 85 D2 74 0F E8 ?? ?? ?? ?? 85 C0 0F 95 C0 48 83 C4 28 C3 32 C0 48 83 C4 28 C3") &&
                        image_.relative(reader + 10, 1, 5) == function(Role::EventStorage))
                        eventReaderCalled = true;
                }
        if (!eventReaderCalled)
            return fail("minimap event-storage reader/caller contract changed");
        return error_.empty();
    }

    bool guardFields() {
        required("player raw/map", Role::Player, {"89 47 14 C7 47 24 FF FF FF FF"});
        required("player XY", Role::Player, {"48 89 47 28 89 4F 24", "48 89 47 28 89 5F 24"});
        required("player underground", Role::Player, {"80 7F 17 0C 0F 94 C0 88 47 30"});
        required("player angle", Role::Player, {"F3 0F 11 4F 34", "F3 0F 11 47 34"});
        auto converted = required("death XY", Role::Death, {"48 8D 97 AC 00 00 00 E8 ?? ?? ?? ??"});
        if (std::none_of(converted.begin(), converted.end(), [&](uint32_t address) { return image_.relative(address + 7, 1, 5) == function(Role::Convert); }))
            return fail("minimap death converter call changed");
        required("death validity/map", Role::Death, {"88 97 A9 00 00 00 44 89 BF B4 00 00 00", "88 8F A9 00 00 00 44 89 BF B4 00 00 00"});
        required("death home map", Role::Death, {"8B 91 78 02 00 00"});
        required("beacon slots/capacity", Role::MarkerSaveBind, {"48 89 51 08 48 89 59 10"});
        required("beacon active count", Role::MarkerSaveBind, {"49 83 7E 40 00"});
        required("beacon record stride", Role::MarkerAdd, {"48 C1 E0 04", "48 C1 FB 04"});
        required("beacon icon", Role::MarkerNumber, {"48 63 4F 18 48 03 C9 48 8B 47 10 0F B6 54 C8 0D"});
        required("beacon slot number", Role::MarkerNumber, {"44 8B 47 18 41 FF C0"});
        // Include the record and its address argument so an E8 inside an
        // unrelated instruction operand cannot satisfy this call contract.
        auto listAdds = required("beacon record", Role::MarkerListAdd, {"88 54 24 44 49 8B 00 48 89 44 24 3C 44 88 4C 24 45 48 8D 54 24 38 E8 ?? ?? ?? ?? 8B F0 85 C0"});
        if (std::none_of(listAdds.begin(), listAdds.end(), [&](uint32_t address) { return image_.relative(address + 22, 1, 5) == function(Role::MarkerAdd); }))
            return fail("minimap beacon list/add call changed");
        required("beacon save binding", Role::MarkerBind, {"48 89 51 38 48 83 C1 08 E8 ?? ?? ?? ??"});
        auto binds = required("beacon view binding", Role::ViewCtor, {"49 8D 8F 00 03 00 00 E8 ?? ?? ?? ??", "49 8D 8E 00 03 00 00 E8 ?? ?? ?? ??"});
        if (std::none_of(binds.begin(), binds.end(), [&](uint32_t address) { return image_.relative(address + 7, 1, 5) == function(Role::MarkerBind); }))
            return fail("minimap beacon view/binder call changed");
        auto limits = required("beacon limit", Role::MarkerPlace, {"FF C0 39 05 ?? ?? ?? ??"});
        std::vector<uint32_t> addresses;
        for (auto address: limits)
            addresses.push_back(image_.relativeAddress(address + 2, 2, 6));
        if (value(unique(std::move(addresses), "beacon limit global"), 0) != 5)
            return fail("minimap beacon placement limit changed");
        required("beacon numbered icon", Role::MarkerPlace, {"41 B9 01 00 00 00"});
        auto saves = required("beacon reserved slots", Role::MarkerSaveCtor, {"41 B8 0A 00 00 00 48 8D 8F ?? ?? ?? ?? E8 ?? ?? ?? ??"});
        if (std::any_of(saves.begin(), saves.end(), [&](uint32_t address) { return image_.relative(address + 13, 1, 5) != function(Role::MarkerSaveBind); }))
            return fail("minimap beacon save/binder call changed");
        required("converter count", Role::Death, {"48 3B 9F 80 02 00 00", "48 39 9F 80 02 00 00"});
        required("converter array", Role::Death, {"4C 8D A7 F8 00 00 00"});
        required("converter stride", Role::Death, {"48 83 C6 30"});
        required("grace identity", Role::GraceCtor, {"41 0F 10 06 0F 11 86 38 02 00 00", "41 0F 10 07 41 0F 11 86 38 02 00 00"});
        required("grace base icon", Role::GraceCtor, {"89 86 48 02 00 00", "41 89 86 48 02 00 00"});
        required("grace forbidden icon", Role::GraceCtor, {"89 86 88 02 00 00", "41 89 86 88 02 00 00"});
        return error_.empty();
    }

    const PeImage &image_;
    std::string &error_;
    std::span<const uint8_t> text_;
    std::span<const uint8_t> data_;
    uint32_t textRva_ = 0, dataRva_ = 0;
    std::vector<Range> ranges_;
    std::array<uint32_t, size_t(Role::Count)> functions_{};
};

} // namespace detail

[[nodiscard]] inline bool resolve(const PeImage &image, GameBindings &result, std::string &error) {
    result = {};
    error.clear();
    GameBindings found{};
    if (!detail::Scanner(image, error).resolve(found))
        return false;
    result = found;
    return true;
}

} // namespace er::util::gamescan
