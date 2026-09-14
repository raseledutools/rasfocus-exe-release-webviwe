#define _CRT_SECURE_NO_WARNINGS
// winsock2.h আগে include করতে হবে — নইলে windows.h winsock1 টেনে আনে এবং conflict হয়
// WIN32_LEAN_AND_MEAN ব্যবহার করা যাচ্ছে না কারণ GDI+ এর IStream/PROPID দরকার।
// তাই _WINSOCKAPI_ define করে winsock1 block করি।
#define _WINSOCKAPI_
#include "accounts.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <wininet.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <fstream>
#include <process.h>
#include <wrl.h>
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace Microsoft::WRL;

// ── Google OAuth WebView2 Globals ──
extern ComPtr<ICoreWebView2Environment> g_sharedEnv; // mini_browser.cpp থেকে shared env

static HWND   s_googlePopupHwnd      = NULL;  // OAuth popup window
static ComPtr<ICoreWebView2Controller> s_googleController;
static ComPtr<ICoreWebView2>           s_googleWebView;
static bool   s_googleSignInPending   = false; // sign-in চলছে কিনা

// Google OAuth constants
static const std::string GOOGLE_CLIENT_ID     = "868329616276-jtv50h50toa7e563cdcihmrdv66hgvfd.apps.googleusercontent.com";
static const std::string GOOGLE_CLIENT_SECRET = "GOCSPX-placeholder"; // set in Google Console → OAuth → client secret
static const std::string GOOGLE_REDIRECT_URI  = "http://localhost:7878";
static const std::string FIREBASE_API_KEY     = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";

#ifndef IDI_APP_ICON
#define IDI_APP_ICON 101
#endif

using namespace Gdiplus;
using namespace std;

// ============================================================
//  GLOBALS
// ============================================================
bool    g_isPremiumUser    = false;
wstring g_loggedInEmail    = L"";
wstring g_loggedInName     = L"";
string  g_loggedInUserUid  = "";
string  g_loggedInIdToken  = "";   // Firebase Auth ID token — RasGram Firestore QR/chat এর জন্য
string  g_currentPackage   = "FREE_BASIC"; // প্যাকেজের নাম সেভ রাখার জন্য
bool    g_openCheckoutAfterLogin = false; // signup → auto-login → checkout

extern int selectedTab; // My Account Tab Close করার জন্য

static firebase::App* s_firebaseApp = nullptr;

// ── UI State ──
static wchar_t s_email   [512]  = {};
static wchar_t s_password[512]  = {};
static bool    s_saveLogin      = false;
static bool    s_showPassword   = false;
static int     s_focusField     = 0;       
static bool    s_isLoading      = false;
static wstring s_statusMsg      = L"";
static bool    s_isError        = false;
static DWORD   s_lastBlinkTime  = 0;
static bool    s_resetLoading   = false;  // Password Reset চলছে কিনা

// ── Hover states ──
static bool s_hoverLogin      = false;
static bool s_hoverCancel     = false;
static bool s_hoverSignup     = false;
static bool s_hoverReset      = false;
static bool s_hoverPrivacy    = false;
static bool s_hoverSaveCheck  = false;
static bool s_hoverEye        = false;
static bool s_hoverLogout     = false;
static bool s_hoverClose      = false;   // ← close (X) button
static bool s_hoverUpgrade    = false;   // ← upgrade button
static bool s_hoverGoogle     = false;   // ← Google Sign-In button

// ── Sign Up State ──
static bool    s_showSignup         = false;
bool           g_showSignupFromUpgrade = false; // upgrade.cpp থেকে সেট হয়
static bool    s_pendingCheckoutAfterSignup = false; // signup এর পর auto checkout
static wchar_t s_su_name    [512]   = {};
static wchar_t s_su_email   [512]   = {};
static wchar_t s_su_pass    [512]   = {};
static wchar_t s_su_confirm [512]   = {};
static int     s_su_focus           = 0;   
static bool    s_su_showPass        = false;
static bool    s_su_showConfirm     = false;
static bool    s_su_isLoading       = false;
static wstring s_su_statusMsg       = L"";
static bool    s_su_isError         = false;
static bool    s_su_success         = false;
static bool    s_hoverSuBtn         = false;
static bool    s_hoverSuBack        = false;
static bool    s_hoverSuEye1        = false;
static bool    s_hoverSuEye2        = false;

// ── Saved credentials path ──
static string GetCredsPath() {
    char appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData)))
        return string(appData) + "\\.rasfocus\\creds.dat";
    return "rasfocus_creds.dat";
}

static string XorStr(const string& s) {
    const char key[] = "RasFocus2024!@#$";
    string out = s;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] ^= key[i % (sizeof(key) - 1)];
    return out;
}

static void SaveCredentials(const wstring& email, const wstring& password) {
    char emailA[512] = {}, passA[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, email.c_str(),    -1, emailA, 511, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1, passA,  511, NULL, NULL);
    string data = string(emailA) + "\n" + string(passA);
    string enc  = XorStr(data);
    ofstream f(GetCredsPath(), ios::binary);
    if (f.is_open()) { f.write(enc.c_str(), enc.size()); f.close(); }
}

static bool LoadSavedCredentials() {
    ifstream f(GetCredsPath(), ios::binary);
    if (!f.is_open()) return false;
    string enc((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
    f.close();
    if (enc.empty()) return false;
    string data = XorStr(enc);
    size_t nl = data.find('\n');
    if (nl == string::npos) return false;
    string emailA = data.substr(0, nl);
    string passA  = data.substr(nl + 1);
    MultiByteToWideChar(CP_UTF8, 0, emailA.c_str(), -1, s_email,    511);
    MultiByteToWideChar(CP_UTF8, 0, passA.c_str(),  -1, s_password, 511);
    s_saveLogin = true;
    return true;
}

static void ClearSavedCredentials() {
    remove(GetCredsPath().c_str());
}

// ============================================================
//  FIREBASE REST LOGIN  (Email + Password)
// ============================================================
struct LoginResult {
    bool    success;
    string  idToken;
    string  localId;
    string  email;
    string  errorMsg;
};

// JSON থেকে বেসিক ভ্যালু বের করার ফাংশন
static string JsonExtract(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    
    size_t colon = json.find(":", pos);
    if (colon == string::npos) return "";
    
    size_t startQuote = json.find("\"", colon);
    if (startQuote == string::npos) return "";
    
    size_t endQuote = json.find("\"", startQuote + 1);
    if (endQuote == string::npos) return "";
    
    return json.substr(startQuote + 1, endQuote - startQuote - 1);
}

// 🔥 [BUG 2 FIX]: Firestore এর "stringValue" ডাইনামিক্যালি বের করার পারফেক্ট ফাংশন
static string FirestoreExtractString(const string& json, const string& fieldName) {
    string search = "\"" + fieldName + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    size_t svPos = json.find("\"stringValue\"", pos);
    if (svPos == string::npos) return "";
    size_t colon = json.find(":", svPos);
    size_t startQuote = json.find("\"", colon);
    if (startQuote == string::npos) return "";
    size_t endQuote = json.find("\"", startQuote + 1);
    if (endQuote == string::npos) return "";
    return json.substr(startQuote + 1, endQuote - startQuote - 1);
}

static LoginResult FirebaseLogin(const string& email, const string& password) {
    LoginResult res = { false, "", "", "", "" };
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:signInWithPassword?key=" + API_KEY;
    string body = "{\"email\":\"" + email + "\",\"password\":\"" + password + "\",\"returnSecureToken\":true}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) { res.errorMsg = "No internet connection."; return res; }

    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); res.errorMsg = "Connection failed."; return res; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); res.errorMsg = "Request failed."; return res; }

    string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());

    string response = "";
    if (sent) {
        char buf[4096]; DWORD bytesRead = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0'; response += buf;
        }
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    if (response.empty()) { res.errorMsg = "Empty server response."; return res; }
    
    if (response.find("\"error\"") != string::npos || response.find("\"errors\"") != string::npos) {
        string msg = JsonExtract(response, "message");
        if (msg == "EMAIL_NOT_FOUND" || msg == "INVALID_EMAIL") res.errorMsg = "Email not found. Please sign up first.";
        else if (msg == "INVALID_PASSWORD" || msg == "INVALID_LOGIN_CREDENTIALS" || msg.find("INVALID_LOGIN_CREDENTIALS") != string::npos) res.errorMsg = "Wrong password or email. Please try again.";
        else if (msg == "USER_DISABLED") res.errorMsg = "This account has been disabled.";
        else if (msg == "TOO_MANY_ATTEMPTS_TRY_LATER" || msg.find("TOO_MANY_ATTEMPTS") != string::npos) res.errorMsg = "Too many attempts. Try later.";
        else res.errorMsg = "Login failed: " + msg;
    } else {
        res.idToken = JsonExtract(response, "idToken");
        res.localId = JsonExtract(response, "localId");
        res.email   = JsonExtract(response, "email");
        res.success = !res.localId.empty();
        if (!res.success) res.errorMsg = "Login failed. Please try again.";
    }

    if (!res.success) {
        string dbg = "RAW SERVER RESPONSE:\n\n" + response;
        MessageBoxA(NULL, dbg.c_str(), "Firebase Login Debugger", MB_OK | MB_ICONWARNING);
    }

    return res;
}

// ── FIREBASE SIGNUP ──
static LoginResult FirebaseSignUp(const string& email, const string& password) {
    LoginResult res = { false, "", "", "", "" };
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:signUp?key=" + API_KEY;
    string body = "{\"email\":\"" + email + "\",\"password\":\"" + password + "\",\"returnSecureToken\":true}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) { res.errorMsg = "No internet connection."; return res; }

    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); res.errorMsg = "Connection failed."; return res; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); res.errorMsg = "Request failed."; return res; }

    string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());

    string response = "";
    if (sent) {
        char buf[4096]; DWORD bytesRead = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0'; response += buf;
        }
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    if (response.empty()) { res.errorMsg = "Empty server response."; return res; }
    
    if (response.find("\"error\"") != string::npos || response.find("\"errors\"") != string::npos) {
        string msg = JsonExtract(response, "message");
        if (msg == "EMAIL_EXISTS")                    res.errorMsg = "This email is already registered.";
        else if (msg == "INVALID_EMAIL")              res.errorMsg = "Invalid email address.";
        else if (msg == "WEAK_PASSWORD : Password should be at least 6 characters" ||
                 msg.find("WEAK_PASSWORD") != string::npos) res.errorMsg = "Password must be at least 6 characters.";
        else if (msg == "TOO_MANY_ATTEMPTS_TRY_LATER") res.errorMsg = "Too many attempts. Try later.";
        else                                           res.errorMsg = "Sign up failed: " + msg;
    } else {
        res.idToken = JsonExtract(response, "idToken");
        res.localId = JsonExtract(response, "localId");
        res.email   = JsonExtract(response, "email");
        res.success = !res.localId.empty();
        if (!res.success) res.errorMsg = "Sign up failed. Please try again.";
    }

    if (!res.success) {
        string dbg = "RAW SERVER RESPONSE:\n\n" + response;
        MessageBoxA(NULL, dbg.c_str(), "Firebase SignUp Debugger", MB_OK | MB_ICONWARNING);
    }

    return res;
}

// ── GOOGLE ID TOKEN দিয়ে Firebase এ Sign In ──
static LoginResult FirebaseGoogleSignIn(const string& googleIdToken) {
    LoginResult res = { false, "", "", "", "" };
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:signInWithIdp?key=" + API_KEY;
    // Google IdP দিয়ে Firebase login
    string postBody = "id_token=" + googleIdToken + "&providerId=google.com";
    string body = "{\"requestUri\":\"http://localhost\","
                  "\"postBody\":\"" + postBody + "\","
                  "\"returnSecureToken\":true,"
                  "\"returnIdpCredential\":true}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) { res.errorMsg = "No internet connection."; return res; }
    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); res.errorMsg = "Connection failed."; return res; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); res.errorMsg = "Request failed."; return res; }

    string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());
    string response = "";
    if (sent) {
        char buf[4096]; DWORD bytesRead = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0'; response += buf;
        }
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    if (response.empty()) { res.errorMsg = "Empty server response."; return res; }
    if (response.find("\"error\"") != string::npos) {
        string msg = JsonExtract(response, "message");
        res.errorMsg = "Google sign-in failed: " + msg;
    } else {
        res.idToken = JsonExtract(response, "idToken");
        res.localId = JsonExtract(response, "localId");
        res.email   = JsonExtract(response, "email");
        res.success = !res.localId.empty();
        if (!res.success) res.errorMsg = "Google sign-in failed. Please try again.";
    }
    return res;
}

