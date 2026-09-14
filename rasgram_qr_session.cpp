// rasgram_qr_session.cpp
// RasGram Desktop — QR Login Session (PC side)
//
// ============================================================
// WHAT THIS FILE DOES
// ============================================================
//  1. Generates a cryptographically random 32-char hex token
//  2. Writes  qr_sessions/{token}  to Firestore:
//       { status:"waiting", createdAt:<epochMs>, expiryAt:<+60s> }
//  3. Encodes the token string into a real QR code using the
//     Nayuki QR Code generator (header-only C++ port bundled below)
//  4. Exposes  RgQr_Poll()  which checks Firestore every 2 s
//     for status=="confirmed" written by the Android app
//  5. On confirmation, returns uid/mobile/name/idToken so the
//     caller can call RgNet_Init() and switch to App screen

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>       // CryptGenRandom
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

#include "rasgram_qr_session.h"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstdint>

using namespace std;

#include "qrcodegen.hpp"
using namespace qrcodegen;

// ============================================================
// MODULE STATE
// ============================================================
static string s_apiKey;
static string s_firestoreHost;
static string s_projectId;

static string     s_token;
static RgQrMatrix s_matrix;
static RgQrUser   s_user;

static atomic<RgQrStatus> s_status { RgQrStatus::Waiting };
static DWORD  s_startTick = 0;
static DWORD  s_lastPollTick = 0;
static mutex  s_mtx;

static const DWORD SESSION_TTL_MS  = 60000;  // 60 s
static const DWORD POLL_INTERVAL_MS = 2000;  // 2 s

// ============================================================
// HELPERS — HTTP / FIRESTORE REST (WinINet)
// ============================================================
static string HttpRequest(const string& method, const string& host,
                          const string& path, const string& body = "",
                          const string& extraHeaders = "") {
    string resp;
    HINTERNET hI = InternetOpenA("RasGram-QR/1.0",
                      INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hI) return resp;

    HINTERNET hC = InternetConnectA(hI, host.c_str(),
                      INTERNET_DEFAULT_HTTPS_PORT,
                      NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hC) {
        HINTERNET hR = HttpOpenRequestA(hC, method.c_str(), path.c_str(),
                          NULL, NULL, NULL,
                          INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                          INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hR) {
            string hdrs = "Content-Type: application/json\r\n" + extraHeaders;
            HttpSendRequestA(hR, hdrs.c_str(), (DWORD)hdrs.size(),
                             body.empty() ? NULL : (LPVOID)body.c_str(),
                             (DWORD)body.size());
            char buf[4096]; DWORD n = 0;
            while (InternetReadFile(hR, buf, sizeof(buf)-1, &n) && n > 0) {
                buf[n] = 0; resp += buf;
            }
            InternetCloseHandle(hR);
        }
        InternetCloseHandle(hC);
    }
    InternetCloseHandle(hI);
    return resp;
}

static string HttpPost(const string& host, const string& path,
                       const string& body, const string& extraHeaders = "") {
    return HttpRequest("POST", host, path, body, extraHeaders);
}

static string HttpGet(const string& host, const string& path) {
    return HttpRequest("GET", host, path);
}

static string ParseStr(const string& json, const string& field) {
    auto tryPat = [&](const string& pat) -> string {
        size_t p = json.find(pat);
        if (p == string::npos) return "";
        p += pat.size();
        string v;
        while (p < json.size() && json[p] != '"') {
            if (json[p] == '\\' && p+1 < json.size()) { p++; v += json[p]; }
            else v += json[p];
            p++;
        }
        return v;
    };
    string sv = tryPat("\"" + field + "\":{\"stringValue\":\"");
    if (!sv.empty()) return sv;
    return tryPat("\"" + field + "\":\"");
}

// ── Firestore path builder (with API key) ────────────────────
static string FsPath(const string& collection, const string& docId) {
    return "/v1/projects/" + s_projectId +
           "/databases/(default)/documents/" + collection + "/" + docId +
           "?key=" + s_apiKey;
}

// ── Token generator (32 hex chars via CryptGenRandom) ────────
static string MakeToken() {
    BYTE raw[16];
    HCRYPTPROV prov = 0;
    if (CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(prov, sizeof(raw), raw);
        CryptReleaseContext(prov, 0);
    } else {
        // Fallback: mix tick + counter
        DWORD t = GetTickCount();
        for (int i = 0; i < 16; i++) {
            t = t * 1664525u + 1013904223u;
            raw[i] = (BYTE)(t >> 16);
        }
    }
    string hex;
    hex.reserve(32);
    for (BYTE b : raw) {
        static const char H[] = "0123456789abcdef";
        hex += H[b >> 4]; hex += H[b & 0xF];
    }
    return hex;
}

