#include "winhttp.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

struct winhttp_dll {
    HMODULE dll;
    FARPROC OrignalDllCanUnloadNow;
    FARPROC OrignalDllGetClassObject;
    FARPROC OrignalPrivate1;
    FARPROC OrignalSvchostPushServiceGlobals;
    FARPROC OrignalWinHttpAddRequestHeaders;
    FARPROC OrignalWinHttpAddRequestHeadersEx;
    FARPROC OrignalWinHttpAutoProxySvcMain;
    FARPROC OrignalWinHttpCheckPlatform;
    FARPROC OrignalWinHttpCloseHandle;
    FARPROC OrignalWinHttpConnect;
    FARPROC OrignalWinHttpConnectionDeletePolicyEntries;
    FARPROC OrignalWinHttpConnectionDeleteProxyInfo;
    FARPROC OrignalWinHttpConnectionFreeNameList;
    FARPROC OrignalWinHttpConnectionFreeProxyInfo;
    FARPROC OrignalWinHttpConnectionFreeProxyList;
    FARPROC OrignalWinHttpConnectionGetNameList;
    FARPROC OrignalWinHttpConnectionGetProxyInfo;
    FARPROC OrignalWinHttpConnectionGetProxyList;
    FARPROC OrignalWinHttpConnectionOnlyConvert;
    FARPROC OrignalWinHttpConnectionOnlyReceive;
    FARPROC OrignalWinHttpConnectionOnlySend;
    FARPROC OrignalWinHttpConnectionSetPolicyEntries;
    FARPROC OrignalWinHttpConnectionSetProxyInfo;
    FARPROC OrignalWinHttpConnectionUpdateIfIndexTable;
    FARPROC OrignalWinHttpCrackUrl;
    FARPROC OrignalWinHttpCreateProxyResolver;
    FARPROC OrignalWinHttpCreateUrl;
    FARPROC OrignalWinHttpDetectAutoProxyConfigUrl;
    FARPROC OrignalWinHttpFreeProxyResult;
    FARPROC OrignalWinHttpFreeProxyResultEx;
    FARPROC OrignalWinHttpFreeProxySettings;
    FARPROC OrignalWinHttpFreeProxySettingsEx;
    FARPROC OrignalWinHttpFreeQueryConnectionGroupResult;
    FARPROC OrignalWinHttpGetDefaultProxyConfiguration;
    FARPROC OrignalWinHttpGetIEProxyConfigForCurrentUser;
    FARPROC OrignalWinHttpGetProxyForUrl;
    FARPROC OrignalWinHttpGetProxyForUrlEx;
    FARPROC OrignalWinHttpGetProxyForUrlEx2;
    FARPROC OrignalWinHttpGetProxyForUrlHvsi;
    FARPROC OrignalWinHttpGetProxyResult;
    FARPROC OrignalWinHttpGetProxyResultEx;
    FARPROC OrignalWinHttpGetProxySettingsEx;
    FARPROC OrignalWinHttpGetProxySettingsResultEx;
    FARPROC OrignalWinHttpGetProxySettingsVersion;
    FARPROC OrignalWinHttpGetTunnelSocket;
    FARPROC OrignalWinHttpOpen;
    FARPROC OrignalWinHttpOpenRequest;
    FARPROC OrignalWinHttpPacJsWorkerMain;
    FARPROC OrignalWinHttpProbeConnectivity;
    FARPROC OrignalWinHttpQueryAuthSchemes;
    FARPROC OrignalWinHttpQueryConnectionGroup;
    FARPROC OrignalWinHttpQueryDataAvailable;
    FARPROC OrignalWinHttpQueryHeaders;
    FARPROC OrignalWinHttpQueryHeadersEx;
    FARPROC OrignalWinHttpQueryOption;
    FARPROC OrignalWinHttpReadData;
    FARPROC OrignalWinHttpReadDataEx;
    FARPROC OrignalWinHttpReadProxySettings;
    FARPROC OrignalWinHttpReadProxySettingsHvsi;
    FARPROC OrignalWinHttpReceiveResponse;
    FARPROC OrignalWinHttpRegisterProxyChangeNotification;
    FARPROC OrignalWinHttpResetAutoProxy;
    FARPROC OrignalWinHttpSaveProxyCredentials;
    FARPROC OrignalWinHttpSendRequest;
    FARPROC OrignalWinHttpSetCredentials;
    FARPROC OrignalWinHttpSetDefaultProxyConfiguration;
    FARPROC OrignalWinHttpSetOption;
    FARPROC OrignalWinHttpSetProxySettingsPerUser;
    FARPROC OrignalWinHttpSetSecureLegacyServersAppCompat;
    FARPROC OrignalWinHttpSetStatusCallback;
    FARPROC OrignalWinHttpSetTimeouts;
    FARPROC OrignalWinHttpTimeFromSystemTime;
    FARPROC OrignalWinHttpTimeToSystemTime;
    FARPROC OrignalWinHttpUnregisterProxyChangeNotification;
    FARPROC OrignalWinHttpWebSocketClose;
    FARPROC OrignalWinHttpWebSocketCompleteUpgrade;
    FARPROC OrignalWinHttpWebSocketQueryCloseStatus;
    FARPROC OrignalWinHttpWebSocketReceive;
    FARPROC OrignalWinHttpWebSocketSend;
    FARPROC OrignalWinHttpWebSocketShutdown;
    FARPROC OrignalWinHttpWriteData;
    FARPROC OrignalWinHttpWriteProxySettings;
} winhttp;

