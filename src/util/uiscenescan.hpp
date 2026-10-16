#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace er::util::uiscene {

template<size_t N>
struct Pattern {
    std::array<uint8_t, N> bytes{}, mask{};
    size_t size = 0;
};

template<size_t N>
consteval auto pattern(const char (&text)[N]) {
    Pattern<N / 3 + 1> result;
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
    for (size_t i = 0; i + 1 < N;) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        bool known = text[i] != '?';
        result.bytes[result.size] = known ? uint8_t(digit(text[i]) * 16 + digit(text[i + 1])) : 0;
        result.mask[result.size++] = known ? 255 : 0;
        i += 2;
    }
    return result;
}

#include "uiscene_patterns.inc"

// An image view uses RVAs throughout. The same resolver runs against the live
// module and a PE mapped from disk by the offline verifier.
class Image {
public:
    explicit Image(std::span<const uint8_t> bytes, uintptr_t base) : bytes_(bytes), base_(base) {}

    template<typename T>
    [[nodiscard]] bool read(uint32_t rva, T &value) const {
        if (rva > bytes_.size() || sizeof(T) > bytes_.size() - rva)
            return false;
        std::memcpy(&value, bytes_.data() + rva, sizeof(T));
        return true;
    }

    [[nodiscard]] std::span<const uint8_t> section(const char *name, uint32_t &rva) const {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if (!read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || !read(uint32_t(dos.e_lfanew), nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.SizeOfImage > bytes_.size() ||
            nt.FileHeader.NumberOfSections > 96)
            return {};
        uint64_t table = uint32_t(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
        for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER entry{};
            if (table + uint64_t(i) * sizeof(entry) > UINT32_MAX || !read(uint32_t(table + uint64_t(i) * sizeof(entry)), entry))
                return {};
            if (std::strncmp(reinterpret_cast<const char *>(entry.Name), name, sizeof(entry.Name)) != 0)
                continue;
            rva = entry.VirtualAddress;
            size_t size = entry.Misc.VirtualSize;
            if (!size || rva > bytes_.size() || size > bytes_.size() - rva)
                return {};
            return bytes_.subspan(rva, size);
        }
        return {};
    }

    [[nodiscard]] bool executable(uint32_t rva) const {
        uint32_t start = 0;
        auto text = section(".text", start);
        return rva >= start && rva - start < text.size();
    }

    template<size_t N>
    [[nodiscard]] bool matches(uint32_t rva, const Pattern<N> &query) const {
        if (rva > bytes_.size() || query.size > bytes_.size() - rva)
            return false;
        for (size_t i = 0; i < query.size; ++i)
            if (query.mask[i] && bytes_[rva + i] != query.bytes[i])
                return false;
        return true;
    }

    template<size_t N>
    [[nodiscard]] uint32_t unique(const Pattern<N> &query) const {
        uint32_t start = 0;
        auto text = section(".text", start);
        size_t anchor = 0, length = 0;
        for (size_t i = 0; i < query.size;) {
            if (!query.mask[i]) {
                ++i;
                continue;
            }
            size_t begin = i;
            while (i < query.size && query.mask[i])
                ++i;
            if (i - begin > length) {
                anchor = begin;
                length = i - begin;
            }
        }
        if (length < 4 || text.size() < query.size)
            return 0;
        uint32_t result = 0;
        auto cursor = text.begin();
        while (cursor != text.end()) {
            cursor = std::search(cursor, text.end(), query.bytes.begin() + anchor, query.bytes.begin() + anchor + length);
            if (cursor == text.end())
                break;
            size_t offset = size_t(cursor - text.begin());
            if (offset >= anchor && offset - anchor <= text.size() - query.size && matches(start + uint32_t(offset - anchor), query)) {
                if (result)
                    return 0;
                result = start + uint32_t(offset - anchor);
            }
            ++cursor;
        }
        return result;
    }

    [[nodiscard]] uint32_t relative(uint32_t instruction, uint32_t displacement, uint32_t size) const {
        int32_t delta = 0;
        if (!read(instruction + displacement, delta))
            return 0;
        int64_t target = int64_t(instruction) + size + delta;
        return target > 0 && target <= UINT32_MAX && executable(uint32_t(target)) ? uint32_t(target) : 0;
    }

    // MSVC x64 RTTI: TypeDescriptor -> COL (image-relative) -> vtable[-1].
    [[nodiscard]] uint32_t vtable(std::string_view typeName) const {
        uint32_t dataRva = 0, rdataRva = 0;
        auto data = section(".data", dataRva);
        auto rdata = section(".rdata", rdataRva);
        auto hit = std::search(data.begin(), data.end(), typeName.begin(), typeName.end());
        if (hit == data.end() || size_t(hit - data.begin()) < 16 || size_t(hit - data.begin()) + typeName.size() >= data.size() || hit[typeName.size()] != 0)
            return 0;
        if (std::search(hit + typeName.size(), data.end(), typeName.begin(), typeName.end()) != data.end())
            return 0;
        uint32_t descriptor = dataRva + uint32_t(hit - data.begin()) - 16;
        uint32_t result = 0;
        for (size_t offset = 0; offset + 24 <= rdata.size(); offset += 4) {
            std::array<uint32_t, 6> col{};
            std::memcpy(col.data(), rdata.data() + offset, sizeof(col));
            if (col[0] != 1 || col[1] != 0 || col[3] != descriptor || col[5] != rdataRva + offset)
                continue;
            uintptr_t locator = base_ + rdataRva + offset;
            for (size_t p = 0; p + 16 <= rdata.size(); p += 8) {
                uintptr_t pointer = 0;
                std::memcpy(&pointer, rdata.data() + p, sizeof(pointer));
                if (pointer != locator)
                    continue;
                if (result)
                    return 0;
                result = rdataRva + uint32_t(p) + 8;
            }
        }
        return result;
    }

private:
    std::span<const uint8_t> bytes_;
    uintptr_t base_;
};

struct Addresses {
    uint32_t submit = 0, enqueue = 0, consume = 0, pixelFormat = 0;
    uint32_t renderViewVtable = 0, textureVtable = 0;
    uint32_t swapChainsOffset = 0, viewportOffset = 0, contextEnvironmentOffset = 0;
    uint32_t backendOffset = 0, queueOffset = 0;
    uint32_t composite = 0, renderPostOffset = 0;
    uint32_t queueIndexOffset = 0, queuesOffset = 0;
};

[[nodiscard]] inline bool resolve(const Image &image, Addresses &result, const char *&error) {
    result = {};
    auto fail = [&](const char *message) {
        error = message;
        return false;
    };
    uint32_t submit = image.unique(QUEUE_SUBMIT);
    if (!submit)
        return fail("draw queue submit signature is missing or ambiguous");
    uint32_t enqueue = image.relative(submit + 0x3a, 1, 5);
    if (enqueue != image.relative(submit + 0x57, 1, 5) || !image.matches(enqueue, QUEUE_CALLBACK))
        return fail("draw queue callback contract changed");
    uint32_t sort = image.relative(submit + 0x5f, 1, 5);
    uint32_t execute = image.relative(submit + 0x84, 1, 5);
    if (!image.matches(sort, QUEUE_SORT) || !image.matches(execute, QUEUE_EXECUTE))
        return fail("draw queue sort/execute contract changed");
    uint32_t consume = image.relative(execute + 0xd, 1, 5);
    if (!image.matches(consume, QUEUE_CONSUME))
        return fail("draw queue flush-before-callback contract changed");
    uint32_t begin = image.relative(submit + 0x30, 3, 7);
    if (!image.matches(begin, BEGIN_CALLBACK))
        return fail("draw scene begin callback changed");
    uint32_t setup = image.relative(begin + 0x4a, 1, 5);
    if (!image.matches(setup, SCENE_SETUP))
        return fail("draw scene layout changed");
    uint32_t factory = image.unique(RTV_CREATE);
    if (!factory)
        return fail("render target view format contract changed");
    Addresses found{};
    found.composite = image.unique(UI_COMPOSITE);
    uint32_t postAgain = 0, compositeSwap = 0;
    if (!found.composite || !image.read(found.composite + 0x114, found.renderPostOffset) || !image.read(found.composite + 0x248, postAgain) ||
        !image.read(found.composite + 0x46, compositeSwap) || found.renderPostOffset != postAgain || !found.renderPostOffset || found.renderPostOffset > 4096)
        return fail("native UI composition/scene target contract changed");
    uint32_t nativeEnqueue = image.unique(NATIVE_ENQUEUE), indexAgain = 0;
    if (!nativeEnqueue || image.relative(found.composite + 0x4b0, 1, 5) != nativeEnqueue || !image.read(nativeEnqueue + 0x23, found.queueIndexOffset) ||
        !image.read(nativeEnqueue + 0x4e, indexAgain) || !image.read(nativeEnqueue + 0x5e, found.queuesOffset) || found.queueIndexOffset != indexAgain || !found.queueIndexOffset ||
        found.queueIndexOffset > 4096 || !found.queuesOffset || found.queuesOffset > 4096)
        return fail("native UI composition queue layout changed");
    found.submit = submit;
    found.enqueue = enqueue;
    found.consume = consume;
    found.pixelFormat = image.relative(factory + 0x64, 1, 5);
    found.renderViewVtable = image.vtable(".?AVCGRenderTargetView@DLCG3@@");
    found.textureVtable = image.vtable(".?AVCGTexture2D@DLCG3@@");
    if (!found.pixelFormat || !found.renderViewVtable || !found.textureVtable)
        return fail("render target RTTI/format converter is missing");
    if (!image.read(setup + 0x4c, found.swapChainsOffset) || !image.read(setup + 0xfe, found.viewportOffset) || !image.read(consume + 0x25, found.contextEnvironmentOffset))
        return fail("draw scene operands are truncated");
    if (compositeSwap != found.swapChainsOffset)
        return fail("native UI compositor uses a different swap-chain layout");
    // The consumer's batched flush forwards through two small engine thunks.
    uint32_t flush = image.relative(consume + 0xb2, 1, 5);
    constexpr auto forward = pattern("48 8B 49 ?? E9 ?? ?? ?? ??");
    if (!image.matches(flush, forward))
        return fail("draw queue backend forwarding changed");
    uint8_t backend = 0;
    if (!image.read(flush + 3, backend))
        return fail("draw queue backend operand is truncated");
    uint32_t batch = image.relative(flush + 4, 1, 5);
    for (int i = 0; i < 4; ++i) {
        uint8_t opcode = 0;
        if (!image.read(batch, opcode) || opcode != 0xe9)
            break;
        batch = image.relative(batch, 1, 5);
    }
    constexpr auto queueLoad = pattern("48 8B 41 ?? 48 89 45 C0 41 8B D8");
    constexpr auto submitLists = pattern("48 8B 07 4D 8B C7 8B D3 48 8B CF FF 50 50");
    uint8_t queue = 0;
    if (!image.matches(batch + 0x3d, queueLoad) || !image.matches(batch + 0x229, submitLists) || !image.read(batch + 0x40, queue))
        return fail("native ExecuteCommandLists contract changed");
    found.backendOffset = backend;
    found.queueOffset = queue;
    if (!found.swapChainsOffset || found.swapChainsOffset > 4096 || !found.viewportOffset || found.viewportOffset > 4096 || found.contextEnvironmentOffset > 16384 || !backend ||
        !queue)
        return fail("draw scene offsets are out of range");
    result = found;
    error = nullptr;
    return true;
}

} // namespace er::util::uiscene
