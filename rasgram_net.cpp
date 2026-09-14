// rasgram_net.cpp
// RasGram Desktop — Network & Backend Implementation
//
// Features:
//   • Firebase Firestore REST API (WinINet/HTTPS) — same DB as Android RasGram
//   • LAN peer discovery: UDP broadcast port 5555 (identical to Android LanChatManager)
//   • LAN messaging: TCP port 5556 (JSON header + binary payload)
//   • Audio call: WASAPI capture → Opus encode → UDP RTP → decode → WASAPI render
//   • Video call: DirectShow camera → MJPEG/RGB → UDP → GDI+ render
//
// Architecture note:
//   All Firestore operations use WinINet HTTPS (same helper pattern as main.cpp
//   SendFirestoreRequest). The idToken from accounts login is reused for
//   authenticated writes. Unauthenticated reads work via open Firestore rules
//   (same as Android dev environment).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "rasgram_net.h"
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>    // CryptAcquireContext, CryptHashData — SHA-256 for E2EE key
#include <bcrypt.h>      // BCryptEncrypt / BCryptDecrypt — AES-CBC E2EE (same as Android)
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <dshow.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <initguid.h>

// ISampleGrabberCB / ISampleGrabber — manually declared since qedit.h
// is not reliably present in modern Windows SDKs.
DEFINE_GUID(IID_ISampleGrabberCB_rg,
    0x0579154a,0x2b53,0x4994,0xb0,0xd0,0xe7,0x73,0x14,0x8e,0xff,0x85);
DEFINE_GUID(IID_ISampleGrabber_rg,
    0x6b652fff,0x11fe,0x4fce,0x92,0xad,0x02,0x66,0xb5,0xd7,0xc7,0x8f);
DEFINE_GUID(CLSID_SampleGrabber_rg,
    0xc1f400a0,0x3f08,0x11d3,0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37);
DEFINE_GUID(CLSID_NullRenderer_rg,
    0xc1f400a4,0x3f08,0x11d3,0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37);

#undef IID_ISampleGrabberCB
#define IID_ISampleGrabberCB IID_ISampleGrabberCB_rg
#undef IID_ISampleGrabber
#define IID_ISampleGrabber   IID_ISampleGrabber_rg
#undef CLSID_SampleGrabber
#define CLSID_SampleGrabber  CLSID_SampleGrabber_rg
#undef CLSID_NullRenderer
#define CLSID_NullRenderer   CLSID_NullRenderer_rg

struct ISampleGrabberCB : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double SampleTime, IMediaSample* pSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double SampleTime, BYTE* pBuffer, long BufferLen) = 0;
};
struct ISampleGrabber : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* pBufferSize, long* pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* pCallback, long WhichMethodToCallback) = 0;
};
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <functional>
#include <map>
#include <chrono>
#include <ctime>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "strmiids.lib")

using namespace std;

// ============================================================
// MODULE STATE
// ============================================================
static string g_myMobile;
static string g_myName;
static string g_myUid;
static string g_idToken;      // Firebase Auth ID token (from accounts module)

static atomic<bool> g_chatPollRunning  { false };
static atomic<bool> g_msgPollRunning   { false };
static atomic<bool> g_lanRunning       { false };
static atomic<bool> g_callActive       { false };
static atomic<bool> g_callMuted        { false };
static atomic<bool> g_callVideo        { false };
static atomic<int>  g_callDuration     { 0 };

static thread g_chatPollThread;
static thread g_msgPollThread;
static thread g_lanBeaconThread;
static thread g_lanListenThread;
static thread g_lanTcpThread;
static thread g_callThread;

static SOCKET g_udpBeaconSock  = INVALID_SOCKET;
static SOCKET g_udpListenSock  = INVALID_SOCKET;
static SOCKET g_tcpServerSock  = INVALID_SOCKET;
static SOCKET g_callUdpSock    = INVALID_SOCKET;

static mutex g_lanPeersMtx;
static map<string, RgLanPeer>    g_lanPeers;      // mobile → peer
static map<string, long long>    g_lanPeerSeen;   // mobile → last seen ms

// ============================================================
// HELPERS — TIME
// ============================================================
static long long NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count();
}

// ============================================================
// HELPERS — HTTP / FIRESTORE REST
// ============================================================
string RgFirestoreGet(const string& path) {
    string resp;
    HINTERNET hInet = InternetOpenA("RasGram-Desktop/1.0",
                        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return resp;

    HINTERNET hConn = InternetConnectA(hInet, RG_FIRESTORE_HOST,
                        INTERNET_DEFAULT_HTTPS_PORT,
                        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConn) {
        HINTERNET hReq = HttpOpenRequestA(hConn, "GET", path.c_str(),
                            NULL, NULL, NULL,
                            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                            INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hReq) {
            // Add Authorization header if we have an ID token
            string headers;
            if (!g_idToken.empty())
                headers = "Authorization: Bearer " + g_idToken + "\r\n";
            HttpSendRequestA(hReq,
                headers.empty() ? NULL : headers.c_str(),
                (DWORD)headers.size(),
                NULL, 0);
            char buf[4096]; DWORD n = 0;
            while (InternetReadFile(hReq, buf, sizeof(buf)-1, &n) && n > 0) {
                buf[n] = '\0'; resp += buf;
            }
            InternetCloseHandle(hReq);
        }
        InternetCloseHandle(hConn);
    }
    InternetCloseHandle(hInet);
    return resp;
}

string RgFirestorePost(const string& method, const string& path,
                       const string& jsonBody) {
    string resp;
    HINTERNET hInet = InternetOpenA("RasGram-Desktop/1.0",
                        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return resp;

    HINTERNET hConn = InternetConnectA(hInet, RG_FIRESTORE_HOST,
                        INTERNET_DEFAULT_HTTPS_PORT,
                        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConn) {
        HINTERNET hReq = HttpOpenRequestA(hConn, method.c_str(), path.c_str(),
                            NULL, NULL, NULL,
                            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                            INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hReq) {
            string headers = "Content-Type: application/json\r\n";
            if (!g_idToken.empty())
                headers += "Authorization: Bearer " + g_idToken + "\r\n";
            HttpSendRequestA(hReq,
                headers.c_str(), (DWORD)headers.size(),
                (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size());
            char buf[4096]; DWORD n = 0;
            while (InternetReadFile(hReq, buf, sizeof(buf)-1, &n) && n > 0) {
                buf[n] = '\0'; resp += buf;
            }
            InternetCloseHandle(hReq);
        }
        InternetCloseHandle(hConn);
    }
    InternetCloseHandle(hInet);
    return resp;
}

// ============================================================
// HELPERS — JSON PARSING (lightweight, no dependency)
// ============================================================
string RgParseField(const string& json, const string& field) {
    // Looks for "field":{"stringValue":"VALUE"} or "field":"VALUE"
    auto tryPattern = [&](const string& pat) -> string {
        size_t p = json.find(pat);
        if (p == string::npos) return "";
        p += pat.size();
        // skip whitespace
        while (p < json.size() && (json[p]==' '||json[p]=='\t')) p++;
        if (p >= json.size()) return "";
        char delim = json[p];
        if (delim != '"') return "";
        p++;
        string val;
        while (p < json.size() && json[p] != '"') {
            if (json[p]=='\\' && p+1<json.size()) { p++; val += json[p]; }
            else val += json[p];
            p++;
        }
        return val;
    };

    // Try stringValue pattern first (Firestore REST format)
    string sv = tryPattern("\"" + field + "\":{\"stringValue\":\"");
    if (!sv.empty()) return sv;
    // Try plain string
    sv = tryPattern("\"" + field + "\":\"");
    return sv;
}

long long RgParseIntField(const string& json, const string& field) {
    // "field":{"integerValue":"VALUE"} or "field":NUMBER
    size_t p = json.find("\"" + field + "\":{\"integerValue\":\"");
    if (p != string::npos) {
        p += ("\"" + field + "\":{\"integerValue\":\"").size();
        string num;
        while (p < json.size() && json[p] != '"') num += json[p++];
        return num.empty() ? 0 : atoll(num.c_str());
    }
    p = json.find("\"" + field + "\":");
    if (p != string::npos) {
        p += ("\"" + field + "\":").size();
        while (p < json.size() && (json[p]==' '||json[p]=='\t')) p++;
        string num;
        while (p < json.size() && (isdigit(json[p])||json[p]=='-')) num += json[p++];
        return num.empty() ? 0 : atoll(num.c_str());
    }
    return 0;
}

bool RgParseBoolField(const string& json, const string& field) {
    size_t p = json.find("\"" + field + "\":{\"booleanValue\":");
    if (p != string::npos) {
        p += ("\"" + field + "\":{\"booleanValue\":").size();
        return json.substr(p, 4) == "true";
    }
    return false;
}

string RgFormatTime(long long timestampMs) {
    if (timestampMs <= 0) return "";
    time_t t = (time_t)(timestampMs / 1000);
    struct tm* tm_info = localtime(&t);
    if (!tm_info) return "";
    char buf[16];
    strftime(buf, sizeof(buf), "%I:%M %p", tm_info);
    return buf;
}

// ============================================================
// E2EE: AES-128-CBC  (matches Android AESCrypto exactly)
// key+iv = SHA-256(chatId + SALT), split 16+16 bytes
// output format: "E2EE:" + Base64(ciphertext)
// ============================================================
static const char* RG_E2EE_SALT = "RasGram_E2EE_Secret_Salt_2026";

static void RgE2EE_GetKeyIv(const string& chatId,
                              BYTE key[16], BYTE iv[16]) {
    // SHA-256(chatId + SALT) via CryptoAPI
    string data = chatId + RG_E2EE_SALT;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);
    CryptHashData(hHash, (BYTE*)data.c_str(), (DWORD)data.size(), 0);
    BYTE hash[32]; DWORD hashLen = 32;
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    memcpy(key, hash,      16);
    memcpy(iv,  hash + 16, 16);
}

// Base64 encode (RFC 4648, same as Android Base64.DEFAULT)
static string RgBase64Encode(const BYTE* data, DWORD len) {
    static const char* B64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    for (DWORD i = 0; i < len; i += 3) {
        DWORD rem = len - i;
        BYTE b0 = data[i], b1 = rem>1?data[i+1]:0, b2 = rem>2?data[i+2]:0;
        out += B64[b0>>2];
        out += B64[((b0&3)<<4)|(b1>>4)];
        out += rem>1 ? B64[((b1&0xF)<<2)|(b2>>6)] : '=';
        out += rem>2 ? B64[b2&0x3F] : '=';
    }
    return out;
}

// Base64 decode
static vector<BYTE> RgBase64Decode(const string& s) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    };
    vector<BYTE> out;
    int val = 0, bits = -8;
    for (unsigned char c : s) {
        if (c >= 256 || T[c] == -1) { if (c == '=') break; continue; }
        val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back((BYTE)(val >> bits)); bits -= 8; }
    }
    return out;
}

