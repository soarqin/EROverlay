// Sample-specific CPU file-loader probe. See minimap-probes.md.
// No debugger, breakpoints, hooks, code patches or game GPU handles.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <bcrypt.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace er::minimap::probe {
namespace {
HMODULE gModule;
uintptr_t gGameBase;
SRWLOCK gLogLock = SRWLOCK_INIT;
std::atomic_uint gPending{0};
wchar_t gOutputDir[MAX_PATH];

constexpr unsigned char SAMPLE_HASH[] = {
    0x1a, 0x35, 0x47, 0x10, 0x13, 0x27, 0xf6, 0x5d, 0x0c, 0x76, 0xda, 0x2f, 0x91, 0x90, 0xac, 0x0a,
    0xa6, 0x68, 0x71, 0xea, 0x42, 0xba, 0xe2, 0xae, 0xcc, 0x61, 0xe1, 0x1a, 0x8b, 0x59, 0x78, 0x91,
};

struct ReadRequest {
    const wchar_t *path;
    void *allocator;
    uint64_t alignment;
    void (*complete)(uint32_t, void *, void *, uint64_t);
    void (*stage)(); // Unused; its ABI is not needed by this probe.
    void *context;
    uint32_t flags;
    uint16_t parameter;
    uint16_t padding;
    uint32_t priority;
    uint32_t tailPadding;
};
static_assert(sizeof(ReadRequest) == 64);
static_assert(offsetof(ReadRequest, complete) == 0x18);
static_assert(offsetof(ReadRequest, flags) == 0x30);
static_assert(offsetof(ReadRequest, priority) == 0x38);

struct Item {
    const wchar_t *path;
    const wchar_t *output;
    uint32_t flags;
    bool cancel;
    void *allocator = nullptr;
    uint64_t requestId = 0;
    void *bytes = nullptr;
    uint64_t size = 0;
    uint32_t status = 0;
};

Item gItems[] = {
#ifdef MINIMAP_SPRITE_PROBE
    {L"menu:/Win/02_120_WorldMap.gfx", L"worldmap-win.gfx", 0, false},
    {L"menu:/02_120_WorldMap.gfx", L"worldmap.gfx", 0, false},
    {L"menu:/Win/01_Common.gfx", L"common-win.gfx", 0, false},
    {L"menu:/01_Common.gfx", L"common.gfx", 0, false},
#else
    {L"system:/steam_appid.txt", L"steam-appid.bin", 0, false},
    {L"menu:/71_MapTile.tpfbhd", L"map-index.bin", 0, false},
    {L"menu:/71_MapTile.mtmskbnd.dcx", L"map-masks.bin", 0x40, false},
    {L"menutpfbnd:/71_MapTile/MENU_MapTile_M00_L0_20_20_00000000.tpf.dcx", L"surface-v0.tpf", 0x40, false},
    {L"menutpfbnd:/71_MapTile/MENU_MapTile_M00_L0_20_20_00008000.tpf.dcx", L"surface-v8000.tpf", 0x40, false},
    {L"menutpfbnd:/71_MapTile/MENU_MapTile_M00_L1_15_15_00000000.tpf.dcx", L"surface-l1.tpf", 0x40, false},
    {L"menutpfbnd:/71_MapTile/MENU_MapTile_M00_L2_04_04_00000000.tpf.dcx", L"surface-l2.tpf", 0x40, false},
    {L"menutpfbnd:/71_MapTile/MENU_MapTile_M01_L0_20_20_00000000.tpf.dcx", L"underground.tpf", 0x40, false},
    {L"menutpfbnd:/71_MapTile/MENU_MapTile_M10_L0_20_20_00000003.tpf.dcx", L"dlc-v3.tpf", 0x40, false},
    {L"menutpfbnd:/71_MapTile/MENU_MapTile_M00_L0_20_20_00000000.tpf.dcx", L"cancelled.tpf", 0x40, true},
    {L"menu:/Hi/01_Common.sblytbnd.dcx", L"common-layouts.bin", 0x40, false},
    {L"menu:/Hi/01_Common.tpf.dcx", L"common-atlas.tpf", 0x40, false},
#endif
};

void logLine(const char *line) {
    AcquireSRWLockExclusive(&gLogLock);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s/events.log", gOutputDir);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&gLogLock);
}

[[nodiscard]] bool hashMatches() {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool success = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        unsigned char buffer[65536];
        DWORD read = 0;
        bool readOk = true;
        while (true) {
            if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
                readOk = false;
                break;
            }
            if (!read)
                break;
            if (BCryptHashData(hash, buffer, read, 0) < 0) {
                readOk = false;
                break;
            }
        }
        unsigned char actual[32]{};
        success = readOk && BCryptFinishHash(hash, actual, sizeof(actual), 0) >= 0 && memcmp(actual, SAMPLE_HASH, sizeof(actual)) == 0;
    }
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return success;
}

