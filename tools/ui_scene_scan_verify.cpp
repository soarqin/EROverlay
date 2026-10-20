// Uses the production resolver on mapped PE images without loading game code.
#define NOMINMAX
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "util/uiscenescan.hpp"

namespace {
using namespace er::util::uiscene;
using er::util::PeImage;

bool verify(const std::filesystem::path &path, bool negative) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < sizeof(IMAGE_DOS_HEADER))
        return false;
    std::vector<uint8_t> raw(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(raw.data()), raw.size()))
        return false;
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, raw.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || uint64_t(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > raw.size())
        return false;
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, raw.data() + dos.e_lfanew, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage > 512 * 1024 * 1024 ||
        nt.OptionalHeader.SizeOfHeaders > raw.size() || nt.OptionalHeader.SizeOfHeaders > nt.OptionalHeader.SizeOfImage)
        return false;
    std::vector<uint8_t> mapped(nt.OptionalHeader.SizeOfImage);
    std::memcpy(mapped.data(), raw.data(), nt.OptionalHeader.SizeOfHeaders);
    size_t table = size_t(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (table + sizeof(section) > raw.size())
            return false;
        std::memcpy(&section, raw.data() + table, sizeof(section));
        table += sizeof(section);
        if (uint64_t(section.PointerToRawData) + section.SizeOfRawData > raw.size() || uint64_t(section.VirtualAddress) + section.SizeOfRawData > mapped.size())
            return false;
        std::memcpy(mapped.data() + section.VirtualAddress, raw.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    PeImage image(mapped, uintptr_t(nt.OptionalHeader.ImageBase));
    Addresses addresses{};
    const char *error = nullptr;
    bool ok = resolve(image, addresses, error);
    std::cout << path.parent_path().parent_path().filename().string() << "\t";
    if (!ok) {
        std::cout << "UNSUPPORTED\t" << error << '\n';
        return !negative;
    }
    std::cout << "PASS\tsubmit=0x" << std::hex << addresses.submit << " enqueue=0x" << addresses.enqueue << " consume=0x" << addresses.consume << " format=0x"
              << addresses.pixelFormat << " view=0x" << addresses.renderViewVtable << " texture=0x" << addresses.textureVtable << " offsets=" << addresses.swapChainsOffset << ','
              << addresses.viewportOffset << ',' << addresses.contextEnvironmentOffset << ',' << addresses.backendOffset << ',' << addresses.queueOffset << " composite=0x"
              << addresses.composite << " render-post=" << addresses.renderPostOffset << " queues=" << addresses.queueIndexOffset << ',' << addresses.queuesOffset << std::dec
              << '\n';
    if (!negative)
        return true;
    auto rejects = [&](const char *name) {
        Addresses ignored{};
        bool rejected = !resolve(image, ignored, error) && ignored.submit == 0;
        std::cout << "negative " << name << ": " << (rejected ? "PASS" : "FAIL") << '\n';
        return rejected;
    };
    bool all = true;
    // A duplicate signature must never choose an arbitrary hook target.
    uint32_t textRva = 0;
    if (image.section(".text", textRva).empty())
        return false;
    uint32_t duplicate = textRva + 0x1000;
    size_t submitSize = er::util::Signature(QUEUE_SUBMIT).size();
    std::vector<uint8_t> saved(mapped.begin() + duplicate, mapped.begin() + duplicate + submitSize);
    std::memcpy(mapped.data() + duplicate, mapped.data() + addresses.submit, submitSize);
    all &= rejects("duplicate submit");
    std::memcpy(mapped.data() + duplicate, saved.data(), saved.size());
    // A valid opcode signature with a redirected callee must also be rejected.
    uint8_t original = mapped[addresses.submit + 0x3b];
    mapped[addresses.submit + 0x3b] ^= 0x40;
    all &= rejects("redirected enqueue");
    mapped[addresses.submit + 0x3b] = original;
    // Preserve call operands while changing the flush-before-callback flow.
    original = mapped[addresses.consume + 0x99];
    mapped[addresses.consume + 0x99] ^= 1;
    all &= rejects("consumer control flow");
    mapped[addresses.consume + 0x99] = original;
    original = mapped[addresses.composite + 0x4b1];
    mapped[addresses.composite + 0x4b1] ^= 0x40;
    all &= rejects("redirected native composition enqueue");
    mapped[addresses.composite + 0x4b1] = original;
    uint32_t dataRva = 0;
    auto data = image.section(".data", dataRva);
    constexpr std::string_view name = ".?AVCGRenderTargetView@DLCG3@@";
    auto hit = std::search(data.begin(), data.end(), name.begin(), name.end());
    if (hit == data.end())
        return false;
    uint32_t type = dataRva + uint32_t(hit - data.begin());
    mapped[type] ^= 1;
    all &= rejects("missing RTTI");
    mapped[type] ^= 1;
    PeImage truncated(std::span<const uint8_t>(mapped.data(), 64), uintptr_t(nt.OptionalHeader.ImageBase));
    Addresses ignored{};
    all &= !resolve(truncated, ignored, error);
    std::cout << "negative truncated PE: " << (!resolve(truncated, ignored, error) ? "PASS" : "FAIL") << '\n';
    // Model the loader's DIR64 relocations and use the relocated module base.
    const auto &directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!directory.Size || uint64_t(directory.VirtualAddress) + directory.Size > mapped.size())
        return false;
    constexpr uint64_t delta = 0x200000000ULL;
    uint32_t cursor = directory.VirtualAddress;
    while (cursor < uint64_t(directory.VirtualAddress) + directory.Size) {
        IMAGE_BASE_RELOCATION block{};
        if (!image.read(cursor, block) || block.SizeOfBlock < sizeof(block) || uint64_t(cursor) + block.SizeOfBlock > uint64_t(directory.VirtualAddress) + directory.Size)
            return false;
        for (uint32_t offset = sizeof(block); offset + 2 <= block.SizeOfBlock; offset += 2) {
            uint16_t entry = 0;
            if (!image.read(cursor + offset, entry))
                return false;
            if ((entry >> 12) == IMAGE_REL_BASED_ABSOLUTE)
                continue;
            if ((entry >> 12) != IMAGE_REL_BASED_DIR64)
                return false;
            uint64_t location = uint64_t(block.VirtualAddress) + (entry & 0xfff);
            uint64_t pointer = 0;
            if (location > UINT32_MAX || !image.read(uint32_t(location), pointer))
                return false;
            pointer += delta;
            std::memcpy(mapped.data() + location, &pointer, sizeof(pointer));
        }
        cursor += block.SizeOfBlock;
    }
    PeImage relocated(mapped, uintptr_t(nt.OptionalHeader.ImageBase + delta));
    Addresses rebased{};
    bool aslr = resolve(relocated, rebased, error) && rebased.submit == addresses.submit && rebased.renderViewVtable == addresses.renderViewVtable &&
                rebased.textureVtable == addresses.textureVtable;
    std::cout << "relocated PE (+8 GiB): " << (aslr ? "PASS" : "FAIL") << '\n';
    all &= aslr;
    return all;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2)
        return 2;
    bool negative = argc > 2 && std::wstring_view(argv[2]) == L"--negative";
    std::filesystem::path path(argv[1]);
    if (std::filesystem::is_regular_file(path))
        return verify(path, negative) ? 0 : 1;
    bool ok = true;
    unsigned count = 0;
    for (const auto &entry: std::filesystem::directory_iterator(path)) {
        auto executable = entry.path() / L"Game/eldenring.exe";
        if (std::filesystem::is_regular_file(executable)) {
            ok &= verify(executable, false);
            ++count;
        }
    }
    std::cout << "images=" << count << '\n';
    return ok && count ? 0 : 1;
}