// Encrypt plain text → "E2EE:<base64>" (AES-128-CBC + PKCS5 padding via BCrypt)
static string RgE2EE_Encrypt(const string& chatId, const string& plain) {
    if (plain.empty()) return plain;
    BYTE key[16], iv[16];
    RgE2EE_GetKeyIv(chatId, key, iv);

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0)))
        return plain;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                      (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      (ULONG)((wcslen(BCRYPT_CHAIN_MODE_CBC)+1)*sizeof(wchar_t)), 0);

    BCRYPT_KEY_DATA_BLOB_HEADER* blob;
    DWORD blobSz = sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + 16;
    vector<BYTE> keyBlob(blobSz);
    blob = (BCRYPT_KEY_DATA_BLOB_HEADER*)keyBlob.data();
    blob->dwMagic   = BCRYPT_KEY_DATA_BLOB_MAGIC;
    blob->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    blob->cbKeyData = 16;
    memcpy(keyBlob.data() + sizeof(*blob), key, 16);
    BCryptImportKey(hAlg, NULL, BCRYPT_KEY_DATA_BLOB, &hKey,
                    NULL, 0, keyBlob.data(), blobSz, 0);

    // PKCS5 padding
    DWORD blkSz = 0, dummy = 0;
    BCryptGetProperty(hAlg, BCRYPT_BLOCK_LENGTH, (PUCHAR)&blkSz, sizeof(blkSz), &dummy, 0);
    DWORD padLen = blkSz - ((DWORD)plain.size() % blkSz);
    vector<BYTE> padded(plain.begin(), plain.end());
    for (DWORD i = 0; i < padLen; i++) padded.push_back((BYTE)padLen);

    vector<BYTE> ivCopy(iv, iv+16);
    DWORD outLen = 0;
    BCryptEncrypt(hKey, padded.data(), (ULONG)padded.size(),
                  NULL, ivCopy.data(), 16, NULL, 0, &outLen, BCRYPT_BLOCK_PADDING);
    vector<BYTE> cipher(outLen);
    BCryptEncrypt(hKey, padded.data(), (ULONG)padded.size(),
                  NULL, ivCopy.data(), 16, cipher.data(), outLen, &outLen, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    return "E2EE:" + RgBase64Encode(cipher.data(), outLen);
}

// Decrypt "E2EE:<base64>" → plain text
static string RgE2EE_Decrypt(const string& chatId, const string& enc) {
    if (enc.empty() || enc.substr(0,5) != "E2EE:") return enc;
    BYTE key[16], iv[16];
    RgE2EE_GetKeyIv(chatId, key, iv);

    string b64 = enc.substr(5);
    // strip whitespace/newlines Android Base64.DEFAULT may add
    b64.erase(remove_if(b64.begin(), b64.end(), [](char c){return c=='\n'||c=='\r';}), b64.end());
    vector<BYTE> cipher = RgBase64Decode(b64);
    if (cipher.empty()) return "🔓 (Decryption failed)";

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0)))
        return enc;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                      (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      (ULONG)((wcslen(BCRYPT_CHAIN_MODE_CBC)+1)*sizeof(wchar_t)), 0);

    DWORD blobSz = sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + 16;
    vector<BYTE> keyBlob(blobSz);
    auto* blob = (BCRYPT_KEY_DATA_BLOB_HEADER*)keyBlob.data();
    blob->dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
    blob->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    blob->cbKeyData = 16;
    memcpy(keyBlob.data() + sizeof(*blob), key, 16);
    BCryptImportKey(hAlg, NULL, BCRYPT_KEY_DATA_BLOB, &hKey,
                    NULL, 0, keyBlob.data(), blobSz, 0);

    vector<BYTE> ivCopy(iv, iv+16);
    DWORD outLen = 0;
    BCryptDecrypt(hKey, cipher.data(), (ULONG)cipher.size(),
                  NULL, ivCopy.data(), 16, NULL, 0, &outLen, BCRYPT_BLOCK_PADDING);
    vector<BYTE> plain(outLen);
    BCryptDecrypt(hKey, cipher.data(), (ULONG)cipher.size(),
                  NULL, ivCopy.data(), 16, plain.data(), outLen, &outLen, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // Remove PKCS5 padding
    if (outLen > 0) {
        BYTE pad = plain[outLen-1];
        if (pad <= 16) outLen -= pad;
    }
    return string((char*)plain.data(), outLen);
}

string RgBuildChatId(const string& mobileA, const string& mobileB) {
    // Android: generateChatId = if (m1 < m2) "${m1}_${m2}" else "${m2}_${m1}"
    // Collection name = "pvt_msg_{chatId}"
    string a = mobileA, b = mobileB;
    if (a > b) swap(a, b);
    return a + "_" + b;   // pure chatId; prepend "pvt_msg_" only in collection name
}

// Build the Firestore collection name for a private chat
string RgChatCollection(const string& chatId) {
    return "pvt_msg_" + chatId;
}

string RgBuildPath(const string& col, const string& docId,
                   const string& sub, const string& subId) {
    string path = "/v1/projects/" RG_FIREBASE_PROJECT
                  "/databases/(default)/documents/" + col;
    if (!docId.empty()) path += "/" + docId;
    if (!sub.empty())   path += "/" + sub;
    if (!subId.empty()) path += "/" + subId;
    return path;
}

// ============================================================
// INIT / SHUTDOWN
// ============================================================
void RgNet_Init(const string& myMobile, const string& myName,
                const string& myUid,    const string& idToken) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    g_myMobile = myMobile;
    g_myName   = myName;
    g_myUid    = myUid;
    g_idToken  = idToken;
}

void RgNet_Shutdown() {
    g_chatPollRunning = false;
    g_msgPollRunning  = false;
    g_lanRunning      = false;
    g_callActive      = false;

    if (g_udpBeaconSock != INVALID_SOCKET) { closesocket(g_udpBeaconSock); g_udpBeaconSock = INVALID_SOCKET; }
    if (g_udpListenSock != INVALID_SOCKET) { closesocket(g_udpListenSock); g_udpListenSock = INVALID_SOCKET; }
    if (g_tcpServerSock != INVALID_SOCKET) { closesocket(g_tcpServerSock); g_tcpServerSock = INVALID_SOCKET; }
    if (g_callUdpSock   != INVALID_SOCKET) { closesocket(g_callUdpSock);   g_callUdpSock   = INVALID_SOCKET; }

    if (g_chatPollThread.joinable())  g_chatPollThread.detach();
    if (g_msgPollThread.joinable())   g_msgPollThread.detach();
    if (g_lanBeaconThread.joinable()) g_lanBeaconThread.detach();
    if (g_lanListenThread.joinable()) g_lanListenThread.detach();
    if (g_lanTcpThread.joinable())    g_lanTcpThread.detach();
    if (g_callThread.joinable())      g_callThread.detach();

    WSACleanup();
}

// ============================================================
// PRESENCE
// ============================================================
void RgNet_SetOnline(bool online) {
    if (g_myMobile.empty()) return;
    // Android uses chat_users/{mobile}  with field  "lastActive" (epoch ms)
    // NOT "users" collection, NOT "isOnline"/"lastSeen" fields
    string path = RgBuildPath("chat_users", g_myMobile);
    long long now = NowMs();
    string payload = "{\"fields\":{"
        "\"lastActive\":{\"integerValue\":\"" + to_string(now) + "\"}"
        "}}";
    thread([path, payload]() {
        RgFirestorePost("PATCH", path + "?updateMask.fieldPaths=lastActive", payload);
    }).detach();
}

// ============================================================
// CHAT LIST POLLING
// ============================================================
static vector<RgChatPreview> ParseChatPreviews(const string& json) {
    vector<RgChatPreview> result;
    // Firestore list query response: {"documents":[{...},{...}]}
    size_t pos = 0;
    while ((pos = json.find("\"name\":", pos)) != string::npos) {
        // Find the document block
        size_t blockEnd = json.find("\"name\":", pos + 7);
        string block = json.substr(pos, blockEnd == string::npos
                                        ? json.size() - pos
                                        : blockEnd - pos);
        RgChatPreview cp;
        cp.contactMobile    = RgParseField(block, "contactMobile");
        cp.contactName      = RgParseField(block, "contactName");
        cp.contactAvatarUrl = RgParseField(block, "contactAvatarUrl");
        cp.lastMessageText  = RgParseField(block, "lastMessageText");
        cp.lastMessageSender= RgParseField(block, "lastMessageSender");
        cp.lastTimestamp    = RgParseIntField(block, "lastTimestamp");
        cp.lastTimeString   = RgFormatTime(cp.lastTimestamp);
        cp.unreadCount      = (int)RgParseIntField(block, "unreadCount");
        cp.isPinned         = RgParseBoolField(block, "isPinned");
        cp.isMuted          = RgParseBoolField(block, "isMuted");
        if (!cp.contactMobile.empty())
            result.push_back(cp);
        pos = blockEnd == string::npos ? json.size() : blockEnd;
    }
    // Sort: pinned first, then by timestamp desc
    sort(result.begin(), result.end(), [](const RgChatPreview& a, const RgChatPreview& b){
        if (a.isPinned != b.isPinned) return a.isPinned > b.isPinned;
        return a.lastTimestamp > b.lastTimestamp;
    });
    return result;
}