// ── Write qr_sessions/{token} to Firestore ───────────────────
static bool WriteSession(const string& token) {
    long long now = (long long)GetTickCount();
    // Use epoch ms from FILETIME for a real timestamp
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULONGLONG ull = (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    long long epochMs = (long long)((ull - 116444736000000000ULL) / 10000ULL);

    string body =
        "{\"fields\":{"
        "\"status\":{\"stringValue\":\"waiting\"},"
        "\"createdAt\":{\"integerValue\":\"" + to_string(epochMs) + "\"},"
        "\"expiryAt\":{\"integerValue\":\"" + to_string(epochMs + SESSION_TTL_MS) + "\"}"
        "}}";

    // PATCH (create or overwrite) Firestore document
    string path = FsPath("qr_sessions", token);
    string resp = HttpRequest("PATCH", s_firestoreHost, path, body);
    return resp.find("\"name\"") != string::npos ||
           resp.find(token) != string::npos;
}

// ── Poll qr_sessions/{token} ─────────────────────────────────
static RgQrUser PollSession(const string& token) {
    string path = FsPath("qr_sessions", token);
    string resp = HttpGet(s_firestoreHost, path);
    RgQrUser u;
    if (resp.empty()) return u;
    string status = ParseStr(resp, "status");
    if (status != "confirmed") return u;
    u.uid     = ParseStr(resp, "uid");
    u.mobile  = ParseStr(resp, "mobile");
    u.name    = ParseStr(resp, "name");
    u.idToken = ParseStr(resp, "idToken");
    u.email   = ParseStr(resp, "email");
    return u;
}

// ============================================================
// PUBLIC API IMPLEMENTATION
// ============================================================

void RgQr_Init(const string& apiKey,
               const string& firestoreHost,
               const string& projectId) {
    s_apiKey       = apiKey;
    s_firestoreHost = firestoreHost;
    s_projectId    = projectId;
}

void RgQr_StartSession() {
    lock_guard<mutex> lk(s_mtx);
    s_status = RgQrStatus::Waiting;
    s_user   = {};

    string token = MakeToken();
    s_token  = token;
    s_startTick  = GetTickCount();
    s_lastPollTick = s_startTick;

    // Build QR matrix using official Nayuki library
    RgQrMatrix mat;
    try {
        std::string qrData = "rasgram://qr/" + token;   // phone expects this prefix
        QrCode qr = QrCode::encodeText(qrData.c_str(), QrCode::Ecc::MEDIUM);
        int sz = qr.getSize();
        mat.size = sz;
        mat.cells.assign(sz, vector<bool>(sz));
        for (int r = 0; r < sz; r++)
            for (int c = 0; c < sz; c++)
                mat.cells[r][c] = qr.getModule(c, r);
        mat.token = token;
        mat.ready = true;
    } catch (...) {
        mat.ready = false;
    }
    s_matrix = mat;

    // Write session to Firestore on background thread (don't block UI)
    thread([token]() {
        bool ok = WriteSession(token);
        if (!ok) {
            lock_guard<mutex> lk2(s_mtx);
            if (s_token == token) s_status = RgQrStatus::Error;
        }
    }).detach();
}

RgQrStatus RgQr_Poll() {
    lock_guard<mutex> lk(s_mtx);

    if (s_token.empty()) return RgQrStatus::Error;

    DWORD now = GetTickCount();

    // Check expiry
    if (now - s_startTick >= SESSION_TTL_MS) {
        s_status = RgQrStatus::Expired;
        return RgQrStatus::Expired;
    }

    // Already confirmed or errored — return cached status
    if (s_status == RgQrStatus::Confirmed ||
        s_status == RgQrStatus::Error)
        return s_status.load();

    // Throttle Firestore calls
    if (now - s_lastPollTick < POLL_INTERVAL_MS)
        return s_status.load();
    s_lastPollTick = now;

    // Poll on background thread to keep UI responsive
    string tokenCopy = s_token;
    thread([tokenCopy]() {
        RgQrUser u = PollSession(tokenCopy);
        if (!u.uid.empty()) {
            lock_guard<mutex> lk2(s_mtx);
            if (s_token == tokenCopy) {
                s_user   = u;
                s_status = RgQrStatus::Confirmed;
            }
        }
    }).detach();

    return s_status.load();
}

RgQrStatus RgQr_GetStatus() {
    return s_status.load();
}

const RgQrMatrix& RgQr_GetMatrix() {
    return s_matrix;
}

const RgQrUser& RgQr_GetUser() {
    return s_user;
}

DWORD RgQr_ElapsedMs() {
    if (s_startTick == 0) return 0;
    return GetTickCount() - s_startTick;
}

void RgQr_Clear() {
    lock_guard<mutex> lk(s_mtx);
    s_token  = "";
    s_matrix = {};
    s_user   = {};
    s_status = RgQrStatus::Waiting;
    s_startTick = 0;
    s_lastPollTick = 0;
}
