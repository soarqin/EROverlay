#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "gamefiles.hpp"
#include "util/assets.hpp"
#include "util/gameflags.hpp"
#include "util/mapstate.hpp"
#include "util/nativelog.hpp"

namespace er {
namespace {
// A version number can be shared by executables with different layouts.
constexpr uint8_t EXACT_HASH[32] = {0x1a, 0x35, 0x47, 0x10, 0x13, 0x27, 0xf6, 0x5d, 0x0c, 0x76, 0xda, 0x2f, 0x91, 0x90, 0xac, 0x0a,
                                    0xa6, 0x68, 0x71, 0xea, 0x42, 0xba, 0xe2, 0xae, 0xcc, 0x61, 0xe1, 0x1a, 0x8b, 0x59, 0x78, 0x91};

template<typename T>
bool readGame(uintptr_t address, T &value) {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(value), &read) && read == sizeof(value);
}
uintptr_t pointer(uintptr_t address) {
    uintptr_t value = 0;
    return readGame(address, value) ? value : 0;
}
bool hashMatches() {
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path))
        return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok)
        ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    uint8_t buffer[65536], digest[32];
    DWORD read;
    while (ok) {
        ok = ReadFile(file, buffer, sizeof(buffer), &read, nullptr) != FALSE;
        if (!ok || !read)
            break;
        ok = BCryptHashData(hash, buffer, read, 0) >= 0;
    }
    if (ok)
        ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0 && !std::memcmp(digest, EXACT_HASH, sizeof(digest));
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok;
}
bool entryMatches(uintptr_t base, uintptr_t rva, std::initializer_list<uint8_t> bytes) {
    uint8_t actual[16];
    SIZE_T read = 0;
    return bytes.size() <= sizeof(actual) && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(base + rva), actual, bytes.size(), &read) && read == bytes.size() &&
           std::equal(bytes.begin(), bytes.end(), actual);
}
struct ReadRequest {
    const wchar_t *path;
    void *allocator;
    uint64_t alignment;
    void (*complete)(uint32_t, void *, void *, uint64_t);
    void *stage;
    void *context;
    uint32_t flags;
    uint16_t parameter;
    uint16_t padding;
    uint32_t priority;
    uint32_t tail;
};
static_assert(sizeof(ReadRequest) == 64);
} // namespace

struct GameFiles::Impl {
    struct Item {
        struct Part {
            struct Deleter {
                void operator()(uint8_t *bytes) const noexcept {
                    if (bytes)
                        HeapFree(GetProcessHeap(), 0, bytes);
                }
            };
            std::unique_ptr<uint8_t, Deleter> bytes;
            size_t size = 0;
            bool copy(util::Bytes data) {
                bytes.reset(static_cast<uint8_t *>(HeapAlloc(GetProcessHeap(), 0, data.size())));
                if (!bytes)
                    return false;
                std::memcpy(bytes.get(), data.data(), data.size());
                size = data.size();
                return true;
            }
        };
        std::wstring path;
        uint32_t flags = 0;
        uint64_t maxBytes = 0;
        uint64_t nativeId = 0;
        bool submitted = false;
        uintptr_t manager = 0;
        void *allocator = nullptr;
        std::vector<std::wstring> names;
        std::vector<Part> parts;
        std::atomic<ERFileStatus> status{ER_FILE_QUEUED};
        int32_t nativeStatus = 0;
        bool released = false;
        bool cancelSent = false;
        bool logged = false;
    };
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::atomic_bool checked{false};
    std::atomic_bool supported{false};
    std::mutex mutex;
    std::condition_variable condition;
    std::unordered_map<uint64_t, std::shared_ptr<Item>> items;
    std::thread worker;
    uint64_t nextToken = 1;
    bool stopping = false;
    uint64_t mapGeneration = 1;
    uintptr_t lastView = 0;
    uintptr_t lastPlayer = 0;
    uintptr_t lastFlags = 0;
    uintptr_t lastPieceTable = 0;
    uint8_t lastReveal = 0;
    uint64_t maskRefresh = 0;
    uint64_t maskLog = 0;
    uint32_t activeMasks[3]{};

