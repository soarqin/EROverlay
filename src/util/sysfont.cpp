#include "sysfont.hpp"

#include "string.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>

namespace er::util {

wchar_t *const * systemFontFileList(const wchar_t *fontname) {
    WCHAR windir[MAX_PATH];
    GetEnvironmentVariableW(L"windir", windir, MAX_PATH);
    PathAppendW(windir, L"Fonts");
    HKEY hKey = nullptr;
    DWORD type, size;
    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink", &hKey) != ERROR_SUCCESS) return {};
    if (RegQueryValueExW(hKey, fontname, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_MULTI_SZ) {
        RegCloseKey(hKey);
        return {};
    }
    auto *fontlink = new WCHAR[size / sizeof(WCHAR) + 1];
    ZeroMemory(fontlink, (size / sizeof(WCHAR) + 1) * sizeof(WCHAR));
    if (RegQueryValueExW(hKey, fontname, nullptr, &type, reinterpret_cast<LPBYTE>(&fontlink[0]), &size) != ERROR_SUCCESS) {
        delete[] fontlink;
        RegCloseKey(hKey);
        return {};
    }
    std::vector<std::wstring> result;
    WCHAR fontfile[MAX_PATH];
    auto *p = fontlink;
    for (auto len = lstrlenW(p); len > 0; p += len + 1, len = lstrlenW(p)) {
        auto sl = splitString<std::wstring>(p, L',');
        if (sl.size() != 2) {
            continue;
        }
        PathCombineW(fontfile, windir, sl[0].c_str());
        result.emplace_back(fontfile);
    }
    delete[] fontlink;
    RegCloseKey(hKey);
    auto sz = result.size();
    auto *paths = new wchar_t *[sz + 1];
    for (size_t i = 0; i < sz; ++i) {
        paths[i] = new wchar_t[result[i].size() + 1];
        lstrcpyW(paths[i], result[i].c_str());
    }
    paths[sz] = nullptr;
    return paths;
}

void freeSystemFontFileList(wchar_t *const *pathList) {
    for (auto *p = pathList; *p; ++p) {
        delete[] *p;
    }
    delete[] pathList;
}

}