// ── EMAIL VERIFICATION পাঠানোর ফাংশন ──
static void SendEmailVerification(const string& idToken) {
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:sendOobCode?key=" + API_KEY;
    string body = "{\"requestType\":\"VERIFY_EMAIL\",\"idToken\":\"" + idToken + "\"}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return;
    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return; }

    string headers = "Content-Type: application/json\r\n";
    HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);
}

// ── ইউজার VERIFIED কিনা চেক করার ফাংশন ──
static bool IsEmailVerified(const string& idToken) {
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:lookup?key=" + API_KEY;
    string body = "{\"idToken\":\"" + idToken + "\"}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());
    string response = "";
    if (sent) {
        char buf[4096]; DWORD bytesRead = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0'; response += buf;
        }
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    // JSON response এ "emailVerified":true আছে কিনা চেক করা
    return response.find("\"emailVerified\":true") != string::npos;
}

// ── PASSWORD RESET EMAIL পাঠানোর ফাংশন (Firebase REST API) ──
static bool SendPasswordResetEmail(const string& email) {
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:sendOobCode?key=" + API_KEY;
    string body = "{\"requestType\":\"PASSWORD_RESET\",\"email\":\"" + email + "\"}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());
    string response = "";
    if (sent) {
        char buf[4096]; DWORD bytesRead = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0'; response += buf;
        }
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    // সফল হলে response এ "email" থাকবে, error থাকবে না
    return response.find("\"error\"") == string::npos && response.find("\"email\"") != string::npos;
}

struct SignUpThreadData { HWND hWnd; string email; string password; string name; };

static void CreateFirestoreUserDoc(const string& uid, const string& idToken,
                                   const string& email, const string& name) {
    string host = "firestore.googleapis.com";
    string path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/users/" + uid;

    string body =
        "{"
        "\"fields\":{"
          "\"email\":{\"stringValue\":\"" + email + "\"},"
          "\"name\":{\"stringValue\":\"" + name + "\"},"
          "\"current_package\":{\"stringValue\":\"FREE_BASIC\"},"
          "\"created_at\":{\"stringValue\":\"" + to_string(time(nullptr)) + "\"}"
        "}"
        "}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return;
    HINTERNET hConn = InternetConnectA(hInet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "PATCH", path.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return; }

    string headers = "Content-Type: application/json\r\nAuthorization: Bearer " + idToken + "\r\n";
    HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());

    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);
}

void __cdecl SignUpThread(void* param) {
    SignUpThreadData* data = (SignUpThreadData*)param;
    LoginResult res = FirebaseSignUp(data->email, data->password);
    s_su_isLoading = false;
    if (res.success) {
        if (!res.idToken.empty() && !res.localId.empty())
            CreateFirestoreUserDoc(res.localId, res.idToken, res.email, data->name);

        // ✅ Email Verification পাঠানো (signup সফল হলেই)
        if (!res.idToken.empty())
            SendEmailVerification(res.idToken);

        // ✅ FIX: signup এর পরে g_loggedIn* সেট করা যাতে My Account এ নাম দেখা যায়
        wchar_t emailW[512] = {}, nameW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.email.c_str(),  -1, emailW, 511);
        MultiByteToWideChar(CP_UTF8, 0, data->name.c_str(), -1, nameW,  511);
        g_loggedInEmail   = emailW;
        g_loggedInName    = nameW;
        g_loggedInUserUid = res.localId;
        g_loggedInIdToken = res.idToken;   // QR/chat Firestore auth এর জন্য

        s_su_success   = true;
        s_su_statusMsg = L"";
        s_su_isError   = false;
    } else {
        wchar_t errW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.errorMsg.c_str(), -1, errW, 511);
        s_su_statusMsg = errW;
        s_su_isError   = true;
    }
    if (data->hWnd) InvalidateRect(data->hWnd, NULL, FALSE);
    delete data;
    _endthread();
}

static bool CheckPremiumFromFirebase(const string& uid, const string& idToken) {
    string host = "firestore.googleapis.com";
    string path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/users/" + uid;

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return false;

    HINTERNET hConn = InternetConnectA(hInet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "GET", path.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    string headers = "Authorization: Bearer " + idToken + "\r\n";
    HttpSendRequestA(hReq, headers.c_str(), headers.length(), NULL, 0);

    string response = ""; char buf[4096]; DWORD br = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &br) && br > 0) { buf[br] = '\0'; response += buf; }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    // 🔥 [BUG 2 FIX]: FirestoreExtractString ব্যবহার করে একদম সঠিক নাম বের করা
    string nameA = FirestoreExtractString(response, "name");
    if (!nameA.empty()) {
        wchar_t nameW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, nameA.c_str(), -1, nameW, 511);
        g_loggedInName = nameW;
    }

    // 🔥 [BUG 3 FIX]: প্যাকেজের আসল নাম বের করে সেভ করা
    string pkgName = FirestoreExtractString(response, "current_package");
    if (!pkgName.empty()) {
        g_currentPackage = pkgName;
    } else {
        g_currentPackage = "FREE_BASIC";
    }

    bool isPremium = false;
    if (pkgName == "PREMIUM" || pkgName == "1 Month Pro" || pkgName == "6 Month Saver" || 
        pkgName == "1 Year Ultimate" || pkgName == "STUDENT" || pkgName == "PARENTAL") 
    {
        isPremium = true;
    }
    
    g_isPremiumUser = isPremium;
    return isPremium;
}

struct LoginThreadData { HWND hWnd; string email; string password; bool saveLogin; };

// ── PASSWORD RESET THREAD ──
struct ResetThreadData { HWND hWnd; string email; };

void __cdecl PasswordResetThread(void* param) {
    ResetThreadData* data = (ResetThreadData*)param;
    bool ok = SendPasswordResetEmail(data->email);
    s_resetLoading = false;
    if (ok) {
        s_statusMsg = L"Password reset email sent! Please check your inbox.";
        s_isError   = false;
    } else {
        s_statusMsg = L"Could not send reset email. Check your email address.";
        s_isError   = true;
    }
    if (data->hWnd) InvalidateRect(data->hWnd, NULL, FALSE);
    delete data;
    _endthread();
}

// ============================================================
//  GOOGLE SIGN-IN — WebView2 OAuth Popup
// ============================================================

// ── Auth Code Flow helpers ──
// Google এখন desktop apps এ implicit flow (response_type=token) block করে।
// তাই Authorization Code Flow ব্যবহার করতে হবে:
//   1. response_type=code দিয়ে OAuth URL খোলো
//   2. localhost:7878 এ একটা mini HTTP server চালাও
//   3. Google সেখানে redirect করবে ?code=XXX দিয়ে
//   4. code দিয়ে token endpoint থেকে id_token নাও
//   5. id_token দিয়ে Firebase login করো

static string BuildGoogleOAuthURL_Str() {
    return
        "https://accounts.google.com/o/oauth2/v2/auth"
        "?client_id=" + GOOGLE_CLIENT_ID +
        "&redirect_uri=" + GOOGLE_REDIRECT_URI +
        "&response_type=code"
        "&scope=openid%20email%20profile"
        "&access_type=offline"
        "&prompt=select_account";
}

static wstring BuildGoogleOAuthURL() {
    string s = BuildGoogleOAuthURL_Str();
    return wstring(s.begin(), s.end());
}

// query string から param 값 추출 (e.g. "code=XXX&state=YYY" → "XXX")
static string ExtractQueryParam(const string& query, const string& key) {
    string search = key + "=";
    size_t pos = query.find(search);
    if (pos == string::npos) return "";
    size_t start = pos + search.size();
    size_t end   = query.find('&', start);
    return (end == string::npos) ? query.substr(start) : query.substr(start, end - start);
}

// auth code → id_token exchange (Google token endpoint)
static string ExchangeCodeForIdToken(const string& code) {
    const string HOST = "oauth2.googleapis.com";
    const string PATH = "/token";
    string body =
        "code="          + code +
        "&client_id="    + GOOGLE_CLIENT_ID +
        "&client_secret="+ GOOGLE_CLIENT_SECRET +
        "&redirect_uri=" + GOOGLE_REDIRECT_URI +
        "&grant_type=authorization_code";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return "";
    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT,
                                       NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return ""; }
    HINTERNET hReq  = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq)  { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return ""; }

    string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(),
                     (LPVOID)body.c_str(), (DWORD)body.size());

    string response; char buf[4096]; DWORD br = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf)-1, &br) && br > 0) {
        buf[br] = '\0'; response += buf;
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    return JsonExtract(response, "id_token");
}

// localhost:7878 mini HTTP server — auth code를 한 번 받아서 반환
// blocking call이므로 반드시 별도 thread에서 호출할 것
static string WaitForAuthCode() {
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) return "";

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(7878);

    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) != 0) { closesocket(srv); return ""; }
    if (listen(srv, 1) != 0)                             { closesocket(srv); return ""; }

    // 120s timeout
    DWORD to = 120000;
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));

    SOCKET cli = accept(srv, NULL, NULL);
    closesocket(srv);
    if (cli == INVALID_SOCKET) return "";

    char req[4096] = {};
    recv(cli, req, sizeof(req)-1, 0);

    // HTTP response — success page
    const char* resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n\r\n"
        "<html><body style='font-family:Segoe UI;text-align:center;padding:60px'>"
        "<h2 style='color:#009BB4'>&#10003; Signed in successfully!</h2>"
        "<p>You can close this window and return to RasFocus.</p>"
        "</body></html>";
    send(cli, resp, (int)strlen(resp), 0);
    closesocket(cli);

    // GET /? code=XXX&... から code を取り出す
    string reqStr(req);
    size_t getPos = reqStr.find("GET /?");
    if (getPos == string::npos) return "";
    size_t qStart = getPos + 6;
    size_t qEnd   = reqStr.find(" HTTP", qStart);
    if (qEnd == string::npos) return "";
    string query = reqStr.substr(qStart, qEnd - qStart);
    return ExtractQueryParam(query, "code");
}

// wstring URL に含まれる ?code= を取り出す (WebView2 navigation hook 用)
static string ExtractCodeFromUrl(const wstring& url) {
    size_t qPos = url.find(L'?');
    if (qPos == wstring::npos) return "";
    wstring wQuery = url.substr(qPos + 1);
    string  query(wQuery.begin(), wQuery.end());
    return ExtractQueryParam(query, "code");
}

// Google Sign-In Thread — token পেলে Firebase login করে
struct GoogleSignInData { HWND hMainWnd; string idToken; };

void __cdecl GoogleSignInThread(void* param) {
    GoogleSignInData* data = (GoogleSignInData*)param;

    LoginResult res = FirebaseGoogleSignIn(data->idToken);
    s_googleSignInPending = false;

    if (res.success) {
        // Firestore এ user doc তৈরি/আপডেট করা
        if (!res.idToken.empty() && !res.localId.empty()) {
            // Google user এর display name বের করার চেষ্টা
            string nameA = JsonExtract(res.localId, "displayName");
            if (nameA.empty()) nameA = res.email; // fallback
            CreateFirestoreUserDoc(res.localId, res.idToken, res.email, nameA);
        }

        bool isPremium = CheckPremiumFromFirebase(res.localId, res.idToken);

        wchar_t emailW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.email.c_str(), -1, emailW, 511);
        g_loggedInEmail   = emailW;
        g_loggedInUserUid = res.localId;
        g_loggedInIdToken = res.idToken;   // QR/chat Firestore auth এর জন্য

        s_statusMsg = isPremium
            ? L"Google sign-in successful! Premium active."
            : L"Google sign-in successful!";
        s_isError   = false;

        if (g_openCheckoutAfterLogin && !g_loggedInUserUid.empty()) {
            g_openCheckoutAfterLogin = false;
            string urlStr = "https://raseledutools.github.io/checkout.html?uid=" + g_loggedInUserUid;
            wstring wUrl(urlStr.begin(), urlStr.end());
            ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    } else {
        wchar_t errW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.errorMsg.c_str(), -1, errW, 511);
        s_statusMsg = errW;
        s_isError   = true;
    }

    if (data->hMainWnd) InvalidateRect(data->hMainWnd, NULL, FALSE);
    delete data;
    _endthread();
}

// Google OAuth Popup এর WndProc
static HWND s_googleMainHwnd = NULL; // main window ref (thread callback এ দরকার)

