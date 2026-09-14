// tab_file_manager.cpp
// File Manager Plus Tab — Local File Explorer + Google Drive Integration
// Replaces "Dashboard" as the first sidebar tab (index 0 -> logical 12)

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include "tab_file_manager.h"
#include "globals.h"
#include <string>
#include <vector>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <fstream>
#include <algorithm>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

// Preview WebView2 embedded panel
#include "browser/mini_browser.h"

using namespace Gdiplus;
using namespace std;

// Forward declaration of global window handle
extern HWND hParentWnd;

// ============================================================
// STATE
// ============================================================
static int fm_activeSubTab = 0;   // 0 = Local Files, 1 = Google Drive

// --- Local File Explorer State ---
static wstring fm_currentPath = L"C:\\";
static vector<wstring> fm_breadcrumb;
static vector<pair<wstring, bool>> fm_items;
static int fm_selectedItem = -1;
static int fm_scrollOffset = 0;
static int fm_hovItem      = -1;
static bool fm_hovUp       = false;
static bool fm_hovSearch   = false;
static int fm_hovBreadcrumb = -1;

// --- Sub-tab hover ---
static bool fm_hovTabLocal = false;
static bool fm_hovTabDrive = false;

// ============================================================
// PREVIEW PANEL STATE
// ============================================================
static bool    fm_previewVisible   = false;   // panel shown?
static wstring fm_previewPath      = L"";     // file being previewed
static wstring fm_previewExt       = L"";     // lowercase extension

// Preview type enum
enum class PreviewType { None, Image, Text, WebView };
static PreviewType fm_previewType = PreviewType::None;

// GDI+ image cache (image preview)
static Image* fm_previewImage = nullptr;

// Text preview lines cache
static vector<wstring> fm_previewLines;

// WebView bounds (stored so we can update on resize)
static RECT fm_webViewBounds = {};

// Preview panel layout constant — fraction of file-list area taken by preview
static const float PREVIEW_WIDTH_RATIO = 0.42f; // 42% of list area

// Extension classification helpers
static bool IsImageExt(const wstring& ext) {
    return ext==L"jpg"||ext==L"jpeg"||ext==L"png"||ext==L"gif"||
           ext==L"bmp"||ext==L"webp"||ext==L"ico"||ext==L"tiff"||ext==L"tif";
}
static bool IsTextExt(const wstring& ext) {
    return ext==L"txt"||ext==L"log"||ext==L"ini"||ext==L"cfg"||
           ext==L"md"||ext==L"csv"||ext==L"json"||ext==L"xml"||
           ext==L"html"||ext==L"htm"||ext==L"css"||ext==L"js"||
           ext==L"ts"||ext==L"py"||ext==L"cpp"||ext==L"h"||
           ext==L"c"||ext==L"cs"||ext==L"java"||ext==L"kt"||
           ext==L"bat"||ext==L"sh"||ext==L"yaml"||ext==L"yml"||
           ext==L"toml"||ext==L"rs"||ext==L"go"||ext==L"php"||
           ext==L"rb"||ext==L"sql"||ext==L"env"||ext==L"gitignore";
}
static bool IsWebViewExt(const wstring& ext) {
    // PDF, video, audio → render via WebView2
    return ext==L"pdf"||
           ext==L"mp4"||ext==L"mkv"||ext==L"avi"||ext==L"mov"||ext==L"webm"||
           ext==L"mp3"||ext==L"wav"||ext==L"flac"||ext==L"aac"||ext==L"ogg"||ext==L"m4a";
}

// Load/clear preview for a given file path
static void LoadPreview(const wstring& fullPath, const wstring& ext) {
    // Clear old state
    if (fm_previewImage) { delete fm_previewImage; fm_previewImage = nullptr; }
    fm_previewLines.clear();
    fm_previewType    = PreviewType::None;
    fm_previewPath    = fullPath;
    fm_previewExt     = ext;

    if (fullPath.empty()) {
        DestroyEmbeddedPreview();
        fm_previewVisible = false;
        return;
    }

    fm_previewVisible = true;

    if (IsImageExt(ext)) {
        fm_previewType  = PreviewType::Image;
        fm_previewImage = Image::FromFile(fullPath.c_str());
        DestroyEmbeddedPreview();

    } else if (IsTextExt(ext)) {
        fm_previewType = PreviewType::Text;
        DestroyEmbeddedPreview();

        // Read first ~300 lines (UTF-8 or ANSI)
        FILE* f = _wfopen(fullPath.c_str(), L"rb");
        if (f) {
            char buf[65536]; size_t n = fread(buf, 1, sizeof(buf)-1, f); fclose(f);
            buf[n] = 0;
            // Convert to wide (try UTF-8 first)
            int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, (int)n, nullptr, 0);
            wstring ws;
            if (wlen > 0) {
                ws.resize(wlen);
                MultiByteToWideChar(CP_UTF8, 0, buf, (int)n, &ws[0], wlen);
            } else {
                // Fallback ANSI
                wlen = MultiByteToWideChar(CP_ACP, 0, buf, (int)n, nullptr, 0);
                ws.resize(wlen);
                MultiByteToWideChar(CP_ACP, 0, buf, (int)n, &ws[0], wlen);
            }
            // Split lines
            wstring line;
            for (wchar_t ch : ws) {
                if (ch == L'\r') continue;
                if (ch == L'\n') {
                    fm_previewLines.push_back(line);
                    line.clear();
                    if ((int)fm_previewLines.size() >= 300) break;
                } else {
                    line += ch;
                }
            }
            if (!line.empty() && (int)fm_previewLines.size() < 300)
                fm_previewLines.push_back(line);
        }

    } else if (IsWebViewExt(ext)) {
        fm_previewType = PreviewType::WebView;
        // WebView bounds set in DrawFileManagerTab (needs panel geometry)
        // Trigger deferred creation — bounds filled in draw pass
        // We use a "file://" URL for local files; for video/audio a data URL html wrapper
        // The actual CreateEmbeddedPreviewWebView() is called from DrawFileManagerTab
        // when panel geometry is known. Here we just mark type & path.
        // (DestroyEmbeddedPreview is NOT called here — DrawFileManagerTab will call Create)
    } else {
        // Unsupported: show icon + name only
        fm_previewType  = PreviewType::None;
        fm_previewVisible = true; // still show panel (name + "no preview")
        DestroyEmbeddedPreview();
    }
}

// --- Sidebar: Google Drive entry hover ---
static bool fm_hovSideGDrive = false;

// --- Toolbar button hovers ---
static bool fm_hovRefresh  = false;
static bool fm_hovNewFolder= false;
static bool fm_hovDelete   = false;
static bool fm_hovOpen     = false;

// ============================================================
// GOOGLE DRIVE — Real OAuth2 + REST API
// ============================================================
// OAuth2 config (Google Cloud Console -> Desktop app)
// Credentials assembled at runtime
static wstring GD_CLIENT_ID() {
    return L"868329616276-jtv50h50toa7e563cdcihmrdv66hgvfd" L".apps.googleusercontent.com";
}
static wstring GD_CLIENT_SECRET() {
    wstring a = L"GOCSPX-4oDwhONJBPcRj0"; wstring b = L"_abj0yfUO9idgc"; return a + b;
}
#define GD_REDIRECT_URI  L"http://localhost:5050"
#define GD_SCOPE         L"https://www.googleapis.com/auth/drive.readonly"

// Drive item (populated via API)
struct DriveItem {
    wstring id;
    wstring name;
    wstring mimeType;
    wstring modified;
    wstring size;
};

// State
static bool    fm_driveSignedIn   = false;
static bool    fm_hovDriveSignIn  = false;
static bool    fm_driveLoading    = false;   // API call in progress
static int     fm_driveHovItem    = -1;
static int     fm_driveScrollOff  = 0;
static int     fm_driveSelectedItem = -1;
static wstring fm_driveAccessToken;
static wstring fm_driveRefreshToken;
static wstring fm_driveUserEmail   = L"";
static wstring fm_driveCurrentFolderId = L"root";
static vector<wstring> fm_driveFolderStack;  // navigation stack
static vector<wstring> fm_driveFolderNameStack;
static vector<DriveItem> fm_driveItems;
static wstring fm_driveStatusMsg;  // error/status text

// OAuth local server state
static SOCKET  fm_oauthSocket    = INVALID_SOCKET;
static HWND    fm_oauthBrowserWnd = NULL;

// ------------------------------------------------------------
// Narrow/Wide helpers
// ------------------------------------------------------------
static string WstrToStr(const wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string s(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], sz, nullptr, nullptr);
    return s;
}
static wstring StrToWstr(const string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    wstring w(sz - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], sz);
    return w;
}
static string UrlEncode(const string& s) {
    string out; out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char buf[4]; sprintf_s(buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// ------------------------------------------------------------
// Simple JSON field extractor (no dep)
// ------------------------------------------------------------
static string JsonField(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == string::npos) return {};
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == string::npos) return {};
    if (json[pos] == '"') {
        size_t end = pos + 1;
        while (end < json.size() && !(json[end] == '"' && json[end-1] != '\\')) end++;
        return json.substr(pos + 1, end - pos - 1);
    }
    // number/bool
    size_t end = json.find_first_of(",}]\n", pos);
    return json.substr(pos, end == string::npos ? string::npos : end - pos);
}

// ------------------------------------------------------------
// WinInet HTTPS GET/POST helper
// ------------------------------------------------------------
static string HttpsRequest(const wstring& host, const wstring& path,
                           const string& method,
                           const string& body,
                           const vector<pair<string,string>>& headers)
{
    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return {};
    HINTERNET hConn = InternetConnectW(hInet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT,
                                       NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return {}; }

    DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hReq = HttpOpenRequestA(hConn, method.c_str(),
                                      WstrToStr(path).c_str(),
                                      NULL, NULL, NULL, flags, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return {}; }

    string hdrStr;
    for (auto& h : headers) hdrStr += h.first + ": " + h.second + "\r\n";

    BOOL ok = HttpSendRequestA(hReq,
                               hdrStr.empty() ? NULL : hdrStr.c_str(),
                               (DWORD)hdrStr.size(),
                               body.empty() ? NULL : (LPVOID)body.c_str(),
                               (DWORD)body.size());
    string result;
    if (ok) {
        char buf[4096]; DWORD read;
        while (InternetReadFile(hReq, buf, sizeof(buf)-1, &read) && read > 0) {
            buf[read] = 0; result += buf;
        }
    }
    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return result;
}

// ------------------------------------------------------------
// Drive API: list files in folder
// ------------------------------------------------------------
static void DriveListFolder(const wstring& folderId) {
    fm_driveItems.clear();
    fm_driveLoading = true;
    fm_driveStatusMsg = L"Loading...";
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);

    // Run in background thread
    wstring token = fm_driveAccessToken;
    wstring fid   = folderId;

    thread([token, fid]() {
        // q='<id>' in parents and trashed=false
        string qParam = UrlEncode("'" + WstrToStr(fid) + "' in parents and trashed=false");
        string fields = UrlEncode("files(id,name,mimeType,modifiedTime,size),nextPageToken");
        string pathStr = "/drive/v3/files?q=" + qParam +
                         "&fields=" + fields +
                         "&pageSize=100&orderBy=folder,name";

        string resp = HttpsRequest(L"www.googleapis.com", StrToWstr(pathStr),
            "GET", "",
            {{"Authorization", "Bearer " + WstrToStr(token)}});

        // Parse on UI thread via PostMessage
        // Store in a shared buffer
        static string s_resp;
        s_resp = resp;

        PostMessage(hParentWnd, WM_USER + 50, 0, (LPARAM)&s_resp);
    }).detach();
}