#if defined(__cplusplus)
extern "C" {
#endif

void *winhttp_FakeDllCanUnloadNow() { return (void *)winhttp.OrignalDllCanUnloadNow(); }
void *winhttp_FakeDllGetClassObject() { return (void *)winhttp.OrignalDllGetClassObject(); }
void *winhttp_FakePrivate1() { return (void *)winhttp.OrignalPrivate1(); }
void *winhttp_FakeSvchostPushServiceGlobals() { return (void *)winhttp.OrignalSvchostPushServiceGlobals(); }
void *winhttp_FakeWinHttpAddRequestHeaders() { return (void *)winhttp.OrignalWinHttpAddRequestHeaders(); }
void *winhttp_FakeWinHttpAddRequestHeadersEx() { return (void *)winhttp.OrignalWinHttpAddRequestHeadersEx(); }
void *winhttp_FakeWinHttpAutoProxySvcMain() { return (void *)winhttp.OrignalWinHttpAutoProxySvcMain(); }
void *winhttp_FakeWinHttpCheckPlatform() { return (void *)winhttp.OrignalWinHttpCheckPlatform(); }
void *winhttp_FakeWinHttpCloseHandle() { return (void *)winhttp.OrignalWinHttpCloseHandle(); }
void *winhttp_FakeWinHttpConnect() { return (void *)winhttp.OrignalWinHttpConnect(); }
void *winhttp_FakeWinHttpConnectionDeletePolicyEntries() { return (void *)winhttp.OrignalWinHttpConnectionDeletePolicyEntries(); }
void *winhttp_FakeWinHttpConnectionDeleteProxyInfo() { return (void *)winhttp.OrignalWinHttpConnectionDeleteProxyInfo(); }
void *winhttp_FakeWinHttpConnectionFreeNameList() { return (void *)winhttp.OrignalWinHttpConnectionFreeNameList(); }
void *winhttp_FakeWinHttpConnectionFreeProxyInfo() { return (void *)winhttp.OrignalWinHttpConnectionFreeProxyInfo(); }
void *winhttp_FakeWinHttpConnectionFreeProxyList() { return (void *)winhttp.OrignalWinHttpConnectionFreeProxyList(); }
void *winhttp_FakeWinHttpConnectionGetNameList() { return (void *)winhttp.OrignalWinHttpConnectionGetNameList(); }
void *winhttp_FakeWinHttpConnectionGetProxyInfo() { return (void *)winhttp.OrignalWinHttpConnectionGetProxyInfo(); }
void *winhttp_FakeWinHttpConnectionGetProxyList() { return (void *)winhttp.OrignalWinHttpConnectionGetProxyList(); }
void *winhttp_FakeWinHttpConnectionOnlyConvert() { return (void *)winhttp.OrignalWinHttpConnectionOnlyConvert(); }
void *winhttp_FakeWinHttpConnectionOnlyReceive() { return (void *)winhttp.OrignalWinHttpConnectionOnlyReceive(); }
void *winhttp_FakeWinHttpConnectionOnlySend() { return (void *)winhttp.OrignalWinHttpConnectionOnlySend(); }
void *winhttp_FakeWinHttpConnectionSetPolicyEntries() { return (void *)winhttp.OrignalWinHttpConnectionSetPolicyEntries(); }
void *winhttp_FakeWinHttpConnectionSetProxyInfo() { return (void *)winhttp.OrignalWinHttpConnectionSetProxyInfo(); }
void *winhttp_FakeWinHttpConnectionUpdateIfIndexTable() { return (void *)winhttp.OrignalWinHttpConnectionUpdateIfIndexTable(); }
void *winhttp_FakeWinHttpCrackUrl() { return (void *)winhttp.OrignalWinHttpCrackUrl(); }
void *winhttp_FakeWinHttpCreateProxyResolver() { return (void *)winhttp.OrignalWinHttpCreateProxyResolver(); }
void *winhttp_FakeWinHttpCreateUrl() { return (void *)winhttp.OrignalWinHttpCreateUrl(); }
void *winhttp_FakeWinHttpDetectAutoProxyConfigUrl() { return (void *)winhttp.OrignalWinHttpDetectAutoProxyConfigUrl(); }
void *winhttp_FakeWinHttpFreeProxyResult() { return (void *)winhttp.OrignalWinHttpFreeProxyResult(); }
void *winhttp_FakeWinHttpFreeProxyResultEx() { return (void *)winhttp.OrignalWinHttpFreeProxyResultEx(); }
void *winhttp_FakeWinHttpFreeProxySettings() { return (void *)winhttp.OrignalWinHttpFreeProxySettings(); }
void *winhttp_FakeWinHttpFreeProxySettingsEx() { return (void *)winhttp.OrignalWinHttpFreeProxySettingsEx(); }
void *winhttp_FakeWinHttpFreeQueryConnectionGroupResult() { return (void *)winhttp.OrignalWinHttpFreeQueryConnectionGroupResult(); }
void *winhttp_FakeWinHttpGetDefaultProxyConfiguration() { return (void *)winhttp.OrignalWinHttpGetDefaultProxyConfiguration(); }
void *winhttp_FakeWinHttpGetIEProxyConfigForCurrentUser() { return (void *)winhttp.OrignalWinHttpGetIEProxyConfigForCurrentUser(); }
void *winhttp_FakeWinHttpGetProxyForUrl() { return (void *)winhttp.OrignalWinHttpGetProxyForUrl(); }
void *winhttp_FakeWinHttpGetProxyForUrlEx() { return (void *)winhttp.OrignalWinHttpGetProxyForUrlEx(); }
void *winhttp_FakeWinHttpGetProxyForUrlEx2() { return (void *)winhttp.OrignalWinHttpGetProxyForUrlEx2(); }
void *winhttp_FakeWinHttpGetProxyForUrlHvsi() { return (void *)winhttp.OrignalWinHttpGetProxyForUrlHvsi(); }
void *winhttp_FakeWinHttpGetProxyResult() { return (void *)winhttp.OrignalWinHttpGetProxyResult(); }
void *winhttp_FakeWinHttpGetProxyResultEx() { return (void *)winhttp.OrignalWinHttpGetProxyResultEx(); }
void *winhttp_FakeWinHttpGetProxySettingsEx() { return (void *)winhttp.OrignalWinHttpGetProxySettingsEx(); }
void *winhttp_FakeWinHttpGetProxySettingsResultEx() { return (void *)winhttp.OrignalWinHttpGetProxySettingsResultEx(); }
void *winhttp_FakeWinHttpGetProxySettingsVersion() { return (void *)winhttp.OrignalWinHttpGetProxySettingsVersion(); }
void *winhttp_FakeWinHttpGetTunnelSocket() { return (void *)winhttp.OrignalWinHttpGetTunnelSocket(); }
void *winhttp_FakeWinHttpOpen() { return (void *)winhttp.OrignalWinHttpOpen(); }
void *winhttp_FakeWinHttpOpenRequest() { return (void *)winhttp.OrignalWinHttpOpenRequest(); }
void *winhttp_FakeWinHttpPacJsWorkerMain() { return (void *)winhttp.OrignalWinHttpPacJsWorkerMain(); }
void *winhttp_FakeWinHttpProbeConnectivity() { return (void *)winhttp.OrignalWinHttpProbeConnectivity(); }
void *winhttp_FakeWinHttpQueryAuthSchemes() { return (void *)winhttp.OrignalWinHttpQueryAuthSchemes(); }
void *winhttp_FakeWinHttpQueryConnectionGroup() { return (void *)winhttp.OrignalWinHttpQueryConnectionGroup(); }
void *winhttp_FakeWinHttpQueryDataAvailable() { return (void *)winhttp.OrignalWinHttpQueryDataAvailable(); }
void *winhttp_FakeWinHttpQueryHeaders() { return (void *)winhttp.OrignalWinHttpQueryHeaders(); }
void *winhttp_FakeWinHttpQueryHeadersEx() { return (void *)winhttp.OrignalWinHttpQueryHeadersEx(); }
void *winhttp_FakeWinHttpQueryOption() { return (void *)winhttp.OrignalWinHttpQueryOption(); }
void *winhttp_FakeWinHttpReadData() { return (void *)winhttp.OrignalWinHttpReadData(); }
void *winhttp_FakeWinHttpReadDataEx() { return (void *)winhttp.OrignalWinHttpReadDataEx(); }
void *winhttp_FakeWinHttpReadProxySettings() { return (void *)winhttp.OrignalWinHttpReadProxySettings(); }
void *winhttp_FakeWinHttpReadProxySettingsHvsi() { return (void *)winhttp.OrignalWinHttpReadProxySettingsHvsi(); }
void *winhttp_FakeWinHttpReceiveResponse() { return (void *)winhttp.OrignalWinHttpReceiveResponse(); }
void *winhttp_FakeWinHttpRegisterProxyChangeNotification() { return (void *)winhttp.OrignalWinHttpRegisterProxyChangeNotification(); }
void *winhttp_FakeWinHttpResetAutoProxy() { return (void *)winhttp.OrignalWinHttpResetAutoProxy(); }
void *winhttp_FakeWinHttpSaveProxyCredentials() { return (void *)winhttp.OrignalWinHttpSaveProxyCredentials(); }
void *winhttp_FakeWinHttpSendRequest() { return (void *)winhttp.OrignalWinHttpSendRequest(); }
void *winhttp_FakeWinHttpSetCredentials() { return (void *)winhttp.OrignalWinHttpSetCredentials(); }
void *winhttp_FakeWinHttpSetDefaultProxyConfiguration() { return (void *)winhttp.OrignalWinHttpSetDefaultProxyConfiguration(); }
void *winhttp_FakeWinHttpSetOption() { return (void *)winhttp.OrignalWinHttpSetOption(); }
void *winhttp_FakeWinHttpSetProxySettingsPerUser() { return (void *)winhttp.OrignalWinHttpSetProxySettingsPerUser(); }
void *winhttp_FakeWinHttpSetSecureLegacyServersAppCompat() { return (void *)winhttp.OrignalWinHttpSetSecureLegacyServersAppCompat(); }
void *winhttp_FakeWinHttpSetStatusCallback() { return (void *)winhttp.OrignalWinHttpSetStatusCallback(); }
void *winhttp_FakeWinHttpSetTimeouts() { return (void *)winhttp.OrignalWinHttpSetTimeouts(); }
void *winhttp_FakeWinHttpTimeFromSystemTime() { return (void *)winhttp.OrignalWinHttpTimeFromSystemTime(); }
void *winhttp_FakeWinHttpTimeToSystemTime() { return (void *)winhttp.OrignalWinHttpTimeToSystemTime(); }
void *winhttp_FakeWinHttpUnregisterProxyChangeNotification() { return (void *)winhttp.OrignalWinHttpUnregisterProxyChangeNotification(); }
void *winhttp_FakeWinHttpWebSocketClose() { return (void *)winhttp.OrignalWinHttpWebSocketClose(); }
void *winhttp_FakeWinHttpWebSocketCompleteUpgrade() { return (void *)winhttp.OrignalWinHttpWebSocketCompleteUpgrade(); }
void *winhttp_FakeWinHttpWebSocketQueryCloseStatus() { return (void *)winhttp.OrignalWinHttpWebSocketQueryCloseStatus(); }
void *winhttp_FakeWinHttpWebSocketReceive() { return (void *)winhttp.OrignalWinHttpWebSocketReceive(); }
void *winhttp_FakeWinHttpWebSocketSend() { return (void *)winhttp.OrignalWinHttpWebSocketSend(); }
void *winhttp_FakeWinHttpWebSocketShutdown() { return (void *)winhttp.OrignalWinHttpWebSocketShutdown(); }
void *winhttp_FakeWinHttpWriteData() { return (void *)winhttp.OrignalWinHttpWriteData(); }
void *winhttp_FakeWinHttpWriteProxySettings() { return (void *)winhttp.OrignalWinHttpWriteProxySettings(); }

bool load_winhttp_proxy() {
    wchar_t path[MAX_PATH], syspath[MAX_PATH];
    GetSystemDirectoryW(syspath, MAX_PATH);
    _snwprintf(path, MAX_PATH, L"%ls\\winhttp.dll", syspath);
    winhttp.dll = LoadLibraryW(path);
    if (!winhttp.dll) {
        fprintf(stderr, "Cannot load original winhttp.dll library\n");
        return false;
    }
    winhttp.OrignalDllCanUnloadNow = GetProcAddress(winhttp.dll, "DllCanUnloadNow");
    winhttp.OrignalDllGetClassObject = GetProcAddress(winhttp.dll, "DllGetClassObject");
    winhttp.OrignalPrivate1 = GetProcAddress(winhttp.dll, "Private1");
    winhttp.OrignalSvchostPushServiceGlobals = GetProcAddress(winhttp.dll, "SvchostPushServiceGlobals");
    winhttp.OrignalWinHttpAddRequestHeaders = GetProcAddress(winhttp.dll, "WinHttpAddRequestHeaders");
    winhttp.OrignalWinHttpAddRequestHeadersEx = GetProcAddress(winhttp.dll, "WinHttpAddRequestHeadersEx");
    winhttp.OrignalWinHttpAutoProxySvcMain = GetProcAddress(winhttp.dll, "WinHttpAutoProxySvcMain");
    winhttp.OrignalWinHttpCheckPlatform = GetProcAddress(winhttp.dll, "WinHttpCheckPlatform");
    winhttp.OrignalWinHttpCloseHandle = GetProcAddress(winhttp.dll, "WinHttpCloseHandle");
    winhttp.OrignalWinHttpConnect = GetProcAddress(winhttp.dll, "WinHttpConnect");
    winhttp.OrignalWinHttpConnectionDeletePolicyEntries = GetProcAddress(winhttp.dll, "WinHttpConnectionDeletePolicyEntries");
    winhttp.OrignalWinHttpConnectionDeleteProxyInfo = GetProcAddress(winhttp.dll, "WinHttpConnectionDeleteProxyInfo");
    winhttp.OrignalWinHttpConnectionFreeNameList = GetProcAddress(winhttp.dll, "WinHttpConnectionFreeNameList");
    winhttp.OrignalWinHttpConnectionFreeProxyInfo = GetProcAddress(winhttp.dll, "WinHttpConnectionFreeProxyInfo");
    winhttp.OrignalWinHttpConnectionFreeProxyList = GetProcAddress(winhttp.dll, "WinHttpConnectionFreeProxyList");
    winhttp.OrignalWinHttpConnectionGetNameList = GetProcAddress(winhttp.dll, "WinHttpConnectionGetNameList");
    winhttp.OrignalWinHttpConnectionGetProxyInfo = GetProcAddress(winhttp.dll, "WinHttpConnectionGetProxyInfo");
    winhttp.OrignalWinHttpConnectionGetProxyList = GetProcAddress(winhttp.dll, "WinHttpConnectionGetProxyList");
    winhttp.OrignalWinHttpConnectionOnlyConvert = GetProcAddress(winhttp.dll, "WinHttpConnectionOnlyConvert");
    winhttp.OrignalWinHttpConnectionOnlyReceive = GetProcAddress(winhttp.dll, "WinHttpConnectionOnlyReceive");
    winhttp.OrignalWinHttpConnectionOnlySend = GetProcAddress(winhttp.dll, "WinHttpConnectionOnlySend");
    winhttp.OrignalWinHttpConnectionSetPolicyEntries = GetProcAddress(winhttp.dll, "WinHttpConnectionSetPolicyEntries");
    winhttp.OrignalWinHttpConnectionSetProxyInfo = GetProcAddress(winhttp.dll, "WinHttpConnectionSetProxyInfo");
    winhttp.OrignalWinHttpConnectionUpdateIfIndexTable = GetProcAddress(winhttp.dll, "WinHttpConnectionUpdateIfIndexTable");
    winhttp.OrignalWinHttpCrackUrl = GetProcAddress(winhttp.dll, "WinHttpCrackUrl");
    winhttp.OrignalWinHttpCreateProxyResolver = GetProcAddress(winhttp.dll, "WinHttpCreateProxyResolver");
    winhttp.OrignalWinHttpCreateUrl = GetProcAddress(winhttp.dll, "WinHttpCreateUrl");
    winhttp.OrignalWinHttpDetectAutoProxyConfigUrl = GetProcAddress(winhttp.dll, "WinHttpDetectAutoProxyConfigUrl");
    winhttp.OrignalWinHttpFreeProxyResult = GetProcAddress(winhttp.dll, "WinHttpFreeProxyResult");
    winhttp.OrignalWinHttpFreeProxyResultEx = GetProcAddress(winhttp.dll, "WinHttpFreeProxyResultEx");
    winhttp.OrignalWinHttpFreeProxySettings = GetProcAddress(winhttp.dll, "WinHttpFreeProxySettings");
    winhttp.OrignalWinHttpFreeProxySettingsEx = GetProcAddress(winhttp.dll, "WinHttpFreeProxySettingsEx");
    winhttp.OrignalWinHttpFreeQueryConnectionGroupResult = GetProcAddress(winhttp.dll, "WinHttpFreeQueryConnectionGroupResult");
    winhttp.OrignalWinHttpGetDefaultProxyConfiguration = GetProcAddress(winhttp.dll, "WinHttpGetDefaultProxyConfiguration");
    winhttp.OrignalWinHttpGetIEProxyConfigForCurrentUser = GetProcAddress(winhttp.dll, "WinHttpGetIEProxyConfigForCurrentUser");
    winhttp.OrignalWinHttpGetProxyForUrl = GetProcAddress(winhttp.dll, "WinHttpGetProxyForUrl");
    winhttp.OrignalWinHttpGetProxyForUrlEx = GetProcAddress(winhttp.dll, "WinHttpGetProxyForUrlEx");
    winhttp.OrignalWinHttpGetProxyForUrlEx2 = GetProcAddress(winhttp.dll, "WinHttpGetProxyForUrlEx2");
    winhttp.OrignalWinHttpGetProxyForUrlHvsi = GetProcAddress(winhttp.dll, "WinHttpGetProxyForUrlHvsi");
    winhttp.OrignalWinHttpGetProxyResult = GetProcAddress(winhttp.dll, "WinHttpGetProxyResult");
    winhttp.OrignalWinHttpGetProxyResultEx = GetProcAddress(winhttp.dll, "WinHttpGetProxyResultEx");
    winhttp.OrignalWinHttpGetProxySettingsEx = GetProcAddress(winhttp.dll, "WinHttpGetProxySettingsEx");
    winhttp.OrignalWinHttpGetProxySettingsResultEx = GetProcAddress(winhttp.dll, "WinHttpGetProxySettingsResultEx");
    winhttp.OrignalWinHttpGetProxySettingsVersion = GetProcAddress(winhttp.dll, "WinHttpGetProxySettingsVersion");
    winhttp.OrignalWinHttpGetTunnelSocket = GetProcAddress(winhttp.dll, "WinHttpGetTunnelSocket");
    winhttp.OrignalWinHttpOpen = GetProcAddress(winhttp.dll, "WinHttpOpen");
    winhttp.OrignalWinHttpOpenRequest = GetProcAddress(winhttp.dll, "WinHttpOpenRequest");
    winhttp.OrignalWinHttpPacJsWorkerMain = GetProcAddress(winhttp.dll, "WinHttpPacJsWorkerMain");
    winhttp.OrignalWinHttpProbeConnectivity = GetProcAddress(winhttp.dll, "WinHttpProbeConnectivity");
    winhttp.OrignalWinHttpQueryAuthSchemes = GetProcAddress(winhttp.dll, "WinHttpQueryAuthSchemes");
    winhttp.OrignalWinHttpQueryConnectionGroup = GetProcAddress(winhttp.dll, "WinHttpQueryConnectionGroup");
    winhttp.OrignalWinHttpQueryDataAvailable = GetProcAddress(winhttp.dll, "WinHttpQueryDataAvailable");
    winhttp.OrignalWinHttpQueryHeaders = GetProcAddress(winhttp.dll, "WinHttpQueryHeaders");
    winhttp.OrignalWinHttpQueryHeadersEx = GetProcAddress(winhttp.dll, "WinHttpQueryHeadersEx");
    winhttp.OrignalWinHttpQueryOption = GetProcAddress(winhttp.dll, "WinHttpQueryOption");
    winhttp.OrignalWinHttpReadData = GetProcAddress(winhttp.dll, "WinHttpReadData");
    winhttp.OrignalWinHttpReadDataEx = GetProcAddress(winhttp.dll, "WinHttpReadDataEx");
    winhttp.OrignalWinHttpReadProxySettings = GetProcAddress(winhttp.dll, "WinHttpReadProxySettings");
    winhttp.OrignalWinHttpReadProxySettingsHvsi = GetProcAddress(winhttp.dll, "WinHttpReadProxySettingsHvsi");
    winhttp.OrignalWinHttpReceiveResponse = GetProcAddress(winhttp.dll, "WinHttpReceiveResponse");
    winhttp.OrignalWinHttpRegisterProxyChangeNotification = GetProcAddress(winhttp.dll, "WinHttpRegisterProxyChangeNotification");
    winhttp.OrignalWinHttpResetAutoProxy = GetProcAddress(winhttp.dll, "WinHttpResetAutoProxy");
    winhttp.OrignalWinHttpSaveProxyCredentials = GetProcAddress(winhttp.dll, "WinHttpSaveProxyCredentials");
    winhttp.OrignalWinHttpSendRequest = GetProcAddress(winhttp.dll, "WinHttpSendRequest");
    winhttp.OrignalWinHttpSetCredentials = GetProcAddress(winhttp.dll, "WinHttpSetCredentials");
    winhttp.OrignalWinHttpSetDefaultProxyConfiguration = GetProcAddress(winhttp.dll, "WinHttpSetDefaultProxyConfiguration");
    winhttp.OrignalWinHttpSetOption = GetProcAddress(winhttp.dll, "WinHttpSetOption");
    winhttp.OrignalWinHttpSetProxySettingsPerUser = GetProcAddress(winhttp.dll, "WinHttpSetProxySettingsPerUser");
    winhttp.OrignalWinHttpSetSecureLegacyServersAppCompat = GetProcAddress(winhttp.dll, "WinHttpSetSecureLegacyServersAppCompat");
    winhttp.OrignalWinHttpSetStatusCallback = GetProcAddress(winhttp.dll, "WinHttpSetStatusCallback");
    winhttp.OrignalWinHttpSetTimeouts = GetProcAddress(winhttp.dll, "WinHttpSetTimeouts");
    winhttp.OrignalWinHttpTimeFromSystemTime = GetProcAddress(winhttp.dll, "WinHttpTimeFromSystemTime");
    winhttp.OrignalWinHttpTimeToSystemTime = GetProcAddress(winhttp.dll, "WinHttpTimeToSystemTime");
    winhttp.OrignalWinHttpUnregisterProxyChangeNotification = GetProcAddress(winhttp.dll, "WinHttpUnregisterProxyChangeNotification");
    winhttp.OrignalWinHttpWebSocketClose = GetProcAddress(winhttp.dll, "WinHttpWebSocketClose");
    winhttp.OrignalWinHttpWebSocketCompleteUpgrade = GetProcAddress(winhttp.dll, "WinHttpWebSocketCompleteUpgrade");
    winhttp.OrignalWinHttpWebSocketQueryCloseStatus = GetProcAddress(winhttp.dll, "WinHttpWebSocketQueryCloseStatus");
    winhttp.OrignalWinHttpWebSocketReceive = GetProcAddress(winhttp.dll, "WinHttpWebSocketReceive");
    winhttp.OrignalWinHttpWebSocketSend = GetProcAddress(winhttp.dll, "WinHttpWebSocketSend");
    winhttp.OrignalWinHttpWebSocketShutdown = GetProcAddress(winhttp.dll, "WinHttpWebSocketShutdown");
    winhttp.OrignalWinHttpWriteData = GetProcAddress(winhttp.dll, "WinHttpWriteData");
    winhttp.OrignalWinHttpWriteProxySettings = GetProcAddress(winhttp.dll, "WinHttpWriteProxySettings");
    return true;
}

#if defined(__cplusplus)
}
#endif