LRESULT CALLBACK GoogleOAuthWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (s_googleController) {
            RECT rc; GetClientRect(hWnd, &rc);
            s_googleController->put_Bounds(rc);
        }
        break;
    case WM_CLOSE:
        // Popup বন্ধ হলে cleanup
        s_googleController = nullptr;
        s_googleWebView    = nullptr;
        s_googleSignInPending = false;
        s_googlePopupHwnd  = NULL;
        if (s_googleMainHwnd) {
            s_statusMsg = L"Google sign-in cancelled.";
            s_isError   = false;
            InvalidateRect(s_googleMainHwnd, NULL, FALSE);
        }
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        s_googlePopupHwnd = NULL;
        break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// Google OAuth Popup তৈরি এবং WebView2 সেট আপ
static void OpenGoogleSignInPopup(HWND hMainWnd) {
    if (s_googlePopupHwnd) {
        // আগেই popup খোলা আছে — সামনে আনো
        SetForegroundWindow(s_googlePopupHwnd);
        return;
    }
    s_googleMainHwnd = hMainWnd;

    // Window class register করা
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = GoogleOAuthWndProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"GoogleOAuthPopup";
        RegisterClassExW(&wc);
        classReg = true;
    }

    // Popup window তৈরি
    int popW = 480, popH = 620;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int popX = (screenW - popW) / 2;
    int popY = (screenH - popH) / 2;

    s_googlePopupHwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"GoogleOAuthPopup",
        L"Sign in with Google",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        popX, popY, popW, popH,
        hMainWnd, NULL, GetModuleHandleW(NULL), NULL
    );
    if (!s_googlePopupHwnd) return;

    // ✅ FIX: Google এর WebView detection bypass করতে
    // g_sharedEnv এর বদলে আলাদা environment তৈরি করা
    // যেখানে AutomationControlled disable এবং proper browser args আছে
    HWND popupHwnd = s_googlePopupHwnd;
    HWND mainHwnd  = hMainWnd;

    // আলাদা user data dir Google এর জন্য
    wchar_t appDataPath[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath);
    std::wstring googleUdDir = std::wstring(appDataPath) + L"\\RasFocusGoogleAuth";
    CreateDirectoryW(googleUdDir.c_str(), NULL);

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(
        L"--disable-blink-features=AutomationControlled "
        L"--disable-features=IsolateOrigins,site-per-process "
        L"--disable-web-security "
        L"--allow-running-insecure-content"
    );

    ComPtr<ICoreWebView2EnvironmentOptions> envOptions;
    options.As(&envOptions);

    HRESULT hr2 = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, googleUdDir.c_str(), envOptions.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [popupHwnd, mainHwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) {
                // নিজস্ব env তৈরি ব্যর্থ হলে — g_sharedEnv দিয়ে চেষ্টা
                ICoreWebView2Environment* fallbackEnv = g_sharedEnv.Get();
                if (fallbackEnv) {
                    fallbackEnv->CreateCoreWebView2Controller(popupHwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [popupHwnd, mainHwnd](HRESULT h2, ICoreWebView2Controller* ctl) -> HRESULT {
                            if (FAILED(h2) || !ctl) {
                                s_statusMsg = L"Google sign-in failed: WebView2 not available.";
                                s_isError   = true;
                                InvalidateRect(mainHwnd, NULL, FALSE);
                                PostMessageW(popupHwnd, WM_CLOSE, 0, 0);
                                return S_OK;
                            }
                            s_googleController = ctl;
                            ctl->get_CoreWebView2(&s_googleWebView);
                            RECT rc; GetClientRect(popupHwnd, &rc);
                            ctl->put_Bounds(rc);
                            s_googleWebView->Navigate(BuildGoogleOAuthURL().c_str());
                            return S_OK;
                        }).Get());
                } else {
                    s_statusMsg = L"Google sign-in failed: WebView2 not available.";
                    s_isError   = true;
                    InvalidateRect(mainHwnd, NULL, FALSE);
                    PostMessageW(popupHwnd, WM_CLOSE, 0, 0);
                }
                return S_OK;
            }
            env->CreateCoreWebView2Controller(popupHwnd,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [popupHwnd, mainHwnd](HRESULT h, ICoreWebView2Controller* ctl) -> HRESULT {
            if (FAILED(h) || !ctl) return S_OK;
            s_googleController = ctl;
            ctl->get_CoreWebView2(&s_googleWebView);

            // WebView2 size ঠিক করা
            RECT rc; GetClientRect(popupHwnd, &rc);
            ctl->put_Bounds(rc);

            // ✅ FIX: Google এর WebView block bypass করার জন্য
            // User-Agent কে Chrome এর মতো করা + WebView string সরানো
            ComPtr<ICoreWebView2Settings> settings;
            if (SUCCEEDED(s_googleWebView->get_Settings(&settings)) && settings) {
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_AreDevToolsEnabled(FALSE);

                // Settings2 দিয়ে User-Agent override করা
                ComPtr<ICoreWebView2Settings2> settings2;
                if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings2)))) {
                    // "WebView/" অংশ সরিয়ে pure Chrome UA দেওয়া
                    settings2->put_UserAgent(
                        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                        L"AppleWebKit/537.36 (KHTML, like Gecko) "
                        L"Chrome/124.0.0.0 Safari/537.36"
                    );
                }
            }

            // ✅ FIX: document.createElement override করে
            // navigator.userAgent থেকেও WebView string মুছে দেওয়া
            s_googleWebView->AddScriptToExecuteOnDocumentCreated(
                L"Object.defineProperty(navigator,'userAgent',{get:function(){"
                L"return 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                L"AppleWebKit/537.36 (KHTML, like Gecko) "
                L"Chrome/124.0.0.0 Safari/537.36';}});"
                L"Object.defineProperty(navigator,'webdriver',{get:()=>false});"
                L"window.chrome={runtime:{},loadTimes:function(){},csi:function(){}};"
                L"Object.defineProperty(navigator,'plugins',{get:()=>[1,2,3,4,5]});"
                L"Object.defineProperty(navigator,'languages',{get:()=>['en-US','en']});",
                nullptr);

            // Navigation এ URL monitor করা — redirect ধরব
            s_googleWebView->add_NavigationStarting(
                Callback<ICoreWebView2NavigationStartingEventHandler>(
                [mainHwnd, popupHwnd](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    LPWSTR uriW = nullptr;
                    args->get_Uri(&uriW);
                    if (!uriW) return S_OK;
                    wstring uri(uriW);
                    CoTaskMemFree(uriW);

                    // Redirect URI এ পৌঁছলে auth code ধরা
                    if (uri.find(L"http://localhost:7878") == 0) {
                        args->put_Cancel(TRUE); // navigation বন্ধ করা

                        // auth code extract করা (?code=XXX)
                        string authCode = ExtractCodeFromUrl(uri);
                        if (!authCode.empty()) {
                            s_statusMsg = L"Signing in with Google...";
                            s_isError   = false;
                            InvalidateRect(mainHwnd, NULL, FALSE);

                            // auth code → id_token → Firebase login (thread এ)
                            GoogleSignInData* gd = new GoogleSignInData();
                            gd->hMainWnd = mainHwnd;
                            gd->idToken  = authCode; // idToken field এ code রাখছি
                            // আলাদা wrapper thread: code → id_token exchange → Firebase
                            _beginthread([](void* p) {
                                GoogleSignInData* d = (GoogleSignInData*)p;
                                // idToken field এ আসলে auth code আছে — exchange করো
                                string idToken = ExchangeCodeForIdToken(d->idToken);
                                if (!idToken.empty()) {
                                    d->idToken = idToken;
                                    GoogleSignInThread(d); // এটা delete d করে নেয়
                                } else {
                                    s_statusMsg = L"Google sign-in failed: token exchange error.";
                                    s_isError   = true;
                                    InvalidateRect(d->hMainWnd, NULL, FALSE);
                                    delete d;
                                }
                                _endthread();
                            }, 0, gd);
                        } else {
                            s_statusMsg = L"Google sign-in failed: no auth code received.";
                            s_isError   = true;
                            InvalidateRect(mainHwnd, NULL, FALSE);
                        }

                        // Popup বন্ধ করা
                        PostMessageW(popupHwnd, WM_CLOSE, 0, 0);
                    }
                    return S_OK;
                }).Get(), nullptr);

            // Google OAuth URL এ navigate করা
            s_googleWebView->Navigate(BuildGoogleOAuthURL().c_str());
            return S_OK;
            }).Get());
            return S_OK;
        }).Get()
    );
}

void __cdecl LoginThread(void* param) {
    LoginThreadData* data = (LoginThreadData*)param;
    LoginResult res = FirebaseLogin(data->email, data->password);

    if (res.success) {
        // ✅ Email Verified কিনা চেক করা
        bool verified = IsEmailVerified(res.idToken);
        if (!verified) {
            // Verified না হলে login বন্ধ, re-send verification email
            SendEmailVerification(res.idToken);
            s_isLoading = false;
            s_statusMsg = L"Please verify your email first. A new verification link has been sent.";
            s_isError   = true;
            if (data->hWnd) InvalidateRect(data->hWnd, NULL, FALSE);
            delete data;
            _endthread();
            return;
        }

        bool isPremium = CheckPremiumFromFirebase(res.localId, res.idToken);

        wchar_t emailW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.email.c_str(), -1, emailW, 511);
        g_loggedInEmail   = emailW;
        g_loggedInUserUid = res.localId;
        g_loggedInIdToken = res.idToken;   // QR/chat Firestore auth এর জন্য

        if (data->saveLogin) {
            wchar_t passW[512] = {};
            MultiByteToWideChar(CP_UTF8, 0, data->password.c_str(), -1, passW, 511);
            SaveCredentials(emailW, passW);
        }

        s_isLoading = false;
        s_statusMsg = isPremium ? L"Welcome back! Premium account active." : L"Logged in successfully!";
        s_isError   = false;
        ZeroMemory(s_password, sizeof(s_password));

        // 🔥 [BUG 1 FIX]: নতুন ডোমেইনের পেমেন্ট পেজে রিডাইরেক্ট
        if (g_openCheckoutAfterLogin && !g_loggedInUserUid.empty()) {
            g_openCheckoutAfterLogin = false;
            string urlStr = "https://raseledutools.github.io/checkout.html?uid=" + g_loggedInUserUid;
            wstring wUrl(urlStr.begin(), urlStr.end());
            ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    } else {
        s_isLoading = false;
        wchar_t errW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.errorMsg.c_str(), -1, errW, 511);
        s_statusMsg = errW;
        s_isError   = true;
    }

    if (data->hWnd) InvalidateRect(data->hWnd, NULL, FALSE);
    delete data;
    _endthread();
}

void __cdecl CursorBlinkThread(void* p) {
    while (true) {
        if (s_focusField != 0) {
            if (HWND hw = FindWindowA("RasFocusCore", NULL)) {
                InvalidateRect(hw, NULL, FALSE);
            }
        }
        Sleep(500);
    }
    _endthread();
}

void InitAccountsModule(firebase::App* app) {
    s_firebaseApp = app;
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    if (LoadSavedCredentials()) s_statusMsg = L"";
    _beginthread(CursorBlinkThread, 0, NULL);
}

// ============================================================
//  LAYOUT
// ============================================================
extern int   windowWidth;
extern int   windowHeight;
extern const int SIDEBAR_WIDTH;
extern const int TITLEBAR_HEIGHT;
extern const int SUBHEADER_HEIGHT;
extern float g_scaleFactor;

struct CardLayout {
    float cardX, cardY, cardW, cardH;
    float logoSz, logoX, logoY;
    float fieldX, fieldW, fldH;
    float emailY, passY, checkY, linksY;
    float loginBtnX, loginBtnY, loginBtnW, loginBtnH;
    float cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH;
    float eyeX, eyeY, eyeW;
    float signupX, signupY, signupW, signupH;
    float resetX, resetY, resetW, resetH;
    float privacyX, privacyY, privacyW, privacyH;
    float googleBtnX, googleBtnY, googleBtnW, googleBtnH; // Google Sign-In
};