// Call this from WM_USER+50 handler in main.cpp (see ProcessDriveApiResponse)
void ProcessDriveApiResponse(const string& json) {
    fm_driveItems.clear();
    fm_driveLoading = false;

    if (json.empty() || json.find("error") != string::npos) {
        fm_driveStatusMsg = L"Failed to load. Check connection.";
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
        return;
    }
    fm_driveStatusMsg = L"";

    // Parse files array
    size_t arr = json.find("\"files\"");
    if (arr == string::npos) { if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); return; }
    size_t start = json.find('[', arr);
    size_t end   = json.rfind(']');
    if (start == string::npos || end == string::npos) { if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); return; }

    // Split objects
    string arrStr = json.substr(start + 1, end - start - 1);
    int depth = 0;
    size_t objStart = string::npos;
    for (size_t i = 0; i <= arrStr.size(); i++) {
        char c = i < arrStr.size() ? arrStr[i] : '}';
        if (c == '{') { if (depth++ == 0) objStart = i; }
        else if (c == '}') {
            if (--depth == 0 && objStart != string::npos) {
                string obj = arrStr.substr(objStart, i - objStart + 1);
                DriveItem item;
                item.id       = StrToWstr(JsonField(obj, "id"));
                item.name     = StrToWstr(JsonField(obj, "name"));
                string mime   = JsonField(obj, "mimeType");
                item.mimeType = StrToWstr(mime);
                // friendly type
                if      (mime == "application/vnd.google-apps.folder")       item.mimeType = L"Folder";
                else if (mime == "application/vnd.google-apps.document")      item.mimeType = L"Google Docs";
                else if (mime == "application/vnd.google-apps.spreadsheet")   item.mimeType = L"Google Sheets";
                else if (mime == "application/vnd.google-apps.presentation")  item.mimeType = L"Google Slides";
                else if (mime == "application/pdf")                            item.mimeType = L"PDF";
                else {
                    size_t sl = mime.rfind('/');
                    item.mimeType = StrToWstr(sl != string::npos ? mime.substr(sl+1) : mime);
                }
                string mod = JsonField(obj, "modifiedTime"); // 2026-09-05T12:34:00.000Z
                if (mod.size() >= 10) item.modified = StrToWstr(mod.substr(0,10));
                string sz = JsonField(obj, "size");
                if (!sz.empty()) {
                    long long bytes = atoll(sz.c_str());
                    wchar_t buf[32];
                    if      (bytes < 1024)             swprintf(buf,32,L"%lld B",   bytes);
                    else if (bytes < 1024*1024)        swprintf(buf,32,L"%lld KB",  bytes/1024);
                    else if (bytes < 1024LL*1024*1024) swprintf(buf,32,L"%lld MB",  bytes/(1024*1024));
                    else                               swprintf(buf,32,L"%.1f GB",  bytes/(1024.0*1024*1024));
                    item.size = buf;
                } else {
                    item.size = L"—";
                }
                fm_driveItems.push_back(item);
                objStart = string::npos;
            }
        }
    }
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