// ── Resolve my mobile number from Firestore chat_users (uid → mobile) ──────
// EXE logs in with email+password → uid known, mobile unknown.
// Android stores chat_users/{mobile} docs with a "uid" field.
// We list chat_users and find the doc whose uid == g_myUid.
string RgNet_ResolveMyMobile(const string& uid) {
    // GET all chat_users documents (pageSize=300 should cover any user base)
    string path = "/v1/projects/" RG_FIREBASE_PROJECT
                  "/databases/(default)/documents/chat_users?pageSize=300";
    string resp = RgFirestoreGet(path);
    if (resp.empty()) return "";
    // Walk through documents, find the one with matching uid
    size_t pos = 0;
    while ((pos = resp.find("\"name\":", pos)) != string::npos) {
        size_t blockEnd = resp.find("\"name\":", pos + 7);
        string block = resp.substr(pos, blockEnd == string::npos
                                        ? resp.size() - pos
                                        : blockEnd - pos);
        // Extract doc id (last path segment of "name" value)
        string docId;
        {
            size_t np = block.find("\"name\":");
            if (np != string::npos) {
                np += 7;
                while (np < block.size() && block[np] != '"') np++;
                if (np < block.size()) {
                    np++;
                    string fullName;
                    while (np < block.size() && block[np] != '"') fullName += block[np++];
                    size_t lastSlash = fullName.rfind('/');
                    if (lastSlash != string::npos) docId = fullName.substr(lastSlash + 1);
                }
            }
        }
        string docUid = RgParseField(block, "uid");
        if (!docId.empty() && docUid == uid) return docId;
        pos = blockEnd == string::npos ? resp.size() : blockEnd;
    }
    return "";
}

// ── Fetch all users from chat_users + their latest message ────────────────
// Returns RgChatPreview list — same data Android's ChatsTab shows
static vector<RgChatPreview> FetchChatListFromChatUsers() {
    vector<RgChatPreview> result;
    if (g_myMobile.empty()) return result;

    // 1) List all chat_users
    string path = "/v1/projects/" RG_FIREBASE_PROJECT
                  "/databases/(default)/documents/chat_users?pageSize=300";
    string resp = RgFirestoreGet(path);
    if (resp.empty()) return result;

    // 2) Parse each user doc (skip myself)
    vector<pair<string,string>> contacts; // (mobile, name)
    size_t pos = 0;
    while ((pos = resp.find("\"name\":", pos)) != string::npos) {
        size_t blockEnd = resp.find("\"name\":", pos + 7);
        string block = resp.substr(pos, blockEnd == string::npos
                                        ? resp.size() - pos
                                        : blockEnd - pos);
        string docId;
        {
            size_t np = block.find("\"name\":");
            if (np != string::npos) {
                np += 7;
                while (np < block.size() && block[np] != '"') np++;
                if (np < block.size()) {
                    np++;
                    string fullName;
                    while (np < block.size() && block[np] != '"') fullName += block[np++];
                    size_t lastSlash = fullName.rfind('/');
                    if (lastSlash != string::npos) docId = fullName.substr(lastSlash + 1);
                }
            }
        }
        string name = RgParseField(block, "name");
        string avatarUrl = RgParseField(block, "avatarUrl");
        if (!docId.empty() && docId != g_myMobile) {
            contacts.push_back({docId, name});
        }
        pos = blockEnd == string::npos ? resp.size() : blockEnd;
    }

    // 3) For each contact, fetch latest message from pvt_msg_{chatId}
    for (auto& [mobile, name] : contacts) {
        string chatId = RgBuildChatId(g_myMobile, mobile);
        string collection = RgChatCollection(chatId);
        // GET last 1 message ordered by timestamp desc
        string msgPath = "/v1/projects/" RG_FIREBASE_PROJECT
                         "/databases/(default)/documents/" + collection
                         + "?orderBy=timestamp%20desc&pageSize=1";
        string msgResp = RgFirestoreGet(msgPath);

        RgChatPreview cp;
        cp.contactMobile = mobile;
        cp.contactName   = name.empty() ? mobile : name;

        if (!msgResp.empty() && msgResp.find("\"documents\"") != string::npos) {
            // Parse single message block
            size_t mp = msgResp.find("\"name\":");
            if (mp != string::npos) {
                size_t blockEnd2 = msgResp.find("\"name\":", mp + 7);
                string mblock = msgResp.substr(mp, blockEnd2 == string::npos
                                               ? msgResp.size() - mp
                                               : blockEnd2 - mp);
                // chatId needed for decryption
                string previewChatId = RgBuildChatId(g_myMobile, mobile);
                string rawText       = RgParseField(mblock, "text");
                cp.lastMessageText   = RgE2EE_Decrypt(previewChatId, rawText);
                cp.lastMessageSender = RgParseField(mblock, "senderMobile");
                cp.lastTimestamp     = RgParseIntField(mblock, "timestamp");
                // Use the stored timeString from Firestore (same format Android shows)
                {
                    string ts2 = RgParseField(mblock, "timeString");
                    cp.lastTimeString = ts2.empty() ? RgFormatTime(cp.lastTimestamp) : ts2;
                }
                cp.lastFileType      = RgParseField(mblock, "fileType");
                cp.lastIsCallLog     = RgParseBoolField(mblock, "isCallLog");
                // unread: messages where read==false && senderMobile != myMobile
                // (expensive to count exactly — skip for now, set 0)
                cp.unreadCount = 0;
            }
        }
        // Only include contacts we have a chat with (or include all for contact list)
        result.push_back(cp);
    }

    // Sort: most recent first
    sort(result.begin(), result.end(), [](const RgChatPreview& a, const RgChatPreview& b){
        return a.lastTimestamp > b.lastTimestamp;
    });
    return result;
}

void RgNet_FetchContacts(RgChatsCallback cb) {
    if (g_myMobile.empty()) return;
    thread([cb]() {
        // Use the same approach as StartChatListPolling:
        // list chat_users → fetch latest message per contact
        auto previews = FetchChatListFromChatUsers();
        if (cb) cb(previews);
    }).detach();
}

void RgNet_StartChatListPolling(RgChatsCallback cb) {
    if (g_chatPollRunning) return;
    g_chatPollRunning = true;
    g_chatPollThread = thread([cb]() {
        while (g_chatPollRunning) {
            if (!g_myMobile.empty()) {
                auto previews = FetchChatListFromChatUsers();
                if (cb && g_chatPollRunning) cb(previews);
            }
            // Poll every 5 seconds (REST polling is heavier than Firestore SDK)
            for (int i = 0; i < 50 && g_chatPollRunning; i++)
                Sleep(100);
        }
    });
}

void RgNet_StopChatListPolling() {
    g_chatPollRunning = false;
    if (g_chatPollThread.joinable()) g_chatPollThread.detach();
}

// ============================================================
// MESSAGE LOADING
// ============================================================
static vector<RgMessage> ParseMessages(const string& json) {
    vector<RgMessage> result;
    size_t pos = 0;
    while ((pos = json.find("\"name\":", pos)) != string::npos) {
        size_t blockEnd = json.find("\"name\":", pos + 7);
        string block = json.substr(pos, blockEnd == string::npos
                                        ? json.size() - pos
                                        : blockEnd - pos);
        RgMessage m;
        // Extract document ID from name field: .../messages/DOC_ID
        {
            size_t np = block.find("\"name\":");
            if (np != string::npos) {
                np += 7;
                while (np < block.size() && block[np] != '"') np++;
                if (np < block.size()) {
                    np++;
                    string fullName;
                    while (np < block.size() && block[np] != '"') fullName += block[np++];
                    size_t lastSlash = fullName.rfind('/');
                    if (lastSlash != string::npos) m.id = fullName.substr(lastSlash+1);
                }
            }
        }
        m.chatId         = RgParseField(block, "chatId");
        m.senderMobile   = RgParseField(block, "senderMobile");
        m.senderName     = RgParseField(block, "senderName");
        // Decrypt E2EE text — Android AESCrypto.decrypt(chatId, encryptedText)
        {
            string rawText = RgParseField(block, "text");
            m.text = RgE2EE_Decrypt(m.chatId, rawText);
        }
        m.timestamp      = RgParseIntField(block, "timestamp");
        // Prefer the stored timeString (already formatted by sender)
        {
            string ts2 = RgParseField(block, "timeString");
            m.timeString = ts2.empty() ? RgFormatTime(m.timestamp) : ts2;
        }
        m.fileUrl        = RgParseField(block, "fileUrl");
        m.fileName       = RgParseField(block, "fileName");
        m.fileType       = RgParseField(block, "fileType");
        m.fileSizeBytes  = RgParseIntField(block, "fileSizeBytes");
        m.reaction       = RgParseField(block, "reaction");
        m.read           = RgParseBoolField(block, "read");
        m.delivered      = RgParseBoolField(block, "delivered");
        m.isCallLog      = RgParseBoolField(block, "isCallLog");
        m.callStatus     = RgParseField(block, "callStatus");
        m.callType       = RgParseField(block, "callType");
        m.isDeleted      = RgParseBoolField(block, "isDeleted");
        m.replyToId      = RgParseField(block, "replyToId");
        // Decrypt reply quote text as well
        {
            string rawReply = RgParseField(block, "replyToText");
            m.replyToText = RgE2EE_Decrypt(m.chatId, rawReply);
        }
        m.replyToSender  = RgParseField(block, "replyToSender");
        m.duration       = (int)RgParseIntField(block, "duration");  // Android field is "duration" not "durationSecs"
        m.deliveredViaLan= RgParseBoolField(block, "deliveredViaLan");

        if (!m.id.empty() && !m.isDeleted)
            result.push_back(m);

        pos = blockEnd == string::npos ? json.size() : blockEnd;
    }
    // Sort by timestamp ascending (oldest first, like WhatsApp/Telegram)
    sort(result.begin(), result.end(), [](const RgMessage& a, const RgMessage& b){
        return a.timestamp < b.timestamp;
    });
    return result;
}

