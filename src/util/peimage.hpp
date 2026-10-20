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

#include "signature.hpp"

namespace er::util {

// An image view uses RVAs throughout. The same resolver runs against the live
// module and a PE mapped from disk by the offline verifier.
class PeImage {
public:
    explicit PeImage(std::span<const uint8_t> bytes, uintptr_t base) : bytes_(bytes), base_(base) {}

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

    [[nodiscard]] bool matches(uint32_t rva, const char *query) const {
        if (rva >= bytes_.size())
            return false;
        auto begin = MemoryHandle(const_cast<uint8_t *>(bytes_.data() + rva));
        return Signature(query).matches(MemoryRegion(begin, bytes_.size() - rva));
    }

    [[nodiscard]] uint32_t unique(const char *query, const char *sectionName = ".text") const {
        uint32_t start = 0;
        auto bytes = section(sectionName, start);
        auto begin = MemoryHandle(const_cast<uint8_t *>(bytes.data()));
        auto hit = Signature(query).scanUnique(MemoryRegion(begin, bytes.size()));
        return hit ? start + uint32_t(hit.as<const uint8_t *>() - bytes.data()) : 0;
    }

    [[nodiscard]] uint32_t relativeAddress(uint32_t instruction, uint32_t displacement, uint32_t size) const {
        int32_t delta = 0;
        if (!instruction || uint64_t(instruction) + size > bytes_.size() || displacement > size || sizeof(delta) > size - displacement ||
            uint64_t(instruction) + displacement > UINT32_MAX || !read(instruction + displacement, delta))
            return 0;
        int64_t target = int64_t(instruction) + size + delta;
        return target > 0 && uint64_t(target) < bytes_.size() && target <= UINT32_MAX ? uint32_t(target) : 0;
    }

    [[nodiscard]] uint32_t relative(uint32_t instruction, uint32_t displacement, uint32_t size) const {
        auto target = relativeAddress(instruction, displacement, size);
        return target && executable(target) ? target : 0;
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

} // namespace er::util