// ------------------------------------------------------------
// OAuth: exchange code for tokens
// ------------------------------------------------------------
static void DriveExchangeCode(const string& code) {
    string body = "code=" + UrlEncode(code) +
                  "&client_id=" + UrlEncode(WstrToStr(GD_CLIENT_ID())) +
                  "&client_secret=" + UrlEncode(WstrToStr(GD_CLIENT_SECRET())) +
                  "&redirect_uri=" + UrlEncode(WstrToStr(GD_REDIRECT_URI)) +
                  "&grant_type=authorization_code";

    string resp = HttpsRequest(L"oauth2.googleapis.com", L"/token",
        "POST", body,
        {{"Content-Type","application/x-www-form-urlencoded"}});

    fm_driveAccessToken  = StrToWstr(JsonField(resp, "access_token"));
    fm_driveRefreshToken = StrToWstr(JsonField(resp, "refresh_token"));

    if (!fm_driveAccessToken.empty()) {
        // Get user email
        string me = HttpsRequest(L"www.googleapis.com",
            L"/oauth2/v1/userinfo?alt=json", "GET", "",
            {{"Authorization", "Bearer " + WstrToStr(fm_driveAccessToken)}});
        fm_driveUserEmail = StrToWstr(JsonField(me, "email"));
        fm_driveSignedIn  = true;
        fm_driveFolderStack.clear();
        fm_driveFolderNameStack.clear();
        fm_driveCurrentFolderId = L"root";
        DriveListFolder(L"root");
    } else {
        fm_driveStatusMsg = L"Sign-in failed. Please try again.";
        fm_driveSignedIn  = false;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

// ------------------------------------------------------------
// OAuth: start local HTTP server & open browser
// ------------------------------------------------------------
static void DriveStartOAuth() {
    // 1. Listen on localhost:5050
    WSADATA wsd; WSAStartup(MAKEWORD(2,2), &wsd);
    fm_oauthSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(5050);
    int reuse = 1;
    setsockopt(fm_oauthSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    bind(fm_oauthSocket, (sockaddr*)&sa, sizeof(sa));
    listen(fm_oauthSocket, 1);

    // 2. Build OAuth URL
    string authUrl =
        "https://accounts.google.com/o/oauth2/v2/auth"
        "?client_id=" + UrlEncode(WstrToStr(GD_CLIENT_ID())) +
        "&redirect_uri=" + UrlEncode(WstrToStr(GD_REDIRECT_URI)) +
        "&response_type=code"
        "&scope=" + UrlEncode(WstrToStr(GD_SCOPE)) +
        "&access_type=offline"
        "&prompt=consent";

    // 3. Open in default browser (WebView2 popup would need more infra)
    ShellExecuteA(NULL, "open", authUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);

    // 4. Wait for redirect in background thread
    SOCKET srv = fm_oauthSocket;
    thread([srv]() {
        SOCKET client = accept(srv, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        char buf[4096] = {}; int n = recv(client, buf, sizeof(buf)-1, 0);
        // Send success page
        const char* resp_html =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<html><body style=\'font-family:sans-serif;text-align:center;padding-top:80px\'>"
            "<h2>&#9989; Signed in! You can close this tab.</h2>"
            "<p>Return to RasFocus.</p></body></html>";
        send(client, resp_html, (int)strlen(resp_html), 0);
        closesocket(client);
        closesocket(srv);

        // Extract code from GET line
        string req(buf, n);
        size_t codePos = req.find("code=");
        if (codePos == string::npos) return;
        size_t codeEnd = req.find_first_of("& \r\n", codePos + 5);
        string code = req.substr(codePos + 5, codeEnd == string::npos ? string::npos : codeEnd - codePos - 5);
        // Exchange on a thread (posts WM_USER+51 when done)
        DriveExchangeCode(code);
    }).detach();
}

// Refresh token
static void DriveRefreshAccessToken() {
    if (fm_driveRefreshToken.empty()) { fm_driveSignedIn = false; return; }
    string body = "refresh_token=" + UrlEncode(WstrToStr(fm_driveRefreshToken)) +
                  "&client_id="    + UrlEncode(WstrToStr(GD_CLIENT_ID())) +
                  "&client_secret="+ UrlEncode(WstrToStr(GD_CLIENT_SECRET())) +
                  "&grant_type=refresh_token";
    string resp = HttpsRequest(L"oauth2.googleapis.com", L"/token",
        "POST", body, {{"Content-Type","application/x-www-form-urlencoded"}});
    string tok = JsonField(resp, "access_token");
    if (!tok.empty()) fm_driveAccessToken = StrToWstr(tok);
}

// Open a Drive file/folder in browser
static void DriveOpenItem(const DriveItem& item) {
    if (item.mimeType == L"Folder") {
        // Navigate in-app
        fm_driveFolderStack.push_back(fm_driveCurrentFolderId);
        fm_driveFolderNameStack.push_back(item.name);
        fm_driveCurrentFolderId = item.id;
        fm_driveScrollOff = 0;
        fm_driveSelectedItem = -1;
        DriveListFolder(item.id);
    } else {
        // Open in browser (Google-hosted editor / download)
        wstring url = L"https://drive.google.com/file/d/" + item.id + L"/view";
        if (item.mimeType == L"Google Docs")
            url = L"https://docs.google.com/document/d/" + item.id + L"/edit";
        else if (item.mimeType == L"Google Sheets")
            url = L"https://docs.google.com/spreadsheets/d/" + item.id + L"/edit";
        else if (item.mimeType == L"Google Slides")
            url = L"https://docs.google.com/presentation/d/" + item.id + L"/edit";
        ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

static void DriveGoBack() {
    if (fm_driveFolderStack.empty()) return;
    fm_driveCurrentFolderId = fm_driveFolderStack.back();
    fm_driveFolderStack.pop_back();
    fm_driveFolderNameStack.pop_back();
    fm_driveScrollOff = 0;
    fm_driveSelectedItem = -1;
    DriveListFolder(fm_driveCurrentFolderId);
}

static void DriveSignOut() {
    fm_driveSignedIn = false;
    fm_driveAccessToken.clear();
    fm_driveRefreshToken.clear();
    fm_driveUserEmail.clear();
    fm_driveItems.clear();
    fm_driveFolderStack.clear();
    fm_driveFolderNameStack.clear();
    fm_driveCurrentFolderId = L"root";
    fm_driveScrollOff = 0;
    fm_driveSelectedItem = -1;
    fm_driveStatusMsg.clear();
}

// --- Geometry cache ---
static float g_fm_cx = 0, g_fm_cy = 0, g_fm_cw = 0, g_fm_ch = 0;

// ============================================================
// DRAW PREVIEW PANEL
// ============================================================
static void DrawPreviewPanel(Graphics& g, float px, float py, float pw, float ph)
{
    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fBold (&ff, 13, FontStyleBold,    UnitPixel);
    Font fTitle(&ff, 14, FontStyleBold,    UnitPixel);
    Font fIcon (&ffIcons, 32, FontStyleRegular, UnitPixel);
    Font fIconSm(&ffIcons, 16, FontStyleRegular, UnitPixel);
    Font fCode (&FontFamily(L"Consolas"), 11, FontStyleRegular, UnitPixel);

    SolidBrush bBg    (Color(255, 245, 248, 250));
    SolidBrush bWhite (Color(255, 255, 255, 255));
    SolidBrush bDark  (Color(255,  40,  40,  40));
    SolidBrush bGray  (Color(255, 120, 120, 120));
    SolidBrush bTeal  (Color(255,   0, 150, 160));
    SolidBrush bCode  (Color(255,  30,  30,  30));
    SolidBrush bLineNo(Color(255, 150, 160, 170));
    SolidBrush bCodeBg(Color(255, 250, 250, 252));
    Pen pBrd(Color(255, 218, 225, 232), 1.0f);
    Pen pLeft(Color(255, 200, 210, 220), 1.5f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);  fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);

    // Panel background
    g.FillRectangle(&bBg, px, py, pw, ph);
    // Left border separator
    g.DrawLine(&pLeft, px, py, px, py + ph);

    // ── Header bar ──────────────────────────────────
    float hdrH = 34.0f;
    g.FillRectangle(&bWhite, px, py, pw, hdrH);
    g.DrawLine(&pBrd, px, py + hdrH, px + pw, py + hdrH);

    // File name in header
    wstring fname = fm_previewPath;
    size_t sl = fname.rfind(L'\\');
    if (sl != wstring::npos) fname = fname.substr(sl + 1);
    g.DrawString(L"\xE8A5 ", -1, &fIconSm, RectF(px + 8.0f, py, 22.0f, hdrH), &fmtL, &bTeal);
    g.DrawString(fname.empty() ? L"Preview" : fname.c_str(), -1, &fBold,
        RectF(px + 28.0f, py, pw - 36.0f, hdrH), &fmtL, &bDark);

    float cY = py + hdrH;
    float cH = ph - hdrH;

    // ── Content area ────────────────────────────────
    if (!fm_previewVisible || fm_previewPath.empty()) {
        // Empty state
        g.DrawString(L"\xEC50", -1, &fIcon,
            RectF(px, cY + cH/2.0f - 48.0f, pw, 48.0f), &fmtC,
            &SolidBrush(Color(160, 0, 150, 160)));
        SolidBrush bHint(Color(255, 160, 170, 180));
        g.DrawString(L"Select a file to preview", -1, &fSmall,
            RectF(px, cY + cH/2.0f + 4.0f, pw, 24.0f), &fmtC, &bHint);
        return;
    }

    switch (fm_previewType) {

    // ────────────────────────────────────────────────
    case PreviewType::Image: {
        if (!fm_previewImage || fm_previewImage->GetLastStatus() != Ok) {
            SolidBrush bErr(Color(255, 180, 60, 60));
            g.DrawString(L"Cannot load image", -1, &fSmall,
                RectF(px, cY, pw, cH), &fmtC, &bErr);
            break;
        }
        // Fill background white for images
        g.FillRectangle(&bWhite, px, cY, pw, cH);

        float iw = (float)fm_previewImage->GetWidth();
        float ih = (float)fm_previewImage->GetHeight();
        float pad = 12.0f;
        float maxW = pw - pad * 2.0f;
        float maxH = cH - pad * 2.0f;

        // Fit while keeping aspect ratio
        float scale = min(maxW / iw, maxH / ih);
        float dw = iw * scale;
        float dh = ih * scale;
        float dx = px + (pw - dw) / 2.0f;
        float dy = cY + (cH - dh) / 2.0f;

        // Checkerboard for transparency
        for (int r = 0; r < (int)(dh / 8) + 1; r++) {
            for (int c = 0; c < (int)(dw / 8) + 1; c++) {
                bool odd = (r + c) % 2;
                SolidBrush bChk(odd ? Color(255, 200, 200, 200) : Color(255, 220, 220, 220));
                float tx = dx + c * 8.0f, ty = dy + r * 8.0f;
                float tw = min(8.0f, dx + dw - tx), th = min(8.0f, dy + dh - ty);
                if (tw > 0 && th > 0) g.FillRectangle(&bChk, tx, ty, tw, th);
            }
        }
        g.DrawImage(fm_previewImage, RectF(dx, dy, dw, dh));

        // Image info strip at bottom
        wchar_t info[80];
        swprintf(info, 80, L"%d × %d px", fm_previewImage->GetWidth(), fm_previewImage->GetHeight());
        SolidBrush bInfoBg(Color(200, 30, 30, 30));
        g.FillRectangle(&bInfoBg, px, cY + cH - 22.0f, pw, 22.0f);
        SolidBrush bInfoTxt(Color(255, 230, 230, 230));
        g.DrawString(info, -1, &fSmall, RectF(px + 4.0f, cY + cH - 22.0f, pw - 8.0f, 22.0f), &fmtL, &bInfoTxt);
        break;
    }

    // ────────────────────────────────────────────────
    case PreviewType::Text: {
        g.FillRectangle(&bCodeBg, px, cY, pw, cH);

        float lineH  = 16.0f;
        float xNum   = px + 4.0f;
        float xCode  = px + 42.0f;
        float codeW  = pw - 46.0f;

        // Line number gutter background
        SolidBrush bGutter(Color(255, 238, 240, 242));
        g.FillRectangle(&bGutter, px, cY, 38.0f, cH);
        g.DrawLine(&pBrd, px + 38.0f, cY, px + 38.0f, cY + cH);

        // Clip to code area
        g.SetClip(RectF(px, cY, pw, cH));

        int maxLines = (int)(cH / lineH);
        for (int i = 0; i < (int)fm_previewLines.size() && i < maxLines; i++) {
            float ly = cY + i * lineH;
            // Line number
            wchar_t numStr[8]; swprintf(numStr, 8, L"%d", i + 1);
            g.DrawString(numStr, -1, &fCode, RectF(xNum, ly, 32.0f, lineH), &fmtL, &bLineNo);
            // Code line (truncate long lines)
            wstring codeLine = fm_previewLines[i];
            if (codeLine.size() > 200) codeLine = codeLine.substr(0, 200) + L"…";
            g.DrawString(codeLine.c_str(), -1, &fCode, RectF(xCode, ly, codeW, lineH), &fmtL, &bCode);
        }
        g.ResetClip();

        // "Showing first N lines" footer when truncated
        if ((int)fm_previewLines.size() > maxLines) {
            SolidBrush bFtBg(Color(255, 230, 235, 240));
            g.FillRectangle(&bFtBg, px, cY + cH - 18.0f, pw, 18.0f);
            SolidBrush bFt(Color(255, 120, 130, 140));
            wchar_t ftStr[48];
            swprintf(ftStr, 48, L"Showing %d of %d lines", maxLines, (int)fm_previewLines.size());
            g.DrawString(ftStr, -1, &fSmall, RectF(px + 4.0f, cY + cH - 18.0f, pw - 8.0f, 18.0f), &fmtL, &bFt);
        }
        break;
    }

    // ────────────────────────────────────────────────
    case PreviewType::WebView: {
        // WebView2 renders on top — we just draw a placeholder background
        // The actual WebView2 is positioned via UpdateEmbeddedPreviewBounds()
        // called from DrawFileManagerTab after computing panel geometry.
        g.FillRectangle(&bWhite, px, cY, pw, cH);
        // Subtle loading indicator (WebView2 will cover this)
        SolidBrush bHint(Color(200, 0, 150, 160));
        g.DrawString(fm_previewExt == L"pdf" ? L"\xEA90" :
                     fm_previewExt == L"mp4" || fm_previewExt == L"mkv" || fm_previewExt == L"avi" ||
                     fm_previewExt == L"mov" || fm_previewExt == L"webm" ? L"\xE8B2" : L"\xEC4F",
                     -1, &fIcon, RectF(px, cY + 20.0f, pw, 48.0f), &fmtC, &bHint);
        SolidBrush bHintTxt(Color(255, 150, 160, 170));
        g.DrawString(L"Loading preview…", -1, &fSmall,
            RectF(px, cY + 72.0f, pw, 24.0f), &fmtC, &bHintTxt);
        break;
    }

    // ────────────────────────────────────────────────
    default: {
        // Unknown / unsupported — show file icon + name
        SolidBrush bGrayIco(Color(255, 160, 170, 180));
        g.DrawString(L"\xE8A5", -1, &fIcon,
            RectF(px, cY + cH/2.0f - 52.0f, pw, 48.0f), &fmtC, &bGrayIco);
        SolidBrush bHint(Color(255, 140, 150, 160));
        g.DrawString(L"No preview available", -1, &fSmall,
            RectF(px, cY + cH/2.0f + 2.0f, pw, 24.0f), &fmtC, &bHint);
        // Show extension
        wstring extUpper = fm_previewExt;
        for (auto& ch : extUpper) ch = towupper(ch);
        g.DrawString(extUpper.empty() ? L"FILE" : extUpper.c_str(), -1, &fBold,
            RectF(px, cY + cH/2.0f + 24.0f, pw, 24.0f), &fmtC, &bGrayIco);
        break;
    }
    }
}

// ============================================================
// HELPERS
// ============================================================
static void FillRect_(Graphics& g, SolidBrush* br, Pen* pen, float x, float y, float w, float h, float r = 0.0f) {
    if (r <= 0.0f) {
        if (br)  g.FillRectangle(br,  x, y, w, h);
        if (pen) g.DrawRectangle(pen, x, y, w, h);
    } else {
        GraphicsPath path;
        float d = r * 2.0f;
        path.AddArc(x,       y,       d, d, 180.0f, 90.0f);
        path.AddArc(x+w-d,   y,       d, d, 270.0f, 90.0f);
        path.AddArc(x+w-d,   y+h-d,   d, d,   0.0f, 90.0f);
        path.AddArc(x,       y+h-d,   d, d,  90.0f, 90.0f);
        path.CloseFigure();
        if (br)  g.FillPath(br,  &path);
        if (pen) g.DrawPath(pen, &path);
    }
}

static bool PtIn(float px, float py, float rx, float ry, float rw, float rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

static void RefreshLocalDir() {
    fm_items.clear();
    fm_scrollOffset = 0;
    fm_selectedItem = -1;
    fm_hovItem      = -1;

    wstring search = fm_currentPath;
    if (search.back() != L'\\') search += L'\\';
    search += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        // Skip hidden and system files/folders (like Windows Explorer default)
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fm_items.push_back({ name, isDir });
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // Dirs first, then files, alphabetical
    sort(fm_items.begin(), fm_items.end(), [](const pair<wstring,bool>& a, const pair<wstring,bool>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    // Build breadcrumb
    fm_breadcrumb.clear();
    wstring p = fm_currentPath;
    if (p.back() == L'\\') p.pop_back();
    size_t pos = 0;
    while ((pos = p.find(L'\\')) != wstring::npos) {
        wstring seg = p.substr(0, pos);
        if (!seg.empty()) fm_breadcrumb.push_back(seg + L"\\");
        p = p.substr(pos + 1);
    }
    if (!p.empty()) fm_breadcrumb.push_back(p);
}

void NavigateFileManagerTo(const wstring& path) {
    fm_currentPath = path;
    if (!fm_currentPath.empty() && fm_currentPath.back() != L'\\')
        fm_currentPath += L'\\';
    RefreshLocalDir();
}

// PopulateDriveItems() replaced by real DriveListFolder()

// ============================================================
// DRAW
// ============================================================
void DrawFileManagerTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_fm_cx = cx; g_fm_cy = cy; g_fm_cw = cw; g_fm_ch = ch;

    // Init local dir on first draw
    static bool inited = false;
    if (!inited) { RefreshLocalDir(); inited = true; }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // --- Fonts ---
    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fTitle(&ff, 15, FontStyleBold,    UnitPixel);
    Font fSub  (&ff, 13, FontStyleRegular, UnitPixel);
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fBold (&ff, 13, FontStyleBold,    UnitPixel);
    Font fIcon (&ffIcons, 16, FontStyleRegular, UnitPixel);
    Font fIconSm(&ffIcons, 14, FontStyleRegular, UnitPixel);

    // --- Brushes ---
    SolidBrush bWhite (Color(255, 255, 255, 255));
    SolidBrush bBg    (Color(255, 245, 248, 250));
    SolidBrush bDark  (Color(255,  40,  40,  40));
    SolidBrush bGray  (Color(255, 120, 120, 120));
    SolidBrush bTeal  (Color(255,   0, 150, 160));
    SolidBrush bTealLt(Color(255, 230, 250, 252));
    SolidBrush bBlue  (Color(255,  66, 133, 244));  // Google Drive blue
    SolidBrush bHov   (Color(255, 235, 248, 250));
    SolidBrush bSelBg (Color(255, 210, 240, 245));
    SolidBrush bSideBg(Color(255, 250, 252, 254));
    SolidBrush bRed   (Color(255, 220,  60,  60));
    SolidBrush bGreen (Color(255,  52, 168,  83));
    SolidBrush bYellow(Color(255, 245, 158,  11));

    Pen pBrd(Color(255, 218, 225, 232), 1.0f);
    Pen pTeal(Color(255, 0, 150, 160), 2.0f);
    Pen pWhite(Color(255, 255, 255, 255), 1.5f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter);  fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);     fmtR.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);
    fmtR.SetFormatFlags(StringFormatFlagsNoWrap);

    // ============================
    // BACKGROUND
    // ============================
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // ============================
    // TOP SUB-TAB BAR  (height 48)
    // ============================
    float tabBarH = 48.0f;
    g.FillRectangle(&bWhite, cx, cy, cw, tabBarH);
    Pen pTabBrd(Color(255, 218, 225, 232), 1.0f);
    g.DrawLine(&pTabBrd, cx, cy + tabBarH, cx + cw, cy + tabBarH);

    auto DrawSubTab = [&](float tx, float ty, float tw, float th, const wchar_t* icon, const wchar_t* label, bool active, bool hov) {
        if (active) {
            g.FillRectangle(&bTealLt, tx, ty, tw, th);
            g.DrawLine(&pTeal, tx, ty + th - 2.0f, tx + tw, ty + th - 2.0f);
            g.DrawString(icon,  -1, &fIcon,  RectF(tx + 10.0f, ty, 24.0f, th), &fmtL, &bTeal);
            g.DrawString(label, -1, &fBold,  RectF(tx + 36.0f, ty, tw - 40.0f, th), &fmtL, &bTeal);
        } else {
            if (hov) { SolidBrush bh(Color(255, 245, 245, 245)); g.FillRectangle(&bh, tx, ty, tw, th); }
            g.DrawString(icon,  -1, &fIcon,  RectF(tx + 10.0f, ty, 24.0f, th), &fmtL, &bGray);
            g.DrawString(label, -1, &fSub,   RectF(tx + 36.0f, ty, tw - 40.0f, th), &fmtL, &bGray);
        }
    };

    float stW = 200.0f;
    DrawSubTab(cx + 10.0f,        cy + 4.0f, stW, tabBarH - 8.0f, L"\xEC50", L"Local Files",    fm_activeSubTab == 0, fm_hovTabLocal);
    DrawSubTab(cx + 10.0f + stW,  cy + 4.0f, stW, tabBarH - 8.0f, L"\xE753", L"Google Drive",   fm_activeSubTab == 1, fm_hovTabDrive);

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    // ============================
    // LOCAL FILES TAB
    // ============================
    if (fm_activeSubTab == 0) {

        // ---- TOOLBAR (height 44) ----
        float tbH = 44.0f;
        g.FillRectangle(&bWhite, cx, bodyY, cw, tbH);
        Pen pTbBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pTbBrd, cx, bodyY + tbH, cx + cw, bodyY + tbH);

        // Up button
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;
        {
            SolidBrush bBtnBg(fm_hovUp ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, btnW, btnH, 4.0f);
            g.DrawString(L"\xE74A", -1, &fIconSm, RectF(bx, btnY, btnW, btnH), &fmtC, fm_hovUp ? &bTeal : &bGray);
        }
        bx += btnW + 6.0f;

        // Refresh
        {
            SolidBrush bBtnBg(fm_hovRefresh ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, btnW, btnH, 4.0f);
            g.DrawString(L"\xE72C", -1, &fIconSm, RectF(bx, btnY, btnW, btnH), &fmtC, fm_hovRefresh ? &bTeal : &bGray);
        }
        bx += btnW + 6.0f;

        // New Folder
        float nbW = 110.0f;
        {
            SolidBrush bBtnBg(fm_hovNewFolder ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, nbW, btnH, 4.0f);
            g.DrawString(L"\xE2AC  New Folder", -1, &fSmall, RectF(bx + 4.0f, btnY, nbW - 8.0f, btnH), &fmtL, fm_hovNewFolder ? &bTeal : &bGray);
        }
        bx += nbW + 6.0f;

        // Delete (only if item selected)
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            SolidBrush bDel(fm_hovDelete ? Color(255, 255, 220, 220) : Color(255, 245, 248, 250));
            Pen pDel(Color(255, 220, 60, 60), 1.0f);
            FillRect_(g, &bDel, &pDel, bx, btnY, dW, btnH, 4.0f);
            g.DrawString(L"\xE74D  Delete", -1, &fSmall, RectF(bx + 4.0f, btnY, dW - 8.0f, btnH), &fmtL, &bRed);
            bx += dW + 6.0f;
        }

        // Open (only if item selected)
        if (fm_selectedItem >= 0) {
            float oW = 80.0f;
            SolidBrush bOp(fm_hovOpen ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bOp, &pBrd, bx, btnY, oW, btnH, 4.0f);
            g.DrawString(L"\xE8A7  Open", -1, &fSmall, RectF(bx + 4.0f, btnY, oW - 8.0f, btnH), &fmtL, fm_hovOpen ? &bTeal : &bGray);
        }

        // ---- BREADCRUMB (height 32) ----
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        SolidBrush bBcBg(Color(255, 250, 252, 254));
        g.FillRectangle(&bBcBg, cx, bcY, cw, bcH);
        Pen pBcBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pBcBrd, cx, bcY + bcH, cx + cw, bcY + bcH);

        float bcX = cx + 10.0f;
        // Drive root icon
        g.DrawString(L"\xEC50", -1, &fIconSm, RectF(bcX, bcY, 20.0f, bcH), &fmtL, &bGray);
        bcX += 22.0f;

        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            bool hov = (fm_hovBreadcrumb == i);
            SolidBrush* c = hov ? &bTeal : &bGray;
            wstring seg = fm_breadcrumb[i];
            if (!seg.empty() && seg.back() == L'\\') seg.pop_back();
            RectF tr(bcX, bcY, 200.0f, bcH);
            g.DrawString(seg.c_str(), -1, hov ? &fBold : &fSmall, tr, &fmtL, c);
            // measure width
            RectF sz;
            { RectF layoutRect(0,0,500.0f,bcH); g.MeasureString(seg.c_str(), -1, hov ? &fBold : &fSmall, layoutRect, &sz); }
            bcX += sz.Width;
            if (i < (int)fm_breadcrumb.size() - 1) {
                g.DrawString(L"\xE76C", -1, &fIconSm, RectF(bcX, bcY, 16.0f, bcH), &fmtL, &bGray);
                bcX += 16.0f;
            }
        }

        // ---- PREVIEW PANEL (right side) ----
        // Compute geometry first so file list knows its available width.
        // Sidebar is drawn by tab_special.cpp; cx/cw here is already the content area.
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;

        // Preview panel takes right PREVIEW_WIDTH_RATIO of the full content width
        float listAreaW = cw; // no internal sidebar — full content width
        float previewW  = fm_previewVisible ? (listAreaW * PREVIEW_WIDTH_RATIO) : 0.0f;
        float fileListW = listAreaW - previewW; // file list actual width
        float previewX  = cx + fileListW;

        // Draw preview panel if visible
        if (fm_previewVisible) {
            DrawPreviewPanel(g, previewX, listY, previewW, listH);
        }

        // Handle WebView2 positioning for WebView previews
        if (fm_previewVisible && fm_previewType == PreviewType::WebView && hParentWnd) {
            // Convert panel rect to screen/client coords for WebView2
            float hdrH2 = 34.0f;
            RECT wvRect;
            wvRect.left   = (LONG)(previewX);
            wvRect.top    = (LONG)(listY + hdrH2);
            wvRect.right  = (LONG)(previewX + previewW);
            wvRect.bottom = (LONG)(listY + listH);
            if (wvRect.right > wvRect.left && wvRect.bottom > wvRect.top) {
                if (memcmp(&wvRect, &fm_webViewBounds, sizeof(RECT)) != 0) {
                    fm_webViewBounds = wvRect;
                    // Build URL:
                    // PDF → file:// URL (WebView2 renders PDFs natively)
                    // Video/Audio → data: HTML with <video>/<audio> tag
                    wstring wvUrl;
                    if (fm_previewExt == L"pdf") {
                        wvUrl = L"file:///" + fm_previewPath;
                        // Replace backslashes
                        for (auto& ch : wvUrl) if (ch == L'\\') ch = L'/';
                    } else if (fm_previewExt==L"mp4"||fm_previewExt==L"mkv"||
                               fm_previewExt==L"avi"||fm_previewExt==L"mov"||fm_previewExt==L"webm") {
                        wstring furl = L"file:///" + fm_previewPath;
                        for (auto& ch : furl) if (ch == L'\\') ch = L'/';
                        wvUrl = L"data:text/html,<html><body style='margin:0;background:#111'>"
                                L"<video controls autoplay style='width:100%;height:100%;max-height:100vh' src='"
                                + furl + L"'></video></body></html>";
                    } else {
                        // Audio (mp3, wav, flac, aac, ogg, m4a)
                        wstring furl = L"file:///" + fm_previewPath;
                        for (auto& ch : furl) if (ch == L'\\') ch = L'/';
                        // Get filename for display
                        wstring dispName = fm_previewPath;
                        size_t sl2 = dispName.rfind(L'\\');
                        if (sl2 != wstring::npos) dispName = dispName.substr(sl2 + 1);
                        wvUrl = L"data:text/html,<html><body style='margin:0;background:#1a1a2e;"
                                L"display:flex;flex-direction:column;align-items:center;"
                                L"justify-content:center;height:100vh;font-family:Segoe UI;color:#ccc'>"
                                L"<div style='font-size:64px;margin-bottom:16px'>&#127925;</div>"
                                L"<div style='font-size:14px;margin-bottom:20px;max-width:90%;text-align:center;word-break:break-all'>"
                                + dispName + L"</div>"
                                L"<audio controls autoplay style='width:85%' src='" + furl
                                + L"'></audio></body></html>";
                    }
                    CreateEmbeddedPreviewWebView(hParentWnd, wvRect, wvUrl);
                } else {
                    UpdateEmbeddedPreviewBounds(wvRect);
                }
            }
        } else if (!fm_previewVisible || fm_previewType != PreviewType::WebView) {
            // Hide WebView if switching away
            static PreviewType lastType = PreviewType::None;
            if (lastType == PreviewType::WebView &&
                (fm_previewType != PreviewType::WebView || !fm_previewVisible)) {
                DestroyEmbeddedPreview();
            }
            lastType = fm_previewType;
        }

        // ---- FILE LIST (full content width, sidebar is drawn by tab_special.cpp) ----
        // tab_special.cpp already draws the sidebar (Quick access, This PC, drives, Google Drive)
        // and calls DrawFileManagerTab() with contentX offset — so we use the full cx here.
        float flX = cx;
        float flW = fileListW;  // narrowed when preview panel is visible

        // Column header — Windows Explorer style (flat, white, border separators)
        float colHdrH = 26.0f;
        SolidBrush bColHdrBg(Color(255, 250, 250, 250));
        g.FillRectangle(&bColHdrBg, flX, listY, flW, colHdrH);

        float c1W = flW * 0.45f, c2W = flW * 0.15f, c3W = flW * 0.25f, c4W = flW * 0.15f;
        float hdrY = listY;

        // Column header text
        Font fHdrCol(&ff, 12, FontStyleRegular, UnitPixel);
        SolidBrush bHdrTxt(Color(255, 50, 50, 50));
        g.DrawString(L"Name",          -1, &fHdrCol, RectF(flX + 28.0f,           hdrY, c1W, colHdrH), &fmtL, &bHdrTxt);
        g.DrawString(L"Type",          -1, &fHdrCol, RectF(flX + c1W + 4.0f,      hdrY, c2W, colHdrH), &fmtL, &bHdrTxt);
        g.DrawString(L"Date modified", -1, &fHdrCol, RectF(flX + c1W + c2W + 4.0f,hdrY, c3W, colHdrH), &fmtL, &bHdrTxt);
        g.DrawString(L"Size",          -1, &fHdrCol, RectF(flX + c1W+c2W+c3W,     hdrY, c4W-20.0f, colHdrH), &fmtR, &bHdrTxt);

        // Column dividers (vertical lines between headers)
        Pen pColDiv(Color(255, 213, 213, 213), 1.0f);
        g.DrawLine(&pColDiv, flX + c1W,           hdrY + 4.0f, flX + c1W,           hdrY + colHdrH - 4.0f);
        g.DrawLine(&pColDiv, flX + c1W + c2W,     hdrY + 4.0f, flX + c1W + c2W,     hdrY + colHdrH - 4.0f);
        g.DrawLine(&pColDiv, flX + c1W+c2W+c3W,   hdrY + 4.0f, flX + c1W+c2W+c3W,   hdrY + colHdrH - 4.0f);

        // Bottom border of header
        Pen pHdrBtm(Color(255, 213, 213, 213), 1.0f);
        g.DrawLine(&pHdrBtm, flX, hdrY + colHdrH, flX + flW, hdrY + colHdrH);

        // ================================================================
        // FILE LIST — Windows Explorer style
        // ================================================================
        float rowH   = 24.0f;   // compact like Explorer (was 34)
        float rowsY  = listY + colHdrH;
        float rowsH  = listH - colHdrH;
        int   maxVis = (int)(rowsH / rowH);

        // Scrollbar geometry (right edge, always reserve 16px like Explorer)
        float sbW   = 16.0f;
        float sbX   = flX + flW - sbW;
        float listW = flW - sbW;  // actual list width excluding scrollbar

        // Clip rows to content area (NO bleed = no replace effect)
        g.SetClip(RectF(flX, rowsY, listW, rowsH));

        if (fm_items.empty()) {
            g.ResetClip();
            SolidBrush bEmpty(Color(255, 160, 160, 160));
            g.DrawString(L"This folder is empty.", -1, &fSub,
                RectF(flX, rowsY + rowsH / 2.0f - 12.0f, listW, 24.0f), &fmtC, &bEmpty);
        } else {
            for (int i = fm_scrollOffset; i < (int)fm_items.size(); i++) {
                float ry = rowsY + (i - fm_scrollOffset) * rowH;
                if (ry >= rowsY + rowsH) break;   // strictly stop at bottom
                if (ry + rowH <= rowsY) continue;  // not yet visible

                bool isDir = fm_items[i].second;
                bool isSel = (fm_selectedItem == i);
                bool isHov = (fm_hovItem == i);

                // Row background — Windows Explorer style
                if (isSel) {
                    // Selected: blue highlight (Explorer blue)
                    SolidBrush bSelRow(Color(255, 204, 232, 255));
                    Pen pSelBrd(Color(255, 153, 209, 255), 1.0f);
                    g.FillRectangle(&bSelRow, flX, ry, listW, rowH);
                    g.DrawRectangle(&pSelBrd, flX, ry, listW - 1.0f, rowH - 1.0f);
                } else if (isHov) {
                    // Hover: very light blue (Explorer hover)
                    SolidBrush bHovRow(Color(255, 229, 243, 255));
                    Pen pHovBrd(Color(255, 204, 232, 255), 1.0f);
                    g.FillRectangle(&bHovRow, flX, ry, listW, rowH);
                    g.DrawRectangle(&pHovBrd, flX, ry, listW - 1.0f, rowH - 1.0f);
                } else {
                    // Normal: pure white (Explorer default)
                    g.FillRectangle(&bWhite, flX, ry, listW, rowH);
                    // Very subtle bottom line
                    Pen pRowLine(Color(30, 0, 0, 0), 1.0f);
                    g.DrawLine(&pRowLine, flX, ry + rowH - 1.0f, flX + listW, ry + rowH - 1.0f);
                }

                wstring fullPath = fm_currentPath + fm_items[i].first;

                // --- Icon: file-type colored like Explorer ---
                const wchar_t* ico = L"\xE8A5"; // generic file
                Color icoClr(255, 100, 130, 200); // default
                if (isDir) {
                    ico = L"\xED41"; // folder
                    icoClr = Color(255, 255, 196, 37); // Explorer yellow
                } else {
                    wstring nm = fm_items[i].first;
                    size_t dot = nm.rfind(L'.');
                    if (dot != wstring::npos) {
                        wstring ext = nm.substr(dot + 1);
                        // lowercase ext
                        for (auto& ch : ext) ch = towlower(ch);
                        if (ext==L"exe"||ext==L"msi")          { ico=L"\xE756"; icoClr=Color(255,0,120,215); }
                        else if (ext==L"pdf")                   { ico=L"\xEA90"; icoClr=Color(255,220,38,38); }
                        else if (ext==L"jpg"||ext==L"jpeg"||ext==L"png"||ext==L"gif"||ext==L"webp"||ext==L"bmp")
                                                                { ico=L"\xEB9F"; icoClr=Color(255,168,85,247); }
                        else if (ext==L"mp4"||ext==L"mkv"||ext==L"avi"||ext==L"mov")
                                                                { ico=L"\xE8B2"; icoClr=Color(255,236,72,153); }
                        else if (ext==L"mp3"||ext==L"wav"||ext==L"flac"||ext==L"aac")
                                                                { ico=L"\xEC4F"; icoClr=Color(255,20,184,166); }
                        else if (ext==L"zip"||ext==L"rar"||ext==L"7z")
                                                                { ico=L"\xE7B8"; icoClr=Color(255,245,158,11); }
                        else if (ext==L"txt"||ext==L"log"||ext==L"ini"||ext==L"cfg")
                                                                { ico=L"\xE8A5"; icoClr=Color(255,100,116,139); }
                        else if (ext==L"docx"||ext==L"doc")    { ico=L"\xE8A5"; icoClr=Color(255,43,87,154); }
                        else if (ext==L"xlsx"||ext==L"xls"||ext==L"csv")
                                                                { ico=L"\xE9F9"; icoClr=Color(255,33,115,70); }
                        else if (ext==L"pptx"||ext==L"ppt")   { ico=L"\xE8D1"; icoClr=Color(255,209,52,56); }
                        else if (ext==L"cpp"||ext==L"h"||ext==L"py"||ext==L"js"||ext==L"ts"||ext==L"cs")
                                                                { ico=L"\xE943"; icoClr=Color(255,88,28,135); }
                    }
                }
                SolidBrush bIco(icoClr);
                g.DrawString(ico, -1, &fIconSm, RectF(flX + 4.0f, ry, 20.0f, rowH), &fmtL, &bIco);

                // --- Name ---
                SolidBrush bNameClr(isSel ? Color(255,0,0,0) : Color(255,0,0,0));
                g.DrawString(fm_items[i].first.c_str(), -1, &fSmall,
                    RectF(flX + 26.0f, ry, c1W - 30.0f, rowH), &fmtL, &bNameClr);

                // --- Type ---
                wstring typeStr = isDir ? L"File folder" : L"File";
                wstring name2 = fm_items[i].first;
                size_t dot2 = name2.rfind(L'.');
                if (!isDir && dot2 != wstring::npos) {
                    wstring ext2 = name2.substr(dot2 + 1);
                    for (auto& ch : ext2) ch = towupper(ch);
                    typeStr = ext2 + L" File";
                }
                SolidBrush bTypeTxt(Color(255, 80, 80, 80));
                g.DrawString(typeStr.c_str(), -1, &fSmall,
                    RectF(flX + c1W, ry, c2W, rowH), &fmtL, &bTypeTxt);

                // --- Date Modified ---
                WIN32_FILE_ATTRIBUTE_DATA fad;
                wstring modStr = L"";
                if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fad)) {
                    FILETIME ft  = fad.ftLastWriteTime;
                    FILETIME lft; FileTimeToLocalFileTime(&ft, &lft);
                    SYSTEMTIME st; FileTimeToSystemTime(&lft, &st);
                    wchar_t buf[40];
                    // Format: "9/7/2026 3:45 PM" — Explorer style
                    int hr = st.wHour; bool pm = hr >= 12;
                    if (hr == 0) hr = 12; else if (hr > 12) hr -= 12;
                    swprintf(buf, 40, L"%d/%d/%04d %d:%02d %s",
                        st.wMonth, st.wDay, st.wYear, hr, st.wMinute, pm ? L"PM" : L"AM");
                    modStr = buf;
                }
                g.DrawString(modStr.c_str(), -1, &fSmall,
                    RectF(flX + c1W + c2W + 4.0f, ry, c3W - 4.0f, rowH), &fmtL, &bTypeTxt);

                // --- Size ---
                wstring sizeStr = L"";
                if (!isDir) {
                    WIN32_FIND_DATAW fd2;
                    HANDLE h2 = FindFirstFileW(fullPath.c_str(), &fd2);
                    if (h2 != INVALID_HANDLE_VALUE) {
                        ULONGLONG sz = ((ULONGLONG)fd2.nFileSizeHigh << 32) | fd2.nFileSizeLow;
                        wchar_t buf[32];
                        if      (sz < 1024)               swprintf(buf, 32, L"%llu B",   sz);
                        else if (sz < 1024*1024)          swprintf(buf, 32, L"%llu KB",  (sz+1023)/1024);
                        else if (sz < 1024LL*1024*1024)   swprintf(buf, 32, L"%.1f MB",  sz/1048576.0);
                        else                               swprintf(buf, 32, L"%.2f GB",  sz/1073741824.0);
                        sizeStr = buf;
                        FindClose(h2);
                    }
                }
                g.DrawString(sizeStr.c_str(), -1, &fSmall,
                    RectF(flX + c1W + c2W + c3W, ry, c4W - 6.0f, rowH), &fmtR, &bTypeTxt);
            }
            g.ResetClip();
        }

        // ================================================================
        // SCROLLBAR — Windows Explorer style
        // Right-side track + thumb, 16px wide
        // ================================================================
        {
            // Track background (light gray like Explorer)
            SolidBrush bTrack(Color(255, 240, 240, 240));
            g.FillRectangle(&bTrack, sbX, rowsY, sbW, rowsH);
            // Track left border
            Pen pTrackBrd(Color(255, 200, 200, 200), 1.0f);
            g.DrawLine(&pTrackBrd, sbX, rowsY, sbX, rowsY + rowsH);

            if ((int)fm_items.size() > maxVis) {
                float ratio  = (float)maxVis / (float)fm_items.size();
                float thumbH = max(20.0f, rowsH * ratio);
                float maxOff = (float)(fm_items.size() - maxVis);
                float thumbY = rowsY + (rowsH - thumbH) * (fm_scrollOffset / maxOff);
                thumbY = min(thumbY, rowsY + rowsH - thumbH);

                // Up arrow button (top of scrollbar)
                SolidBrush bArrowBg(Color(255, 225, 225, 225));
                g.FillRectangle(&bArrowBg, sbX, rowsY, sbW, 17.0f);
                Font fArrow(&ff, 9, FontStyleRegular, UnitPixel);
                SolidBrush bArrowClr(Color(255, 80, 80, 80));
                g.DrawString(L"▲", -1, &fArrow, RectF(sbX, rowsY, sbW, 17.0f), &fmtC, &bArrowClr);

                // Down arrow button (bottom)
                g.FillRectangle(&bArrowBg, sbX, rowsY + rowsH - 17.0f, sbW, 17.0f);
                g.DrawString(L"▼", -1, &fArrow, RectF(sbX, rowsY + rowsH - 17.0f, sbW, 17.0f), &fmtC, &bArrowClr);

                // Thumb (darker gray, rounded slightly)
                float tY = max(rowsY + 17.0f, thumbY);
                float tH = min(thumbH, rowsH - 34.0f);
                SolidBrush bThumbNorm(Color(255, 173, 173, 173));
                FillRect_(g, &bThumbNorm, nullptr, sbX + 2.0f, tY, sbW - 4.0f, tH, 3.0f);
                // Thumb border
                Pen pThumbBrd(Color(255, 150, 150, 150), 1.0f);
                FillRect_(g, nullptr, &pThumbBrd, sbX + 2.0f, tY, sbW - 4.0f, tH, 3.0f);
            } else {
                // No scroll needed — show greyed out scrollbar
                SolidBrush bNoScroll(Color(255, 240, 240, 240));
                g.FillRectangle(&bNoScroll, sbX, rowsY, sbW, rowsH);
            }
        }

    }

    // ============================
    // GOOGLE DRIVE TAB
    // ============================
    else if (fm_activeSubTab == 1) {

        if (!fm_driveSignedIn) {
            // ---- Sign-in card ----
            float cardW = 400.0f, cardH = 240.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;

            SolidBrush bCard(Color(255, 255, 255, 255));
            Pen pCard(Color(255, 218, 225, 232), 1.5f);
            FillRect_(g, &bCard, &pCard, cardX, cardY, cardW, cardH, 10.0f);

            // Google Drive 3-dot icon
            float icY = cardY + 26.0f;
            float icX = cardX + cardW / 2.0f - 28.0f;
            SolidBrush bDrBlue (Color(255,  66, 133, 244));
            SolidBrush bDrGreen(Color(255,  52, 168,  83));
            SolidBrush bDrYell (Color(255, 251, 188,   5));
            g.FillEllipse(&bDrBlue,  icX,        icY, 24.0f, 24.0f);
            g.FillEllipse(&bDrGreen, icX + 16.0f,icY, 24.0f, 24.0f);
            g.FillEllipse(&bDrYell,  icX + 8.0f, icY + 12.0f, 24.0f, 24.0f);

            FontFamily ffDr(L"Segoe UI");
            Font fDrTitle(&ffDr, 17, FontStyleBold, UnitPixel);
            Font fDrSub  (&ffDr, 13, FontStyleRegular, UnitPixel);
            Font fDrBtn  (&ffDr, 13, FontStyleBold, UnitPixel);

            g.DrawString(L"Google Drive", -1, &fDrTitle,
                RectF(cardX, cardY + 64.0f, cardW, 28.0f), &fmtC, &bDark);
            g.DrawString(L"Sign in to browse your Drive files\ndirectly here — no browser needed.",
                -1, &fDrSub, RectF(cardX + 20.0f, cardY + 98.0f, cardW - 40.0f, 48.0f), &fmtC, &bGray);

            // Sign-in button
            float btnW2 = 220.0f, btnH2 = 40.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 58.0f;
            SolidBrush bSignIn(fm_hovDriveSignIn ? Color(255, 46, 108, 210) : Color(255, 66, 133, 244));
            FillRect_(g, &bSignIn, nullptr, btnX2, btnY2, btnW2, btnH2, 6.0f);
            g.DrawString(L"\xE8A0  Sign in with Google", -1, &fDrBtn,
                RectF(btnX2, btnY2, btnW2, btnH2), &fmtC, &bWhite);

        } else {
            // ========================================
            // GOOGLE DRIVE — Signed-in Beautiful UI
            // ========================================

            // ── TOOLBAR (height 52) ──────────────────
            float tbH = 52.0f;
            SolidBrush bTbBg(Color(255, 255, 255, 255));
            g.FillRectangle(&bTbBg, cx, bodyY, cw, tbH);
            Pen pTbLine(Color(255, 226, 232, 240), 1.0f);
            g.DrawLine(&pTbLine, cx, bodyY + tbH, cx + cw, bodyY + tbH);

            float btnH3 = 32.0f;
            float btnY3 = bodyY + (tbH - btnH3) / 2.0f;
            float bx3   = cx + 12.0f;

            bool canGoBack = !fm_driveFolderStack.empty();

            // Back button — pill style
            {
                Color cBack = canGoBack
                    ? Color(255, 235, 245, 255)
                    : Color(255, 246, 248, 250);
                SolidBrush bBack(cBack);
                Pen pBack(canGoBack ? Color(255, 66, 133, 244) : Color(255, 218, 225, 232), 1.0f);
                FillRect_(g, &bBack, &pBack, bx3, btnY3, 36.0f, btnH3, 8.0f);
                SolidBrush bBackIco(canGoBack ? Color(255, 66, 133, 244) : Color(255, 180, 188, 200));
                g.DrawString(L"\xE76B", -1, &fIconSm, RectF(bx3, btnY3, 36.0f, btnH3), &fmtC, &bBackIco);
            }
            bx3 += 42.0f;

            // Refresh button — pill style
            {
                SolidBrush bRef2(Color(255, 246, 248, 250));
                Pen pRef2(Color(255, 218, 225, 232), 1.0f);
                FillRect_(g, &bRef2, &pRef2, bx3, btnY3, 36.0f, btnH3, 8.0f);
                SolidBrush bRefIco(Color(255, 100, 116, 139));
                g.DrawString(L"\xE72C", -1, &fIconSm, RectF(bx3, btnY3, 36.0f, btnH3), &fmtC, &bRefIco);
            }
            bx3 += 46.0f;

            // Separator
            {
                Pen pSep(Color(255, 226, 232, 240), 1.0f);
                g.DrawLine(&pSep, bx3, btnY3 + 4.0f, bx3, btnY3 + btnH3 - 4.0f);
            }
            bx3 += 10.0f;

            // Drive icon + breadcrumb pill
            {
                // Drive logo dots (mini)
                float dx = bx3, dy = btnY3 + (btnH3 - 14.0f) / 2.0f;
                SolidBrush bDB(Color(255, 66, 133, 244));
                SolidBrush bDG(Color(255, 52, 168, 83));
                SolidBrush bDY(Color(255, 251, 188, 5));
                g.FillEllipse(&bDB, dx,       dy,       8.0f, 8.0f);
                g.FillEllipse(&bDG, dx + 5.0f,dy,       8.0f, 8.0f);
                g.FillEllipse(&bDY, dx + 2.5f,dy + 5.0f,8.0f, 8.0f);
                bx3 += 20.0f;

                // "My Drive" text
                bool atRoot = fm_driveFolderStack.empty();
                SolidBrush bRootClr(atRoot ? Color(255, 30, 64, 175) : Color(255, 100, 116, 139));
                g.DrawString(L"My Drive", -1, atRoot ? &fBold : &fSmall,
                    RectF(bx3, btnY3, 72.0f, btnH3), &fmtL, &bRootClr);
                bx3 += 74.0f;

                for (size_t pi = 0; pi < fm_driveFolderNameStack.size(); pi++) {
                    SolidBrush bChevron(Color(255, 148, 163, 184));
                    g.DrawString(L"\xE76C", -1, &fIconSm, RectF(bx3, btnY3, 14.0f, btnH3), &fmtL, &bChevron);
                    bx3 += 15.0f;
                    bool isLast2 = (pi == fm_driveFolderNameStack.size() - 1);
                    SolidBrush bSegClr(isLast2 ? Color(255, 30, 64, 175) : Color(255, 100, 116, 139));
                    g.DrawString(fm_driveFolderNameStack[pi].c_str(), -1,
                        isLast2 ? &fBold : &fSmall,
                        RectF(bx3, btnY3, 180.0f, btnH3), &fmtL, &bSegClr);
                    bx3 += 182.0f;
                }
            }

            // Right side: avatar chip + sign-out
            {
                // Avatar circle
                float avR = 16.0f;
                float avX = cx + cw - 130.0f;
                float avY = bodyY + (tbH - avR * 2.0f) / 2.0f;
                SolidBrush bAvBg(Color(255, 66, 133, 244));
                FillRect_(g, &bAvBg, nullptr, avX, avY, avR * 2.0f, avR * 2.0f, avR);
                wstring initials = L"?";
                if (!fm_driveUserEmail.empty()) {
                    initials.clear();
                    initials += (wchar_t)towupper(fm_driveUserEmail[0]);
                }
                SolidBrush bAvTxt(Color(255, 255, 255, 255));
                g.DrawString(initials.c_str(), -1, &fBold,
                    RectF(avX, avY, avR * 2.0f, avR * 2.0f), &fmtC, &bAvTxt);

                // Email (truncated)
                wstring emailShow = fm_driveUserEmail;
                if (emailShow.size() > 18) emailShow = emailShow.substr(0, 16) + L"..";
                SolidBrush bEmailClr(Color(255, 100, 116, 139));
                g.DrawString(emailShow.c_str(), -1, &fSmall,
                    RectF(avX + avR * 2.0f + 4.0f, bodyY + 4.0f, 90.0f, tbH / 2.0f), &fmtL, &bEmailClr);

                // Sign-out pill button
                float soW2 = 76.0f;
                float soX2 = cx + cw - soW2 - 10.0f;
                float soY2 = bodyY + tbH - btnH3 - 8.0f;
                SolidBrush bSoHov(Color(255, 254, 242, 242));
                Pen pSoBrd(Color(255, 252, 165, 165), 1.0f);
                FillRect_(g, &bSoHov, &pSoBrd, soX2, soY2, soW2, 24.0f, 6.0f);
                SolidBrush bSoTxt(Color(255, 185, 28, 28));
                g.DrawString(L"\xE8BB  Sign out", -1, &fSmall,
                    RectF(soX2, soY2, soW2, 24.0f), &fmtC, &bSoTxt);
            }

            // ── COLUMN HEADER (height 34) ────────────
            float hdrH3 = tbH;
            float dvListY = bodyY + hdrH3;
            float colHdrH = 34.0f;

            SolidBrush bColHdrBg(Color(255, 248, 250, 252));
            g.FillRectangle(&bColHdrBg, cx, dvListY, cw, colHdrH);
            Pen pColHdrLine(Color(255, 226, 232, 240), 1.0f);
            g.DrawLine(&pColHdrLine, cx, dvListY + colHdrH, cx + cw, dvListY + colHdrH);

            // Column widths
            float dc1 = cw * 0.44f;
            float dc2 = cw * 0.18f;
            float dc3 = cw * 0.22f;
            float dc4 = cw * 0.16f;

            SolidBrush bColHdrTxt(Color(255, 100, 116, 139));
            Font fColHdr(&ff, 11, FontStyleBold, UnitPixel);
            g.DrawString(L"NAME",     -1, &fColHdr, RectF(cx + 50.0f,           dvListY, dc1, colHdrH), &fmtL, &bColHdrTxt);
            g.DrawString(L"TYPE",     -1, &fColHdr, RectF(cx + dc1,             dvListY, dc2, colHdrH), &fmtL, &bColHdrTxt);
            g.DrawString(L"MODIFIED", -1, &fColHdr, RectF(cx + dc1 + dc2,       dvListY, dc3, colHdrH), &fmtL, &bColHdrTxt);
            g.DrawString(L"SIZE",     -1, &fColHdr, RectF(cx + dc1+dc2+dc3,     dvListY, dc4 - 16.0f, colHdrH), &fmtR, &bColHdrTxt);

            // ── FILE ROWS ────────────────────────────
            float dvRowH  = 44.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - hdrH3 - colHdrH;
            int   dvMaxVis = (int)(dvRowsH / dvRowH);

            if (fm_driveLoading) {
                // Animated loading dots (static for now — 3 dots)
                SolidBrush bLd1(Color(255, 66, 133, 244));
                SolidBrush bLd2(Color(180, 66, 133, 244));
                SolidBrush bLd3(Color(100, 66, 133, 244));
                float ldY = dvRowsY + dvRowsH / 2.0f - 6.0f;
                float ldX = cx + cw / 2.0f - 24.0f;
                g.FillEllipse(&bLd1, ldX,        ldY, 12.0f, 12.0f);
                g.FillEllipse(&bLd2, ldX + 16.0f,ldY, 12.0f, 12.0f);
                g.FillEllipse(&bLd3, ldX + 32.0f,ldY, 12.0f, 12.0f);
                SolidBrush bLdTxt(Color(255, 100, 116, 139));
                g.DrawString(L"Loading Google Drive...", -1, &fSub,
                    RectF(cx, ldY + 18.0f, cw, 24.0f), &fmtC, &bLdTxt);

            } else if (!fm_driveStatusMsg.empty()) {
                // Error state with icon
                SolidBrush bErrIco(Color(255, 220, 38, 38));
                g.DrawString(L"\xE7BA", -1, &fIcon,
                    RectF(cx, dvRowsY + dvRowsH / 2.0f - 28.0f, cw, 28.0f), &fmtC, &bErrIco);
                SolidBrush bErrTxt(Color(255, 185, 28, 28));
                g.DrawString(fm_driveStatusMsg.c_str(), -1, &fSub,
                    RectF(cx, dvRowsY + dvRowsH / 2.0f, cw, 24.0f), &fmtC, &bErrTxt);

            } else {
                Region dvClip(RectF(cx, dvRowsY, cw - 14.0f, dvRowsH));
                g.SetClip(&dvClip);

                if (fm_driveItems.empty()) {
                    // Empty folder state
                    SolidBrush bEmptyIco(Color(200, 148, 163, 184));
                    g.DrawString(L"\xED41", -1, &fTitle,
                        RectF(cx, dvRowsY + dvRowsH / 2.0f - 40.0f, cw, 34.0f), &fmtC, &bEmptyIco);
                    SolidBrush bEmptyTxt(Color(255, 148, 163, 184));
                    g.DrawString(L"This folder is empty", -1, &fSub,
                        RectF(cx, dvRowsY + dvRowsH / 2.0f - 4.0f, cw, 24.0f), &fmtC, &bEmptyTxt);
                } else {
                    for (int i = fm_driveScrollOff;
                         i < (int)fm_driveItems.size() && i < fm_driveScrollOff + dvMaxVis + 2; i++) {
                        float ry = dvRowsY + (i - fm_driveScrollOff) * dvRowH;
                        if (ry > dvRowsY + dvRowsH) break;

                        bool isHov = (fm_driveHovItem == i);
                        bool isSel = (fm_driveSelectedItem == i);

                        // Row background
                        if (isSel) {
                            SolidBrush bSelRow(Color(255, 235, 245, 255));
                            g.FillRectangle(&bSelRow, cx, ry, cw - 14.0f, dvRowH);
                            // Left accent bar
                            SolidBrush bAccent(Color(255, 66, 133, 244));
                            g.FillRectangle(&bAccent, cx, ry, 3.0f, dvRowH);
                        } else if (isHov) {
                            SolidBrush bHovRow(Color(255, 248, 250, 255));
                            g.FillRectangle(&bHovRow, cx, ry, cw - 14.0f, dvRowH);
                        }

                        // Row divider
                        Pen pDivider(Color(255, 241, 245, 249), 1.0f);
                        g.DrawLine(&pDivider, cx + 12.0f, ry + dvRowH, cx + cw - 14.0f, ry + dvRowH);

                        bool isFolder = (fm_driveItems[i].mimeType == L"Folder");

                        // ── File type icon pill ──────────────
                        // Determine color + icon by type
                        Color icoColor(255, 100, 116, 139);
                        const wchar_t* icoGlyph = L"\xE8A5";
                        wstring mt = fm_driveItems[i].mimeType;
                        if (isFolder) {
                            icoColor = Color(255, 59, 130, 246);
                            icoGlyph = L"\xED41";
                        } else if (mt == L"Google Docs") {
                            icoColor = Color(255, 66, 133, 244);
                            icoGlyph = L"\xE8A5";
                        } else if (mt == L"Google Sheets") {
                            icoColor = Color(255, 52, 168, 83);
                            icoGlyph = L"\xE9F9";
                        } else if (mt == L"Google Slides") {
                            icoColor = Color(255, 234, 88, 12);
                            icoGlyph = L"\xE8D1";
                        } else if (mt == L"PDF") {
                            icoColor = Color(255, 220, 38, 38);
                            icoGlyph = L"\xEA90";
                        } else if (mt == L"jpg" || mt == L"jpeg" || mt == L"png" || mt == L"gif" || mt == L"webp") {
                            icoColor = Color(255, 168, 85, 247);
                            icoGlyph = L"\xEB9F";
                        } else if (mt == L"mp4" || mt == L"mov" || mt == L"avi" || mt == L"mkv") {
                            icoColor = Color(255, 236, 72, 153);
                            icoGlyph = L"\xE8B2";
                        } else if (mt == L"mp3" || mt == L"wav" || mt == L"flac" || mt == L"aac") {
                            icoColor = Color(255, 20, 184, 166);
                            icoGlyph = L"\xEC4F";
                        } else if (mt == L"zip" || mt == L"rar" || mt == L"7z") {
                            icoColor = Color(255, 245, 158, 11);
                            icoGlyph = L"\xE7B8";
                        }

                        // Icon background pill (28x28 rounded)
                        float icoPillX = cx + 10.0f;
                        float icoPillY = ry + (dvRowH - 28.0f) / 2.0f;
                        Color icoLightBg(40, icoColor.GetR(), icoColor.GetG(), icoColor.GetB());
                        SolidBrush bIcoPill(icoLightBg);
                        FillRect_(g, &bIcoPill, nullptr, icoPillX, icoPillY, 28.0f, 28.0f, 6.0f);
                        SolidBrush bIcoGlyph(icoColor);
                        g.DrawString(icoGlyph, -1, &fIconSm,
                            RectF(icoPillX, icoPillY, 28.0f, 28.0f), &fmtC, &bIcoGlyph);

                        // ── Name ────────────────────────────
                        SolidBrush bNameClr(isSel ? Color(255, 30, 64, 175) : Color(255, 30, 41, 59));
                        g.DrawString(fm_driveItems[i].name.c_str(), -1,
                            isSel ? &fBold : &fSmall,
                            RectF(cx + 46.0f, ry, dc1 - 50.0f, dvRowH), &fmtL, &bNameClr);

                        // ── Type pill ───────────────────────
                        // Draw a tiny colored pill label
                        wstring dispType = fm_driveItems[i].mimeType;
                        Color pillBg(30, icoColor.GetR(), icoColor.GetG(), icoColor.GetB());
                        SolidBrush bPillBg(pillBg);
                        SolidBrush bPillTxt(icoColor);
                        Font fTiny(&ff, 10, FontStyleRegular, UnitPixel);
                        float typeX = cx + dc1 + 4.0f;
                        float typeH = 20.0f;
                        float typeY = ry + (dvRowH - typeH) / 2.0f;
                        // Measure text width
                        RectF typeRect(typeX, typeY, dc2 - 8.0f, typeH);
                        FillRect_(g, &bPillBg, nullptr, typeRect.X, typeRect.Y, typeRect.Width, typeRect.Height, 4.0f);
                        g.DrawString(dispType.c_str(), -1, &fTiny, typeRect, &fmtC, &bPillTxt);

                        // ── Modified date ────────────────────
                        SolidBrush bModTxt(Color(255, 100, 116, 139));
                        g.DrawString(fm_driveItems[i].modified.c_str(), -1, &fSmall,
                            RectF(cx + dc1 + dc2, ry, dc3, dvRowH), &fmtL, &bModTxt);

                        // ── Size ────────────────────────────
                        SolidBrush bSizeTxt(Color(255, 100, 116, 139));
                        g.DrawString(fm_driveItems[i].size.c_str(), -1, &fSmall,
                            RectF(cx + dc1 + dc2 + dc3, ry, dc4 - 20.0f, dvRowH), &fmtR, &bSizeTxt);
                    }
                }
                g.ResetClip();

                // ── SCROLLBAR (wide, pretty) ──────────────
                if ((int)fm_driveItems.size() > dvMaxVis) {
                    float sbW   = 8.0f;
                    float sbX   = cx + cw - sbW - 4.0f;
                    float sbTH  = dvRowsH;
                    float ratio = (float)dvMaxVis / (float)fm_driveItems.size();
                    float thumbH = max(36.0f, sbTH * ratio);
                    float thumbY = dvRowsY + sbTH * ((float)fm_driveScrollOff / (float)fm_driveItems.size());
                    thumbY = min(thumbY, dvRowsY + sbTH - thumbH);

                    // Track
                    SolidBrush bTrack(Color(60, 100, 116, 139));
                    FillRect_(g, &bTrack, nullptr, sbX, dvRowsY, sbW, sbTH, 4.0f);
                    // Thumb
                    SolidBrush bThumb3(Color(200, 66, 133, 244));
                    FillRect_(g, &bThumb3, nullptr, sbX, thumbY, sbW, thumbH, 4.0f);
                }
            }
        }
    }
}