void RgNet_FetchMessages(const string& chatId, RgMessagesCallback cb) {
    thread([chatId, cb]() {
        // Android collection: pvt_msg_{chatId}  (chatId = "mobileA_mobileB")
        string collection = RgChatCollection(chatId);
        string path = "/v1/projects/" RG_FIREBASE_PROJECT
                      "/databases/(default)/documents/" + collection
                      + "?orderBy=timestamp%20desc&pageSize=100";
        string resp = RgFirestoreGet(path);
        auto msgs = ParseMessages(resp);
        // ParseMessages returns oldest-first after reverse; re-sort ascending
        sort(msgs.begin(), msgs.end(), [](const RgMessage& a, const RgMessage& b){
            return a.timestamp < b.timestamp;
        });
        if (cb) cb(msgs);
    }).detach();
}

void RgNet_StartMessagePolling(const string& chatId, long long sinceTs,
                               RgNewMessageCallback cb) {
    g_msgPollRunning = false;
    if (g_msgPollThread.joinable()) g_msgPollThread.detach();
    g_msgPollRunning = true;
    g_msgPollThread = thread([chatId, sinceTs, cb]() {
        long long lastTs = sinceTs;
        while (g_msgPollRunning) {
            string collection = RgChatCollection(chatId);
            string path = "/v1/projects/" RG_FIREBASE_PROJECT
                          "/databases/(default)/documents/" + collection
                          + "?orderBy=timestamp%20desc&pageSize=10";
            string resp = RgFirestoreGet(path);
            auto msgs = ParseMessages(resp);
            for (auto& m : msgs) {
                if (m.timestamp > lastTs) {
                    lastTs = m.timestamp;
                    if (cb && g_msgPollRunning) cb(m);
                }
            }
            for (int i = 0; i < 15 && g_msgPollRunning; i++)
                Sleep(100);
        }
    });
}

void RgNet_StopMessagePolling() {
    g_msgPollRunning = false;
    if (g_msgPollThread.joinable()) g_msgPollThread.detach();
}

// ============================================================
// SEND MESSAGE
// ============================================================
// Escape a plain string for embedding inside a JSON string value
static string JsonEscape(const string& s) {
    string out;
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c < 0x20)  { /* skip control chars */ }
        else                out += (char)c;
    }
    return out;
}

void RgNet_SendText(const string& chatId,
                    const string& text,
                    const string& receiverMobile) {
    if (g_myMobile.empty() || chatId.empty() || text.empty()) return;
    string myMob  = g_myMobile;
    string myName = g_myName;
    thread([chatId, text, receiverMobile, myMob, myName]() {
        long long ts = NowMs();
        string timeStr = RgFormatTime(ts);

        // ── Encrypt exactly as Android AESCrypto.encrypt(chatId, text) ──
        // Output: "E2EE:<base64>"  — Android will decrypt transparently
        string encText = RgE2EE_Encrypt(chatId, text);

        // ── Build Firestore document matching Android sendMessage() exactly ──
        // Fields: text(encrypted), senderMobile, receiverMobile, timestamp,
        //         timeString, fileUrl, fileName, fileType, reaction,
        //         read, delivered, isCallLog, isDeleted, isForwarded,
        //         isStarred, replyToId, replyToText, replyToSender, duration
        auto strF  = [](const string& k, const string& v) -> string {
            return "\"" + k + "\":{\"stringValue\":\"" + JsonEscape(v) + "\"},";
        };
        auto boolF = [](const string& k, bool v) -> string {
            return "\"" + k + "\":{\"booleanValue\":" + (v?"true":"false") + "},";
        };
        auto intF  = [](const string& k, long long v) -> string {
            return "\"" + k + "\":{\"integerValue\":\"" + to_string(v) + "\"},";
        };
        auto nullF = [](const string& k) -> string {
            return "\"" + k + "\":{\"nullValue\":null},";
        };

        string fields =
            strF ("text",           encText)        // AES-encrypted, "E2EE:..."
            + strF ("senderMobile",   myMob)
            + strF ("senderName",     myName)
            + strF ("receiverMobile", receiverMobile)
            + intF ("timestamp",      ts)
            + strF ("timeString",     timeStr)
            + nullF("fileUrl")
            + nullF("fileName")
            + nullF("fileType")
            + nullF("reaction")
            + boolF("read",         false)
            + boolF("delivered",    false)
            + boolF("isCallLog",    false)
            + boolF("isDeleted",    false)
            + boolF("isForwarded",  false)
            + boolF("isStarred",    false)
            + nullF("replyToId")
            + nullF("replyToText")
            + nullF("replyToSender")
            + intF ("duration",       0);

        // Remove trailing comma from last field
        if (!fields.empty() && fields.back() == ',') fields.pop_back();

        string payload = "{\"fields\":{" + fields + "}}";

        // POST to pvt_msg_{chatId} — same collection Android uses
        string collection = RgChatCollection(chatId);
        string path = "/v1/projects/" RG_FIREBASE_PROJECT
                      "/databases/(default)/documents/" + collection;
        RgFirestorePost("POST", path, payload);
    }).detach();
}

void RgNet_MarkRead(const string& chatId, const string& myMobile) {
    // Android query: pvt_msg_{chatId} where senderMobile != myMobile AND read == false
    // Then patches each doc: { read: true }
    // We replicate this via Firestore REST: list → filter → PATCH each unread doc.
    if (chatId.empty() || myMobile.empty()) return;
    thread([chatId, myMobile]() {
        string collection = RgChatCollection(chatId);
        string listPath   = "/v1/projects/" RG_FIREBASE_PROJECT
                            "/databases/(default)/documents/" + collection
                            + "?pageSize=300";
        string resp = RgFirestoreGet(listPath);
        if (resp.empty() || resp.find("\"documents\"") == string::npos) return;

        // Walk all docs and patch those where read==false && senderMobile != myMobile
        size_t pos = 0;
        while (true) {
            size_t np = resp.find("\"name\":", pos);
            if (np == string::npos) break;
            size_t blockEnd = resp.find("\"name\":", np + 7);
            string block = resp.substr(np, blockEnd == string::npos
                                           ? resp.size() - np
                                           : blockEnd - np);
            pos = blockEnd == string::npos ? resp.size() : blockEnd;

            bool read       = RgParseBoolField(block, "read");
            string sender   = RgParseField(block,     "senderMobile");
            if (read || sender == myMobile) continue;  // already read, or my own message

            // Extract full document path from "name" field
            size_t q1 = block.find('"');          // opening "
            size_t q2 = block.find('"', q1 + 1); // skip "name":
            size_t q3 = block.find('"', q2 + 1);
            size_t q4 = block.find('"', q3 + 1);
            if (q3 == string::npos || q4 == string::npos) continue;
            string docName = block.substr(q3 + 1, q4 - q3 - 1);
            // docName looks like "projects/.../databases/(default)/documents/pvt_msg_.../DOCID"
            // Firestore REST PATCH path = /v1/{docName}
            size_t projPos = docName.find("projects/");
            if (projPos == string::npos) continue;
            string patchPath = "/v1/" + docName.substr(projPos)
                               + "?updateMask.fieldPaths=read";
            string payload = "{\"fields\":{\"read\":{\"booleanValue\":true}}}";
            RgFirestorePost("PATCH", patchPath, payload);
        }
    }).detach();
}

// ============================================================
// LAN — PEER DISCOVERY (UDP Broadcast, same as Android)
// ============================================================
static string BuildBeaconJson() {
    // {"type":"beacon","mobile":"...","name":"...","ip":"...","port":5556}
    char myIp[64] = "0.0.0.0";
    // Get local IP
    char hostName[256];
    if (gethostname(hostName, sizeof(hostName)) == 0) {
        addrinfo* res = nullptr;
        addrinfo hints = {}; hints.ai_family = AF_INET;
        if (getaddrinfo(hostName, nullptr, &hints, &res) == 0 && res) {
            inet_ntop(AF_INET,
                &((sockaddr_in*)res->ai_addr)->sin_addr,
                myIp, sizeof(myIp));
            freeaddrinfo(res);
        }
    }
    return "{\"type\":\"beacon\",\"mobile\":\"" + g_myMobile +
           "\",\"name\":\"" + g_myName +
           "\",\"ip\":\"" + string(myIp) +
           "\",\"port\":" + to_string(RG_LAN_TCP_PORT) + "}";
}