static CardLayout GetLayout(float cx, float cy, float cw, float ch) {
    CardLayout L = {};
    L.cardW = 380.0f;
    L.cardH = 490.0f;  // Google Sign-In button এর জন্য বাড়ানো হয়েছে (ছিল 440)
    L.cardX = cx + (cw - L.cardW) / 2.0f;
    L.cardY = cy + (ch - L.cardH) / 2.0f;
    L.logoSz = 56.0f;
    L.logoX  = L.cardX + (L.cardW - L.logoSz) / 2.0f;
    L.logoY  = L.cardY + 25.0f;
    L.fieldW = L.cardW - 70.0f;
    L.fieldX = L.cardX + 35.0f;
    L.fldH   = 36.0f;
    L.emailY = L.logoY + L.logoSz + 45.0f;
    L.passY  = L.emailY + L.fldH + 15.0f;
    L.eyeW   = 32.0f;
    L.eyeX   = L.fieldX + L.fieldW - L.eyeW;
    L.eyeY   = L.passY;
    L.checkY = L.passY + L.fldH + 12.0f;
    float btnGap = 10.0f;
    L.loginBtnW  = (L.fieldW - btnGap) / 2.0f;
    L.loginBtnH  = 36.0f;
    L.loginBtnX  = L.fieldX;
    L.loginBtnY  = L.checkY + 30.0f;
    L.cancelBtnW = L.loginBtnW;
    L.cancelBtnH = L.loginBtnH;
    L.cancelBtnX = L.loginBtnX + L.loginBtnW + btnGap;
    L.cancelBtnY = L.loginBtnY;
    L.linksY  = L.loginBtnY + L.loginBtnH + 20.0f;
    L.signupX = L.cardX + 40.0f;
    L.signupY = L.linksY;
    L.signupW = 120.0f;
    L.signupH = 20.0f;
    L.resetX  = L.cardX + L.cardW - 120.0f;
    L.resetY  = L.linksY;
    L.resetW  = 90.0f;
    L.resetH  = 20.0f;
    // Google Sign-In button
    L.googleBtnW = 200.0f;
    L.googleBtnH = 34.0f;
    L.googleBtnX = L.cardX + (L.cardW - L.googleBtnW) / 2.0f;
    L.googleBtnY = L.linksY + 26.0f;
    L.privacyW = 80.0f;
    L.privacyH = 18.0f;
    L.privacyX = L.cardX + (L.cardW - L.privacyW) / 2.0f;
    L.privacyY = L.cardY + L.cardH - 22.0f;
    return L;
}

static CardLayout GetCurrentLayout() {
    float scaledW = (float)windowWidth  / g_scaleFactor;
    float scaledH = (float)windowHeight / g_scaleFactor;
    float cx = (float)SIDEBAR_WIDTH;
    float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
    float cw = scaledW - cx;
    float ch = scaledH - cy;
    return GetLayout(cx, cy, cw, ch);
}

static bool HitRect(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// ============================================================
//  DRAW SIGN UP FORM
// ============================================================
static void DrawSignUpForm(Graphics& g, float cx, float cy, float cw, float ch) {
    SolidBrush bgBrush(Color(255, 240, 248, 255));
    g.FillRectangle(&bgBrush, cx, cy, cw, ch);

    float cardW = 400.0f, cardH = 530.0f;
    float cardX = cx + (cw - cardW) / 2.0f;
    float cardY = cy + (ch - cardH) / 2.0f;

    for (int i = 3; i >= 0; --i) {
        SolidBrush shadowBrush(Color(10 + i * 5, 0, 80, 140));
        GraphicsPath shadowPath;
        float sr = 12.0f, sd = sr * 2.0f;
        float sx = cardX - i, sy = cardY + i, sw2 = cardW + i * 2.0f, sh2 = cardH;
        shadowPath.AddArc(sx, sy, sd, sd, 180.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy, sd, sd, 270.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy + sh2 - sd, sd, sd, 0.0f, 90.0f);
        shadowPath.AddArc(sx, sy + sh2 - sd, sd, sd, 90.0f, 90.0f);
        shadowPath.CloseFigure();
        g.FillPath(&shadowBrush, &shadowPath);
    }

    GraphicsPath cardPath;
    float rc = 12.0f, dc = rc * 2.0f;
    cardPath.AddArc(cardX, cardY, dc, dc, 180.0f, 90.0f);
    cardPath.AddArc(cardX + cardW - dc, cardY, dc, dc, 270.0f, 90.0f);
    cardPath.AddArc(cardX + cardW - dc, cardY + cardH - dc, dc, dc, 0.0f, 90.0f);
    cardPath.AddArc(cardX, cardY + cardH - dc, dc, dc, 90.0f, 90.0f);
    cardPath.CloseFigure();
    SolidBrush cardBg(Color(255, 255, 255, 255));
    g.FillPath(&cardBg, &cardPath);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    SolidBrush teal(Color(255, 0, 150, 180));
    SolidBrush dark(Color(255, 40, 40, 50));
    SolidBrush gray(Color(255, 130, 140, 150));
    SolidBrush white(Color(255, 255, 255, 255));
    SolidBrush fieldBg(Color(255, 248, 250, 252));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    Font fTitle(&ff, 15, FontStyleBold, UnitPixel);
    g.DrawString(L"Create Account", -1, &fTitle, RectF(cardX, cardY + 22.0f, cardW, 26.0f), &fmtC, &dark);

    float fieldX = cardX + 35.0f;
    float fieldW = cardW - 70.0f;
    float fldH   = 36.0f;
    float eyeW   = 32.0f;

    float nameY    = cardY + 65.0f;
    float emailY   = nameY  + fldH + 14.0f;
    float passY    = emailY + fldH + 14.0f;
    float confirmY = passY  + fldH + 14.0f;

    Font fInput(&ff, 12, FontStyleRegular, UnitPixel);
    Font fFieldIcon(&ffIcons, 13, FontStyleRegular, UnitPixel);
    bool showCursor = ((GetTickCount() - s_lastBlinkTime) % 1000) < 500;

    if (s_su_success) {
        Font fBig(&ff, 16, FontStyleBold, UnitPixel);
        Font fSub(&ff, 11, FontStyleRegular, UnitPixel);
        SolidBrush green(Color(255, 0, 160, 90));
        SolidBrush orange(Color(255, 200, 100, 0));
        g.DrawString(L"\u2713 Account Created Successfully!", -1, &fBig,
            RectF(cardX, cardY + cardH / 2.0f - 70.0f, cardW, 30.0f), &fmtC, &green);
        g.DrawString(L"A verification email has been sent to your inbox.", -1, &fSub,
            RectF(cardX, cardY + cardH / 2.0f - 30.0f, cardW, 24.0f), &fmtC, &gray);
        g.DrawString(L"Please verify your email before logging in.", -1, &fSub,
            RectF(cardX, cardY + cardH / 2.0f - 8.0f, cardW, 24.0f), &fmtC, &orange);

        float btnW = 160.0f, btnH = 38.0f;
        float btnX = cardX + (cardW - btnW) / 2.0f;
        float btnY = cardY + cardH / 2.0f + 30.0f;
        GraphicsPath bp;
        float br2 = 6.0f, bd2 = br2 * 2.0f;
        bp.AddArc(btnX, btnY, bd2, bd2, 180.0f, 90.0f);
        bp.AddArc(btnX + btnW - bd2, btnY, bd2, bd2, 270.0f, 90.0f);
        bp.AddArc(btnX + btnW - bd2, btnY + btnH - bd2, bd2, bd2, 0.0f, 90.0f);
        bp.AddArc(btnX, btnY + btnH - bd2, bd2, bd2, 90.0f, 90.0f);
        bp.CloseFigure();
        LinearGradientBrush btnBg(PointF(btnX, btnY), PointF(btnX + btnW, btnY + btnH),
            Color(255, 0, 120, 140), Color(255, 0, 180, 200));
        g.FillPath(&btnBg, &bp);
        Font fBtn(&ff, 13, FontStyleBold, UnitPixel);
        g.DrawString(L"Go to Login", -1, &fBtn, RectF(btnX, btnY, btnW, btnH), &fmtC, &white);
        return;
    }

    auto DrawField = [&](float fy, int focusId, const wchar_t* placeholder, wchar_t* buf,
                         bool isPass, bool showPass, bool showEye, bool hoverEye) {
        bool focused = (s_su_focus == focusId);
        g.FillRectangle(&fieldBg, fieldX, fy, fieldW, fldH);
        Pen border(focused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), focused ? 1.5f : 1.0f);
        g.DrawRectangle(&border, fieldX, fy, fieldW, fldH);

        wstring str(buf);
        float textX = fieldX + 32.0f;
        float availW = isPass ? fieldW - 65.0f : fieldW - 40.0f;
        SolidBrush inputColor(str.empty() ? Color(255, 160, 170, 180) : Color(255, 50, 50, 50));

        if (!str.empty()) {
            wstring display = (isPass && !showPass) ? wstring(str.size(), L'\u2022') : str;
            g.DrawString(display.c_str(), -1, &fInput, RectF(textX, fy, availW, fldH), &fmtL, &inputColor);
            if (focused && showCursor) {
                RectF bbox;
                g.MeasureString(display.c_str(), -1, &fInput, PointF(textX, fy), &fmtL, &bbox);
                float curX = textX + bbox.Width;
                if (curX > fieldX + fieldW - (isPass ? 40.0f : 12.0f)) curX = fieldX + fieldW - (isPass ? 40.0f : 12.0f);
                Pen cur(Color(255, 0, 150, 160), 2.0f);
                g.DrawLine(&cur, curX, fy + 6.0f, curX, fy + fldH - 6.0f);
            }
        } else {
            if (focused && showCursor) {
                Pen cur(Color(255, 0, 150, 160), 2.0f);
                g.DrawLine(&cur, textX, fy + 8.0f, textX, fy + fldH - 8.0f);
            }
            g.DrawString(placeholder, -1, &fInput, RectF(textX, fy, availW, fldH), &fmtL, &inputColor);
        }

        if (isPass && showEye) {
            float ex = fieldX + fieldW - eyeW;
            Font fEye(&ffIcons, 13, FontStyleRegular, UnitPixel);
            SolidBrush eyeC(hoverEye ? Color(255, 0, 150, 160) : Color(255, 160, 170, 180));
            g.DrawString(showPass ? L"\xED1A" : L"\xE7B3", -1, &fEye, RectF(ex, fy, eyeW, fldH), &fmtC, &eyeC);
        }
    };

    g.DrawString(L"\xE77B", -1, &fFieldIcon, RectF(fieldX + 8.0f, nameY, 24.0f, fldH), &fmtC, wcslen(s_su_name) ? &teal : &gray);
    DrawField(nameY, 1, L"Full Name", s_su_name, false, false, false, false);

    g.DrawString(L"\xE715", -1, &fFieldIcon, RectF(fieldX + 8.0f, emailY, 24.0f, fldH), &fmtC, wcslen(s_su_email) ? &teal : &gray);
    DrawField(emailY, 2, L"Email address", s_su_email, false, false, false, false);

    g.DrawString(L"\xE72E", -1, &fFieldIcon, RectF(fieldX + 8.0f, passY, 24.0f, fldH), &fmtC, wcslen(s_su_pass) ? &teal : &gray);
    DrawField(passY, 3, L"Create Password", s_su_pass, true, s_su_showPass, true, s_hoverSuEye1);

    g.DrawString(L"\xE72E", -1, &fFieldIcon, RectF(fieldX + 8.0f, confirmY, 24.0f, fldH), &fmtC, wcslen(s_su_confirm) ? &teal : &gray);
    DrawField(confirmY, 4, L"Confirm Password", s_su_confirm, true, s_su_showConfirm, true, s_hoverSuEye2);

    float statusY = confirmY + fldH + 8.0f;
    if (!s_su_statusMsg.empty()) {
        SolidBrush statusBrush(s_su_isError ? Color(255, 220, 60, 60) : Color(255, 0, 160, 90));
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(s_su_statusMsg.c_str(), -1, &fStatus, RectF(cardX, statusY, cardW, 18.0f), &fmtC, &statusBrush);
    } else if (s_su_isLoading) {
        SolidBrush statusBrush(Color(255, 0, 150, 180));
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(L"Creating account...", -1, &fStatus, RectF(cardX, statusY, cardW, 18.0f), &fmtC, &statusBrush);
    }

    float btnY    = statusY + 26.0f;
    float btnGap  = 10.0f;
    float btnW    = (fieldW - btnGap) / 2.0f;
    float btnH    = 38.0f;
    float suBtnX  = fieldX;
    float backBtnX = fieldX + btnW + btnGap;

    GraphicsPath bp1;
    float br2 = 6.0f, bd2 = br2 * 2.0f;
    bp1.AddArc(suBtnX, btnY, bd2, bd2, 180.0f, 90.0f);
    bp1.AddArc(suBtnX + btnW - bd2, btnY, bd2, bd2, 270.0f, 90.0f);
    bp1.AddArc(suBtnX + btnW - bd2, btnY + btnH - bd2, bd2, bd2, 0.0f, 90.0f);
    bp1.AddArc(suBtnX, btnY + btnH - bd2, bd2, bd2, 90.0f, 90.0f);
    bp1.CloseFigure();
    LinearGradientBrush suBg(PointF(suBtnX, btnY), PointF(suBtnX + btnW, btnY + btnH),
        Color(255, 0, 120, 140), Color(255, 0, 180, 200));
    SolidBrush suHover(Color(255, 0, 90, 110));
    if (s_hoverSuBtn) g.FillPath(&suHover, &bp1); else g.FillPath(&suBg, &bp1);
    Font fBtn(&ff, 12, FontStyleBold, UnitPixel);
    g.DrawString(L"Create Account", -1, &fBtn, RectF(suBtnX, btnY, btnW, btnH), &fmtC, &white);

    GraphicsPath bp2;
    bp2.AddArc(backBtnX, btnY, bd2, bd2, 180.0f, 90.0f);
    bp2.AddArc(backBtnX + btnW - bd2, btnY, bd2, bd2, 270.0f, 90.0f);
    bp2.AddArc(backBtnX + btnW - bd2, btnY + btnH - bd2, bd2, bd2, 0.0f, 90.0f);
    bp2.AddArc(backBtnX, btnY + btnH - bd2, bd2, bd2, 90.0f, 90.0f);
    bp2.CloseFigure();
    SolidBrush backBg(s_hoverSuBack ? Color(255, 245, 250, 255) : Color(255, 255, 255, 255));
    g.FillPath(&backBg, &bp2);
    Pen backPen(Color(255, 0, 150, 180), 1.5f);
    g.DrawPath(&backPen, &bp2);
    g.DrawString(L"Back to Login", -1, &fBtn, RectF(backBtnX, btnY, btnW, btnH), &fmtC, &teal);

    wstring loginPart1 = L"Already have an account? ";
    wstring loginPart2 = L"Log In";
    Font fSmall(&ff, 11, FontStyleRegular, UnitPixel);
    Font fSmallB(&ff, 11, FontStyleBold, UnitPixel);
    SolidBrush grayLink(Color(255, 130, 140, 150));
    SolidBrush tealLink(Color(255, 0, 150, 160));
    StringFormat sfNear; sfNear.SetAlignment(StringAlignmentNear); sfNear.SetLineAlignment(StringAlignmentCenter);
    RectF m1, m2;
    g.MeasureString(loginPart1.c_str(), -1, &fSmall,  PointF(0,0), &sfNear, &m1);
    g.MeasureString(loginPart2.c_str(), -1, &fSmallB, PointF(0,0), &sfNear, &m2);
    float linkTotalW = m1.Width + m2.Width;
    float linkStartX = cardX + (cardW - linkTotalW) / 2.0f;
    float linkY      = btnY + btnH + 12.0f;
    float linkH      = 18.0f;
    g.DrawString(loginPart1.c_str(), -1, &fSmall,  RectF(linkStartX,             linkY, m1.Width, linkH), &sfNear, &grayLink);
    g.DrawString(loginPart2.c_str(), -1, &fSmallB, RectF(linkStartX + m1.Width,  linkY, m2.Width, linkH), &sfNear, &tealLink);
}