[[nodiscard]] bool entryMatches(uintptr_t rva, const unsigned char *expected, size_t size) {
    unsigned char actual[16]{};
    SIZE_T read = 0;
    return size <= sizeof(actual) && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(gGameBase + rva), actual, size, &read) && read == size &&
           memcmp(actual, expected, size) == 0;
}

void completeRead(uint32_t status, void *context, void *buffer, uint64_t size) {
    auto &item = *static_cast<Item *>(context);
    item.status = status;
    item.size = size;
    if (status == 1 && buffer && size && size <= 256ULL * 1024 * 1024) {
        item.bytes = HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(size));
        if (item.bytes)
            memcpy(item.bytes, buffer, static_cast<size_t>(size));
    }
    if (buffer) {
        auto *vtable = *static_cast<uintptr_t **>(item.allocator);
        auto release = reinterpret_cast<void (*)(void *, void *)>(vtable[13]);
        release(item.allocator, buffer);
    }
    // Publishing is the final access to Item from this callback. The DLL stays loaded.
    gPending.fetch_sub(1, std::memory_order_acq_rel);
}

void saveItems() {
    for (auto &item: gItems) {
        char line[2048];
        unsigned char magic[4]{};
        if (item.bytes && item.size >= sizeof(magic))
            memcpy(magic, item.bytes, sizeof(magic));
        DWORD error = 0;
        if (item.bytes) {
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s/%s", gOutputDir, item.output);
            HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                error = GetLastError();
            } else {
                DWORD written = 0;
                if (!WriteFile(file, item.bytes, static_cast<DWORD>(item.size), &written, nullptr))
                    error = GetLastError();
                else if (written != item.size)
                    error = ERROR_WRITE_FAULT;
                CloseHandle(file);
            }
            HeapFree(GetProcessHeap(), 0, item.bytes);
            item.bytes = nullptr;
        }
        snprintf(line, sizeof(line), "complete path=%ls id=%llx status=%u size=%llu magic=%02x%02x%02x%02x output-error=%lu\n", item.path,
                 static_cast<unsigned long long>(item.requestId), item.status, static_cast<unsigned long long>(item.size), magic[0], magic[1], magic[2], magic[3], error);
        logLine(line);
    }
}

#ifdef MINIMAP_SPRITE_PROBE
template <typename T>
[[nodiscard]] bool readValue(uintptr_t address, T &value) {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(value), &read) && read == sizeof(value);
}

[[nodiscard]] uintptr_t readPointer(uintptr_t address) {
    uintptr_t value = 0;
    if (!readValue(address, value))
        return 0;
    return value;
}

void dumpSpriteParams() {
    const auto repository = readPointer(gGameBase + 0x3D85F58);
    if (!repository)
        return;
    struct Table {
        unsigned group;
        unsigned rowSize;
        const wchar_t *name;
    };
    constexpr Table tables[] = {{87, 256, L"point-rows.bin"}, {43, 236, L"grace-rows.bin"}, {141, 1024, L"game-system-common-rows.bin"}};
    for (const auto &source: tables) {
        const auto entry = readPointer(repository + 0x88 + source.group * 72);
        const auto cap = readPointer(entry ? entry + 0x80 : 0);
        const auto table = readPointer(cap ? cap + 0x80 : 0);
        uint16_t count = 0;
        if (!table || !readValue(table + 0xA, count) || !count || count > 20000)
            continue;
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s/%s", gOutputDir, source.name);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        unsigned saved = 0;
        for (uint16_t index = 0; index < count; ++index) {
            uint64_t id = 0, offset = 0;
            unsigned char row[1024]{};
            SIZE_T copied = 0;
            if (!readValue(table + 0x40 + index * 24, id) || !readValue(table + 0x48 + index * 24, offset) || !offset || offset > 0x1000000 ||
                !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(table + offset), row, source.rowSize, &copied) || copied != source.rowSize)
                continue;
            DWORD written = 0;
            if (!WriteFile(file, &id, sizeof(id), &written, nullptr) || written != sizeof(id) ||
                !WriteFile(file, row, source.rowSize, &written, nullptr) || written != source.rowSize)
                break;
            ++saved;
        }
        CloseHandle(file);
        char line[256];
        snprintf(line, sizeof(line), "parameter group=%u count=%u saved=%u row-size=%u output=%ls\n", source.group, count, saved, source.rowSize, source.name);
        logLine(line);
    }
}
#endif