static string LanJsonField(const string& json, const string& key) {
    size_t p = json.find("\"" + key + "\":\"");
    if (p == string::npos) return "";
    p += key.size() + 4;
    string v;
    while (p < json.size() && json[p] != '"') v += json[p++];
    return v;
}
static int LanJsonInt(const string& json, const string& key) {
    size_t p = json.find("\"" + key + "\":");
    if (p == string::npos) return 0;
    p += key.size() + 3;
    string v;
    while (p < json.size() && isdigit(json[p])) v += json[p++];
    return v.empty() ? 0 : atoi(v.c_str());
}

static void LanBeaconLoop() {
    while (g_lanRunning) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            BOOL bcast = TRUE;
            setsockopt(s, SOL_SOCKET, SO_BROADCAST,
                       (char*)&bcast, sizeof(bcast));
            sockaddr_in dest = {};
            dest.sin_family = AF_INET;
            dest.sin_port   = htons(RG_LAN_UDP_PORT);
            dest.sin_addr.s_addr = INADDR_BROADCAST;
            string beacon = BuildBeaconJson();
            sendto(s, beacon.c_str(), (int)beacon.size(), 0,
                   (sockaddr*)&dest, sizeof(dest));
            closesocket(s);
        }
        for (int i = 0; i < (RG_BEACON_INTERVAL_MS/100) && g_lanRunning; i++)
            Sleep(100);
    }
}

static RgLanPeersCallback g_lanPeersCb;
static RgNewMessageCallback g_lanMsgCb;

static void LanListenLoop() {
    g_udpListenSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udpListenSock == INVALID_SOCKET) return;

    BOOL bcast = TRUE;
    setsockopt(g_udpListenSock, SOL_SOCKET, SO_BROADCAST,
               (char*)&bcast, sizeof(bcast));
    BOOL reuse = TRUE;
    setsockopt(g_udpListenSock, SOL_SOCKET, SO_REUSEADDR,
               (char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(RG_LAN_UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(g_udpListenSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(g_udpListenSock); g_udpListenSock = INVALID_SOCKET; return;
    }

    // 1-second recv timeout so we can check g_lanRunning
    DWORD tv = 1000;
    setsockopt(g_udpListenSock, SOL_SOCKET, SO_RCVTIMEO,
               (char*)&tv, sizeof(tv));

    char buf[2048];
    sockaddr_in from; int fromLen = sizeof(from);
    while (g_lanRunning) {
        int n = recvfrom(g_udpListenSock, buf, sizeof(buf)-1, 0,
                         (sockaddr*)&from, &fromLen);
        if (n <= 0) continue;
        buf[n] = '\0';
        string json(buf);
        if (LanJsonField(json, "type") != "beacon") continue;
        string mobile = LanJsonField(json, "mobile");
        if (mobile.empty() || mobile == g_myMobile) continue;

        char fromIp[64];
        inet_ntop(AF_INET, &from.sin_addr, fromIp, sizeof(fromIp));

        RgLanPeer peer;
        peer.mobile = mobile;
        peer.name   = LanJsonField(json, "name");
        peer.ip     = LanJsonField(json, "ip");
        if (peer.ip.empty()) peer.ip = fromIp;
        peer.port   = LanJsonInt(json, "port");
        if (peer.port <= 0) peer.port = RG_LAN_TCP_PORT;

        {
            lock_guard<mutex> lk(g_lanPeersMtx);
            g_lanPeers[mobile]   = peer;
            g_lanPeerSeen[mobile]= NowMs();
        }

        // Prune stale peers
        {
            lock_guard<mutex> lk(g_lanPeersMtx);
            long long cutoff = NowMs() - RG_PEER_TIMEOUT_MS;
            for (auto it = g_lanPeerSeen.begin(); it != g_lanPeerSeen.end(); ) {
                if (it->second < cutoff) {
                    g_lanPeers.erase(it->first);
                    it = g_lanPeerSeen.erase(it);
                } else ++it;
            }
        }

        if (g_lanPeersCb) {
            vector<RgLanPeer> list;
            { lock_guard<mutex> lk(g_lanPeersMtx);
              for (auto& kv : g_lanPeers) list.push_back(kv.second); }
            g_lanPeersCb(list);
        }
    }
    if (g_udpListenSock != INVALID_SOCKET) {
        closesocket(g_udpListenSock); g_udpListenSock = INVALID_SOCKET;
    }
}

// ── LAN TCP server — receive messages / files ──────────────────
static void HandleLanTcpClient(SOCKET client) {
    // Protocol: 4-byte header length (network order) + header JSON + optional binary
    char lenBuf[4] = {};
    int got = recv(client, lenBuf, 4, MSG_WAITALL);
    if (got != 4) { closesocket(client); return; }
    int headerLen = ntohl(*(int*)lenBuf);
    if (headerLen <= 0 || headerLen > 65536) { closesocket(client); return; }

    vector<char> hdrBuf(headerLen+1, 0);
    got = recv(client, hdrBuf.data(), headerLen, MSG_WAITALL);
    if (got != headerLen) { closesocket(client); return; }
    string header(hdrBuf.data());

    string type         = LanJsonField(header, "type");
    string chatId       = LanJsonField(header, "chatId");
    string senderMobile = LanJsonField(header, "senderMobile");
    string senderName   = LanJsonField(header, "senderName");
    string text         = LanJsonField(header, "text");

    if (type == "text" && !chatId.empty() && g_lanMsgCb) {
        RgMessage msg;
        msg.id           = "lan_" + to_string(NowMs());
        msg.chatId       = chatId;
        msg.senderMobile = senderMobile;
        msg.senderName   = senderName;
        msg.text         = text;
        msg.timestamp    = NowMs();
        msg.timeString   = RgFormatTime(msg.timestamp);
        msg.deliveredViaLan = true;
        g_lanMsgCb(msg);

        // Also persist to Firestore so Android sees it
        RgNet_SendText(chatId, text, senderMobile);
    }
    closesocket(client);
}

static void LanTcpServerLoop() {
    g_tcpServerSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_tcpServerSock == INVALID_SOCKET) return;

    BOOL reuse = TRUE;
    setsockopt(g_tcpServerSock, SOL_SOCKET, SO_REUSEADDR,
               (char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(RG_LAN_TCP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(g_tcpServerSock, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(g_tcpServerSock, 8) != 0) {
        closesocket(g_tcpServerSock); g_tcpServerSock = INVALID_SOCKET; return;
    }

    DWORD tv = 1000;
    setsockopt(g_tcpServerSock, SOL_SOCKET, SO_RCVTIMEO,
               (char*)&tv, sizeof(tv));

    while (g_lanRunning) {
        sockaddr_in caddr; int clen = sizeof(caddr);
        SOCKET client = accept(g_tcpServerSock, (sockaddr*)&caddr, &clen);
        if (client == INVALID_SOCKET) continue;
        thread([client]() { HandleLanTcpClient(client); }).detach();
    }
    if (g_tcpServerSock != INVALID_SOCKET) {
        closesocket(g_tcpServerSock); g_tcpServerSock = INVALID_SOCKET;
    }
}

void RgNet_StartLan(RgLanPeersCallback peersCb, RgNewMessageCallback msgCb) {
    if (g_lanRunning) return;
    g_lanPeersCb  = peersCb;
    g_lanMsgCb    = msgCb;
    g_lanRunning  = true;

    g_lanBeaconThread = thread(LanBeaconLoop);
    g_lanListenThread = thread(LanListenLoop);
    g_lanTcpThread    = thread(LanTcpServerLoop);
}

void RgNet_StopLan() {
    g_lanRunning = false;
    if (g_lanBeaconThread.joinable()) g_lanBeaconThread.detach();
    if (g_lanListenThread.joinable()) g_lanListenThread.detach();
    if (g_lanTcpThread.joinable())    g_lanTcpThread.detach();
}

void RgNet_LanSendText(const RgLanPeer& peer, const string& chatId,
                       const string& text) {
    thread([peer, chatId, text]() {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((u_short)peer.port);
        inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr);
        DWORD tv = 5000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            string header = "{\"type\":\"text\","
                "\"chatId\":\"" + chatId + "\","
                "\"senderMobile\":\"" + g_myMobile + "\","
                "\"senderName\":\"" + g_myName + "\","
                "\"text\":\"" + text + "\"}";
            int hLen = htonl((int)header.size());
            send(s, (char*)&hLen, 4, 0);
            send(s, header.c_str(), (int)header.size(), 0);
        }
        closesocket(s);
    }).detach();
}

// ============================================================
// CLOUD FILE SEND — Cloudinary upload → Firestore message
// ============================================================

// Cloudinary unsigned upload via multipart/form-data over WinINet
// Cloud name and upload_preset come from the same project as Android app.
#define RG_CLOUDINARY_CLOUD   "rasfocus-c746d"
#define RG_CLOUDINARY_PRESET  "rasfocus_unsigned"

static string CloudinaryUpload(const wstring& filePath, const string& mimeType) {
    // Read file into memory
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return "";

    LARGE_INTEGER fSize; GetFileSizeEx(hFile, &fSize);
    if (fSize.QuadPart == 0 || fSize.QuadPart > 50LL * 1024 * 1024) {
        CloseHandle(hFile); return ""; // skip empty or >50 MB
    }

    vector<char> fileData((size_t)fSize.QuadPart);
    DWORD rd = 0;
    ReadFile(hFile, fileData.data(), (DWORD)fileData.size(), &rd, NULL);
    CloseHandle(hFile);

    // Extract filename
    size_t slash = filePath.rfind(L'\\');
    wstring wfn = (slash != wstring::npos) ? filePath.substr(slash + 1) : filePath;
    string fname(wfn.begin(), wfn.end());

    // Build multipart/form-data body
    string boundary = "RasGramBoundary12345";
    string body;
    // upload_preset field
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"upload_preset\"\r\n\r\n";
    body += string(RG_CLOUDINARY_PRESET) + "\r\n";
    // file field
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + fname + "\"\r\n";
    body += "Content-Type: " + mimeType + "\r\n\r\n";
    body.append(fileData.data(), fileData.size());
    body += "\r\n--" + boundary + "--\r\n";

    string contentType = "multipart/form-data; boundary=" + boundary;
    string uploadUrl = "/v1_1/" + string(RG_CLOUDINARY_CLOUD) + "/auto/upload";

    string resp;
    HINTERNET hInet = InternetOpenA("RasGram-Desktop/1.0",
                        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return "";

    HINTERNET hConn = InternetConnectA(hInet, "api.cloudinary.com",
                        INTERNET_DEFAULT_HTTPS_PORT,
                        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConn) {
        HINTERNET hReq = HttpOpenRequestA(hConn, "POST", uploadUrl.c_str(),
                            NULL, NULL, NULL,
                            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                            INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hReq) {
            string headers = "Content-Type: " + contentType + "\r\n";
            HttpSendRequestA(hReq,
                headers.c_str(), (DWORD)headers.size(),
                (LPVOID)body.c_str(), (DWORD)body.size());
            char buf[4096]; DWORD n = 0;
            while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &n) && n > 0) {
                buf[n] = '\0'; resp += buf;
            }
            InternetCloseHandle(hReq);
        }
        InternetCloseHandle(hConn);
    }
    InternetCloseHandle(hInet);

    // Parse "secure_url" from JSON response
    // {"secure_url":"https://res.cloudinary.com/..."}
    string key = "\"secure_url\":\"";
    size_t p = resp.find(key);
    if (p == string::npos) return "";
    p += key.size();
    size_t q = resp.find('"', p);
    if (q == string::npos) return "";
    string url = resp.substr(p, q - p);
    // Unescape forward slashes  (\/ → /)
    string result;
    for (size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '\\' && i + 1 < url.size() && url[i+1] == '/') { result += '/'; ++i; }
        else result += url[i];
    }
    return result;
}

