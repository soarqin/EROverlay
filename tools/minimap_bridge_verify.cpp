// Ordinary mod DLL running the production file bridge without any game hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <thread>

#include "gamefiles.hpp"
#include "util/assets.hpp"
#include "util/nativelog.hpp"

namespace {
HMODULE module = nullptr;
DWORD WINAPI verify(void *) {
    wchar_t path[32768];
    GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (auto slash = wcsrchr(path, L'\\'))
        *slash = 0;
    std::wstring log = std::wstring(path) + L"/bridge.log";
    er::util::nativeLogFile = _wfopen(log.c_str(), L"ab");
    er::GameFiles files;
    ERFileRequest request{L"menu:/Hi/01_Common.sblytbnd.dcx", 0x40, nullptr, 0, 8 * 1024 * 1024};
    auto normal = files.request(request);
    for (unsigned wait = 0; wait < 12000; ++wait) {
        ERFileData data; auto status = files.poll(normal, nullptr, data);
        if (status == ER_FILE_PENDING) break;
        Sleep(1);
    }
    auto cancelled = files.request(request);
    for (unsigned wait = 0; wait < 12000; ++wait) {
        ERFileData data; auto status = files.poll(cancelled, nullptr, data);
        if (status != ER_FILE_QUEUED) break;
        Sleep(1);
    }
    files.release(cancelled);
    const wchar_t *names[] = {L"SB_MapCursor", L"SB_MapCursor_02", L"SB_MapCursor_03_dlc", L"SB_Chara"};
    ERFileRequest atlas{L"menu:/Hi/01_Common.tpf.dcx", 0x40, names, 4, 256ull * 1024 * 1024};
    auto atlasToken = files.request(atlas);
    bool ready = false;
    for (unsigned wait = 0; wait < 12000; ++wait) {
        ERFileData data;
        auto state = files.poll(normal, nullptr, data);
        if (state == ER_FILE_SUCCEEDED) {
            std::vector<er::util::BinderEntry> entries;
            if (!er::util::parseBinder({static_cast<const uint8_t *>(data.data), static_cast<size_t>(data.size)}, entries) || entries.size() != 47)
                break;
            bool done = true;
            size_t bytes = 0;
            for (auto name: names) {
                if (files.poll(atlasToken, name, data) != ER_FILE_SUCCEEDED) {
                    done = false;
                    break;
                }
                er::util::DdsImage image;
                if (!er::util::parseDds({static_cast<const uint8_t *>(data.data), static_cast<size_t>(data.size)}, image) || image.format != 98) {
                    done = false;
                    break;
                }
                bytes += data.size;
            }
            if (done) {
                er::util::nativeLog("PASS: independent request survives cancellation; four target DDS=%zu bytes.\n", bytes);
                ready = true;
                break;
            }
        } else if (state != ER_FILE_PENDING && state != ER_FILE_QUEUED)
            break;
        Sleep(10);
    }
    files.release(normal);
    files.release(atlasToken);
    auto stopped = files.request(request); // stop must also cancel queued work.
    er::util::nativeLog("VERIFY stopping with queued token=%llu\n", static_cast<unsigned long long>(stopped));
    files.stop();
    er::util::nativeLog("%s: production file bridge stop returned after request pool retirement.\n", ready ? "PASS" : "FAIL");
    {
        std::lock_guard lock(er::util::nativeLogMutex);
        if (er::util::nativeLogFile)
            fclose(er::util::nativeLogFile);
        er::util::nativeLogFile = nullptr;
    }
    return ready ? 0 : 1;
}
} // namespace
BOOL WINAPI DllMain(HMODULE instance, DWORD reason, void *) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance;
        DisableThreadLibraryCalls(instance);
        if (auto thread = CreateThread(nullptr, 0, verify, nullptr, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}