DWORD WINAPI runProbe(void *) {
    DWORD length = GetModuleFileNameW(gModule, gOutputDir, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return 1;
    if (auto *last = wcsrchr(gOutputDir, 92))
        *last = 0;
    wchar_t runDir[MAX_PATH];
    swprintf_s(runDir, L"%s/run-%lu-%llu", gOutputDir, GetCurrentProcessId(), GetTickCount64());
    if (!CreateDirectoryW(runDir, nullptr))
        return 1;
    wcscpy_s(gOutputDir, runDir);
    logLine("probe begin; no debugger or breakpoints; retained until process exit\n");
    gGameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const unsigned char submitBytes[] = {0x4c, 0x8b, 0xdc, 0x48, 0x83, 0xec, 0x78};
    const unsigned char flushBytes[] = {0x41, 0x56, 0x48, 0x83, 0xec, 0x30};
    if (!hashMatches() || !entryMatches(0x26EEB60, submitBytes, sizeof(submitBytes)) || !entryMatches(0x26EED40, flushBytes, sizeof(flushBytes))) {
        logLine("sample hash or loaded entry bytes mismatch; refused engine calls\n");
        return 1;
    }
    auto manager = *reinterpret_cast<void **>(gGameBase + 0x48611A0);
    auto allocator = *reinterpret_cast<void **>(gGameBase + 0x3D8B360);
    if (!manager || !allocator || *reinterpret_cast<unsigned char *>(static_cast<unsigned char *>(manager) + 0xE38)) {
        logLine("manager/allocator not-ready or stopping; launch probe after title menu initializes\n");
        return 1;
    }
    auto submit = reinterpret_cast<int (*)(void *, uint64_t *, const ReadRequest *)>(gGameBase + 0x26EEB60);
    auto flush = reinterpret_cast<void (*)(void *)>(gGameBase + 0x26EED40);
    auto queueTask = reinterpret_cast<void (*)(void *, void *, uint64_t)>(gGameBase + 0x26F42E0);
    for (auto &item: gItems) {
        item.allocator = allocator;
        ReadRequest request{item.path, allocator, 128, completeRead, nullptr, &item, item.flags, 0, 0, 0, 0};
        gPending.fetch_add(1, std::memory_order_relaxed);
        int result = submit(manager, &item.requestId, &request);
        char line[2048];
        snprintf(line, sizeof(line), "submit path=%ls result=%d id=%llx\n", item.path, result, static_cast<unsigned long long>(item.requestId));
        logLine(line);
        if (result != 0) {
            item.status = static_cast<uint32_t>(result);
            gPending.fetch_sub(1, std::memory_order_acq_rel);
        } else if (item.cancel) {
            queueTask(static_cast<unsigned char *>(manager) + 0x20, reinterpret_cast<void *>(gGameBase + 0x26EE2C0), item.requestId);
        }
    }
    flush(manager);
    logLine("batch flushed\n");
    for (unsigned elapsed = 0; gPending.load(std::memory_order_acquire); ++elapsed) {
        if (elapsed == 1200)
            logLine("120 seconds elapsed; waiting for terminal callbacks; DLL remains loaded\n");
        Sleep(100);
    }
    saveItems();
#ifdef MINIMAP_SPRITE_PROBE
    dumpSpriteParams();
#endif
    logLine("finished; DLL remains loaded until process exit\n");
    return 0;
}
} // namespace

void start(HMODULE module) {
    gModule = module;
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, runProbe, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}
} // namespace er::minimap::probe

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        er::minimap::probe::start(module);
    return TRUE;
}
