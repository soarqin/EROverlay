// Exercise the production native completion callback with fixture TPF bytes.
// The ordinary verifier EXE is unknown: its worker must refuse game calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

#include "gamefiles.hpp"
// Access the exact production callback and pool logic in this verifier TU.
#include "../src/gamefiles.cpp"

namespace er {
struct NativeFileVerifier {
    static inline unsigned freed = 0;
    static void freeBuffer(void *, void *bytes) {
        ++freed;
        HeapFree(GetProcessHeap(), 0, bytes);
    }
    static std::vector<uint8_t> readFile(const char *path) {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            return {};
        auto size = stream.tellg();
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char *>(bytes.data()), size);
        return bytes;
    }
    static bool completeFixture(GameFiles &files, const char *path, bool partial) {
        auto tpf = readFile(path);
        if (tpf.empty())
            return false;
        auto item = std::make_shared<GameFiles::Impl::Item>();
        for (unsigned i = 0; i < 12; ++i)
            item->names.push_back(std::wstring(L"SB_ModMap_") + (i < 10 ? L"0" : L"") + std::to_wstring(i));
        item->parts.resize(item->names.size());
        item->maxBytes = 256ull * 1024 * 1024;
        std::array<uintptr_t, 14> vtable{};
        vtable[13] = reinterpret_cast<uintptr_t>(freeBuffer);
        struct Allocator {
            uintptr_t *vtable;
        } allocator{vtable.data()};
        item->allocator = &allocator;
        auto *buffer = HeapAlloc(GetProcessHeap(), 0, tpf.size());
        if (!buffer)
            return false;
        std::memcpy(buffer, tpf.data(), tpf.size());
        unsigned before = freed;
        GameFiles::Impl::complete(1, item.get(), buffer, tpf.size());
        if (freed != before + 1 || item->status != ER_FILE_SUCCEEDED)
            return false;
        uint64_t token;
        {
            std::lock_guard lock(files.impl_->mutex);
            token = files.impl_->nextToken++;
            files.impl_->items.emplace(token, item);
        }
        for (size_t i = 0; i < item->names.size(); ++i) {
            ERFileData data{};
            auto status = files.poll(token, item->names[i].c_str(), data);
            util::Bytes expected;
            bool found = util::findTpfDds(tpf, item->names[i], expected);
            if (!found) {
                if (!partial || status != ER_FILE_FAILED || data.data || data.size)
                    return false;
            } else if (status != ER_FILE_SUCCEEDED || data.size != expected.size() || !std::equal(expected.begin(), expected.end(), static_cast<const uint8_t *>(data.data)))
                return false;
        }
        ERFileData data{};
        if (files.poll(token, L"unknown-part", data) != ER_FILE_INVALID)
            return false;
        files.release(token);
        return files.poll(token, item->names.front().c_str(), data) == ER_FILE_INVALID;
    }
    static int run() {
        GameFiles files;
        if (!completeFixture(files, "build/native-checks/mod-fixture/atlases.tpf", false) || !completeFixture(files, "build/native-checks/mod-fixture/partial-atlases.tpf", true))
            return 1;
        std::array<const wchar_t *, 12> names;
        names.fill(L"SB_ModMap_00");
        ERFileRequest request{L"menu:/Hi/01_Common.tpf.dcx", 0x40, names.data(), static_cast<uint32_t>(names.size()), 256ull * 1024 * 1024};
        auto token = files.request(request);
        if (!token)
            return 2;
        ERFileStatus status = ER_FILE_QUEUED;
        for (unsigned retry = 0; retry < 1000 && status == ER_FILE_QUEUED; ++retry) {
            ERFileData data;
            status = files.poll(token, names.front(), data);
            Sleep(1);
        }
        if (status != ER_FILE_UNSUPPORTED || files.compatible())
            return 3;
        files.release(token);
        files.stop();
        std::puts("PASS: production callback extracts 12 DDS, frees native buffer once, isolates missing parts, retires released data, accepts more than eight names and rejects "
                  "unknown EXE calls.");
        return 0;
    }
};
} // namespace er

int main() { return er::NativeFileVerifier::run(); }