void RgNet_SendFile(const string& chatId,
                    const string& receiverMobile,
                    const wstring& localFilePath,
                    const string& mimeType) {
    if (g_myMobile.empty() || chatId.empty() || localFilePath.empty()) return;
    thread([chatId, receiverMobile, localFilePath, mimeType]() {
        // 1) Upload to Cloudinary
        string fileUrl = CloudinaryUpload(localFilePath, mimeType);
        if (fileUrl.empty()) return; // upload failed — silently skip

        // 2) Get filename for display
        size_t slash = localFilePath.rfind(L'\\');
        wstring wfn = (slash != wstring::npos) ? localFilePath.substr(slash + 1) : localFilePath;
        string fname(wfn.begin(), wfn.end());

        long long ts = NowMs();
        string timeStr = RgFormatTime(ts);

        // Determine message type from mimeType
        string msgType = "file";
        if (mimeType.find("image/") == 0)  msgType = "image";
        else if (mimeType.find("video/") == 0) msgType = "video";
        else if (mimeType.find("audio/") == 0) msgType = "audio";

        // 3) Write message to Firestore
        string payload = "{\"fields\":{"
            "\"chatId\":{\"stringValue\":\"" + chatId + "\"},"
            "\"senderMobile\":{\"stringValue\":\"" + g_myMobile + "\"},"
            "\"senderName\":{\"stringValue\":\"" + g_myName + "\"},"
            "\"text\":{\"stringValue\":\"" + fname + "\"},"
            "\"fileUrl\":{\"stringValue\":\"" + fileUrl + "\"},"
            "\"fileName\":{\"stringValue\":\"" + fname + "\"},"
            "\"mimeType\":{\"stringValue\":\"" + mimeType + "\"},"
            "\"type\":{\"stringValue\":\"" + msgType + "\"},"
            "\"timestamp\":{\"integerValue\":\"" + to_string(ts) + "\"},"
            "\"timeString\":{\"stringValue\":\"" + timeStr + "\"},"
            "\"read\":{\"booleanValue\":false},"
            "\"delivered\":{\"booleanValue\":true},"
            "\"isDeleted\":{\"booleanValue\":false},"
            "\"isCallLog\":{\"booleanValue\":false}"
            "}}";

        // POST to pvt_msg_{chatId} collection — matches Android path exactly
        string collection = RgChatCollection(chatId);
        string path = "/v1/projects/" RG_FIREBASE_PROJECT
                      "/databases/(default)/documents/" + collection;
        RgFirestorePost("POST", path, payload);
    }).detach();
}

void RgNet_LanSendFile(const RgLanPeer& peer, const string& chatId,
                       const wstring& filePath, const string& mimeType) {
    // File send: open file → send header + 8-byte size + raw bytes
    thread([peer, chatId, filePath, mimeType]() {
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER fSize; GetFileSizeEx(hFile, &fSize);

        // Get filename
        size_t slash = filePath.rfind(L'\\');
        wstring wfn = (slash != wstring::npos) ? filePath.substr(slash+1) : filePath;
        string fname(wfn.begin(), wfn.end());

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((u_short)peer.port);
        inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr);
        if (s != INVALID_SOCKET && connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            string header = "{\"type\":\"file\","
                "\"chatId\":\"" + chatId + "\","
                "\"senderMobile\":\"" + g_myMobile + "\","
                "\"senderName\":\"" + g_myName + "\","
                "\"mimeType\":\"" + mimeType + "\","
                "\"fileName\":\"" + fname + "\"}";
            int hLen = htonl((int)header.size());
            send(s, (char*)&hLen, 4, 0);
            send(s, header.c_str(), (int)header.size(), 0);
            // Send file size (8 bytes big-endian) then raw bytes
            long long fs = fSize.QuadPart;
            long long fsNet = _byteswap_uint64(fs);
            send(s, (char*)&fsNet, 8, 0);
            char buf[8192]; DWORD rd = 0;
            while (ReadFile(hFile, buf, sizeof(buf), &rd, NULL) && rd > 0)
                send(s, buf, rd, 0);
        }
        if (s != INVALID_SOCKET) closesocket(s);
        CloseHandle(hFile);
    }).detach();
}

// ============================================================
// AUDIO / VIDEO CALL  (WASAPI + DirectShow + UDP)
// ============================================================
static RgCallStateCallback g_callStateCb;
static RgVideoFrameCallback g_callVideoCb;
static RgCallParams         g_callParams;
static DWORD                g_callStartTick = 0;

// Simple WASAPI audio capture helper
struct WasapiCapture {
    IMMDeviceEnumerator* devEnum  = nullptr;
    IMMDevice*           dev      = nullptr;
    IAudioClient*        client   = nullptr;
    IAudioCaptureClient* capture  = nullptr;
    WAVEFORMATEX*        wfx      = nullptr;
    bool Init() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&devEnum))) return false;
        if (FAILED(devEnum->GetDefaultAudioEndpoint(eCapture, eCommunications, &dev))) return false;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client))) return false;
        if (FAILED(client->GetMixFormat(&wfx))) return false;
        REFERENCE_TIME bufDur = 200000; // 20ms
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufDur, 0, wfx, NULL))) return false;
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture))) return false;
        client->Start();
        return true;
    }
    int Read(vector<BYTE>& out) {
        UINT32 packetSize = 0;
        if (FAILED(capture->GetNextPacketSize(&packetSize))) return 0;
        int total = 0;
        while (packetSize > 0) {
            BYTE* data; UINT32 frames; DWORD flags;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, NULL, NULL))) break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                int bytes = frames * wfx->nBlockAlign;
                out.insert(out.end(), data, data + bytes);
                total += bytes;
            }
            capture->ReleaseBuffer(frames);
            capture->GetNextPacketSize(&packetSize);
        }
        return total;
    }
    void Shutdown() {
        if (client)   { client->Stop(); client->Release(); client = nullptr; }
        if (capture)  { capture->Release(); capture = nullptr; }
        if (dev)      { dev->Release(); dev = nullptr; }
        if (devEnum)  { devEnum->Release(); devEnum = nullptr; }
        if (wfx)      { CoTaskMemFree(wfx); wfx = nullptr; }
    }
};

// Simple WASAPI audio render (speaker output)
struct WasapiRender {
    IMMDeviceEnumerator* devEnum = nullptr;
    IMMDevice*           dev     = nullptr;
    IAudioClient*        client  = nullptr;
    IAudioRenderClient*  render  = nullptr;
    WAVEFORMATEX*        wfx     = nullptr;
    bool Init() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&devEnum))) return false;
        if (FAILED(devEnum->GetDefaultAudioEndpoint(eRender, eCommunications, &dev))) return false;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client))) return false;
        if (FAILED(client->GetMixFormat(&wfx))) return false;
        REFERENCE_TIME bufDur = 200000;
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufDur, 0, wfx, NULL))) return false;
        if (FAILED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render))) return false;
        client->Start();
        return true;
    }
    void Write(const BYTE* data, int bytes) {
        UINT32 bufSize = 0, padding = 0;
        if (FAILED(client->GetBufferSize(&bufSize))) return;
        if (FAILED(client->GetCurrentPadding(&padding))) return;
        UINT32 available = bufSize - padding;
        UINT32 frames = min((UINT32)(bytes / wfx->nBlockAlign), available);
        if (frames == 0) return;
        BYTE* buf;
        if (FAILED(render->GetBuffer(frames, &buf))) return;
        memcpy(buf, data, frames * wfx->nBlockAlign);
        render->ReleaseBuffer(frames, 0);
    }
    void Shutdown() {
        if (client)  { client->Stop(); client->Release(); client = nullptr; }
        if (render)  { render->Release(); render = nullptr; }
        if (dev)     { dev->Release(); dev = nullptr; }
        if (devEnum) { devEnum->Release(); devEnum = nullptr; }
        if (wfx)     { CoTaskMemFree(wfx); wfx = nullptr; }
    }
};