// ============================================================
//  DRAW
// ============================================================

// 🔥 [BUG 3 FIX]: ডায়নামিক প্যাকেজের নাম দেখানো
static wstring GetPlanLabel(const wstring& pkg) {
    if (pkg == L"PREMIUM")           return L"★ Premium";
    if (pkg == L"1 Month Pro")       return L"★ 1 Month Pro";
    if (pkg == L"6 Month Saver")     return L"★ 6 Month Saver";
    if (pkg == L"1 Year Ultimate")   return L"★ 1 Year Ultimate";
    if (pkg == L"STUDENT")           return L"★ Student";
    if (pkg == L"PARENTAL")          return L"Parental";
    if (pkg == L"TRIAL" || pkg == L"14_DAY_TRIAL") return L"14-Day Trial";
    return L"Free Basic";
}

void DrawAccountsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    if (g_showSignupFromUpgrade) {
        g_showSignupFromUpgrade = false;
        s_showSignup            = true;
        s_pendingCheckoutAfterSignup = true;
        s_su_focus              = 1;
        s_su_statusMsg          = L"";
        s_su_success            = false;
    }
    if (s_showSignup) { DrawSignUpForm(g, cx, cy, cw, ch); return; }

    SolidBrush bgBrush(Color(255, 240, 248, 255));
    g.FillRectangle(&bgBrush, cx, cy, cw, ch);

    if (!g_loggedInEmail.empty()) {
        float cardW = 420.0f, cardH = 520.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + (ch - cardH) / 2.0f;

        float rc = 12.0f, dc = rc * 2.0f;
        GraphicsPath card;
        card.AddArc(cardX, cardY, dc, dc, 180.0f, 90.0f);
        card.AddArc(cardX + cardW - dc, cardY, dc, dc, 270.0f, 90.0f);
        card.AddArc(cardX + cardW - dc, cardY + cardH - dc, dc, dc, 0.0f, 90.0f);
        card.AddArc(cardX, cardY + cardH - dc, dc, dc, 90.0f, 90.0f);
        card.CloseFigure();
        SolidBrush cardBg(Color(255, 255, 255, 255));
        g.FillPath(&cardBg, &card);
        Pen cardShadow(Color(30, 0, 100, 180), 1.5f);
        g.DrawPath(&cardShadow, &card);

        GraphicsPath hdrPath;
        hdrPath.AddArc(cardX, cardY, dc, dc, 180.0f, 90.0f);
        hdrPath.AddArc(cardX + cardW - dc, cardY, dc, dc, 270.0f, 90.0f);
        hdrPath.AddLine(cardX + cardW, cardY + 90.0f, cardX, cardY + 90.0f);
        hdrPath.CloseFigure();
        LinearGradientBrush hdrBg(PointF(cardX, cardY), PointF(cardX + cardW, cardY + 90.0f),
            Color(255, 0, 120, 160), Color(255, 0, 180, 200));
        g.FillPath(&hdrBg, &hdrPath);

        FontFamily ff(L"Segoe UI");
        FontFamily ffIcons(L"Segoe MDL2 Assets");
        SolidBrush white(Color(255, 255, 255, 255));
        SolidBrush teal(Color(255, 0, 150, 180));
        SolidBrush dark(Color(255, 40, 40, 50));
        SolidBrush gray(Color(255, 120, 130, 140));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

        float closeSize = 26.0f;
        float closeX = cardX + cardW - closeSize - 8.0f;
        float closeY = cardY + 8.0f;
        SolidBrush closeBg(s_hoverClose ? Color(200, 255, 255, 255) : Color(80, 255, 255, 255));
        GraphicsPath closePath;
        closePath.AddEllipse(closeX, closeY, closeSize, closeSize);
        g.FillPath(&closeBg, &closePath);
        Font fClose(&ffIcons, 11, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE711", -1, &fClose, RectF(closeX, closeY, closeSize, closeSize), &fmtC, &white);

        float avR  = 40.0f;
        float avCX = cardX + cardW / 2.0f;
        float avCY = cardY + 90.0f;
        SolidBrush avBg(Color(255, 255, 255, 255));
        g.FillEllipse(&avBg, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Pen avBorder(Color(255, 0, 150, 180), 3.0f);
        g.DrawEllipse(&avBorder, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Font fAvIcon(&ffIcons, 30, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE77B", -1, &fAvIcon, RectF(avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f), &fmtC, &teal);

        Font fHdrTitle(&ff, 16, FontStyleBold, UnitPixel);
        g.DrawString(L"My Account", -1, &fHdrTitle, RectF(cardX, cardY, cardW, 55.0f), &fmtC, &white);

        float fieldX = cardX + 35.0f;
        float fieldW = cardW - 70.0f;
        float infoY  = avCY + avR + 20.0f;
        Font fLabel(&ff, 11, FontStyleRegular, UnitPixel);
        Font fValue(&ff, 13, FontStyleBold,    UnitPixel);

        if (!g_loggedInName.empty()) {
            g.DrawString(L"Name", -1, &fLabel, RectF(fieldX, infoY, fieldW, 18.0f), &fmtL, &gray);
            g.DrawString(g_loggedInName.c_str(), -1, &fValue, RectF(fieldX, infoY + 18.0f, fieldW, 22.0f), &fmtL, &dark);
            infoY += 50.0f;
        }

        g.DrawString(L"Email", -1, &fLabel, RectF(fieldX, infoY, fieldW, 18.0f), &fmtL, &gray);
        g.DrawString(g_loggedInEmail.c_str(), -1, &fValue, RectF(fieldX, infoY + 18.0f, fieldW, 22.0f), &fmtL, &dark);
        infoY += 50.0f;

        g.DrawString(L"User ID", -1, &fLabel, RectF(fieldX, infoY, fieldW, 18.0f), &fmtL, &gray);
        wstring uid28;
        if (!g_loggedInUserUid.empty()) {
            string uid = g_loggedInUserUid;
            string shortUid = uid.size() > 16 ? uid.substr(0,8) + "..." + uid.substr(uid.size()-6) : uid;
            wchar_t uidW[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, shortUid.c_str(), -1, uidW, 63);
            uid28 = uidW;
        } else {
            uid28 = L"N/A";
        }
        Font fUid(&ff, 12, FontStyleRegular, UnitPixel);
        SolidBrush uidColor(Color(255, 80, 80, 100));
        g.DrawString(uid28.c_str(), -1, &fUid, RectF(fieldX, infoY + 18.0f, fieldW, 22.0f), &fmtL, &uidColor);
        infoY += 50.0f;

        g.DrawString(L"Current Plan", -1, &fLabel, RectF(fieldX, infoY, fieldW, 18.0f), &fmtL, &gray);

        wstring pkgW;
        { wchar_t tmp[128]={}; MultiByteToWideChar(CP_UTF8,0,g_currentPackage.c_str(),-1,tmp,127); pkgW=tmp; }
        bool isPrem  = (g_currentPackage == "PREMIUM" || g_currentPackage == "1 Month Pro" || g_currentPackage == "6 Month Saver" || g_currentPackage == "1 Year Ultimate" || g_currentPackage == "STUDENT" || g_currentPackage == "PARENTAL");
        bool isTrial = (g_currentPackage=="TRIAL" || g_currentPackage=="14_DAY_TRIAL");
        Color badgeColor = isPrem ? Color(255,255,140,0) : isTrial ? Color(255,80,160,80) : Color(255,0,150,180);

        float badgeW=130.0f, badgeH=26.0f;
        float br=6.0f, bd=br*2.0f;
        float badgeDrawY = infoY + 20.0f;
        GraphicsPath badge;
        badge.AddArc(fieldX, badgeDrawY, bd, bd, 180.0f, 90.0f);
        badge.AddArc(fieldX+badgeW-bd, badgeDrawY, bd, bd, 270.0f, 90.0f);
        badge.AddArc(fieldX+badgeW-bd, badgeDrawY+badgeH-bd, bd, bd, 0.0f, 90.0f);
        badge.AddArc(fieldX, badgeDrawY+badgeH-bd, bd, bd, 90.0f, 90.0f);
        badge.CloseFigure();
        SolidBrush badgeBg(badgeColor);
        g.FillPath(&badgeBg, &badge);
        Font fBadge(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(GetPlanLabel(pkgW).c_str(), -1, &fBadge, RectF(fieldX, badgeDrawY, badgeW, badgeH), &fmtC, &white);

        infoY += 58.0f;

        float br2=6.0f, bd2=br2*2.0f;

        if (!isPrem) {
            float upW=160.0f, upH=36.0f;
            float upX=cardX+(cardW-upW)/2.0f - 85.0f;
            float upY=infoY;
            GraphicsPath upPath;
            upPath.AddArc(upX, upY, bd2, bd2, 180.0f, 90.0f);
            upPath.AddArc(upX+upW-bd2, upY, bd2, bd2, 270.0f, 90.0f);
            upPath.AddArc(upX+upW-bd2, upY+upH-bd2, bd2, bd2, 0.0f, 90.0f);
            upPath.AddArc(upX, upY+upH-bd2, bd2, bd2, 90.0f, 90.0f);
            upPath.CloseFigure();
            LinearGradientBrush upBg(PointF(upX,upY), PointF(upX+upW,upY+upH),
                Color(255,243,156,18), Color(255,230,100,0));
            SolidBrush upHov(Color(255,200,80,0));
            if (s_hoverUpgrade) g.FillPath(&upHov, &upPath); else g.FillPath(&upBg, &upPath);
            Font fUp(&ff, 12, FontStyleBold, UnitPixel);
            g.DrawString(L"\xE8EC  Upgrade Now", -1, &fUp, RectF(upX, upY, upW, upH), &fmtC, &white);

            float logW=120.0f, logH=36.0f;
            float logX=upX+upW+10.0f, logY=upY;
            GraphicsPath lPath;
            lPath.AddArc(logX, logY, bd2, bd2, 180.0f, 90.0f);
            lPath.AddArc(logX+logW-bd2, logY, bd2, bd2, 270.0f, 90.0f);
            lPath.AddArc(logX+logW-bd2, logY+logH-bd2, bd2, bd2, 0.0f, 90.0f);
            lPath.AddArc(logX, logY+logH-bd2, bd2, bd2, 90.0f, 90.0f);
            lPath.CloseFigure();
            SolidBrush lBg(s_hoverLogout ? Color(255,180,20,20) : Color(255,220,50,50));
            g.FillPath(&lBg, &lPath);
            Font fLogout(&ff, 12, FontStyleBold, UnitPixel);
            g.DrawString(L"\xE7E8 Log Out", -1, &fLogout, RectF(logX, logY, logW, logH), &fmtC, &white);
        } else {
            float logW=150.0f, logH=38.0f;
            float logX=cardX+(cardW-logW)/2.0f, logY=infoY;
            GraphicsPath lPath;
            lPath.AddArc(logX, logY, bd2, bd2, 180.0f, 90.0f);
            lPath.AddArc(logX+logW-bd2, logY, bd2, bd2, 270.0f, 90.0f);
            lPath.AddArc(logX+logW-bd2, logY+logH-bd2, bd2, bd2, 0.0f, 90.0f);
            lPath.AddArc(logX, logY+logH-bd2, bd2, bd2, 90.0f, 90.0f);
            lPath.CloseFigure();
            SolidBrush lBg(s_hoverLogout ? Color(255,180,20,20) : Color(255,220,50,50));
            g.FillPath(&lBg, &lPath);
            Font fLogout(&ff, 13, FontStyleBold, UnitPixel);
            g.DrawString(L"\xE7E8 Log Out", -1, &fLogout, RectF(logX, logY, logW, logH), &fmtC, &white);
        }

        return;
    }

    // ─── LOGIN VIEW ───
    CardLayout L = GetLayout(cx, cy, cw, ch);

    for (int i = 3; i >= 0; --i) {
        SolidBrush shadowBrush(Color(10 + i * 5, 0, 80, 140));
        GraphicsPath shadowPath;
        float sr = 12.0f, sd = sr * 2.0f;
        float sx = L.cardX - i, sy = L.cardY + i, sw2 = L.cardW + i * 2.0f, sh2 = L.cardH;
        shadowPath.AddArc(sx, sy, sd, sd, 180.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy, sd, sd, 270.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy + sh2 - sd, sd, sd, 0.0f, 90.0f);
        shadowPath.AddArc(sx, sy + sh2 - sd, sd, sd, 90.0f, 90.0f);
        shadowPath.CloseFigure();
        g.FillPath(&shadowBrush, &shadowPath);
    }

    GraphicsPath cardPath;
    float rc = 12.0f, dc = rc * 2.0f;
    cardPath.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
    cardPath.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
    cardPath.AddArc(L.cardX + L.cardW - dc, L.cardY + L.cardH - dc, dc, dc, 0.0f, 90.0f);
    cardPath.AddArc(L.cardX, L.cardY + L.cardH - dc, dc, dc, 90.0f, 90.0f);
    cardPath.CloseFigure();
    SolidBrush cardBg(Color(255, 255, 255, 255));
    g.FillPath(&cardBg, &cardPath);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    SolidBrush teal(Color(255, 0, 150, 180));
    SolidBrush dark(Color(255, 40, 40, 50));
    SolidBrush gray(Color(255, 130, 140, 150));
    SolidBrush white(Color(255, 255, 255, 255));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    HICON hIconLg = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, (int)L.logoSz, (int)L.logoSz, LR_SHARED);
    if (hIconLg) {
        Bitmap bmp(hIconLg);
        g.DrawImage(&bmp, L.logoX, L.logoY, L.logoSz, L.logoSz);
    }

    Font fWelcome(&ff, 15, FontStyleBold, UnitPixel);
    g.DrawString(L"Sign in to RasFocus+", -1, &fWelcome, RectF(L.cardX, L.logoY + L.logoSz + 10.0f, L.cardW, 26.0f), &fmtC, &dark);

    // ── Email Field ──
    bool emailFocused = (s_focusField == 1);
    SolidBrush fieldBg(Color(255, 248, 250, 252));
    g.FillRectangle(&fieldBg, L.fieldX, L.emailY, L.fieldW, L.fldH);
    Pen emailBorder(emailFocused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), emailFocused ? 1.5f : 1.0f);
    g.DrawRectangle(&emailBorder, L.fieldX, L.emailY, L.fieldW, L.fldH);

    Font fInput(&ff, 12, FontStyleRegular, UnitPixel);
    wstring emailStr(s_email);
    SolidBrush inputColor(emailStr.empty() ? Color(255, 160, 170, 180) : Color(255, 50, 50, 50));
    float textX = L.fieldX + 32.0f;

    bool showCursor = ((GetTickCount() - s_lastBlinkTime) % 1000) < 500;

    if (!emailStr.empty()) {
        g.DrawString(emailStr.c_str(), -1, &fInput, RectF(textX, L.emailY, L.fieldW - 40.0f, L.fldH), &fmtL, &inputColor);
        if (emailFocused && showCursor) {
            RectF bbox;
            g.MeasureString(emailStr.c_str(), -1, &fInput, PointF(textX, L.emailY), &fmtL, &bbox);
            float curX = textX + bbox.Width;
            if (curX > L.fieldX + L.fieldW - 12.0f) curX = L.fieldX + L.fieldW - 12.0f;
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, curX, L.emailY + 6.0f, curX, L.emailY + L.fldH - 6.0f);
        }
    } else if (emailFocused) {
        if (showCursor) {
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, textX, L.emailY + 8.0f, textX, L.emailY + L.fldH - 8.0f);
        }
        g.DrawString(L"Email address", -1, &fInput, RectF(textX, L.emailY, L.fieldW - 40.0f, L.fldH), &fmtL, &inputColor);
    } else {
        g.DrawString(L"Email address", -1, &fInput, RectF(textX, L.emailY, L.fieldW - 40.0f, L.fldH), &fmtL, &inputColor);
    }

    Font fFieldIcon(&ffIcons, 13, FontStyleRegular, UnitPixel);
    g.DrawString(L"\xE715", -1, &fFieldIcon, RectF(L.fieldX + 8.0f, L.emailY, 24.0f, L.fldH), &fmtC, emailStr.empty() ? &gray : &teal);

    // ── Password Field ──
    bool passFocused = (s_focusField == 2);
    g.FillRectangle(&fieldBg, L.fieldX, L.passY, L.fieldW, L.fldH);
    Pen passBorder(passFocused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), passFocused ? 1.5f : 1.0f);
    g.DrawRectangle(&passBorder, L.fieldX, L.passY, L.fieldW, L.fldH);

    wstring passStr(s_password);
    float passTextX = L.fieldX + 32.0f;

    if (!passStr.empty()) {
        wstring passDisplay = s_showPassword ? passStr : wstring(passStr.size(), L'\u2022');
        SolidBrush passColorVal(Color(255, 50, 50, 50));
        g.DrawString(passDisplay.c_str(), -1, &fInput, RectF(passTextX, L.passY, L.fieldW - 65.0f, L.fldH), &fmtL, &passColorVal);
        if (passFocused && showCursor) {
            RectF bbox;
            g.MeasureString(passDisplay.c_str(), -1, &fInput, PointF(passTextX, L.passY), &fmtL, &bbox);
            float curX = passTextX + bbox.Width;
            if (curX > L.fieldX + L.fieldW - 40.0f) curX = L.fieldX + L.fieldW - 40.0f;
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, curX, L.passY + 6.0f, curX, L.passY + L.fldH - 6.0f);
        }
    } else if (passFocused) {
        if (showCursor) {
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, passTextX, L.passY + 8.0f, passTextX, L.passY + L.fldH - 8.0f);
        }
        SolidBrush passColorVal(Color(255, 160, 170, 180));
        g.DrawString(L"Password", -1, &fInput, RectF(passTextX, L.passY, L.fieldW - 65.0f, L.fldH), &fmtL, &passColorVal);
    } else {
        SolidBrush passColorVal(Color(255, 160, 170, 180));
        g.DrawString(L"Password", -1, &fInput, RectF(passTextX, L.passY, L.fieldW - 65.0f, L.fldH), &fmtL, &passColorVal);
    }
    g.DrawString(L"\xE72E", -1, &fFieldIcon, RectF(L.fieldX + 8.0f, L.passY, 24.0f, L.fldH), &fmtC, passStr.empty() ? &gray : &teal);

    Font fEye(&ffIcons, 13, FontStyleRegular, UnitPixel);
    SolidBrush eyeColor(s_hoverEye ? Color(255, 0, 150, 160) : Color(255, 160, 170, 180));
    g.DrawString(s_showPassword ? L"\xED1A" : L"\xE7B3", -1, &fEye, RectF(L.eyeX, L.eyeY, L.eyeW, L.fldH), &fmtC, &eyeColor);

    // ── Save Login Checkbox ──
    float chkSize = 14.0f;
    float chkY = L.checkY;
    Pen chkBorder(Color(255, 0, 150, 160), 1.5f);
    if (s_saveLogin) {
        g.FillRectangle(&teal, L.fieldX, chkY, chkSize, chkSize);
        Font fCheck(&ffIcons, 10, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE73E", -1, &fCheck, RectF(L.fieldX, chkY, chkSize, chkSize), &fmtC, &white);
    } else {
        g.DrawRectangle(&chkBorder, L.fieldX, chkY, chkSize, chkSize);
    }
    Font fChkLabel(&ff, 11, FontStyleRegular, UnitPixel);
    g.DrawString(L"Remember me", -1, &fChkLabel, RectF(L.fieldX + 22.0f, chkY - 2.0f, 150.0f, 18.0f), &fmtL, &dark);

    // ── Status Message ──
    if (!s_statusMsg.empty()) {
        SolidBrush statusBrush(s_isError ? Color(255, 220, 60, 60) : Color(255, 0, 160, 90));
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(s_statusMsg.c_str(), -1, &fStatus, RectF(L.cardX, L.loginBtnY - 22.0f, L.cardW, 18.0f), &fmtC, &statusBrush);
    } else if (s_isLoading) {
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(L"Logging in...", -1, &fStatus, RectF(L.cardX, L.loginBtnY - 22.0f, L.cardW, 18.0f), &fmtC, &teal);
    }

    // ── Login Button ──
    GraphicsPath btnP1;
    float br = 6.0f, bd = br * 2.0f;
    btnP1.AddArc(L.loginBtnX, L.loginBtnY, bd, bd, 180.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX + L.loginBtnW - bd, L.loginBtnY, bd, bd, 270.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX + L.loginBtnW - bd, L.loginBtnY + L.loginBtnH - bd, bd, bd, 0.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX, L.loginBtnY + L.loginBtnH - bd, bd, bd, 90.0f, 90.0f);
    btnP1.CloseFigure();
    LinearGradientBrush loginBg(PointF(L.loginBtnX, L.loginBtnY), PointF(L.loginBtnX + L.loginBtnW, L.loginBtnY + L.loginBtnH),
        Color(255, 0, 120, 140), Color(255, 0, 180, 200));
    g.FillPath(&loginBg, &btnP1);
    Font fBtn(&ff, 13, FontStyleBold, UnitPixel);
    g.DrawString(L"Log In", -1, &fBtn, RectF(L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH), &fmtC, &white);

    // ── Cancel Button ──
    GraphicsPath btnP2;
    btnP2.AddArc(L.cancelBtnX, L.cancelBtnY, bd, bd, 180.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX + L.cancelBtnW - bd, L.cancelBtnY, bd, bd, 270.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX + L.cancelBtnW - bd, L.cancelBtnY + L.cancelBtnH - bd, bd, bd, 0.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX, L.cancelBtnY + L.cancelBtnH - bd, bd, bd, 90.0f, 90.0f);
    btnP2.CloseFigure();
    SolidBrush cancelBg(s_hoverCancel ? Color(255, 245, 250, 255) : Color(255, 255, 255, 255));
    g.FillPath(&cancelBg, &btnP2);
    Pen cancelPen(Color(255, 0, 150, 180), 1.5f);
    g.DrawPath(&cancelPen, &btnP2);
    g.DrawString(L"Cancel", -1, &fBtn, RectF(L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH), &fmtC, &teal);

    // ── Links ──
    Font fLinkU(&ff, 11, FontStyleUnderline, UnitPixel);
    SolidBrush linkTeal(Color(255, 0, 150, 180));
    SolidBrush linkHover(Color(255, 0, 100, 130));

    g.DrawString(L"Create an account", -1, &fLinkU, RectF(L.signupX, L.signupY, L.signupW, L.signupH), &fmtL, s_hoverSignup ? &linkHover : &linkTeal);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar); fmtR.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Reset Password", -1, &fLinkU, RectF(L.resetX, L.resetY, L.resetW, L.resetH), &fmtR, s_hoverReset ? &linkHover : &linkTeal);
    g.DrawString(L"Privacy Policy", -1, &fLinkU, RectF(L.privacyX, L.privacyY, L.privacyW, L.privacyH), &fmtC, s_hoverPrivacy ? &linkHover : &linkTeal);

    // ── Google Sign-In Button ──
    float gBtnW = 200.0f, gBtnH = 34.0f;
    float gBtnX = L.cardX + (L.cardW - gBtnW) / 2.0f;
    float gBtnY = L.linksY + 26.0f;

    GraphicsPath gBtnPath;
    float gbr = 6.0f, gbd = gbr * 2.0f;
    gBtnPath.AddArc(gBtnX, gBtnY, gbd, gbd, 180.0f, 90.0f);
    gBtnPath.AddArc(gBtnX + gBtnW - gbd, gBtnY, gbd, gbd, 270.0f, 90.0f);
    gBtnPath.AddArc(gBtnX + gBtnW - gbd, gBtnY + gBtnH - gbd, gbd, gbd, 0.0f, 90.0f);
    gBtnPath.AddArc(gBtnX, gBtnY + gBtnH - gbd, gbd, gbd, 90.0f, 90.0f);
    gBtnPath.CloseFigure();

    bool hoverGoogle = s_hoverGoogle;
    SolidBrush gBtnBg(hoverGoogle ? Color(255, 235, 245, 250) : Color(255, 248, 250, 252));
    g.FillPath(&gBtnBg, &gBtnPath);
    Pen gBtnBorder(Color(255, 200, 210, 220), 1.0f);
    g.DrawPath(&gBtnBorder, &gBtnPath);

    // Google "G" রঙিন আইকন
    Font fGIcon(&ff, 13, FontStyleBold, UnitPixel);
    SolidBrush gRed  (Color(255, 234,  67,  53));
    SolidBrush gBlue (Color(255,  66, 133, 244));
    SolidBrush gGreen(Color(255,  52, 168,  83));
    SolidBrush gYellow(Color(255, 251, 188,   4));
    // "G" এর চার রং — simplified: একটা রঙিন G অক্ষর
    g.DrawString(L"G", -1, &fGIcon, RectF(gBtnX + 10.0f, gBtnY, 20.0f, gBtnH), &fmtC, &gBlue);

    Font fGText(&ff, 11, FontStyleRegular, UnitPixel);
    SolidBrush gTextColor(Color(255, 60, 70, 80));
    g.DrawString(L"Sign in with Google", -1, &fGText,
        RectF(gBtnX + 28.0f, gBtnY, gBtnW - 30.0f, gBtnH), &fmtL, &gTextColor);
}