    static void complete(uint32_t status, void *context, void *buffer, uint64_t size) {
        auto &item = *static_cast<Item *>(context);
        item.nativeStatus = static_cast<int32_t>(status);
        bool ok = status == 1 && buffer && size && size <= item.maxBytes && size <= SIZE_MAX;
        if (ok) {
            util::Bytes bytes(static_cast<const uint8_t *>(buffer), static_cast<size_t>(size));
            if (item.names.empty()) {
                ok = item.parts[0].copy(bytes);
            } else {
                for (size_t i = 0; ok && i < item.names.size(); ++i) {
                    util::Bytes dds;
                    ok = util::findTpfDds(bytes, item.names[i], dds) && item.parts[i].copy(dds);
                }
            }
        }
        if (buffer) {
            auto release = reinterpret_cast<void (*)(void *, void *)>((*static_cast<uintptr_t **>(item.allocator))[13]);
            release(item.allocator, buffer);
        }
        // Publication is the last access to Item. The worker also waits for the
        // native request pool generation to advance before deleting this item
        // or allowing the DLL to unload, proving the callback has returned.
        item.status.store(status == 2 ? ER_FILE_CANCELLED : ok ? ER_FILE_SUCCEEDED : ER_FILE_FAILED, std::memory_order_release);
    }

    bool retired(const Item &item) const {
        if (!item.submitted)
            return true;
        // FD4 request objects return to their shared pool through 0x26F9340.
        // Read its published owner rather than depending on containment in
        // the manager; this sample resolves it to manager +0xF0.
        auto poolOwner = pointer(base + 0x4860D70);
        auto pool = poolOwner ? pointer(poolOwner) : 0;
        auto generations = pool ? pointer(pool + 0x50) : 0;
        uint16_t generation = 0;
        return generations && readGame(generations + (item.nativeId & 0xffff) * 2, generation) && generation != ((item.nativeId >> 16) & 0xffff);
    }