// ============================================================
// MOUSE MOVE
// ============================================================
void ProcessFileManagerMouseMove(float x, float y) {
    float cx = g_fm_cx, cy = g_fm_cy, cw = g_fm_cw, ch = g_fm_ch;

    bool old_hovTabLocal  = fm_hovTabLocal;
    bool old_hovTabDrive  = fm_hovTabDrive;
    bool old_hovUp        = fm_hovUp;
    bool old_hovRefresh   = fm_hovRefresh;
    bool old_hovNewFolder = fm_hovNewFolder;
    bool old_hovDelete    = fm_hovDelete;
    bool old_hovOpen      = fm_hovOpen;
    int  old_hovItem      = fm_hovItem;
    int  old_hovBreadcrumb= fm_hovBreadcrumb;
    bool old_hovDriveSignIn = fm_hovDriveSignIn;
    int  old_driveHovItem = fm_driveHovItem;

    fm_hovDriveSignIn = false;
    fm_driveHovItem = -1;
    fm_hovTabLocal = fm_hovTabDrive = false;
    fm_hovUp = fm_hovRefresh = fm_hovNewFolder = fm_hovDelete = fm_hovOpen = false;
    fm_hovItem = -1; fm_hovBreadcrumb = -1;
    fm_hovDriveSignIn = false; fm_driveHovItem = -1;

    // Sub tabs
    float tabBarH = 48.0f;
    float stW = 200.0f;
    if (PtIn(x, y, cx + 10.0f, cy + 4.0f, stW, tabBarH - 8.0f)) fm_hovTabLocal = true;
    if (PtIn(x, y, cx + 10.0f + stW, cy + 4.0f, stW, tabBarH - 8.0f)) fm_hovTabDrive = true;

    // Drive sign-in button hover
    if (fm_activeSubTab == 1 && !fm_driveSignedIn) {
        float bH = ch - tabBarH;
        float cardW = 400.0f, cardH = 240.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + tabBarH + (bH - cardH) / 2.0f;
        float btnW2 = 220.0f, btnH2 = 40.0f;
        float btnX2 = cardX + (cardW - btnW2) / 2.0f;
        float btnY2 = cardY + cardH - 58.0f;
        bool newHovSI = PtIn(x, y, btnX2, btnY2, btnW2, btnH2);
        if (newHovSI != fm_hovDriveSignIn) { fm_hovDriveSignIn = newHovSI; }
    }

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    fm_hovSideGDrive = false;

    if (fm_activeSubTab == 0) {
        // Toolbar
        float tbH = 44.0f;
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;
        if (PtIn(x, y, bx, btnY, btnW, btnH)) fm_hovUp = true;
        bx += btnW + 6.0f;
        if (PtIn(x, y, bx, btnY, btnW, btnH)) fm_hovRefresh = true;
        bx += btnW + 6.0f;
        float nbW = 110.0f;
        if (PtIn(x, y, bx, btnY, nbW, btnH)) fm_hovNewFolder = true;
        bx += nbW + 6.0f;
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            if (PtIn(x, y, bx, btnY, dW, btnH)) fm_hovDelete = true;
            bx += dW + 6.0f;
            float oW = 80.0f;
            if (PtIn(x, y, bx, btnY, oW, btnH)) fm_hovOpen = true;
        }

        // Breadcrumb
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        float bcX = cx + 32.0f;
        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            if (PtIn(x, y, bcX, bcY, 150.0f, bcH)) { fm_hovBreadcrumb = i; break; }
            bcX += 100.0f; // rough estimate
        }

        // File rows — account for preview panel width
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;
        float colHdrH = 28.0f;
        float flX = cx; // no internal sidebar; cx is already the content area start
        float listAreaWM = cw;
        float previewWM  = fm_previewVisible ? (listAreaWM * PREVIEW_WIDTH_RATIO) : 0.0f;
        float flW = listAreaWM - previewWM;
        float rowH = 24.0f; // match draw rowH
        float rowsY = listY + colHdrH;
        float rowsH = listH - colHdrH;
        float sbWM = 16.0f;
        if (PtIn(x, y, flX, rowsY, flW - sbWM, rowsH)) {
            int idx = (int)((y - rowsY) / rowH) + fm_scrollOffset;
            if (idx >= 0 && idx < (int)fm_items.size()) fm_hovItem = idx;
        }

    } else if (fm_activeSubTab == 1) {
        if (!fm_driveSignedIn) {
            float cardW = 380.0f, cardH = 220.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;
            float btnW2 = 200.0f, btnH2 = 38.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 55.0f;
            if (PtIn(x, y, btnX2, btnY2, btnW2, btnH2)) fm_hovDriveSignIn = true;
        } else {
            float hdrH = 56.0f;
            float dvListY = bodyY + hdrH;
            float colHdrH = 28.0f;
            float dvRowH = 38.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - hdrH - colHdrH;
            if (PtIn(x, y, cx, dvRowsY, cw, dvRowsH)) {
                int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
                if (idx >= 0 && idx < (int)fm_driveItems.size()) fm_driveHovItem = idx;
            }
        }
    }

    extern HWND hParentWnd;
    bool changed = (old_hovTabLocal != fm_hovTabLocal || old_hovTabDrive != fm_hovTabDrive ||
                    old_hovUp != fm_hovUp || old_hovRefresh != fm_hovRefresh ||
                    old_hovNewFolder != fm_hovNewFolder || old_hovDelete != fm_hovDelete ||
                    old_hovOpen != fm_hovOpen || old_hovItem != fm_hovItem ||
                    old_hovBreadcrumb != fm_hovBreadcrumb || old_hovDriveSignIn != fm_hovDriveSignIn ||
                    old_driveHovItem != fm_driveHovItem);
    // Drive row hover
    if (fm_activeSubTab == 1 && fm_driveSignedIn) {
        float tbH = 44.0f;
        float tabBarH2 = 48.0f;
        float dvListY = g_fm_cy + tabBarH2 + tbH;
        float colHdrH = 28.0f, dvRowH = 38.0f;
        float dvRowsY = dvListY + colHdrH;
        float dvRowsH = g_fm_ch - 48.0f - tbH - colHdrH;
        int newHov = -1;
        if (x >= g_fm_cx && x <= g_fm_cx + g_fm_cw &&
            y >= dvRowsY && y <= dvRowsY + dvRowsH) {
            int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
            if (idx >= 0 && idx < (int)fm_driveItems.size()) newHov = idx;
        }
        if (newHov != fm_driveHovItem) { fm_driveHovItem = newHov; changed = true; }
    }
    if (changed && hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}