// ============================================================
//  MOUSE MOVE
// ============================================================
void ProcessAccountsMouseMove(float x, float y) {
    if (s_showSignup) {
        float scaledW = (float)windowWidth  / g_scaleFactor;
        float scaledH = (float)windowHeight / g_scaleFactor;
        float cx = (float)SIDEBAR_WIDTH;
        float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float cw = scaledW - cx, ch = scaledH - cy;
        float cardW = 400.0f, cardH = 530.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + (ch - cardH) / 2.0f;
        float fieldX = cardX + 35.0f, fieldW = cardW - 70.0f, fldH = 36.0f;
        float nameY = cardY + 65.0f, emailY = nameY + fldH + 14.0f;
        float passY = emailY + fldH + 14.0f, confirmY = passY + fldH + 14.0f;
        float statusY = confirmY + fldH + 8.0f;
        float btnY = statusY + 26.0f, btnGap = 10.0f;
        float btnW = (fieldW - btnGap) / 2.0f, btnH = 38.0f;
        float suBtnX = fieldX, backBtnX = fieldX + btnW + btnGap;
        float eyeW = 32.0f;

        s_hoverSuBtn  = HitRect(x, y, suBtnX,   btnY, btnW, btnH);
        s_hoverSuBack = HitRect(x, y, backBtnX,  btnY, btnW, btnH);
        s_hoverSuEye1 = HitRect(x, y, fieldX + fieldW - eyeW, passY,    eyeW, fldH);
        s_hoverSuEye2 = HitRect(x, y, fieldX + fieldW - eyeW, confirmY, eyeW, fldH);

        bool onText = HitRect(x, y, fieldX, nameY, fieldW, fldH) ||
                      HitRect(x, y, fieldX, emailY, fieldW, fldH) ||
                      HitRect(x, y, fieldX, passY,  fieldW - eyeW, fldH) ||
                      HitRect(x, y, fieldX, confirmY, fieldW - eyeW, fldH);
        if (onText)                                        SetCursor(LoadCursor(NULL, IDC_IBEAM));
        else if (s_hoverSuBtn || s_hoverSuBack || s_hoverSuEye1 || s_hoverSuEye2)
                                                           SetCursor(LoadCursor(NULL, IDC_HAND));
        else                                               SetCursor(LoadCursor(NULL, IDC_ARROW));

        if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
        return;
    }

    // 🔥 [BUG 2 FIX]: লগইন অবস্থায় সমস্ত বাটনের হোভার চেক লজিক আপডেট করা হলো
    if (!g_loggedInEmail.empty()) {
        float scaledW = (float)windowWidth  / g_scaleFactor;
        float scaledH = (float)windowHeight / g_scaleFactor;
        float cx = (float)SIDEBAR_WIDTH;
        float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float cw = scaledW - cx, ch = scaledH - cy;

        float cardW = 420.0f, cardH = 520.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + (ch - cardH) / 2.0f;

        float closeSize = 26.0f;
        float closeX = cardX + cardW - closeSize - 8.0f;
        float closeY = cardY + 8.0f;
        s_hoverClose = HitRect(x, y, closeX, closeY, closeSize, closeSize);

        float avR  = 40.0f;
        float avCY = cardY + 90.0f;
        float infoY  = avCY + avR + 20.0f;
        if (!g_loggedInName.empty()) { infoY += 50.0f; }
        infoY += 50.0f; // Email
        infoY += 50.0f; // UID
        infoY += 58.0f; // Badge

        bool isPrem = (g_currentPackage=="PREMIUM" || g_currentPackage=="1 Month Pro" || g_currentPackage=="6 Month Saver" || g_currentPackage=="1 Year Ultimate" || g_currentPackage=="STUDENT" || g_currentPackage=="PARENTAL");

        s_hoverUpgrade = false;
        s_hoverLogout = false;

        if (!isPrem) {
            float upW = 160.0f, upH = 36.0f;
            float upX = cardX + (cardW - upW) / 2.0f - 85.0f;
            float upY = infoY;
            s_hoverUpgrade = HitRect(x, y, upX, upY, upW, upH);

            float logW = 120.0f, logH = 36.0f;
            float logX = upX + upW + 10.0f, logY = upY;
            s_hoverLogout = HitRect(x, y, logX, logY, logW, logH);
        } else {
            float logW = 150.0f, logH = 38.0f;
            float logX = cardX + (cardW - logW) / 2.0f, logY = infoY;
            s_hoverLogout = HitRect(x, y, logX, logY, logW, logH);
        }

        if (s_hoverClose || s_hoverLogout || s_hoverUpgrade) SetCursor(LoadCursor(NULL, IDC_HAND));
        else SetCursor(LoadCursor(NULL, IDC_ARROW));

        if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
        return;
    }

    CardLayout L = GetCurrentLayout();
    s_hoverLogin     = HitRect(x, y, L.loginBtnX,  L.loginBtnY,  L.loginBtnW,  L.loginBtnH);
    s_hoverCancel    = HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH);
    s_hoverEye       = HitRect(x, y, L.eyeX,       L.eyeY,       L.eyeW,       L.fldH);
    s_hoverSaveCheck = HitRect(x, y, L.fieldX,     L.checkY - 5.0f, 120.0f,    24.0f);
    s_hoverSignup    = HitRect(x, y, L.signupX,    L.signupY,    L.signupW,    L.signupH);
    s_hoverReset     = HitRect(x, y, L.resetX,     L.resetY,     L.resetW,     L.resetH);
    s_hoverPrivacy   = HitRect(x, y, L.privacyX,   L.privacyY,   L.privacyW,   L.privacyH);
    s_hoverGoogle    = HitRect(x, y, L.googleBtnX, L.googleBtnY, L.googleBtnW, L.googleBtnH);

    bool hoverEmailText = HitRect(x, y, L.fieldX, L.emailY, L.fieldW - L.eyeW, L.fldH);
    bool hoverPassText  = HitRect(x, y, L.fieldX, L.passY, L.fieldW - L.eyeW, L.fldH);

    if (hoverEmailText || hoverPassText) {
        SetCursor(LoadCursor(NULL, IDC_IBEAM)); 
    } else if (s_hoverLogin || s_hoverCancel || s_hoverSignup || s_hoverReset || s_hoverPrivacy || s_hoverSaveCheck || s_hoverEye) {
        SetCursor(LoadCursor(NULL, IDC_HAND)); 
    } else {
        SetCursor(LoadCursor(NULL, IDC_ARROW)); 
    }
}