static void CallAudioLoop(SOCKET udpSock, const string& peerIp, int peerPort,
                          bool isVideo) {
    WasapiCapture cap;  cap.Init();
    WasapiRender  rend; rend.Init();

    sockaddr_in peer = {};
    peer.sin_family = AF_INET;
    peer.sin_port   = htons((u_short)peerPort);
    inet_pton(AF_INET, peerIp.c_str(), &peer.sin_addr);

    // Non-blocking recv
    u_long mode = 1; ioctlsocket(udpSock, FIONBIO, &mode);
    g_callStartTick = GetTickCount();

    vector<BYTE> capBuf;
    BYTE recvBuf[8192];

    while (g_callActive) {
        // Capture & send
        capBuf.clear();
        cap.Read(capBuf);
        if (!g_callMuted && !capBuf.empty()) {
            sendto(udpSock, (char*)capBuf.data(), (int)capBuf.size(), 0,
                   (sockaddr*)&peer, sizeof(peer));
        }
        // Receive & render
        sockaddr_in from; int fromLen = sizeof(from);
        int n = recvfrom(udpSock, (char*)recvBuf, sizeof(recvBuf), 0,
                         (sockaddr*)&from, &fromLen);
        if (n > 0) {
            rend.Write(recvBuf, n);
        }

        // Update duration
        g_callDuration = (int)((GetTickCount() - g_callStartTick) / 1000);
        Sleep(10); // ~100 Hz loop
    }

    cap.Shutdown();
    rend.Shutdown();
}

// ── Video call forward declarations ─────────────────────────
struct RgSampleGrabberCB;
static bool StartCamera(int targetW, int targetH);
static void StopCamera();
static void CallVideoSendLoop(SOCKET udpSock, const string& peerIp, int peerPort);
static void CallVideoRecvLoop(SOCKET udpSock);

static void CallSignalFirebase(bool start) {
    // Write call signal to Firestore: calls/{chatId}/state
    string path = RgBuildPath("calls", g_callParams.chatId, "state", "current");
    if (start) {
        string payload = "{\"fields\":{"
            "\"callerMobile\":{\"stringValue\":\"" + g_myMobile + "\"},"
            "\"callerName\":{\"stringValue\":\"" + g_myName + "\"},"
            "\"calleeMobile\":{\"stringValue\":\"" + g_callParams.peerMobile + "\"},"
            "\"callType\":{\"stringValue\":\"" + (g_callParams.isVideo ? "video" : "audio") + "\"},"
            "\"status\":{\"stringValue\":\"ringing\"},"
            "\"timestamp\":{\"integerValue\":\"" + to_string(NowMs()) + "\"}"
            "}}";
        RgFirestorePost("PATCH", path, payload);
    } else {
        string payload = "{\"fields\":{"
            "\"status\":{\"stringValue\":\"ended\"},"
            "\"endedAt\":{\"integerValue\":\"" + to_string(NowMs()) + "\"}"
            "}}";
        RgFirestorePost("PATCH", path + "?updateMask.fieldPaths=status&updateMask.fieldPaths=endedAt",
                        payload);
    }
}

void RgCall_StartOutgoing(const RgCallParams& p,
                          RgCallStateCallback stateCb,
                          RgVideoFrameCallback videoCb) {
    if (g_callActive) return;
    g_callParams  = p;
    g_callStateCb = stateCb;
    g_callVideoCb = videoCb;
    g_callActive  = true;
    g_callMuted   = false;
    g_callVideo   = p.isVideo;

    // Signal via Firebase (for internet calls) or direct UDP (LAN)
    thread([p, stateCb]() {
        CallSignalFirebase(true);
        if (stateCb) stateCb(true);

        // Setup UDP socket for audio
        g_callUdpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in local = {};
        local.sin_family      = AF_INET;
        local.sin_port        = htons(RG_CALL_UDP_PORT);
        local.sin_addr.s_addr = INADDR_ANY;
        (void)::bind(g_callUdpSock, (sockaddr*)&local, sizeof(local));

        string peerIp = p.peerIp;

        // Video call: start DirectShow camera + video send/recv threads
        if (p.isVideo) {
            if (StartCamera(320, 240)) {
                SOCKET vs = g_callUdpSock;
                thread([vs, peerIp](){ CallVideoSendLoop(vs, peerIp, RG_CALL_UDP_PORT); StopCamera(); }).detach();
                thread([vs](){ CallVideoRecvLoop(vs); }).detach();
            }
        }

        CallAudioLoop(g_callUdpSock, peerIp, RG_CALL_UDP_PORT, p.isVideo);

        CallSignalFirebase(false);
        if (stateCb) stateCb(false);
        if (g_callUdpSock != INVALID_SOCKET) {
            closesocket(g_callUdpSock); g_callUdpSock = INVALID_SOCKET;
        }
    }).detach();
}

void RgCall_AcceptIncoming(const RgCallParams& p,
                           RgCallStateCallback stateCb,
                           RgVideoFrameCallback videoCb) {
    RgCall_StartOutgoing(p, stateCb, videoCb); // same flow, just receiver side
}

void RgCall_Hangup() {
    g_callActive = false;
    if (g_callUdpSock != INVALID_SOCKET) {
        closesocket(g_callUdpSock); g_callUdpSock = INVALID_SOCKET;
    }
    CallSignalFirebase(false);
}

void RgCall_ToggleMute(bool mute)    { g_callMuted  = mute; }
void RgCall_ToggleSpeaker(bool on)   { /* WASAPI render always on */ }
void RgCall_ToggleCamera(bool on)    { g_callVideo  = on; }
bool RgCall_IsActive()               { return g_callActive.load(); }
bool RgCall_IsMuted()                { return g_callMuted.load(); }
bool RgCall_IsVideo()                { return g_callVideo.load(); }
int  RgCall_GetDurationSeconds()     { return g_callDuration.load(); }

// ============================================================
// DECLINE INCOMING CALL
// ============================================================
void RgCall_Decline() {
    // Write "declined" to Firestore so caller knows
    string path = RgBuildPath("calls", g_callParams.chatId, "state", "current");
    string payload = "{\"fields\":{"
        "\"status\":{\"stringValue\":\"declined\"},"
        "\"endedAt\":{\"integerValue\":\"" + to_string(NowMs()) + "\"}"
        "}}";
    thread([path, payload](){
        RgFirestorePost("PATCH",
            path + "?updateMask.fieldPaths=status&updateMask.fieldPaths=endedAt",
            payload);
    }).detach();
    g_callActive = false;
}

// ============================================================
// INCOMING CALL POLLING
// ============================================================
static atomic<bool>        g_incomingPollRun { false };
static RgIncomingCallCallback g_incomingCb;

// Firestore path: calls/{myMobile}/incoming/{docId}
// Android side writes a doc here when dialling the desktop user.
static void IncomingCallPollLoop() {
    // Track the last doc we saw so we don't fire twice
    string lastSeenId;
    while (g_incomingPollRun) {
        if (!g_myMobile.empty() && !g_callActive) {
            string path = RgBuildPath("calls", g_myMobile, "incoming", "");
            // List documents in the incoming sub-collection
            string resp = RgFirestoreGet(path);
            // Look for a document with status == "ringing"
            // Simple scan: find "status" field with "ringing"
            if (resp.find("ringing") != string::npos) {
                // Parse callerMobile, callerName, callType, docId
                string callerMobile = RgParseField(resp, "callerMobile");
                string callerName   = RgParseField(resp, "callerName");
                string callType     = RgParseField(resp, "callType");
                string chatId       = RgParseField(resp, "chatId");
                string docId        = callerMobile; // one doc per caller

                if (!callerMobile.empty() && docId != lastSeenId) {
                    lastSeenId = docId;
                    RgCallParams cp;
                    cp.chatId     = chatId.empty()
                                      ? RgBuildChatId(g_myMobile, callerMobile)
                                      : chatId;
                    cp.peerMobile = callerMobile;
                    cp.peerName   = callerName;
                    cp.isVideo    = (callType == "video");
                    cp.isLan      = false;
                    if (g_incomingCb) g_incomingCb(cp);
                }
            } else {
                // No active ringing doc — reset so next call fires again
                if (!lastSeenId.empty() && resp.find(lastSeenId) == string::npos)
                    lastSeenId.clear();
            }
        }
        for (int i = 0; i < 20 && g_incomingPollRun; i++) Sleep(100); // poll every 2s
    }
}

void RgNet_StartIncomingCallPolling(RgIncomingCallCallback cb) {
    if (g_incomingPollRun) return;
    g_incomingCb      = cb;
    g_incomingPollRun = true;
    thread(IncomingCallPollLoop).detach();
}

void RgNet_StopIncomingCallPolling() {
    g_incomingPollRun = false;
    g_incomingCb      = nullptr;
}

// ============================================================
// DESKTOP NOTIFICATIONS  (Win32 Shell_NotifyIcon balloon)
// ============================================================
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#define RG_NOTIFY_WM     (WM_APP + 55)
#define RG_NOTIFY_ICON_ID 501

static HWND  g_notifyOwner = nullptr;
static bool  g_notifyInited = false;
static NOTIFYICONDATAW g_nid = {};

static LRESULT CALLBACK RgNotifyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RgNotify_Init(HWND ownerHwnd) {
    g_notifyOwner = ownerHwnd;
    if (!ownerHwnd) return;

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = ownerHwnd;
    g_nid.uID              = RG_NOTIFY_ICON_ID;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = RG_NOTIFY_WM;
    // Use the app's own icon (first icon resource) or fallback to default
    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcscpy_s(g_nid.szTip, L"RasFocus — RasGram");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_notifyInited = true;
}