// ============================================================
// MOUSE CLICK
// ============================================================
void ProcessFileManagerMouseClick(float x, float y, HWND hWnd) {
    float cx = g_fm_cx, cy = g_fm_cy, cw = g_fm_cw, ch = g_fm_ch;
    float tabBarH = 48.0f;
    float stW = 200.0f;

    // Sub-tab switch
    if (PtIn(x, y, cx + 10.0f, cy + 4.0f, stW, tabBarH - 8.0f)) {
        fm_activeSubTab = 0;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }
    if (PtIn(x, y, cx + 10.0f + stW, cy + 4.0f, stW, tabBarH - 8.0f)) {
        fm_activeSubTab = 1;
        // Hide preview when switching to Drive tab
        if (fm_previewVisible) { LoadPreview(L"", L""); fm_previewVisible = false; }
        if (fm_driveSignedIn && fm_driveItems.empty()) DriveListFolder(fm_driveCurrentFolderId);
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    if (fm_activeSubTab == 0) {
        float tbH = 44.0f;
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;

        // Up button
        if (PtIn(x, y, bx, btnY, btnW, btnH)) {
            wstring p = fm_currentPath;
            if (!p.empty() && p.back() == L'\\') p.pop_back();
            size_t pos = p.rfind(L'\\');
            if (pos != wstring::npos) NavigateFileManagerTo(p.substr(0, pos + 1));
            else if (p.length() >= 2) NavigateFileManagerTo(p.substr(0, 3));
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += btnW + 6.0f;

        // Refresh
        if (PtIn(x, y, bx, btnY, btnW, btnH)) {
            RefreshLocalDir();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += btnW + 6.0f;

        // New Folder
        float nbW = 110.0f;
        if (PtIn(x, y, bx, btnY, nbW, btnH)) {
            wchar_t name[MAX_PATH] = L"New Folder";
            // Simple dialog prompt (reuse InputBox style)
            wstring fullPath = fm_currentPath + L"New Folder";
            CreateDirectoryW(fullPath.c_str(), NULL);
            RefreshLocalDir();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += nbW + 6.0f;

        // Delete
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            if (PtIn(x, y, bx, btnY, dW, btnH)) {
                wstring fullPath = fm_currentPath + fm_items[fm_selectedItem].first;
                if (fm_items[fm_selectedItem].second) {
                    RemoveDirectoryW(fullPath.c_str());
                } else {
                    DeleteFileW(fullPath.c_str());
                }
                fm_selectedItem = -1;
                RefreshLocalDir();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            bx += dW + 6.0f;

            // Open
            float oW = 80.0f;
            if (PtIn(x, y, bx, btnY, oW, btnH)) {
                if (fm_selectedItem >= 0) {
                    wstring fullPath = fm_currentPath + fm_items[fm_selectedItem].first;
                    ShellExecuteW(NULL, L"open", fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                return;
            }
        }

        // Breadcrumb click
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        // Rebuild breadcrumb X positions
        float bcX = cx + 32.0f;
        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            if (PtIn(x, y, bcX, bcY, 150.0f, bcH)) {
                // Navigate to this breadcrumb segment
                wstring dest;
                for (int j = 0; j <= i; j++) {
                    dest += fm_breadcrumb[j];
                    if (!dest.empty() && dest.back() != L'\\') dest += L'\\';
                }
                NavigateFileManagerTo(dest);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            bcX += 116.0f;
        }

        // File list click
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;
        float colHdrH = 28.0f;
        float flX = cx; // no internal sidebar; cx is already content area start
        float rowH = 34.0f;
        float rowsY = listY + colHdrH;
        float rowsH = listH - colHdrH;

        {
            float rowHC = 24.0f;
            float sbWC  = 16.0f;
            // Compute file list width (preview panel may shrink it)
            float listAreaWC = g_fm_cw; // full content width, no internal sidebar
            float previewWC  = fm_previewVisible ? (listAreaWC * PREVIEW_WIDTH_RATIO) : 0.0f;
            float fileListWC = listAreaWC - previewWC;
            if (PtIn(x, y, flX, rowsY, fileListWC - sbWC, rowsH)) {
                int idx = (int)((y - rowsY) / rowHC) + fm_scrollOffset;
                if (idx >= 0 && idx < (int)fm_items.size()) {
                    if (fm_selectedItem == idx && fm_items[idx].second) {
                        // Double-click into folder → navigate, clear preview
                        wstring dest = fm_currentPath + fm_items[idx].first + L"\\";
                        LoadPreview(L"", L"");
                        NavigateFileManagerTo(dest);
                        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                        return;
                    }
                    fm_selectedItem = idx;
                    // Single click → load preview
                    if (!fm_items[idx].second) {
                        // It's a file — determine extension
                        wstring fname2 = fm_items[idx].first;
                        size_t dot = fname2.rfind(L'.');
                        wstring ext2;
                        if (dot != wstring::npos) {
                            ext2 = fname2.substr(dot + 1);
                            for (auto& ch2 : ext2) ch2 = towlower(ch2);
                        }
                        wstring fullPath2 = fm_currentPath + fname2;
                        LoadPreview(fullPath2, ext2);
                    } else {
                        // Folder selected — show empty/folder preview
                        LoadPreview(L"", L"");
                        fm_previewVisible = false;
                    }
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                }
            }
        }

        // Sidebar clicks (Quick access, drives, Google Drive) are handled by tab_special.cpp.

    } else if (fm_activeSubTab == 1) {
        if (!fm_driveSignedIn) {
            // Sign-in button
            float cardW = 400.0f, cardH = 240.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;
            float btnW2 = 220.0f, btnH2 = 40.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 58.0f;
            if (PtIn(x, y, btnX2, btnY2, btnW2, btnH2)) {
                DriveStartOAuth();  // Opens browser for OAuth, listens on localhost:5050
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
            }
        } else {
            float tbH = 44.0f;
            float btnH2 = 28.0f, btnY2 = bodyY + (tbH - btnH2) / 2.0f;

            // Back button
            if (PtIn(x, y, cx + 10.0f, btnY2, 34.0f, btnH2) && !fm_driveFolderStack.empty()) {
                DriveGoBack();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                return;
            }
            // Refresh button
            if (PtIn(x, y, cx + 50.0f, btnY2, 34.0f, btnH2)) {
                DriveListFolder(fm_driveCurrentFolderId);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                return;
            }
            // Sign-out button
            float soW = 80.0f, soX = cx + cw - soW - 14.0f;
            if (PtIn(x, y, soX, btnY2, soW, btnH2)) {
                DriveSignOut();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                return;
            }

            // Row click
            float dvListY = bodyY + tbH;
            float colHdrH = 28.0f;
            float dvRowH  = 38.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - tbH - colHdrH;
            if (PtIn(x, y, cx, dvRowsY, cw, dvRowsH)) {
                int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
                if (idx >= 0 && idx < (int)fm_driveItems.size()) {
                    if (fm_driveSelectedItem == idx) {
                        // Double-click: navigate folder or open file
                        DriveOpenItem(fm_driveItems[idx]);
                    } else {
                        fm_driveSelectedItem = idx;
                    }
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                }
            }
        }
    }
}

// ============================================================
// MOUSE WHEEL
// ============================================================
void ProcessFileManagerMouseWheel(float x, float y, int delta) {
    // Windows Explorer style: 3 lines per notch (WHEEL_DELTA=120 = 1 notch)
    int notches = abs(delta) / WHEEL_DELTA;
    if (notches == 0) notches = 1;
    int step = (delta > 0) ? -(3 * notches) : (3 * notches);

    if (fm_activeSubTab == 0) {
        int maxScroll = max(0, (int)fm_items.size() - 1);
        fm_scrollOffset = max(0, min(maxScroll, fm_scrollOffset + step));
    } else {
        int maxScroll = max(0, (int)fm_driveItems.size() - 1);
        fm_driveScrollOff = max(0, min(maxScroll, fm_driveScrollOff + step));
    }
    extern HWND hParentWnd;
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); // FALSE = no erase → no flicker
}
