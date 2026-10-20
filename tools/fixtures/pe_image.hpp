#pragma once

#include <filesystem>
#include <fstream>
#include <vector>

#include "util/peimage.hpp"

namespace er::test {

struct MappedPe {
    std::vector<uint8_t> bytes;
    IMAGE_NT_HEADERS64 header{};

    [[nodiscard]] bool load(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < sizeof(IMAGE_DOS_HEADER))
            return false;
        std::vector<uint8_t> raw(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(raw.data()), raw.size()))
            return false;
        IMAGE_DOS_HEADER dos{};
        std::memcpy(&dos, raw.data(), sizeof(dos));
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || uint64_t(dos.e_lfanew) + sizeof(header) > raw.size())
            return false;
        std::memcpy(&header, raw.data() + dos.e_lfanew, sizeof(header));
        if (header.Signature != IMAGE_NT_SIGNATURE || header.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || header.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            header.OptionalHeader.SizeOfImage > 512 * 1024 * 1024 || header.OptionalHeader.SizeOfHeaders > raw.size() ||
            header.OptionalHeader.SizeOfHeaders > header.OptionalHeader.SizeOfImage)
            return false;
        bytes.assign(header.OptionalHeader.SizeOfImage, 0);
        std::memcpy(bytes.data(), raw.data(), header.OptionalHeader.SizeOfHeaders);
        size_t table = size_t(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + header.FileHeader.SizeOfOptionalHeader;
        for (unsigned i = 0; i < header.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            if (table + sizeof(section) > raw.size())
                return false;
            std::memcpy(&section, raw.data() + table, sizeof(section));
            table += sizeof(section);
            if (uint64_t(section.PointerToRawData) + section.SizeOfRawData > raw.size() || uint64_t(section.VirtualAddress) + section.SizeOfRawData > bytes.size())
                return false;
            std::memcpy(bytes.data() + section.VirtualAddress, raw.data() + section.PointerToRawData, section.SizeOfRawData);
        }
        return true;
    }

    [[nodiscard]] bool relocate(uint64_t delta) {
        const auto &directory = header.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (!directory.Size || uint64_t(directory.VirtualAddress) + directory.Size > bytes.size())
            return false;
        uint64_t cursor = directory.VirtualAddress, end = cursor + directory.Size;
        while (cursor < end) {
            IMAGE_BASE_RELOCATION block{};
            if (end - cursor < sizeof(block))
                return false;
            std::memcpy(&block, bytes.data() + cursor, sizeof(block));
            if (block.SizeOfBlock < sizeof(block) || block.SizeOfBlock > end - cursor || (block.SizeOfBlock - sizeof(block)) % 2)
                return false;
            for (uint32_t offset = sizeof(block); offset < block.SizeOfBlock; offset += 2) {
                uint16_t entry = 0;
                std::memcpy(&entry, bytes.data() + cursor + offset, sizeof(entry));
                if (!(entry >> 12))
                    continue;
                uint64_t address = uint64_t(block.VirtualAddress) + (entry & 0xFFF);
                if (entry >> 12 != IMAGE_REL_BASED_DIR64 || address + sizeof(uint64_t) > bytes.size())
                    return false;
                uint64_t value = 0;
                std::memcpy(&value, bytes.data() + address, sizeof(value));
                value += delta;
                std::memcpy(bytes.data() + address, &value, sizeof(value));
            }
            cursor += block.SizeOfBlock;
        }
        return true;
    }
};

} // namespace er::test