void RgNotify_Destroy() {
    if (g_notifyInited) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_notifyInited = false;
    }
}

// Internal helper — must be called from any thread; posts to UI if needed
static void RgShowBalloon(const wstring& title, const wstring& body, DWORD infoFlags) {
    if (!g_notifyOwner || !g_notifyInited) return;
    // Update nid with balloon fields
    NOTIFYICONDATAW n = g_nid;
    n.uFlags     |= NIF_INFO;
    n.dwInfoFlags = infoFlags;
    n.uTimeout    = 5000;
    wcscpy_s(n.szInfoTitle, title.c_str());
    wcscpy_s(n.szInfo,      body.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static wstring Utf8ToWide_N(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    wstring ws(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], n);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

void RgNotify_Message(const string& senderName, const string& text) {
    // Truncate body to 64 chars to fit balloon
    wstring body = Utf8ToWide_N(text);
    if (body.size() > 64) body = body.substr(0, 61) + L"...";
    thread([senderName, body](){
        RgShowBalloon(Utf8ToWide_N(senderName), body, NIIF_INFO);
    }).detach();
}

void RgNotify_IncomingCall(const string& callerName, bool isVideo) {
    wstring title = isVideo ? L"Incoming Video Call" : L"Incoming Audio Call";
    wstring body  = Utf8ToWide_N(callerName) + L" is calling…";
    thread([title, body](){
        RgShowBalloon(title, body, NIIF_INFO | NIIF_NOSOUND);
        // Also play the system "Ring" sound
        MessageBeep(MB_ICONINFORMATION);
    }).detach();
}

// ============================================================
// VIDEO CALL  — DirectShow camera capture + UDP send/receive
// ============================================================
// Video frame layout over UDP:
//   [4 bytes: width][4 bytes: height][4 bytes: size][RGB24 data…]
// Frames are scaled to 320×240 before sending to keep bandwidth low.
// Receiver decodes and calls g_callVideoCb(frameRGB, w, h).


static IGraphBuilder*  g_dshowGraph   = nullptr;
static ICaptureGraphBuilder2* g_dshowCapture = nullptr;
static IBaseFilter*    g_dshowCamera  = nullptr;
static IMediaControl*  g_dshowCtrl    = nullptr;

// Simple sample grabber callback — stores latest frame
struct RgSampleGrabberCB : public ISampleGrabberCB {
    vector<BYTE> frame;
    int          width  = 0;
    int          height = 0;
    mutex        mtx;
    ULONG ref = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_ISampleGrabberCB || riid == IID_IUnknown) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++ref; }
    ULONG STDMETHODCALLTYPE Release() override { return --ref; }

    HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE* buf, long len) override {
        lock_guard<mutex> lk(mtx);
        frame.assign(buf, buf + len);
        return S_OK;
    }
};
static RgSampleGrabberCB* g_grabberCB = nullptr;

static bool StartCamera(int targetW, int targetH) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IGraphBuilder, (void**)&g_dshowGraph))) return false;
    if (FAILED(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ICaptureGraphBuilder2, (void**)&g_dshowCapture))) return false;
    g_dshowCapture->SetFiltergraph(g_dshowGraph);

    // Enumerate video capture devices and pick first
    ICreateDevEnum* devEnum = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ICreateDevEnum, (void**)&devEnum))) return false;
    IEnumMoniker* enumMon = nullptr;
    devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMon, 0);
    devEnum->Release();
    if (!enumMon) return false;

    IMoniker* mon = nullptr;
    if (enumMon->Next(1, &mon, nullptr) != S_OK) { enumMon->Release(); return false; }
    mon->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void**)&g_dshowCamera);
    mon->Release(); enumMon->Release();
    if (!g_dshowCamera) return false;
    g_dshowGraph->AddFilter(g_dshowCamera, L"Camera");

    // Sample grabber
    IBaseFilter* grabFilter = nullptr;
    CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IBaseFilter, (void**)&grabFilter);
    g_dshowGraph->AddFilter(grabFilter, L"Grabber");
    ISampleGrabber* sg = nullptr;
    grabFilter->QueryInterface(IID_ISampleGrabber, (void**)&sg);
    AM_MEDIA_TYPE mt = {};
    mt.majortype  = MEDIATYPE_Video;
    mt.subtype    = MEDIASUBTYPE_RGB24;
    mt.formattype = FORMAT_VideoInfo;
    sg->SetMediaType(&mt);
    sg->SetOneShot(FALSE);
    sg->SetBufferSamples(TRUE);
    g_grabberCB = new RgSampleGrabberCB();
    sg->SetCallback(g_grabberCB, 1);
    sg->Release();

    // Null renderer
    IBaseFilter* nullRend = nullptr;
    CoCreateInstance(CLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IBaseFilter, (void**)&nullRend);
    g_dshowGraph->AddFilter(nullRend, L"Null");

    g_dshowCapture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video,
                                  g_dshowCamera, grabFilter, nullRend);
    if (nullRend) nullRend->Release();
    if (grabFilter) grabFilter->Release();

    g_dshowGraph->QueryInterface(IID_IMediaControl, (void**)&g_dshowCtrl);
    g_dshowCtrl->Run();
    return true;
}

static void StopCamera() {
    if (g_dshowCtrl)   { g_dshowCtrl->Stop(); g_dshowCtrl->Release();   g_dshowCtrl   = nullptr; }
    if (g_dshowCamera) { g_dshowCamera->Release();                        g_dshowCamera = nullptr; }
    if (g_dshowCapture){ g_dshowCapture->Release();                       g_dshowCapture= nullptr; }
    if (g_dshowGraph)  { g_dshowGraph->Release();                         g_dshowGraph  = nullptr; }
    if (g_grabberCB)   { delete g_grabberCB; g_grabberCB = nullptr; }
}

// Video send loop — runs alongside CallAudioLoop when isVideo=true
static void CallVideoSendLoop(SOCKET udpSock, const string& peerIp, int peerPort) {
    sockaddr_in peer = {};
    peer.sin_family = AF_INET;
    peer.sin_port   = htons((u_short)(peerPort + 1)); // video on port+1
    inet_pton(AF_INET, peerIp.c_str(), &peer.sin_addr);

    while (g_callActive && g_callVideo) {
        if (g_grabberCB) {
            vector<BYTE> frame;
            int w = 0, h = 0;
            {
                lock_guard<mutex> lk(g_grabberCB->mtx);
                frame = g_grabberCB->frame;
                w = g_grabberCB->width;
                h = g_grabberCB->height;
            }
            if (!frame.empty() && w > 0 && h > 0) {
                // Simple scale to 320x240 (just crop/copy for now)
                int tw = 320, th = 240;
                vector<BYTE> out(tw * th * 3);
                for (int row = 0; row < th && row < h; row++) {
                    int srcRow = row * h / th;
                    for (int col = 0; col < tw && col < w; col++) {
                        int srcCol = col * w / tw;
                        int si = (srcRow * w + srcCol) * 3;
                        int di = (row * tw + col) * 3;
                        out[di] = frame[si]; out[di+1] = frame[si+1]; out[di+2] = frame[si+2];
                    }
                }
                // Header: w(4) h(4) size(4) + data
                int size = (int)out.size();
                vector<BYTE> pkt(12 + size);
                memcpy(pkt.data() + 0, &tw,   4);
                memcpy(pkt.data() + 4, &th,   4);
                memcpy(pkt.data() + 8, &size, 4);
                memcpy(pkt.data() + 12, out.data(), size);
                // Send in chunks if needed (UDP max ~65KB; 320*240*3=230KB so split)
                int chunkSz = 60000;
                int offset  = 0;
                int seq     = 0;
                while (offset < (int)pkt.size()) {
                    int len = min(chunkSz, (int)pkt.size() - offset);
                    sendto(udpSock, (char*)pkt.data() + offset, len, 0,
                           (sockaddr*)&peer, sizeof(peer));
                    offset += len; seq++;
                }
            }
        }
        Sleep(33); // ~30 fps
    }
}

// Video receive loop — fires g_callVideoCb with decoded frames
static void CallVideoRecvLoop(SOCKET udpSock) {
    // Bind recv on peerPort+1 for video
    sockaddr_in local = {};
    local.sin_family      = AF_INET;
    local.sin_port        = htons((u_short)(RG_CALL_UDP_PORT + 1));
    local.sin_addr.s_addr = INADDR_ANY;
    SOCKET recvSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ::bind(recvSock, (sockaddr*)&local, sizeof(local));
    u_long mode = 1; ioctlsocket(recvSock, FIONBIO, &mode);

    vector<BYTE> accumBuf;
    int expectW = 0, expectH = 0, expectSize = 0;

    BYTE chunk[65000];
    while (g_callActive) {
        sockaddr_in from; int fromLen = sizeof(from);
        int n = recvfrom(recvSock, (char*)chunk, sizeof(chunk), 0,
                         (sockaddr*)&from, &fromLen);
        if (n > 0) {
            accumBuf.insert(accumBuf.end(), chunk, chunk + n);
            if (accumBuf.size() >= 12 && expectSize == 0) {
                memcpy(&expectW,    accumBuf.data() + 0, 4);
                memcpy(&expectH,    accumBuf.data() + 4, 4);
                memcpy(&expectSize, accumBuf.data() + 8, 4);
            }
            if (expectSize > 0 && (int)accumBuf.size() >= 12 + expectSize) {
                if (g_callVideoCb && expectW > 0 && expectH > 0) {
                    g_callVideoCb(accumBuf.data() + 12, expectW, expectH);
                }
                accumBuf.clear();
                expectW = expectH = expectSize = 0;
            }
        } else {
            Sleep(5);
        }
    }
    closesocket(recvSock);
}

