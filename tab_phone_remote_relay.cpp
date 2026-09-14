// ════════════════════════════════════════════════════════════════════
// tab_phone_remote_relay.cpp
//
// Firebase Firestore REST upload/delete — no SDK, just WinINet HTTP.
// This lets phone find PC by 6-digit code alone (no manual IP needed).
// ════════════════════════════════════════════════════════════════════

#pragma warning(disable: 4996)
#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "tab_phone_remote_relay.h"
#include <windows.h>
#include <wininet.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>
#include <vector>
#include <ctime>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace std;

// Forward declaration — defined in tab_phone_remote.cpp
extern void RelayInjectInput(const std::string& json);

// ── Helpers ──────────────────────────────────────────────────────

// Get machine hostname for display on phone
static string GetHostname() {
    char buf[256] = {};
    DWORD len = sizeof(buf);
    GetComputerNameA(buf, &len);
    return string(buf);
}

// Simple HTTP PATCH via WinINet (Firestore REST uses PATCH to upsert)
static bool HttpRequest(const string& method,   // "PATCH" or "DELETE"
                        const string& url,
                        const string& body) {
    // Parse URL components
    URL_COMPONENTSA uc = {};
    uc.dwStructSize = sizeof(uc);
    char host[256] = {}, path[1024] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = sizeof(host);
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = sizeof(path);
    uc.dwSchemeLength = 1;
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc)) return false;

    bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = uc.nPort ? uc.nPort : (isHttps ? 443 : 80);

    HINTERNET hInet = InternetOpenA("RasFocus/1.0",
                                    INTERNET_OPEN_TYPE_PRECONFIG,
                                    NULL, NULL, 0);
    if (!hInet) return false;

    HINTERNET hConn = InternetConnectA(hInet, host, port,
                                       NULL, NULL,
                                       INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (isHttps) flags |= INTERNET_FLAG_SECURE |
                          INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
                          INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;

    HINTERNET hReq = HttpOpenRequestA(hConn, method.c_str(), path,
                                      NULL, NULL, NULL, flags, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    // Content-Type header for JSON body
    const char* hdrs = "Content-Type: application/json\r\n";
    BOOL ok = HttpSendRequestA(hReq, hdrs, (DWORD)strlen(hdrs),
                               body.empty() ? NULL : (LPVOID)body.c_str(),
                               (DWORD)body.size());
    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return ok == TRUE;
}

// ── Firestore document JSON builder ──────────────────────────────
// Firestore REST API expects a specific JSON format for fields.
static string BuildFirestoreDocument(const string& code,
                                     const string& ip,
                                     int port) {
    time_t now = time(nullptr);
    string ts = to_string((long long)now);
    string hostname = GetHostname();

    // Firestore "document" format
    // {"fields": {"key": {"stringValue": "val"}, ...}}
    string json = "{\"fields\":{"
        "\"code\":{\"stringValue\":\"" + code + "\"},"
        "\"ip\":{\"stringValue\":\"" + ip + "\"},"
        "\"port\":{\"integerValue\":\"" + to_string(port) + "\"},"
        "\"hostname\":{\"stringValue\":\"" + hostname + "\"},"
        "\"platform\":{\"stringValue\":\"windows\"},"
        "\"ts\":{\"integerValue\":\"" + ts + "\"}"
        "}}";
    return json;
}

// ── Public API ───────────────────────────────────────────────────

void RelayRegisterSession(const string& code,
                          const string& localIp,
                          int port) {
    // Run in background thread — don't block the UI
    thread([code, localIp, port]() {
        string url = FIRESTORE_BASE + code + "?key=" + FIREBASE_API_KEY;
        string body = BuildFirestoreDocument(code, localIp, port);
        // PATCH = create or update
        bool ok = HttpRequest("PATCH", url, body);
        // Silent — no UI feedback needed
        (void)ok;
    }).detach();
}

void RelayUnregisterSession(const string& code) {
    if (code.empty()) return;
    thread([code]() {
        string url = FIRESTORE_BASE + code + "?key=" + FIREBASE_API_KEY;
        HttpRequest("DELETE", url, "");
    }).detach();
}

bool RelayIsAvailable() {
    // For now, always return true — relay is always attempted as fallback
    return true;
}

// ════════════════════════════════════════════════════════════════════
// Relay HOST connection
//
// PC connects to wss://relay.rasfocus.com/relay/<code> as host.
// Phone connects to same URL as client.
// Relay server bridges the two — data is piped bidirectionally.
//
// This uses raw WinSock + manual TLS via WinHTTP WebSocket API.
// The relay runs on port 443 (wss).
// ════════════════════════════════════════════════════════════════════

#include <winhttp.h>
#include <atomic>
#include <thread>
#include <vector>
#pragma comment(lib, "winhttp.lib")

static HINTERNET g_relaySession  = NULL;
static HINTERNET g_relayConnect  = NULL;
static HINTERNET g_relayRequest  = NULL;
static HINTERNET g_relayWs       = NULL;
static std::atomic<bool> g_relayRunning { false };

// Callback: called when relay receives data from phone → forward to local WS clients
// This bridges relay→PC→phone direction (relay delivers phone input to PC)

static void RelayHostThread(const std::string& code) {
    g_relayRunning.store(true);

    g_relaySession = WinHttpOpen(L"RasFocus-PC/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_relaySession) { g_relayRunning.store(false); return; }

    g_relayConnect = WinHttpConnect(g_relaySession,
                                     L"relay.rasfocus.com",
                                     INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!g_relayConnect) {
        WinHttpCloseHandle(g_relaySession); g_relaySession = NULL;
        g_relayRunning.store(false); return;
    }

    // Path: /relay/<code>
    std::wstring path = L"/relay/" + std::wstring(code.begin(), code.end());
    g_relayRequest = WinHttpOpenRequest(g_relayConnect, L"GET", path.c_str(),
                                         NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE);
    if (!g_relayRequest) {
        WinHttpCloseHandle(g_relayConnect); g_relayConnect = NULL;
        WinHttpCloseHandle(g_relaySession); g_relaySession = NULL;
        g_relayRunning.store(false); return;
    }

    // Set headers for WS upgrade + identify as host
    WinHttpAddRequestHeaders(g_relayRequest,
        L"X-Relay-Role: host\r\n", (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD);

    // Upgrade to WebSocket
    WinHttpSetOption(g_relayRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    if (!WinHttpSendRequest(g_relayRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(g_relayRequest, NULL)) {
        WinHttpCloseHandle(g_relayRequest); g_relayRequest = NULL;
        WinHttpCloseHandle(g_relayConnect); g_relayConnect = NULL;
        WinHttpCloseHandle(g_relaySession); g_relaySession = NULL;
        g_relayRunning.store(false); return;
    }

    g_relayWs = WinHttpWebSocketCompleteUpgrade(g_relayRequest, NULL);
    WinHttpCloseHandle(g_relayRequest); g_relayRequest = NULL;
    if (!g_relayWs) {
        WinHttpCloseHandle(g_relayConnect); g_relayConnect = NULL;
        WinHttpCloseHandle(g_relaySession); g_relaySession = NULL;
        g_relayRunning.store(false); return;
    }

    // Send relay_host registration
    std::string hostMsg = "{\"type\":\"relay_host\",\"code\":\"" + code + "\"}";
    WinHttpWebSocketSend(g_relayWs, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                          (PVOID)hostMsg.c_str(), (DWORD)hostMsg.size());

    // Receive loop: phone sends input → relay forwards to PC → inject
    std::vector<BYTE> buf(65536);
    while (g_relayRunning.load()) {
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType;
        DWORD total = 0;
        std::vector<BYTE> msg;

        // Read complete message (may come in chunks)
        do {
            DWORD r = 0;
            DWORD err = WinHttpWebSocketReceive(g_relayWs,
                            buf.data(), (DWORD)buf.size(),
                            &r, &bufType);
            if (err != ERROR_SUCCESS) { g_relayRunning.store(false); break; }
            msg.insert(msg.end(), buf.begin(), buf.begin() + r);
        } while (bufType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
                 bufType == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE);

        if (!g_relayRunning.load()) break;

        if (bufType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && !msg.empty()) {
            std::string text(msg.begin(), msg.end());
            // Relay control: route messages
            if (text.find("relay_ready") != std::string::npos ||
                text.find("peer_connected") != std::string::npos) {
                // Phone connected — already streaming via PcStreamerStart
            } else if (text.find("\"type\":\"auth\"") != std::string::npos) {
                // Phone auth via relay — send ready
                std::string rdy = "{\"type\":\"ready\",\"width\":1920,\"height\":1080,\"fps\":30,\"mode\":\"h264\"}";
                WinHttpWebSocketSend(g_relayWs, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                      (PVOID)rdy.c_str(), (DWORD)rdy.size());
            } else if (text.find("\"type\"") != std::string::npos) {
                // Mouse/key/scroll from phone via relay
                RelayInjectInput(text);
            }
        } else if (bufType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            // Binary from phone? Should not happen in relay direction, ignore
        } else if (bufType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            break;
        }
    }

    if (g_relayWs)      { WinHttpWebSocketClose(g_relayWs, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0); WinHttpCloseHandle(g_relayWs); g_relayWs = NULL; }
    if (g_relayConnect) { WinHttpCloseHandle(g_relayConnect); g_relayConnect = NULL; }
    if (g_relaySession) { WinHttpCloseHandle(g_relaySession); g_relaySession = NULL; }
    g_relayRunning.store(false);
}

// Also need to broadcast H264 frames to relay phone clients
// This sends a binary frame to the relay WebSocket so phone gets video via relay
void RelaySendBinary(const void* data, size_t len) {
    if (!g_relayWs || !g_relayRunning.load()) return;
    WinHttpWebSocketSend(g_relayWs,
                          WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                          const_cast<void*>(data), (DWORD)len);
}

void RelayHostStart(const std::string& code) {
    if (g_relayRunning.load()) return;
    std::thread(RelayHostThread, code).detach();
}

void RelayHostStop() {
    g_relayRunning.store(false);
    if (g_relayWs) {
        WinHttpWebSocketClose(g_relayWs, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    }
}
