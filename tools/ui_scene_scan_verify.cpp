// Uses the production resolver on mapped PE images without loading game code.
#define NOMINMAX
#include <filesystem>
#include <iostream>
#include <vector>

#include "fixtures/pe_image.hpp"
#include "util/uiscenescan.hpp"

namespace {
using namespace er::util::uiscene;
using er::util::PeImage;

bool verify(const std::filesystem::path &path, bool negative) {
    er::test::MappedPe fixture;
    if (!fixture.load(path))
        return false;
    auto &mapped = fixture.bytes;
    const auto &nt = fixture.header;
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
    constexpr uint64_t delta = 0x200000000ULL;
    if (!fixture.relocate(delta))
        return false;
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
