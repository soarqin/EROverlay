// Check the shared scanner against guarded memory and known match locations.
#define NOMINMAX
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "util/signature.hpp"

namespace {
using namespace er::util;

bool expect(bool value, const char *name) {
    if (!value)
        std::printf("FAIL: %s\n", name);
    return value;
}

bool boundaries(uint8_t *page, size_t pageSize, const char *query, const uint8_t *needle, size_t length) {
    Signature signature(query);
    constexpr std::array<size_t, 17> sizes = {1, 3, 4, 5, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513, 4096};
    for (size_t size: sizes) {
        if (size > pageSize)
            continue;
        for (size_t alignment = 0; alignment < 64; ++alignment) {
            if (size + alignment > pageSize)
                continue;
            for (bool atEnd: {false, true}) {
                size_t start = atEnd ? pageSize - size - alignment : alignment;
                auto *begin = page + start;
                MemoryRegion region(MemoryHandle(begin), size);
                std::memset(page, 0xCC, pageSize);
                if (size < length) {
                    if (!expect(!signature.scan(region) && !signature.matches(region), "pattern exceeds region"))
                        return false;
                    continue;
                }
                for (size_t offset: {size_t(0), (size - length) / 2, size - length}) {
                    std::memset(page, 0xCC, pageSize);
                    std::memcpy(begin + offset, needle, length);
                    if (!expect(signature.scan(region).as<uint8_t *>() == begin + offset, "first/middle/last legal match") ||
                        !expect(signature.scanUnique(region).as<uint8_t *>() == begin + offset, "unique match") ||
                        !expect(signature.matches(region) == (offset == 0), "match at region start"))
                        return false;
                }
                std::memset(page, 0xCC, pageSize);
                if (start >= length)
                    std::memcpy(begin - length, needle, length);
                if (start + size + length <= pageSize)
                    std::memcpy(begin + size, needle, length);
                if (!expect(!signature.scan(region), "matches outside region are excluded"))
                    return false;
            }
        }
    }
    return true;
}
} // namespace

int main() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    size_t pageSize = system.dwPageSize;
    auto *allocation = static_cast<uint8_t *>(VirtualAlloc(nullptr, pageSize * 3, MEM_RESERVE, PAGE_NOACCESS));
    if (!allocation)
        return 1;
    auto *page = static_cast<uint8_t *>(VirtualAlloc(allocation + pageSize, pageSize, MEM_COMMIT, PAGE_READWRITE));
    if (!page) {
        VirtualFree(allocation, 0, MEM_RELEASE);
        return 1;
    }
    constexpr uint8_t needle[] = {0x48, 0x8B, 0x12, 0x90};
    bool ok = boundaries(page, pageSize, "48 8B ?? 90", needle, sizeof(needle));
    std::array<uint8_t, 160> longNeedle{};
    longNeedle.fill(0x90);
    longNeedle[98] = 0x48;
    longNeedle[99] = 0x8B;
    std::string longQuery;
    for (size_t i = 0; i < longNeedle.size(); ++i)
        longQuery += i == 98 ? "48 " : i == 99 ? "8B " : "?? ";
    ok &= boundaries(page, pageSize, longQuery.c_str(), longNeedle.data(), longNeedle.size());
    std::printf("guarded scanner boundaries (short/long signatures, 64 alignments): %s\n", ok ? "PASS" : "FAIL");

    std::memset(page, 0xAA, pageSize);
    Signature overlap("AA AA AA AA");
    ok &= expect(overlap.scanUnique(MemoryRegion(MemoryHandle(page), 4)).as<uint8_t *>() == page, "exact-size unique match");
    ok &= expect(!overlap.scanUnique(MemoryRegion(MemoryHandle(page), 5)), "overlapping matches are ambiguous");
    std::memset(page, 0xCC, pageSize);
    std::memcpy(page, needle, sizeof(needle));
    std::memcpy(page + pageSize - sizeof(needle), needle, sizeof(needle));
    ok &= expect(!Signature("48 8B ?? 90").scanUnique(MemoryRegion(MemoryHandle(page), pageSize)), "duplicate at last legal start");
    page[0] ^= 1;
    ok &= expect(!Signature("48 8B ?? 90").matches(MemoryRegion(MemoryHandle(page), sizeof(needle))), "first byte mismatch");
    ok &= expect(!Signature("").scan(MemoryRegion(MemoryHandle(page), pageSize)), "empty signature");
    ok &= expect(!Signature("48").scan(MemoryRegion(MemoryHandle(page), 0)), "empty region");
    ok &= expect(!Signature("48").matches(MemoryRegion(MemoryHandle(nullptr), 1)), "null region");
    ok &= expect(Signature("49").matches(MemoryRegion(MemoryHandle(page), 1)), "one-byte signature");
    ok &= expect(Signature("4? ?B ?? 9?").matches(MemoryRegion(MemoryHandle(page), sizeof(needle))), "nibble wildcards");
    ok &= expect(Signature("?? ??").matches(MemoryRegion(MemoryHandle(page), 2)), "wildcard-only signature");
    VirtualFree(allocation, 0, MEM_RELEASE);
    std::printf("shared signature checks: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
