// Check the shared scanner against guarded memory and known match locations.
#define NOMINMAX
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "util/peimage.hpp"

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

bool tails(uint8_t *page, size_t pageSize, const char *query, const uint8_t *needle, size_t length) {
    using namespace Pattern16::Impl;
    struct Backend {
        const char *name;
        const void *(*scan)(const void *, size_t, SplitSignatureU8 &, const Frequencies16 &);
    };
    std::vector<Backend> backends{{"x64", scanT<uint64_t>}, {"SSE2", scanT<__m128i, SSE2>}};
    std::array<int, 4> cpu{};
    PATTERN16_CPUID_LEAF1(cpu);
    if (PATTERN16_FEATURE_TEST(cpu, PATTERN16_FEATURE_SSE4_1))
        backends.push_back({"SSE4.1", scanT<__m128i, SSE4_1>});
    PATTERN16_CPUID_LEAF7(cpu);
    if (PATTERN16_FEATURE_TEST(cpu, PATTERN16_FEATURE_AVX2))
        backends.push_back({"AVX2", scanT<__m256i>});
    auto signature = processSignatureString(query);
    auto frequencies = loadFrequencyCache();
    for (size_t size: {size_t{639}, size_t{778}, size_t{1024}})
        for (size_t alignment = 0; alignment < 64; ++alignment)
            for (bool atEnd: {false, true}) {
                if (size + alignment > pageSize || size < length)
                    return false;
                auto *begin = page + (atEnd ? pageSize - size - alignment : alignment);
                // Exercise every start around the SIMD/cache-line cleanup, not
                // just the first/middle/final matches covered above.
                for (size_t offset = size - std::min(size, length + 256); offset + length <= size; ++offset) {
                    std::memset(page, 0xCC, pageSize);
                    std::memcpy(begin + offset, needle, length);
                    for (const auto &backend: backends) {
                        auto actual = static_cast<const uint8_t *>(backend.scan(begin, size, signature, frequencies));
                        if (!expect(actual == begin + offset, backend.name)) {
                            std::printf("size=%zu alignment=%zu atEnd=%d offset=%zu actual=%td\n", size, alignment, atEnd, offset, actual ? actual - begin : -1);
                            return false;
                        }
                    }
                }
            }
    // Matching the anchor alone must not accept a differing byte elsewhere.
    for (const auto &backend: backends) {
        std::memset(page, 0xCC, pageSize);
        std::memcpy(page + 256, needle, length);
        page[256 + length - 1] ^= 1;
        if (!expect(!backend.scan(page, 1024, signature, frequencies), backend.name))
            return false;
        if (length <= 7) {
            std::memset(page, 0xCC, pageSize);
            std::memcpy(page + 256, needle, length);
            std::memcpy(page + 263, needle, length);
            if (!expect(backend.scan(page, 1024, signature, frequencies) == page + 256, "first match survives later anchor in the same block"))
                return false;
        }
    }
    std::printf("tail checks (%zu-byte signature):", length);
    for (const auto &backend: backends)
        std::printf(" %s", backend.name);
    std::printf(" PASS\n");
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
    ok &= tails(page, pageSize, "48 8B ?? 90", needle, sizeof(needle));
    std::array<uint8_t, 160> longNeedle{};
    longNeedle.fill(0x90);
    longNeedle[98] = 0x48;
    longNeedle[99] = 0x8B;
    std::string longQuery;
    for (size_t i = 0; i < longNeedle.size(); ++i)
        longQuery += i == 98 ? "48 " : i == 99 ? "8B " : "?? ";
    ok &= boundaries(page, pageSize, longQuery.c_str(), longNeedle.data(), longNeedle.size());
    // Also require the final byte, independently of the late two-byte anchor.
    longQuery.replace(longQuery.size() - 3, 3, "90 ");
    ok &= tails(page, pageSize, longQuery.c_str(), longNeedle.data(), longNeedle.size());
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
    std::array<uint8_t, 16> relativeBytes{};
    PeImage relative(relativeBytes, 0);
    ok &= expect(!relative.relativeAddress(0, 3, 7), "missing instruction is not a DOS-header reference");
    ok &= expect(!relative.relativeAddress(4, 2, 5), "operand must fit the instruction");
    ok &= expect(!relative.relativeAddress(12, 0, 5), "entire instruction must fit the image");
    ok &= expect(relative.relativeAddress(4, 1, 5) == 9, "valid relative operand");
    VirtualFree(allocation, 0, MEM_RELEASE);
    std::printf("shared signature checks: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
