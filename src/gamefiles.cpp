#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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
#include "util/gamescan.hpp"
#include "util/mapstate.hpp"
#include "util/nativelog.hpp"

namespace er {
namespace {
template<typename T>
bool readGame(uintptr_t address, T &value) {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(value), &read) && read == sizeof(value);
}
uintptr_t pointer(uintptr_t address) {
    uintptr_t value = 0;
    return readGame(address, value) ? value : 0;
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
            bool valid = false;
            bool copy(util::Bytes data) {
                bytes.reset(static_cast<uint8_t *>(HeapAlloc(GetProcessHeap(), 0, data.size())));
                if (!bytes)
                    return false;
                std::memcpy(bytes.get(), data.data(), data.size());
                size = data.size();
                valid = true;
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
    util::GameBindings bindings{};
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
    uint32_t activeMasks[3]{};
    // Marker refreshes read thousands of flags; resolving each category once
    // avoids a ReadProcessMemory tree walk per flag.
    std::mutex flagMutex;
    util::EventFlagCache flagCache;

    static void complete(uint32_t status, void *context, void *buffer, uint64_t size) {
        auto &item = *static_cast<Item *>(context);
        item.nativeStatus = static_cast<int32_t>(status);
        // Named TPF reads borrow the game's container and copy only selected
        // DDS. Unrelated mod/UI textures must not consume the output budget.
        bool ok = status == 1 && buffer && size && size <= SIZE_MAX && (!item.names.empty() || size <= item.maxBytes);
        if (ok) {
            util::Bytes bytes(static_cast<const uint8_t *>(buffer), static_cast<size_t>(size));
            if (item.names.empty()) {
                ok = item.parts[0].copy(bytes);
            } else {
                std::vector<util::TpfEntry> entries;
                ok = util::parseTpf(bytes, entries);
                std::unordered_map<std::wstring, util::Bytes> found;
                for (const auto &entry: entries) {
                    auto [part, inserted] = found.emplace(entry.name, entry.dds);
                    if (!inserted)
                        part->second = {};
                }
                size_t copied = 0;
                for (size_t i = 0; ok && i < item.names.size(); ++i) {
                    auto entry = found.find(item.names[i]);
                    if (entry != found.end() && !entry->second.empty() && entry->second.size() <= item.maxBytes - copied && item.parts[i].copy(entry->second))
                        copied += entry->second.size();
                    // A missing/invalid entry fails only that part, so an
                    // unused mod/DLC atlas cannot block other valid images.
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
        // FD4 request cleanup returns objects to the shared pool. Use the
        // scan-resolved published pool owner, independent of containment.
        auto poolOwner = pointer(base + bindings.retirePoolOwner);
        auto pool = poolOwner ? pointer(poolOwner) : 0;
        auto generations = pool ? pointer(pool + 0x50) : 0;
        uint16_t generation = 0;
        return generations && readGame(generations + (item.nativeId & 0xffff) * 2, generation) && generation != ((item.nativeId >> 16) & 0xffff);
    }

    void run() {
        auto module = util::Module(nullptr);
        util::PeImage image({module.base().as<const uint8_t *>(), module.size()}, base);
        std::string error;
        bool match = util::gamescan::resolve(image, bindings, error);
        supported.store(match, std::memory_order_release);
        util::nativeLog("game-compatible=%d source=signature-scan base=%llx error=%s\n", match, static_cast<unsigned long long>(base), error.c_str());
        if (match)
            fwprintf(stderr, L"[Minimap] native bindings resolved by signature scan: submit=+%X menu=+%X owner=+%X view=+%X\n", bindings.submit, bindings.menu,
                     bindings.layout.menuOwnerOffset, bindings.layout.ownerViewOffset);
        else
            fwprintf(stderr, L"[Minimap] 原生接口扫描失败：%hs；已禁用游戏调用。\n", error.c_str());
        auto submit = match ? reinterpret_cast<int (*)(void *, uint64_t *, const ReadRequest *)>(base + bindings.submit) : nullptr;
        auto flush = match ? reinterpret_cast<void (*)(void *)>(base + bindings.flush) : nullptr;
        auto enqueue = match ? reinterpret_cast<void (*)(void *, void *, uint64_t)>(base + bindings.enqueue) : nullptr;
        uint64_t logFlush = 0;
        for (;;) {
            std::vector<std::shared_ptr<Item>> submitItems, cancelItems;
            uintptr_t manager = match ? pointer(base + bindings.manager) : 0;
            auto allocator = match ? reinterpret_cast<void *>(pointer(base + bindings.allocator)) : nullptr;
            uint8_t ending = 1;
            bool ready = match && manager && allocator && readGame(manager + 0xE38, ending) && !ending;
            {
                std::unique_lock lock(mutex);
                for (auto it = items.begin(); it != items.end();) {
                    auto &item = *it->second;
                    auto status = item.status.load(std::memory_order_acquire);
                    if (util::nativeLogEnabled() && !item.logged && status != ER_FILE_PENDING && status != ER_FILE_QUEUED) {
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
                enqueue(reinterpret_cast<void *>(item->manager + 0x20), reinterpret_cast<void *>(base + bindings.cancel), item->nativeId);
            }
            if (util::nativeLogEnabled()) {
                auto now = GetTickCount64();
                if (now >= logFlush) {
                    util::flushNativeLog();
                    logFlush = now + 1000;
                }
            }
            std::unique_lock lock(mutex);
            if (items.empty()) {
                // No native request needs polling. Sleep until the next
                // request/stop notification instead of reading game pointers
                // every ten milliseconds throughout the whole session.
                if (util::nativeLogEnabled()) {
                    lock.unlock();
                    util::flushNativeLog();
                    lock.lock();
                }
                if (util::nativeLogEnabled())
                    condition.wait_for(lock, std::chrono::seconds(1), [this] { return stopping || !items.empty(); });
                else
                    condition.wait(lock, [this] { return stopping || !items.empty(); });
            } else
                condition.wait_for(lock, std::chrono::milliseconds(10));
        }
        util::flushNativeLog();
    }
};

GameFiles::GameFiles() : impl_(std::make_unique<Impl>()) {
    impl_->worker = std::thread([this] { impl_->run(); });
}
GameFiles::~GameFiles() noexcept { stop(); }
bool GameFiles::compatible() const { return impl_->supported.load(std::memory_order_acquire); }
uint64_t GameFiles::request(const ERFileRequest &request) {
    if (!request.path || !*request.path || request.flags & ~0x40u || request.tpfNameCount > 4096 || (request.tpfNameCount && !request.tpfNames))
        return 0;
    auto item = std::make_shared<Impl::Item>();
    item->path = request.path;
    if (item->path.size() > 1024)
        return 0;
    // The game's high-level GFX/CSFile callers canonicalize virtual paths
    // before reaching this raw request layer (RVA 0x120EA0). Match that here
    // so archive and mod VFS lookups use the same key. DDS names stay exact.
    auto colon = item->path.find(L':');
    if (colon != std::wstring::npos && colon > 1 && item->path.find_first_of(L"/\\") == colon + 1)
        for (auto &character: item->path)
            if (character >= L'A' && character <= L'Z')
                character += L'a' - L'A';
            else if (character == L'\\')
                character = L'/';
    item->flags = request.flags;
    item->maxBytes = std::clamp(request.maxBytes ? request.maxBytes : 256ull * 1024 * 1024, 1ull, 256ull * 1024 * 1024);
    for (uint32_t i = 0; i < request.tpfNameCount; ++i) {
        if (!request.tpfNames[i] || !*request.tpfNames[i])
            return 0;
        item->names.emplace_back(request.tpfNames[i]);
        if (item->names.back().size() > 4096)
            return 0;
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
    if (status != ER_FILE_PENDING && status != ER_FILE_QUEUED)
        data.nativeStatus = item.nativeStatus;
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
        if (!item.parts[index].valid)
            return ER_FILE_FAILED;
        data.data = item.parts[index].bytes.get();
        data.size = item.parts[index].size;
    }
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
bool GameFiles::readDisplayState(ERDisplayState &state) const {
    state = {true, false};
    if (!compatible())
        return false;
    const auto &bindings = impl_->bindings;
    return util::readDisplayState(impl_->base + bindings.menu, impl_->base + bindings.gameData, bindings.layout, state);
}

bool GameFiles::readMapState(ERMapState &state) {
    state = {};
    if (!compatible())
        return false;
    const auto &bindings = impl_->bindings;
    util::MapContext context;
    bool contextRead = util::readMapContext(impl_->base + bindings.menu, impl_->base + bindings.gameData, bindings.layout, context);
    auto view = context.view, player = context.player;
    if (view != impl_->lastView || player != impl_->lastPlayer) {
        ++impl_->mapGeneration;
        impl_->lastView = view;
        impl_->lastPlayer = player;
        impl_->maskRefresh = 0;
        std::lock_guard lock(impl_->flagMutex);
        impl_->flagCache.reset();
    }
    state.generation = impl_->mapGeneration;
    if (!contextRead)
        return false;
    uint8_t reveal = 0;
    auto repository = pointer(impl_->base + bindings.repository);
    auto pieceEntry = repository ? pointer(repository + 0x88 + 88 * 72) : 0;
    auto pieceCap = pieceEntry ? pointer(pieceEntry + 0x80) : 0;
    auto pieceTable = pieceCap ? pointer(pieceCap + 0x80) : 0;
    auto flags = pointer(impl_->base + bindings.eventFlags);
    if (!util::readWorldMapView(view, state) || !readGame(impl_->base + bindings.reveal, reveal) || !util::mapContextUnchanged(context, bindings.layout))
        return false;
    uint64_t now = GetTickCount64();
    if (now >= impl_->maskRefresh || flags != impl_->lastFlags || pieceTable != impl_->lastPieceTable || reveal != impl_->lastReveal) {
        uint32_t masks[3];
        // Mirror the native mask calculation from PARAM/flags rather than
        // the view model's non-authoritative cached display masks.
        std::unique_lock lock(impl_->flagMutex);
        bool masksRead = util::readMapPieceMasks(pieceTable, flags, reveal != 0, masks, &impl_->flagCache);
        lock.unlock();
        if (!masksRead) {
            impl_->maskRefresh = 0;
            return false;
        }
        std::copy(std::begin(masks), std::end(masks), impl_->activeMasks);
        impl_->lastFlags = flags;
        impl_->lastPieceTable = pieceTable;
        impl_->lastReveal = reveal;
        impl_->maskRefresh = now + 200;
    }
    std::copy(std::begin(impl_->activeMasks), std::end(impl_->activeMasks), state.activeMasks);
    return true;
}

uintptr_t GameFiles::findParamTable(uint32_t group) const {
    if (!compatible() || (group != 43 && group != 87 && group != 88 && group != 141) || group >= impl_->bindings.layout.repositoryGroups)
        return 0;
    auto repository = pointer(impl_->base + impl_->bindings.repository);
    auto entry = repository ? pointer(repository + 0x88 + group * 72) : 0;
    auto cap = entry ? pointer(entry + 0x80) : 0;
    return cap ? pointer(cap + 0x80) : 0;
}
bool GameFiles::readEventFlag(uint32_t id) const {
    if (!compatible() || id == UINT32_MAX)
        return false;
    auto manager = pointer(impl_->base + impl_->bindings.eventFlags);
    bool value = false;
    std::lock_guard lock(impl_->flagMutex);
    return impl_->flagCache.read(manager, id, value) && value;
}
bool GameFiles::readGameLayout(ERGameLayout &layout) const {
    layout = {};
    if (!compatible())
        return false;
    layout = impl_->bindings.layout;
    return true;
}
} // namespace er