    void run() {
        bool match = hashMatches() && entryMatches(base, 0x26EEB60, {0x4c, 0x8b, 0xdc, 0x48, 0x83, 0xec, 0x78}) &&
                     entryMatches(base, 0x26EED40, {0x41, 0x56, 0x48, 0x83, 0xec, 0x30}) && entryMatches(base, 0x26F42E0, {0x41, 0x56, 0x48, 0x83, 0xec, 0x30}) &&
                     entryMatches(base, 0x26EE2C0, {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x15});
        supported.store(match, std::memory_order_release);
        checked.store(true, std::memory_order_release);
        util::nativeLog("game-compatible=%d base=%llx\n", match, static_cast<unsigned long long>(base));
        if (!match)
            fwprintf(stderr, L"[Minimap] 当前 EXE 与原生资源适配器不匹配，已禁用游戏调用。\n");
        auto submit = reinterpret_cast<int (*)(void *, uint64_t *, const ReadRequest *)>(base + 0x26EEB60);
        auto flush = reinterpret_cast<void (*)(void *)>(base + 0x26EED40);
        auto enqueue = reinterpret_cast<void (*)(void *, void *, uint64_t)>(base + 0x26F42E0);
        for (;;) {
            std::vector<std::shared_ptr<Item>> submitItems, cancelItems;
            uintptr_t manager = pointer(base + 0x48611A0);
            auto allocator = reinterpret_cast<void *>(pointer(base + 0x3D8B360));
            uint8_t ending = 1;
            bool ready = match && manager && allocator && readGame(manager + 0xE38, ending) && !ending;
            {
                std::unique_lock lock(mutex);
                for (auto it = items.begin(); it != items.end();) {
                    auto &item = *it->second;
                    auto status = item.status.load(std::memory_order_acquire);
                    if (!item.logged && status != ER_FILE_PENDING && status != ER_FILE_QUEUED) {
                        size_t bytes = 0;
                        for (const auto &part: item.parts)
                            bytes += part.size;
                        util::nativeLog("file-complete path=%ls status=%d native=%d bytes=%zu\n", item.path.c_str(), int(status), item.nativeStatus, bytes);
                        item.logged = true;
                    }
                    if (status == ER_FILE_QUEUED) {
                        if (stopping || item.released)
                            item.status = ER_FILE_CANCELLED;
                        else if (!match)
                            item.status = ER_FILE_UNSUPPORTED;
                        else if (ready) {
                            item.manager = manager;
                            item.allocator = allocator;
                            item.status = ER_FILE_PENDING;
                            submitItems.push_back(it->second);
                        }
                    } else if (status == ER_FILE_PENDING && (item.released || stopping) && !item.cancelSent && ready && item.manager == manager) {
                        item.cancelSent = true;
                        cancelItems.push_back(it->second);
                    }
                    status = item.status.load(std::memory_order_acquire);
                    if ((item.released || stopping) && status != ER_FILE_PENDING && status != ER_FILE_QUEUED && retired(item))
                        it = items.erase(it);
                    else
                        ++it;
                }
                if (stopping && items.empty())
                    break;
            }
            for (const auto &item: submitItems) {
                ReadRequest request{item->path.c_str(), allocator, 128, complete, nullptr, item.get(), item->flags, 0, 0, 0, 0};
                int result = submit(reinterpret_cast<void *>(manager), &item->nativeId, &request);
                item->submitted = result == 0;
                util::nativeLog("file-submit path=%ls id=%llx result=%d\n", item->path.c_str(), static_cast<unsigned long long>(item->nativeId), result);
                if (result != 0) {
                    item->nativeId = 0;
                    item->nativeStatus = result;
                    item->status.store(ER_FILE_FAILED, std::memory_order_release);
                }
            }
            if (!submitItems.empty())
                flush(reinterpret_cast<void *>(manager));
            for (const auto &item: cancelItems) {
                enqueue(reinterpret_cast<void *>(item->manager + 0x20), reinterpret_cast<void *>(base + 0x26EE2C0), item->nativeId);
            }
            std::unique_lock lock(mutex);
            condition.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
};

GameFiles::GameFiles() : impl_(std::make_unique<Impl>()) {
    impl_->worker = std::thread([this] { impl_->run(); });
}
GameFiles::~GameFiles() noexcept { stop(); }
bool GameFiles::compatible() const { return impl_->supported.load(std::memory_order_acquire); }
uint64_t GameFiles::request(const ERFileRequest &request) {
    if (!request.path || !*request.path || request.flags & ~0x40u || request.tpfNameCount > 8 || (request.tpfNameCount && !request.tpfNames))
        return 0;
    auto item = std::make_shared<Impl::Item>();
    item->path = request.path;
    if (item->path.size() > 1024)
        return 0;
    item->flags = request.flags;
    item->maxBytes = std::clamp(request.maxBytes ? request.maxBytes : 256ull * 1024 * 1024, 1ull, 256ull * 1024 * 1024);
    for (uint32_t i = 0; i < request.tpfNameCount; ++i) {
        if (!request.tpfNames[i] || !*request.tpfNames[i])
            return 0;
        item->names.emplace_back(request.tpfNames[i]);
    }
    item->parts.resize(std::max<size_t>(1, item->names.size()));
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping || impl_->items.size() >= 128)
        return 0;
    uint64_t token = impl_->nextToken++;
    impl_->items.emplace(token, std::move(item));
    impl_->condition.notify_one();
    return token;
}
ERFileStatus GameFiles::poll(uint64_t token, const wchar_t *part, ERFileData &data) {
    data = {};
    std::lock_guard lock(impl_->mutex);
    auto it = impl_->items.find(token);
    if (it == impl_->items.end() || it->second->released)
        return ER_FILE_INVALID;
    auto &item = *it->second;
    auto status = item.status.load(std::memory_order_acquire);
    if (status == ER_FILE_SUCCEEDED) {
        size_t index = 0;
        if (!item.names.empty()) {
            if (!part)
                return ER_FILE_INVALID;
            auto found = std::find(item.names.begin(), item.names.end(), part);
            if (found == item.names.end())
                return ER_FILE_INVALID;
            index = found - item.names.begin();
        }
        data.data = item.parts[index].bytes.get();
        data.size = item.parts[index].size;
    }
    if (status != ER_FILE_PENDING && status != ER_FILE_QUEUED)
        data.nativeStatus = item.nativeStatus;
    return status;
}
void GameFiles::release(uint64_t token) {
    std::lock_guard lock(impl_->mutex);
    auto it = impl_->items.find(token);
    if (it != impl_->items.end())
        it->second->released = true;
    impl_->condition.notify_one();
}
void GameFiles::stop() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->condition.notify_one();
    if (impl_->worker.joinable())
        impl_->worker.join();
}
bool GameFiles::readMapState(ERMapState &state) {
    state = {};
    if (!compatible())
        return false;
    auto menu = pointer(impl_->base + 0x3D6F820);
    auto owner = menu ? pointer(menu + 0x80) : 0;
    auto view = owner ? pointer(owner + 0x250) : 0;
    auto gameData = pointer(impl_->base + 0x3D61F98);
    auto player = gameData ? pointer(gameData + 0x58) : 0;
    uint16_t screen = 1;
    if (!menu || !readGame(menu + 0x730, screen) || screen)
        player = 0;
    if (view != impl_->lastView || player != impl_->lastPlayer) {
        ++impl_->mapGeneration;
        impl_->lastView = view;
        impl_->lastPlayer = player;
        impl_->maskRefresh = 0;
    }
    state.generation = impl_->mapGeneration;
    if (!view || !player)
        return false;
    uint8_t reveal = 0;
    auto repository = pointer(impl_->base + 0x3D85F58);
    auto pieceEntry = repository ? pointer(repository + 0x88 + 88 * 72) : 0;
    auto pieceCap = pieceEntry ? pointer(pieceEntry + 0x80) : 0;
    auto pieceTable = pieceCap ? pointer(pieceCap + 0x80) : 0;
    auto flags = pointer(impl_->base + 0x3D6C4B8);
    if (!util::readWorldMapView(view, state) || !readGame(impl_->base + 0x3D71030, reveal) || owner != pointer(menu + 0x80) || view != pointer(owner + 0x250))
        return false;
    uint64_t now = GetTickCount64();
    if (now >= impl_->maskRefresh || flags != impl_->lastFlags || pieceTable != impl_->lastPieceTable || reveal != impl_->lastReveal) {
        uint32_t masks[3];
        // Mirror 0x8892C0 from PARAM/flags. The view model's +0x39C cache
        // is useful for diagnostics, but is not an authoritative save value.
        if (!util::readMapPieceMasks(pieceTable, flags, reveal != 0, masks)) {
            impl_->maskRefresh = 0;
            return false;
        }
        std::copy(std::begin(masks), std::end(masks), impl_->activeMasks);
        if (now >= impl_->maskLog) {
            uint32_t cached[3]{};
            bool cacheReadable = readGame(view + 0x39C, cached);
            util::nativeLog("map-progress masks=%08x,%08x,%08x cached=%08x,%08x,%08x cache-readable=%d reveal=%u\n", masks[0], masks[1], masks[2], cached[0], cached[1], cached[2],
                            cacheReadable, reveal);
            impl_->maskLog = now + 5000;
        }
        impl_->lastFlags = flags;
        impl_->lastPieceTable = pieceTable;
        impl_->lastReveal = reveal;
        impl_->maskRefresh = now + 200;
    }
    std::copy(std::begin(impl_->activeMasks), std::end(impl_->activeMasks), state.activeMasks);
    return true;
}

uintptr_t GameFiles::findParamTable(uint32_t group) const {
    if (!compatible() || (group != 43 && group != 87 && group != 141))
        return 0;
    auto repository = pointer(impl_->base + 0x3D85F58);
    auto entry = repository ? pointer(repository + 0x88 + group * 72) : 0;
    auto cap = entry ? pointer(entry + 0x80) : 0;
    return cap ? pointer(cap + 0x80) : 0;
}
bool GameFiles::readEventFlag(uint32_t id) const {
    bool value = false;
    return compatible() && id != UINT32_MAX && util::readGameEventFlag(pointer(impl_->base + 0x3D6C4B8), id, value) && value;
}
} // namespace er