// ============================================================
//  MOUSE CLICK
// ============================================================
void ProcessAccountsMouseClick(float x, float y, HWND hWnd) {
    if (s_showSignup) {
        float scaledW = (float)windowWidth  / g_scaleFactor;
        float scaledH = (float)windowHeight / g_scaleFactor;
        float cx = (float)SIDEBAR_WIDTH;
        float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float cw = scaledW - cx, ch = scaledH - cy;
        float cardW = 400.0f, cardH = 530.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + (ch - cardH) / 2.0f;
        float fieldX = cardX + 35.0f, fieldW = cardW - 70.0f, fldH = 36.0f;
        float nameY = cardY + 65.0f, emailY = nameY + fldH + 14.0f;
        float passY = emailY + fldH + 14.0f, confirmY = passY + fldH + 14.0f;
        float statusY = confirmY + fldH + 8.0f;
        float btnY = statusY + 26.0f, btnGap = 10.0f;
        float btnW = (fieldW - btnGap) / 2.0f, btnH = 38.0f;
        float suBtnX = fieldX, backBtnX = fieldX + btnW + btnGap;
        float eyeW = 32.0f;

        if (s_su_success) {
            float lbW = 160.0f, lbH = 38.0f;
            float lbX = cardX + (cardW - lbW) / 2.0f;
            float lbY = cardY + cardH / 2.0f + 30.0f;
            if (HitRect(x, y, lbX, lbY, lbW, lbH)) {
                char emailA[512] = {}, passA[512] = {};
                WideCharToMultiByte(CP_UTF8, 0, s_su_email, -1, emailA, 511, NULL, NULL);
                WideCharToMultiByte(CP_UTF8, 0, s_su_pass,  -1, passA,  511, NULL, NULL);

                bool doCheckout = s_pendingCheckoutAfterSignup;
                s_pendingCheckoutAfterSignup = false;

                s_showSignup = false;
                s_su_success = false;
                ZeroMemory(s_su_name,    sizeof(s_su_name));
                ZeroMemory(s_su_email,   sizeof(s_su_email));
                ZeroMemory(s_su_pass,    sizeof(s_su_pass));
                ZeroMemory(s_su_confirm, sizeof(s_su_confirm));
                s_su_statusMsg = L"";
                s_su_focus     = 0;

                if (strlen(emailA) > 0 && strlen(passA) > 0) {
                    s_isLoading = true;
                    LoginThreadData* ld = new LoginThreadData();
                    ld->hWnd      = hWnd;
                    ld->email     = emailA;
                    ld->password  = passA;
                    ld->saveLogin = false;
                    
                    if (doCheckout) {
                        g_openCheckoutAfterLogin = true;
                    }
                    _beginthread(LoginThread, 0, ld);
                }

                InvalidateRect(hWnd, NULL, FALSE);
            }
            return;
        }

        if (HitRect(x, y, fieldX, nameY,    fieldW - eyeW, fldH)) { s_su_focus = 1; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX, emailY,   fieldW - eyeW, fldH)) { s_su_focus = 2; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX, passY,    fieldW - eyeW, fldH)) { s_su_focus = 3; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX, confirmY, fieldW - eyeW, fldH)) { s_su_focus = 4; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }

        if (HitRect(x, y, fieldX + fieldW - eyeW, passY,    eyeW, fldH)) { s_su_showPass    = !s_su_showPass;    InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX + fieldW - eyeW, confirmY, eyeW, fldH)) { s_su_showConfirm = !s_su_showConfirm; InvalidateRect(hWnd, NULL, FALSE); return; }

        if (HitRect(x, y, backBtnX, btnY, btnW, btnH)) {
            s_showSignup = false;
            s_pendingCheckoutAfterSignup = false;
            s_su_statusMsg = L"";
            s_su_focus = 0;
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }

        {
            float linkY = btnY + btnH + 12.0f;
            float linkH = 18.0f;
            float linkX = cardX + 30.0f;
            float linkW = cardW - 60.0f;
            if (HitRect(x, y, linkX, linkY, linkW, linkH)) {
                s_showSignup = false;
                s_pendingCheckoutAfterSignup = false;
                s_su_statusMsg = L"";
                s_su_focus = 0;
                InvalidateRect(hWnd, NULL, FALSE);
                return;
            }
        }

        if (HitRect(x, y, suBtnX, btnY, btnW, btnH)) {
            wstring nameW(s_su_name), emailW(s_su_email), passW(s_su_pass), confirmW(s_su_confirm);            
            if (nameW.empty() || emailW.empty() || passW.empty() || confirmW.empty()) {
                s_su_statusMsg = L"Please fill in all fields."; s_su_isError = true;
                InvalidateRect(hWnd, NULL, FALSE); return;
            }
            if (passW != confirmW) {
                s_su_statusMsg = L"Passwords do not match."; s_su_isError = true;
                InvalidateRect(hWnd, NULL, FALSE); return;
            }
            if (passW.size() < 6) {
                s_su_statusMsg = L"Password must be at least 6 characters."; s_su_isError = true;
                InvalidateRect(hWnd, NULL, FALSE); return;
            }
            s_su_isLoading = true; s_su_statusMsg = L""; s_su_isError = false;
            InvalidateRect(hWnd, NULL, FALSE);

            char emailA[512] = {}, passA[512] = {}, nameA[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, emailW.c_str(), -1, emailA, 511, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, passW.c_str(),  -1, passA,  511, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(),  -1, nameA,  511, NULL, NULL);

            SignUpThreadData* data = new SignUpThreadData();
            data->hWnd     = hWnd;
            data->email    = emailA;
            data->password = passA;
            data->name     = nameA;
            _beginthread(SignUpThread, 0, data);
            return;
        }
        return;
    }

    // 🔥 [BUG 2 FIX]: লগইন অবস্থায় সমস্ত বাটনের ক্লিক ইভেন্ট অ্যাক্টিভ করা হলো
    if (!g_loggedInEmail.empty()) {
        float scaledW = (float)windowWidth  / g_scaleFactor;
        float scaledH = (float)windowHeight / g_scaleFactor;
        float cx = (float)SIDEBAR_WIDTH;
        float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float cw = scaledW - cx, ch = scaledH - cy;

        float cardW = 420.0f, cardH = 520.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + (ch - cardH) / 2.0f;

        float closeSize = 26.0f;
        float closeX = cardX + cardW - closeSize - 8.0f;
        float closeY = cardY + 8.0f;
        
        // 1. Close Button Click (Back to main tab/home)
        if (HitRect(x, y, closeX, closeY, closeSize, closeSize)) {
            selectedTab = 1; // Tab 1 বা Home এ ফেরত যাওয়ার জন্য
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }

        float avR  = 40.0f;
        float avCY = cardY + 90.0f;
        float infoY  = avCY + avR + 20.0f;
        if (!g_loggedInName.empty()) { infoY += 50.0f; }
        infoY += 50.0f; // Email
        infoY += 50.0f; // UID
        infoY += 58.0f; // Badge

        bool isPrem = (g_currentPackage=="PREMIUM" || g_currentPackage=="1 Month Pro" || g_currentPackage=="6 Month Saver" || g_currentPackage=="1 Year Ultimate" || g_currentPackage=="STUDENT" || g_currentPackage=="PARENTAL");

        float upW = 160.0f, upH = 36.0f, upX = 0, upY = 0;
        float logW = 0, logH = 0, logX = 0, logY = 0;

        if (!isPrem) {
            upX = cardX + (cardW - upW) / 2.0f - 85.0f;
            upY = infoY;
            logW = 120.0f; logH = 36.0f;
            logX = upX + upW + 10.0f; logY = upY;
            
            // 2. Upgrade Button Click
            if (HitRect(x, y, upX, upY, upW, upH)) {
                // Sidebar upgrade button - same checkout URL as upgrade.cpp popup
                string urlStr = "https://raseledutools.github.io/checkout.html?uid=" + g_loggedInUserUid;
                wstring wUrl(urlStr.begin(), urlStr.end());
                ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return;
            }
        } else {
            logW = 150.0f; logH = 38.0f;
            logX = cardX + (cardW - logW) / 2.0f; logY = infoY;
        }

        // 3. Log Out Button Click
        if (HitRect(x, y, logX, logY, logW, logH)) {
            g_loggedInEmail   = L"";
            g_loggedInName    = L"";
            g_loggedInUserUid = "";
            g_loggedInIdToken = "";
            g_isPremiumUser   = false;
            g_currentPackage  = "FREE_BASIC";
            ZeroMemory(s_email,    sizeof(s_email));
            ZeroMemory(s_password, sizeof(s_password));
            s_statusMsg = L"Logged out successfully.";
            s_isError   = false;
            if (!s_saveLogin) ClearSavedCredentials();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return;
    }

    CardLayout L = GetCurrentLayout();

    if (HitRect(x, y, L.fieldX, L.emailY, L.fieldW - L.eyeW, L.fldH)) {
        s_focusField = 1; s_lastBlinkTime = GetTickCount();
        InvalidateRect(hWnd, NULL, FALSE); return;
    }
    if (HitRect(x, y, L.fieldX, L.passY, L.fieldW - L.eyeW, L.fldH)) {
        s_focusField = 2; s_lastBlinkTime = GetTickCount();
        InvalidateRect(hWnd, NULL, FALSE); return;
    }
    if (HitRect(x, y, L.eyeX, L.eyeY, L.eyeW, L.fldH)) {
        s_showPassword = !s_showPassword; InvalidateRect(hWnd, NULL, FALSE); return;
    }
    if (HitRect(x, y, L.fieldX, L.checkY - 5.0f, 120.0f, 24.0f)) {
        s_saveLogin = !s_saveLogin; InvalidateRect(hWnd, NULL, FALSE); return;
    }

    if (HitRect(x, y, L.signupX, L.signupY, L.signupW, L.signupH)) {
        s_showSignup = true;
        s_su_focus = 1;
        s_su_statusMsg = L"";
        s_su_success = false;
        s_lastBlinkTime = GetTickCount();
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }
    if (HitRect(x, y, L.resetX, L.resetY, L.resetW, L.resetH)) {
        // Email field এ কিছু থাকলে সেই email এ reset পাঠাও
        wstring emailW(s_email);
        if (emailW.empty()) {
            s_statusMsg = L"Enter your email first, then click Forgot Password.";
            s_isError   = true;
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }
        if (!s_resetLoading) {
            s_resetLoading = true;
            s_statusMsg    = L"Sending password reset email...";
            s_isError      = false;
            InvalidateRect(hWnd, NULL, FALSE);
            char emailA[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, emailW.c_str(), -1, emailA, 511, NULL, NULL);
            ResetThreadData* rd = new ResetThreadData();
            rd->hWnd  = hWnd;
            rd->email = emailA;
            _beginthread(PasswordResetThread, 0, rd);
        }
        return;
    }
    if (HitRect(x, y, L.privacyX, L.privacyY, L.privacyW, L.privacyH)) {
        ShellExecuteW(NULL, L"open", L"https://rasfocus.com/privacy", NULL, NULL, SW_SHOWNORMAL); return;
    }

    // ── Google Sign-In Click ──
    if (HitRect(x, y, L.googleBtnX, L.googleBtnY, L.googleBtnW, L.googleBtnH)) {
        OpenGoogleSignInPopup(hWnd);
        return;
    }

    if (HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH)) {
        s_statusMsg = L""; s_focusField = 0;
        InvalidateRect(hWnd, NULL, FALSE); return;
    }

    if (HitRect(x, y, L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH)) {
        wstring emailW(s_email), passW(s_password);
        if (emailW.empty() || passW.empty()) {
            s_statusMsg = L"Please enter email and password."; s_isError = true;
            InvalidateRect(hWnd, NULL, FALSE); return;
        }
        s_isLoading = true; s_statusMsg = L""; s_isError = false;
        InvalidateRect(hWnd, NULL, FALSE);

        char emailA[512] = {}, passA[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, emailW.c_str(), -1, emailA, 511, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, passW.c_str(),  -1, passA,  511, NULL, NULL);

        LoginThreadData* data = new LoginThreadData();
        data->hWnd      = hWnd;
        data->email     = emailA;
        data->password  = passA;
        data->saveLogin = s_saveLogin;
        _beginthread(LoginThread, 0, data);
        return;
    }
}

// ============================================================
//  KEYBOARD
// ============================================================
void ProcessAccountsChar(wchar_t c) {
    if (!g_loggedInEmail.empty()) return;

    if (s_showSignup && !s_su_success) {
        wchar_t* buf = nullptr;
        if      (s_su_focus == 1) buf = s_su_name;
        else if (s_su_focus == 2) buf = s_su_email;
        else if (s_su_focus == 3) buf = s_su_pass;
        else if (s_su_focus == 4) buf = s_su_confirm;
        if (!buf) return;

        if (c == L'\b') {
            int len = (int)wcslen(buf); if (len > 0) buf[len-1] = L'\0';
        } else if (c == L'\r' || c == L'\n') {
            s_su_focus = (s_su_focus % 4) + 1;
            s_lastBlinkTime = GetTickCount();
        } else if (c >= 32) {
            int len = (int)wcslen(buf); if (len < 510) { buf[len] = c; buf[len+1] = L'\0'; }
            s_lastBlinkTime = GetTickCount();
        }
        if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
        return;
    }

    if (c == L'\b') {
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len > 0) s_email[len-1] = L'\0'; }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len > 0) s_password[len-1] = L'\0'; }
    } else if (c == L'\r' || c == L'\n') {
        if (s_focusField == 1) s_focusField = 2; else s_focusField = 1;
        s_lastBlinkTime = GetTickCount();
    } else if (c >= 32) {
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len < 510) { s_email[len] = c; s_email[len+1] = L'\0'; } }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len < 510) { s_password[len] = c; s_password[len+1] = L'\0'; } }
        s_lastBlinkTime = GetTickCount();
    }
    if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
}

void ProcessAccountsKeyDown(WPARAM wp) {
    if (!g_loggedInEmail.empty()) return;

    if (s_showSignup && !s_su_success) {
        wchar_t* buf = nullptr;
        if      (s_su_focus == 1) buf = s_su_name;
        else if (s_su_focus == 2) buf = s_su_email;
        else if (s_su_focus == 3) buf = s_su_pass;
        else if (s_su_focus == 4) buf = s_su_confirm;

        if (wp == VK_TAB)    { s_su_focus = (s_su_focus % 4) + 1; s_lastBlinkTime = GetTickCount(); }
        else if (wp == VK_DELETE && buf) ZeroMemory(buf, 512 * sizeof(wchar_t));
        else if (wp == VK_ESCAPE) { s_su_focus = 0; s_su_statusMsg = L""; }
        if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
        return;
    }

    if (wp == VK_TAB) {
        s_focusField = (s_focusField == 1) ? 2 : 1;
        s_lastBlinkTime = GetTickCount();
    } else if (wp == VK_DELETE) {
        if (s_focusField == 1) ZeroMemory(s_email,    sizeof(s_email));
        if (s_focusField == 2) ZeroMemory(s_password, sizeof(s_password));
    } else if (wp == VK_ESCAPE) {
        s_focusField = 0; s_statusMsg = L"";
    }
    if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
}