#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)

#include "tab_schedule_blocks.h"
#include "tab_adult.h"   // ← AdultBlock_ApplyForSchedule() এর জন্য
#include <tlhelp32.h>    // ← CreateToolhelp32Snapshot, KillBlockedApps এর জন্য
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <shlobj.h>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <ctime>
#include <thread>        
#include <mutex>         
#include <wininet.h>     
#include <shellapi.h>    
#include <commdlg.h>     
#include <windows.h>
#include <map>           // ← 🟢 NEW: Custom Notification এর জন্য যুক্ত করা হলো

#pragma comment(lib, "wininet.lib")

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;  
static std::mutex g_schMutex; 
static bool isSchThreadRunning = false; 

// ==========================================
// 🟢 BACKGROUND GLOBAL HELPERS
// ==========================================

static wstring SafeUtf8ToWstring(const string& str) {
    if (str.empty()) return wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static void CloseActiveTabOnly(HWND hBrowser) { 
    if (GetForegroundWindow() == hBrowser) {
        keybd_event(VK_CONTROL, 0, 0, 0); 
        keybd_event('W', 0, 0, 0); 
        keybd_event('W', 0, KEYEVENTF_KEYUP, 0); 
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); 
    }
}

// 🟢 NEW: PREMIUM CUSTOM TOAST NOTIFICATION (GDI+ & Win32)
static std::map<wstring, time_t> g_lastNotifyTime;

struct ToastData {
    wstring blockedItem;
    wstring profileName;
};

LRESULT CALLBACK BlockToastWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static BYTE alpha = 0;
    static bool fadingOut = false;
    static ToastData* td = nullptr;

    switch (uMsg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            td = (ToastData*)cs->lpCreateParams;
            alpha = 0; fadingOut = false;
            SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
            SetTimer(hwnd, 1, 20, NULL); // Fade animation timer
            SetTimer(hwnd, 2, 3500, NULL); // Hold time on screen (3.5s)
            return 0;
        }
        case WM_TIMER: {
            if (wParam == 1) { // Fade In/Out logic
                if (!fadingOut) {
                    if (alpha < 240) { alpha += 15; if (alpha >= 240) { alpha = 240; KillTimer(hwnd, 1); } }
                } else {
                    if (alpha > 0) { alpha -= 15; if (alpha <= 0) { alpha = 0; KillTimer(hwnd, 1); DestroyWindow(hwnd); } }
                }
                SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
            } else if (wParam == 2) { // Time's up, start fade out
                KillTimer(hwnd, 2);
                fadingOut = true;
                SetTimer(hwnd, 1, 20, NULL); 
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
            RECT r; GetClientRect(hwnd, &r); RectF bgRect(0, 0, (float)r.right, (float)r.bottom);

            // Premium Red/Crimson Background
            SolidBrush bgBrush(Color(240, 220, 53, 69)); 
            GraphicsPath path; 
            float d = 8.0f * 2.0f;
            path.AddArc(bgRect.X, bgRect.Y, d, d, 180.0f, 90.0f); path.AddArc(bgRect.X + bgRect.Width - d, bgRect.Y, d, d, 270.0f, 90.0f);
            path.AddArc(bgRect.X + bgRect.Width - d, bgRect.Y + bgRect.Height - d, d, d, 0.0f, 90.0f); path.AddArc(bgRect.X, bgRect.Y + bgRect.Height - d, d, d, 90.0f, 90.0f);
            path.CloseFigure();
            g.FillPath(&bgBrush, &path);

            FontFamily ff(L"Segoe UI"); FontFamily ffi(L"Segoe MDL2 Assets");
            Font fTitle(&ff, 16, FontStyleBold, UnitPixel);
            Font fSub(&ff, 14, FontStyleRegular, UnitPixel);
            Font fIcon(&ffi, 22, FontStyleRegular, UnitPixel);

            SolidBrush bWhite(Color(255, 255, 255, 255));
            StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
            StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

            // Warning Icon & Dynamic Text
            g.DrawString(L"\xE7BA", -1, &fIcon, RectF(15.0f, 0.0f, 30.0f, bgRect.Height), &fC, &bWhite);
            if (td) {
                wstring titleStr = L"Blocked: " + td->blockedItem;
                wstring subStr = L"Active Profile: " + td->profileName;
                g.DrawString(titleStr.c_str(), -1, &fTitle, RectF(60.0f, 15.0f, bgRect.Width - 70.0f, 25.0f), &fL, &bWhite);
                g.DrawString(subStr.c_str(), -1, &fSub, RectF(60.0f, 40.0f, bgRect.Width - 70.0f, 20.0f), &fL, &bWhite);
            }
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_DESTROY: {
            if (td) delete td;
            PostQuitMessage(0); return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void ShowCustomBlockToast(const wstring& blockedItem, const wstring& profileName) {
    time_t now = std::time(nullptr);
    if (now - g_lastNotifyTime[blockedItem] < 10) return; // 10s Anti-spam cooldown
    g_lastNotifyTime[blockedItem] = now;

    std::thread([blockedItem, profileName]() {
        HINSTANCE hInstance = GetModuleHandle(NULL);
        WNDCLASSW wc = {0}; wc.lpfnWndProc = BlockToastWndProc; wc.hInstance = hInstance; wc.lpszClassName = L"RasFocusPremiumToast";
        RegisterClassW(&wc); 

        ToastData* td = new ToastData{blockedItem, profileName};
        int screenW = GetSystemMetrics(SM_CXSCREEN); int screenH = GetSystemMetrics(SM_CYSCREEN);
        int width = 340; int height = 80;
        int x = screenW - width - 20; int y = screenH - height - 60; // Hovering above taskbar

        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"RasFocusPremiumToast", L"", WS_POPUP,
            x, y, width, height, NULL, NULL, hInstance, td
        );
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }).detach();
}

static void SetInternetStateSch(bool block) {
    string cmd = block ? "ipconfig /release" : "ipconfig /renew";
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

static vector<wstring> hardcoreKeywords = { L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"hentai", L"milf", L"blowjob", L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers", L"onlyfans", L"chaturbate", L"spankbang", L"redtube", L"youporn", L"চটি", L"পর্ণ", L"সেক্স", L"নগ্ন", L"bhabi", L"chudai", L"bangla choti", L"panu", L"magi", L"choda", L"randi" };
static vector<wstring> romanticKeywords = { L"hot dance", L"seductive", L"item song", L"belly dance", L"kissing scene", L"bikini", L"sexy dance", L"cleavage", L"semi nude", L"lingerie", L"erotic", L"navel show" };
static vector<wstring> g_adultResourceSites;

static void LoadAdultSitesFromResourceOnce() {
    if (!g_adultResourceSites.empty()) return;
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(105), RT_RCDATA);
    if (hRes) {
        HGLOBAL hLoad = LoadResource(NULL, hRes);
        char* pData = (char*)LockResource(hLoad);
        DWORD size = SizeofResource(NULL, hRes);
        if (pData && size > 0) {
            string s(pData, size); stringstream ss(s); string line;
            while (getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) g_adultResourceSites.push_back(SafeUtf8ToWstring(line));
            }
        }
    }
    if (g_adultResourceSites.empty()) {
        g_adultResourceSites = { L"pornhub.com", L"xvideos.com", L"xnxx.com", L"xhamster.com", L"redtube.com" };
    }
}

static void AddRoundedRectPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f); path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f); path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}
static void AddRoundedRectPath(GraphicsPath& path, RectF rect, float r) { AddRoundedRectPath(path, rect.X, rect.Y, rect.Width, rect.Height, r); }

static GraphicsPath* GetSchRoundRectPath(RectF rect, float radius) {
    GraphicsPath* path = new GraphicsPath(); float x = rect.X, y = rect.Y, w = rect.Width, h = rect.Height, d = radius * 2.0f;
    path->AddArc(x, y, d, d, 180.0f, 90.0f); path->AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path->AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f); path->AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

static void DrawSchOverlaySpinner(Graphics& g, float x, float y, const std::wstring& value, bool hMinus, bool hPlus, Font* fIcon, Font* fBold) {
    SolidBrush bDark(Color(255, 50, 50, 50)), bWhite(Color(255, 255, 255, 255));
    Color colTeal(255, 12, 168, 176), colTealHover(255, 30, 185, 195);
    Pen pThin(Color(255, 200, 210, 220), 1.5f);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    RectF minusRect(x, y, 40.0f, 40.0f); GraphicsPath* mp = GetSchRoundRectPath(minusRect, 6.0f);
    SolidBrush mBr(hMinus ? colTealHover : colTeal); g.FillPath(&mBr, mp); delete mp; g.DrawString(L"-", -1, fBold, minusRect, &fC, &bWhite);

    RectF valRect(x + 45.0f, y, 60.0f, 40.0f); GraphicsPath* vp = GetSchRoundRectPath(valRect, 4.0f);
    SolidBrush vBr(Color(255, 248, 250, 252)); g.FillPath(&vBr, vp); g.DrawPath(&pThin, vp); delete vp; g.DrawString(value.c_str(), -1, fBold, valRect, &fC, &bDark);

    RectF plusRect(x + 110.0f, y, 40.0f, 40.0f); GraphicsPath* pp = GetSchRoundRectPath(plusRect, 6.0f);
    SolidBrush pBr(hPlus ? colTealHover : colTeal); g.FillPath(&pBr, pp); delete pp; g.DrawString(L"+", -1, fBold, plusRect, &fC, &bWhite);
}

// ==========================================
// 🟢 ADGUARD AUTO-INSTALL (NO ADMIN NEEDED)
// ==========================================

// AdGuard Extension ID for Chrome/Edge
static const wstring ADGUARD_EXT_ID = L"bgnkhfasdpejbgvffebekngafnmikojm";

// Method 1: Registry-based force install (needs admin — tries silently)
static void TryAdGuardViaRegistry(bool install) {
    wstring chromeReg = L"HKLM\\Software\\Policies\\Google\\Chrome\\ExtensionInstallForcelist";
    wstring edgeReg   = L"HKLM\\Software\\Policies\\Microsoft\\Edge\\ExtensionInstallForcelist";

    wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
    wstring batPath = wstring(tempPath) + L"ras_adguard_reg.bat";

    wstring bat = L"@echo off\n";
    if (install) {
        bat += L"reg add \"" + chromeReg + L"\" /v \"1\" /t REG_SZ /d \"" + ADGUARD_EXT_ID + L";https://clients2.google.com/service/update2/crx\" /f 2>nul\n";
        bat += L"reg add \"" + edgeReg   + L"\" /v \"1\" /t REG_SZ /d \"" + ADGUARD_EXT_ID + L";https://edge.microsoft.com/extensionwebstorebase/v1/crx\" /f 2>nul\n";
    } else {
        bat += L"reg delete \"" + chromeReg + L"\" /v \"1\" /f 2>nul\n";
        bat += L"reg delete \"" + edgeReg   + L"\" /v \"1\" /f 2>nul\n";
    }
    bat += L"del \"%~f0\"\n";

    wofstream out(batPath); out << bat; out.close();
    // runas = UAC prompt; if user cancels → falls to Method 2
    ShellExecuteW(NULL, L"runas", batPath.c_str(), NULL, NULL, SW_HIDE);
}

// Method 2: Per-user registry (NO admin needed, works immediately in Chrome/Edge)
static void TryAdGuardViaUserRegistry(bool install) {
    // Chrome: HKCU policy path (Chrome 85+ respects HKCU ExtensionInstallForcelist)
    wstring chromeKey = L"Software\\Policies\\Google\\Chrome\\ExtensionInstallForcelist";
    wstring edgeKey   = L"Software\\Policies\\Microsoft\\Edge\\ExtensionInstallForcelist";
    wstring extValue  = ADGUARD_EXT_ID + L";https://clients2.google.com/service/update2/crx";
    wstring extValueE = ADGUARD_EXT_ID + L";https://edge.microsoft.com/extensionwebstorebase/v1/crx";

    auto WriteReg = [&](const wstring& key, const wstring& val) {
        HKEY hk;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hk, L"1", 0, REG_SZ,
                (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
        }
    };
    auto DeleteReg = [&](const wstring& key) {
        HKEY hk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            RegDeleteValueW(hk, L"1");
            RegCloseKey(hk);
        }
    };

    if (install) {
        WriteReg(chromeKey, extValue);
        WriteReg(edgeKey,   extValueE);
    } else {
        DeleteReg(chromeKey);
        DeleteReg(edgeKey);
    }
}

// Method 3: Chrome/Edge External Extension JSON drop (no admin, works for Chrome)
static void TryAdGuardViaExternalJson(bool install) {
    // Chrome looks in: %LOCALAPPDATA%\Google\Chrome\User Data\External Extensions
    wchar_t localApp[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp);
    wstring chromeExtDir = wstring(localApp) + L"\\Google\\Chrome\\User Data\\External Extensions";
    wstring edgeExtDir   = wstring(localApp) + L"\\Microsoft\\Edge\\User Data\\External Extensions";
    wstring jsonFile     = ADGUARD_EXT_ID + L".json";
    wstring jsonContent  = L"{\n  \"external_update_url\": \"https://clients2.google.com/service/update2/crx\"\n}\n";

    if (install) {
        for (const auto& dir : { chromeExtDir, edgeExtDir }) {
            CreateDirectoryW(dir.c_str(), NULL);
            wofstream jf(dir + L"\\" + jsonFile);
            if (jf) { jf << jsonContent; jf.close(); }
        }
    } else {
        for (const auto& dir : { chromeExtDir, edgeExtDir }) {
            DeleteFileW((dir + L"\\" + jsonFile).c_str());
        }
    }
}

// Master function: tries all 3 methods
static void ManageAdGuardExtension(bool install) {
    // Method 2 (no admin) — always runs first, instant effect
    TryAdGuardViaUserRegistry(install);
    // Method 3 (no admin) — JSON drop for Chrome
    TryAdGuardViaExternalJson(install);
    // Method 1 (admin UAC) — if user accepts, strongest enforcement
    TryAdGuardViaRegistry(install);
}

// ==========================================
// --- DATA STRUCTURES & GLOBALS ---
// ==========================================
struct SchBlockItem { wstring name; bool isHoveredCross = false; };

struct FocusProfile {
    wstring profileName;
    vector<SchBlockItem> blockedWebsites;
    vector<SchBlockItem> blockedApps;
    vector<SchBlockItem> adultCustomKeywords; 
    
    bool isActive = false;
    int lockMode = 0; 
    time_t lockEndTime = 0; 
    wstring parentsPassword = L""; 
    
    bool activeDays[7] = {false};
    int startHour = 9, startMin = 0, endHour = 17, endMin = 0;
    bool blockInternet = false, blockUninstall = true;

    bool quickBlockYouTubeShorts = false, quickBlockFacebookReels = false, quickBlockAds = false, quickBlockInstagramReels = false;
    bool schAdultWeb = false, schHardcore = false, schRomantic = false, schStrictDns = false;
    
    bool hToggle = false, hEdit = false, hDel = false;
};

static vector<FocusProfile> g_profiles;
static bool isSchDataLoaded = false;
static float sch_tScroll = 0.0f, sch_cScroll = 0.0f;
static float s_cx = 0, s_cy = 0, s_cw = 800, s_ch = 600;

static int s_activeSubTab = 0; 
static float s_listScrollT[3] = {0, 0, 0}; 
static float s_listScrollC[3] = {0, 0, 0};
static float s_listScrollMax[3] = {0, 0, 0};
static bool s_scrollbarDragging = false;
static float s_scrollbarDragStartY = 0.0f, s_scrollbarDragStartScroll = 0.0f;

static vector<wstring> schCommonWebsites = { L"facebook.com", L"youtube.com", L"instagram.com", L"tiktok.com", L"reddit.com", L"twitter.com" };
static vector<wstring> schCommonApps = { L"chrome.exe", L"msedge.exe", L"telegram.exe", L"discord.exe", L"vlc.exe", L"Taskmgr.exe", L"cmd.exe" };
static vector<wstring> schCommonStoreApps = { L"Netflix", L"WhatsApp", L"Spotify", L"Instagram", L"TikTok", L"Facebook" }; 

struct QuickBlockBtn { wstring label; bool hovered = false; };
static vector<QuickBlockBtn> s_quickBlocks = { { L"YT Shorts" }, { L"FB Reels" }, { L"YT Ads" }, { L"IG Reels" } };
static vector<RectF> s_quickBlockRects;

static int editingProfileIdx = -1;
static wstring inpProfileName = L"", inpWeb = L"", inpApp = L"", inpKey = L""; 
static int activeInput = 0; 
static bool editDays[7] = {false};
static int editStH = 9, editStM = 0, editEnH = 17, editEnM = 0;
static bool editBlockInt = false, editBlockUninst = true;

static bool hAddProfileBtn = false, hoverSchWebCombo = false, hoverSchAppCombo = false, hoverSchStoreCombo = false, hoverSchModeDropdown = false;
static int hoverSchWebOptIdx = -1, hoverSchAppOptIdx = -1, hoverSchStoreOptIdx = -1, tempLockMode = 0;
static bool isSchWebComboOpen = false, isSchAppComboOpen = false, isSchStoreComboOpen = false, isSchModeDropdownOpen = false;

static int activeActionProfileIdx = -1;
static bool s_showTimeOverlay = false, s_showPassOverlay = false, s_showTextUnlockOverlay = false;
static int s_focusMonths = 0, s_focusDays = 0, s_focusHours = 1, s_focusMins = 0;
static bool s_hTimeMoM=false, s_hTimeMoP=false, s_hTimeDM=false, s_hTimeDP=false, s_hTimeHM=false, s_hTimeHP=false, s_hTimeMM=false, s_hTimeMP=false, s_hTimeStart=false, s_hTimeCancel=false;
static wstring s_inputPassText = L"", s_currentTypingText = L"";
static bool s_isPassInputActive = true, s_hPassInput = false, s_hPassConfirm = false, s_hPassCancel = false, s_isStoppingFocus = false, s_isTypingActive = true, s_hTextUnlockConfirm = false, s_hTextUnlockCancel = false;
static wstring s_targetUnlockText = L"To unlock this PC, you must realize that focus is the key to success. Avoid distractions, work hard, and never give up. True discipline comes from within. Success is not an accident, it is hard work, perseverance, learning, studying, sacrifice and most of all, love of what you are doing or learning to do. Type this exact text carefully to regain access and prove your self-control.";

static const Color ClrTeal(255, 12, 168, 176), ClrTealHover(255, 30, 185, 195), ClrDark(255, 50, 50, 50), ClrGrayText(255, 120, 120, 120), ClrWhite(255, 255, 255, 255), ClrBg(255, 248, 250, 252), ClrBgHover(255, 235, 248, 250), ClrRed(255, 231, 76, 60), ClrGreen(255, 90, 170, 20), ClrOverlay(180, 0, 0, 0), ClrDisabled(255, 200, 200, 200);

struct EditHitboxes {
    RectF saveBtn, cancelBtn, nextBtn, backBtn;
    RectF subTabRects[4];
    int hSubTab = -1;

    RectF nameInp, modeDrop, days[7], stH_Box, stM_Box, stAmPm, enH_Box, enM_Box, enAmPm, toggleInternet, toggleUninstall;
    bool hoverToggleInternet = false, hoverToggleUninstall = false;
    RectF webInp, webCombo, addWeb, appInp, appCombo, addApp, keyInp, addKey;
    RectF btnAddExe, btnAddStore, btnAddTitle;
    bool hBtnAddExe = false, hBtnAddStore = false, hBtnAddTitle = false;
    
    RectF cbAdultWeb, cbHardcore, cbRomantic, cbStrictDns;
    bool hCbAdultWeb = false, hCbHardcore = false, hCbRomantic = false, hCbStrictDns = false;
    
    vector<pair<RectF, int>> webDel, appDel, keyDel;
    RectF listAreas[3]; 
    RectF modeOpt[3];
    vector<RectF> webOpts, appOpts, storeOpts;

    bool hSave=false, hCancel=false, hNext=false, hBack=false, hStH=false, hStM=false, hStAmPm=false, hEnH=false, hEnM=false, hEnAmPm=false, hAddWeb=false, hAddApp=false, hAddKey=false, hOptSelf=false, hOptParents=false, hOptLongText=false;
    int hDay=-1;
} g_ehb;

// ==========================================
// 🟢 FIXED: BLOCKING LOGIC (GLOBAL SYNC)
// ==========================================
static vector<wstring> GetAllBlockPatterns(const wstring& entry) {
    vector<wstring> patterns; wstring e = entry;
    if (e.substr(0, 8) == L"https://") e = e.substr(8);
    if (e.substr(0, 7) == L"http://") e = e.substr(7);
    if (e.substr(0, 4) == L"www.") e = e.substr(4);
    patterns.push_back(e); patterns.push_back(L"www." + e);
    if (e == L"youtube.com") { patterns.push_back(L"m.youtube.com"); patterns.push_back(L"youtu.be"); patterns.push_back(L"yt3.ggpht.com"); } 
    else if (e == L"facebook.com") { patterns.push_back(L"m.facebook.com"); patterns.push_back(L"l.facebook.com"); }
    return patterns;
}

static void ApplyHostsFileBlocking(const vector<FocusProfile>& safeProfiles) {
    wchar_t sysDir[MAX_PATH]; GetSystemDirectoryW(sysDir, MAX_PATH);
    wstring hostsPath = wstring(sysDir) + L"\\drivers\\etc\\hosts";
    
    wifstream fin(hostsPath); fin.imbue(locale(fin.getloc(), new codecvt_utf8<wchar_t>));
    wstring cleanContent; 
    if (fin) { 
        wstring ln; 
        while (getline(fin, ln)) { if (ln.find(L"# RasFocus") == wstring::npos) cleanContent += ln + L"\n"; }
        fin.close(); 
    }

    vector<wstring> activePats;
    for (const auto& p : safeProfiles) {
        if (!p.isActive) continue;
        for (const auto& w : p.blockedWebsites) { auto pats = GetAllBlockPatterns(w.name); for (const auto& pt : pats) activePats.push_back(pt); }
        // 🟢 FIXED: Removed googlevideo.com to prevent breaking YouTube videos
        if (p.quickBlockAds) { 
            activePats.push_back(L"doubleclick.net"); 
            activePats.push_back(L"googleadservices.com"); 
            activePats.push_back(L"adservice.google.com"); 
        }
        if (p.blockInternet) { activePats.push_back(L"www.msftconnecttest.com"); activePats.push_back(L"ipv6.msftconnecttest.com"); }
    }

    for (const auto& pat : activePats) {
        if (pat.substr(0, 3) == L"kw:") continue; 
        wstring blockLine = L"0.0.0.0 " + pat + L" # RasFocus\n";
        if (cleanContent.find(blockLine) == wstring::npos) cleanContent += blockLine;
    }

    wofstream fout(hostsPath); fout.imbue(locale(fout.getloc(), new codecvt_utf8<wchar_t>));
    if (fout) { fout << cleanContent; fout.close(); }
    ShellExecuteW(NULL, L"open", L"cmd.exe", L"/c ipconfig /flushdns", NULL, SW_HIDE);
}

static void ApplyPACFileBlocking(const vector<FocusProfile>& safeProfiles) {
    wstring pacDir = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(pacDir.c_str(), NULL);
    wstring pacPath = pacDir + L"\\block.pac";

    vector<wstring> allKw; bool blockAllInternet = false;
    for (const auto& p : safeProfiles) {
        if (!p.isActive) continue;
        if (p.blockInternet) blockAllInternet = true; 
        for (const auto& w : p.blockedWebsites) { auto pats = GetAllBlockPatterns(w.name); for (const auto& pt : pats) { if (pt.substr(0, 3) == L"kw:") allKw.push_back(pt.substr(3)); } }
        for (const auto& kw : p.adultCustomKeywords) allKw.push_back(kw.name);
    }

    // 🟢 PAC Case Insensitive Bypass Fix
    wstring pac = L"function FindProxyForURL(url, host) {\n";
    pac += L"  var lowerUrl = url.toLowerCase();\n";
    if (blockAllInternet) { pac += L"  return \"PROXY 127.0.0.1:1\";\n"; } 
    else { 
        for (const auto& kw : allKw) {
            wstring lowerKw = kw; transform(lowerKw.begin(), lowerKw.end(), lowerKw.begin(), ::towlower);
            pac += L"  if (lowerUrl.indexOf(\"" + lowerKw + L"\") !== -1) return \"PROXY 127.0.0.1:1\";\n"; 
        }
    }
    pac += L"  return \"DIRECT\";\n}\n";

    wofstream fout(pacPath.c_str());
    fout.imbue(locale(fout.getloc(), new codecvt_utf8<wchar_t>));
    if (fout) { fout << pac; fout.close(); }

    if (!allKw.empty() || blockAllInternet) {
        wstring pacUrl = L"file://" + pacPath;
        wstring cmd1 = L"/c reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v AutoConfigURL /t REG_SZ /d \"" + pacUrl + L"\" /f";
        ShellExecuteW(NULL, L"open", L"cmd.exe", cmd1.c_str(), NULL, SW_HIDE);
        ShellExecuteW(NULL, L"open", L"cmd.exe", L"/c reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v ProxyEnable /t REG_DWORD /d 0 /f", NULL, SW_HIDE);
        InternetSetOptionW(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0); 
        InternetSetOptionW(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
    } else {
        ShellExecuteW(NULL, L"open", L"cmd.exe", L"/c reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v AutoConfigURL /f", NULL, SW_HIDE);
        InternetSetOptionW(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0); 
        InternetSetOptionW(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
    }
}

// 🟢 FIXED: Modified to pass profileName and show Notification
static void KillBlockedApps(const vector<SchBlockItem>& apps, const wstring& profileName) {
    if (apps.empty()) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            wstring procName = pe.szExeFile; wstring procLower = procName; transform(procLower.begin(), procLower.end(), procLower.begin(), ::towlower);
            for (const auto& app : apps) {
                wstring appLower = app.name; transform(appLower.begin(), appLower.end(), appLower.begin(), ::towlower);
                if (procLower == appLower) { 
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID); 
                    if (hProc) { 
                        TerminateProcess(hProc, 1); CloseHandle(hProc); 
                        ShowCustomBlockToast(procName, profileName); // 🟢 Trigger Notification
                    } 
                    break; 
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

// ==========================================
// 🟢 SAFE SEARCH ENFORCER (DNS + Hosts + Registry)
// ==========================================

static void ApplySafeSearchEnforcement(bool enable) {
    // ── 1. HOSTS FILE: redirect search engines to their SafeSearch IPs ──
    wchar_t sysDir[MAX_PATH]; GetSystemDirectoryW(sysDir, MAX_PATH);
    wstring hostsPath = wstring(sysDir) + L"\\drivers\\etc\\hosts";

    wifstream fin(hostsPath);
    fin.imbue(locale(fin.getloc(), new codecvt_utf8<wchar_t>));
    wstring cleanContent;
    if (fin) {
        wstring ln;
        while (getline(fin, ln)) {
            if (ln.find(L"# RasSafeSearch") == wstring::npos) cleanContent += ln + L"\n";
        }
        fin.close();
    }

    if (enable) {
        // Google SafeSearch: forcesafesearch.google.com = 216.239.38.120
        cleanContent += L"216.239.38.120 www.google.com # RasSafeSearch\n";
        cleanContent += L"216.239.38.120 google.com # RasSafeSearch\n";
        cleanContent += L"216.239.38.120 www.google.com.bd # RasSafeSearch\n";
        // Bing SafeSearch: strict.bing.com = 204.79.197.220
        cleanContent += L"204.79.197.220 www.bing.com # RasSafeSearch\n";
        cleanContent += L"204.79.197.220 bing.com # RasSafeSearch\n";
        // YouTube Restricted: restrict.youtube.com = 216.239.38.120
        cleanContent += L"216.239.38.120 www.youtube.com # RasSafeSearch\n";
        cleanContent += L"216.239.38.120 m.youtube.com # RasSafeSearch\n";
        cleanContent += L"216.239.38.120 youtube.com # RasSafeSearch\n";
        // DuckDuckGo SafeSearch
        cleanContent += L"54.156.15.249 duckduckgo.com # RasSafeSearch\n";
        cleanContent += L"54.156.15.249 www.duckduckgo.com # RasSafeSearch\n";
    }

    wofstream fout(hostsPath);
    fout.imbue(locale(fout.getloc(), new codecvt_utf8<wchar_t>));
    if (fout) { fout << cleanContent; fout.close(); }

    // ── 2. REGISTRY: Force SafeSearch via Chrome/Edge policy ──
    wstring chromeKey = L"Software\\Policies\\Google\\Chrome";
    wstring edgeKey   = L"Software\\Policies\\Microsoft\\Edge";

    auto WriteRegDword = [](const wstring& key, const wstring& name, DWORD val) {
        HKEY hk;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hk, name.c_str(), 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
            RegCloseKey(hk);
        }
        // Also try HKCU (no admin needed)
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hk, name.c_str(), 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
            RegCloseKey(hk);
        }
    };
    auto DeleteRegVal = [](const wstring& key, const wstring& name) {
        HKEY hk;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            RegDeleteValueW(hk, name.c_str()); RegCloseKey(hk);
        }
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            RegDeleteValueW(hk, name.c_str()); RegCloseKey(hk);
        }
    };

    if (enable) {
        // Chrome: ForceGoogleSafeSearch = 1, ForceYouTubeRestrict = 2 (Strict)
        WriteRegDword(chromeKey, L"ForceGoogleSafeSearch", 1);
        WriteRegDword(chromeKey, L"ForceYouTubeRestrict", 2);
        // Edge: ForceGoogleSafeSearch = 1, ForceYouTubeRestrict = 2
        WriteRegDword(edgeKey, L"ForceGoogleSafeSearch", 1);
        WriteRegDword(edgeKey, L"ForceYouTubeRestrict", 2);
        // Firefox via registry (for Firefox 60+)
        wstring ffKey = L"Software\\Policies\\Mozilla\\Firefox";
        WriteRegDword(ffKey, L"EnableTrackingProtection", 1);
    } else {
        DeleteRegVal(chromeKey, L"ForceGoogleSafeSearch");
        DeleteRegVal(chromeKey, L"ForceYouTubeRestrict");
        DeleteRegVal(edgeKey, L"ForceGoogleSafeSearch");
        DeleteRegVal(edgeKey, L"ForceYouTubeRestrict");
    }

    // ── 3. DNS FLUSH ──
    ShellExecuteW(NULL, L"open", L"cmd.exe", L"/c ipconfig /flushdns", NULL, SW_HIDE);

    // ── 4. BAT for admin-level DNS (Dynamic Connected Interface) ──
    if (enable) {
        wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
        wstring batPath = wstring(tempPath) + L"ras_safedns.bat";
        wstring bat = L"@echo off\n";
        bat += L"for /f \"skip=3 tokens=4*\" %%i in ('netsh interface show interface ^| findstr \"Connected\"') do (\n";
        bat += L"  netsh interface ipv4 set dnsservers name=\"%%j\" static 185.228.168.168 both 2>nul\n";
        bat += L"  netsh interface ipv4 add dnsservers name=\"%%j\" 185.228.169.168 index=2 2>nul\n";
        bat += L")\n";
        bat += L"ipconfig /flushdns\n";
        bat += L"del \"%~f0\"\n";
        wofstream out(batPath); out << bat; out.close();
        ShellExecuteW(NULL, L"runas", batPath.c_str(), NULL, NULL, SW_HIDE);
    } else {
        wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
        wstring batPath = wstring(tempPath) + L"ras_safedns_off.bat";
        wstring bat = L"@echo off\n";
        bat += L"for /f \"skip=3 tokens=4*\" %%i in ('netsh interface show interface ^| findstr \"Connected\"') do (\n";
        bat += L"  netsh interface ipv4 set dnsservers name=\"%%j\" dhcp 2>nul\n";
        bat += L")\n";
        bat += L"ipconfig /flushdns\n";
        bat += L"del \"%~f0\"\n";
        wofstream out(batPath); out << bat; out.close();
        ShellExecuteW(NULL, L"runas", batPath.c_str(), NULL, NULL, SW_HIDE);
    }
}

static void ApplyProfileBlockingWorker(vector<FocusProfile> all_profiles) {
    // Rebuild Hosts & PAC globally
    ApplyHostsFileBlocking(all_profiles); 
    ApplyPACFileBlocking(all_profiles);
    
    bool anyIntBlock = false; bool anyAdultDns = false;
    for (const auto& p : all_profiles) { 
        if (p.isActive && p.blockInternet) anyIntBlock = true; 
        if (p.isActive && p.schStrictDns) anyAdultDns = true;
    }
    SetInternetStateSch(anyIntBlock);
    ApplySafeSearchEnforcement(anyAdultDns);
    AdultBlock_ApplyForSchedule(anyAdultDns);
}

void ApplyProfileBlocking() {
    vector<FocusProfile> all_profiles_copy = g_profiles;
    std::thread([all_profiles_copy]() { ApplyProfileBlockingWorker(all_profiles_copy); }).detach();
}

// ==========================================
// --- HISTORY & SAVE/LOAD SYSTEM ---
// ==========================================
void LogHistoryToHiddenFolderSch(wstring action) {
    wstring baseDir = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(baseDir.c_str(), NULL);
    wstring historyDir = baseDir + L"\\History";
    CreateDirectoryW(historyDir.c_str(), NULL);
    SetFileAttributesW(historyDir.c_str(), FILE_ATTRIBUTE_HIDDEN); 
    wstring logFile = historyDir + L"\\schedule_activity_log.txt";

    wofstream out(logFile.c_str(), ios::app);
    time_t now = std::time(0); string dt = std::ctime(&now); dt.pop_back(); 
    if(out) out << L"[" << SafeUtf8ToWstring(dt) << L"] " << action << L"\n";
}

static wstring GetSchSavePath() {
    wstring folderPath = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(folderPath.c_str(), NULL);
    return folderPath + L"\\custom_profiles_v4.dat";
}

static void SaveProfiles() {
    wofstream out(GetSchSavePath().c_str()); 
    out.imbue(locale(out.getloc(), new codecvt_utf8<wchar_t>));
    if (!out) return;
    out << g_profiles.size() << L"\n";
    for (const auto& p : g_profiles) {
        out << p.profileName << L"\n" << p.isActive << L"\n" << p.lockMode << L"\n" << p.lockEndTime << L"\n" << p.parentsPassword << L"\n";
        for(int i=0; i<7; i++) out << p.activeDays[i] << L" ";
        out << L"\n" << p.startHour << L" " << p.startMin << L" " << p.endHour << L" " << p.endMin << L"\n";
        out << p.blockInternet << L" " << p.blockUninstall << L"\n";
        out << p.quickBlockYouTubeShorts << L" " << p.quickBlockFacebookReels << L" " << p.quickBlockAds << L" " << p.quickBlockInstagramReels << L"\n";
        out << p.schAdultWeb << L" " << p.schHardcore << L" " << p.schRomantic << L" " << p.schStrictDns << L"\n";
        out << p.blockedWebsites.size() << L"\n"; for (const auto& w : p.blockedWebsites) out << w.name << L"\n";
        out << p.blockedApps.size() << L"\n"; for (const auto& a : p.blockedApps) out << a.name << L"\n";
        out << p.adultCustomKeywords.size() << L"\n"; for (const auto& k : p.adultCustomKeywords) out << k.name << L"\n";
    } out.close();
}

static void LoadProfiles() {
    wifstream in(GetSchSavePath().c_str()); 
    in.imbue(locale(in.getloc(), new codecvt_utf8<wchar_t>));
    if (!in) {
        FocusProfile defProfile; defProfile.profileName = L"Deep Work Session";
        // 🟢 FIXED: Removed Hardcoded Blocks for safety
        defProfile.schAdultWeb = true; defProfile.schHardcore = true;
        // 🟢 FIXED: Active days turned off by default to prevent unwanted Auto-Activation
        for(int i=0; i<7; i++) defProfile.activeDays[i] = false;
        g_profiles.push_back(defProfile); return;
    }
    size_t pCount = 0; in >> pCount; in.ignore(); g_profiles.clear();
    for (size_t i = 0; i < pCount; ++i) {
        FocusProfile p; getline(in, p.profileName);
        in >> p.isActive >> p.lockMode >> p.lockEndTime; in.ignore(); getline(in, p.parentsPassword);
        for(int d=0; d<7; d++) in >> p.activeDays[d];
        in >> p.startHour >> p.startMin >> p.endHour >> p.endMin >> p.blockInternet >> p.blockUninstall;
        if (in >> p.quickBlockYouTubeShorts >> p.quickBlockFacebookReels >> p.quickBlockAds >> p.quickBlockInstagramReels >> p.schAdultWeb >> p.schHardcore >> p.schRomantic >> p.schStrictDns) { in.ignore(); } else { in.clear(); }
        size_t wCount = 0; if(in >> wCount) { in.ignore(); for(size_t j=0; j<wCount; ++j) { wstring w; getline(in, w); p.blockedWebsites.push_back({w, false}); } }
        size_t aCount = 0; if(in >> aCount) { in.ignore(); for(size_t j=0; j<aCount; ++j) { wstring a; getline(in, a); p.blockedApps.push_back({a, false}); } }
        size_t kCount = 0; if(in >> kCount) { in.ignore(); for(size_t j=0; j<kCount; ++j) { wstring k; getline(in, k); p.adultCustomKeywords.push_back({k, false}); } }
        g_profiles.push_back(p);
    } in.close();
}


// ==========================================
// 🟢 BACKGROUND OBSERVER THREAD 
// ==========================================
void ScheduleObserverThread() {
    int tickCount = 0;
    while (true) {
        Sleep(400); 
        tickCount++;

        // Cursor blink refresh: every 400ms যখন overlay open থাকে
        if (s_showPassOverlay || s_showTextUnlockOverlay || s_showTimeOverlay) {
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
        }

        if (tickCount % 4 != 0) continue; // Profile check: ~1600ms এ একবার
        if (!isSchDataLoaded) continue;

        bool profilesChanged = false; g_schMutex.lock(); 
        time_t t = std::time(nullptr); tm* now = std::localtime(&t); int currentDay = now->tm_wday; int prevDay = (currentDay + 6) % 7; int currentTotalMins = now->tm_hour * 60 + now->tm_min;

        for (size_t i = 0; i < g_profiles.size(); ++i) {
            auto& p = g_profiles[i];
            if (p.isActive && p.lockMode == 0 && p.lockEndTime > 0 && t >= p.lockEndTime) {
                p.isActive = false; p.lockEndTime = 0; profilesChanged = true;
            }
            bool hasSchedule = false; for (int d = 0; d < 7; d++) { if (p.activeDays[d]) hasSchedule = true; }
            
            // 🟢 FIXED: Overnight Precision Math
            if (hasSchedule && p.lockMode == 0 && p.lockEndTime == 0) {
                int startTotalMins = p.startHour * 60 + p.startMin; int endTotalMins = p.endHour * 60 + p.endMin;
                bool shouldBeActive = false;
                
                if (startTotalMins <= endTotalMins) {
                    if (p.activeDays[currentDay]) { shouldBeActive = (currentTotalMins >= startTotalMins && currentTotalMins < endTotalMins); }
                } else {
                    if (p.activeDays[currentDay] && currentTotalMins >= startTotalMins) { shouldBeActive = true; }
                    if (p.activeDays[prevDay] && currentTotalMins < endTotalMins) { shouldBeActive = true; }
                }
                if (shouldBeActive && !p.isActive) { p.isActive = true; profilesChanged = true; } 
                else if (!shouldBeActive && p.isActive) { p.isActive = false; profilesChanged = true; }
            }
        }
        if (profilesChanged) { ApplyProfileBlocking(); SaveProfiles(); if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); }
        vector<FocusProfile> safeProfiles = g_profiles; g_schMutex.unlock(); 

        for (const auto& p : safeProfiles) {
            if (p.isActive) {
                // 🟢 FIXED: Uninstall Protection Background Killer
                vector<SchBlockItem> appsToKill = p.blockedApps;
                if (p.blockUninstall) {
                    appsToKill.push_back({L"taskmgr.exe"}); appsToKill.push_back({L"resmon.exe"}); appsToKill.push_back({L"perfmon.exe"});
                }
                if (!appsToKill.empty()) KillBlockedApps(appsToKill, p.profileName); // 🟢 Updated here

                HWND hActive = GetForegroundWindow();
                if (hActive) {
                    wchar_t windowTitle[512] = { 0 };
                    if (GetWindowTextW(hActive, windowTitle, 512) > 0) {
                        wstring lowerTitle = windowTitle; for (auto& c : lowerTitle) c = towlower(c);
                        bool triggerBlock = false;
                        if (p.quickBlockYouTubeShorts && lowerTitle.find(L"youtube") != wstring::npos && lowerTitle.find(L"shorts") != wstring::npos) triggerBlock = true;
                        if (p.quickBlockFacebookReels && lowerTitle.find(L"facebook") != wstring::npos && lowerTitle.find(L"reels") != wstring::npos) triggerBlock = true;
                        if (p.quickBlockInstagramReels && lowerTitle.find(L"instagram") != wstring::npos && lowerTitle.find(L"reels") != wstring::npos) triggerBlock = true;

                        if (p.schHardcore && !triggerBlock) { for (const auto& kw : hardcoreKeywords) { if (lowerTitle.find(kw) != wstring::npos) { triggerBlock = true; break; } } }
                        if (p.schRomantic && !triggerBlock) { for (const auto& kw : romanticKeywords) { if (lowerTitle.find(kw) != wstring::npos) { triggerBlock = true; break; } } }
                        if (p.schAdultWeb && !triggerBlock) {
                            LoadAdultSitesFromResourceOnce();
                            for (const auto& site : g_adultResourceSites) {
                                size_t dot = site.find(L"."); wstring core = (dot != wstring::npos) ? site.substr(0, dot) : site;
                                if (core.length() > 2 && lowerTitle.find(core) != wstring::npos) { triggerBlock = true; break; }
                            }
                        }
                        if (!triggerBlock) {
                            for (const auto& kw : p.adultCustomKeywords) {
                                wstring lowerKw = kw.name; for (auto& c : lowerKw) c = towlower(c);
                                if (lowerTitle.find(lowerKw) != wstring::npos) { triggerBlock = true; break; }
                            }
                        }
                        
                        // 🟢 FIXED: Uninstall Title Protection
                        if (p.blockUninstall && !triggerBlock) {
                            if (lowerTitle.find(L"task manager") != wstring::npos || lowerTitle.find(L"programs and features") != wstring::npos || lowerTitle.find(L"apps & features") != wstring::npos || lowerTitle.find(L"uninstall") != wstring::npos) triggerBlock = true;
                        }
                        // 🟢 FIXED: Time Cheat Protection
                        if (p.lockMode == 0 && p.lockEndTime > 0 && !triggerBlock) {
                            if (lowerTitle.find(L"date and time") != wstring::npos || lowerTitle.find(L"time & language") != wstring::npos) triggerBlock = true;
                        }

                        // 🟢 Updated here for Custom Notification
                        if (triggerBlock) {
                            CloseActiveTabOnly(hActive);
                            
                            wstring blockReason = L"Restricted Content";
                            if (lowerTitle.find(L"shorts") != wstring::npos) blockReason = L"YouTube Shorts";
                            else if (lowerTitle.find(L"reels") != wstring::npos) blockReason = L"Facebook/IG Reels";
                            else if (lowerTitle.find(L"task manager") != wstring::npos || lowerTitle.find(L"uninstall") != wstring::npos) blockReason = L"System Settings";
                            
                            ShowCustomBlockToast(blockReason, p.profileName);
                        }
                    }
                }
            }
        }
    }
}

// ==========================================
// --- DRAWING LOGIC ---
// ==========================================
void DrawScheduleBlocksTab(Graphics& g, float x, float y, float w, float h) {
    if (!isSchDataLoaded) { 
        LoadProfiles(); isSchDataLoaded = true; 
        if (!isSchThreadRunning) { std::thread(ScheduleObserverThread).detach(); isSchThreadRunning = true; }
    }
    
    std::lock_guard<std::mutex> lock(g_schMutex);
    s_cx = x; s_cy = y; s_cw = w; s_ch = h;
    sch_cScroll += (sch_tScroll - sch_cScroll) * 0.12f;

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 28, FontStyleBold, UnitPixel);
    Font fCardTitle(&ff, 20, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 16, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 16, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 14, FontStyleRegular, UnitPixel);
    Font fSmallBold(&ff, 13, FontStyleBold, UnitPixel);
    FontFamily ffi(L"Segoe MDL2 Assets"); Font fIcon(&ffi, 22, FontStyleRegular, UnitPixel); Font fSmallIcon(&ffi, 16, FontStyleRegular, UnitPixel);

    SolidBrush bDark(ClrDark); SolidBrush bWhite(ClrWhite); SolidBrush bGray(ClrGrayText); SolidBrush bTeal(ClrTeal); SolidBrush bRed(ClrRed); SolidBrush bGreen(ClrGreen); SolidBrush bBgHover(ClrBgHover); SolidBrush bBg(ClrBg);
    Pen pThin(Color(255, 230, 235, 240), 1.5f); Pen pTeal(ClrTeal, 2.0f);
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fTL; fTL.SetAlignment(StringAlignmentNear); fTL.SetLineAlignment(StringAlignmentNear);

    g.DrawString(L"Focus Profiles", -1, &fTitle, RectF(x + 25.0f, y + 20.0f, 350.0f, 40.0f), &fL, &bDark);
    g.DrawString(L"Create dedicated schedules with advanced app, web & internet lock.", -1, &fNorm, RectF(x + 25.0f, y + 65.0f, 700.0f, 25.0f), &fL, &bGray);

    RectF addBtnRect(x + w - 230.0f, y + 20.0f, 210.0f, 45.0f); GraphicsPath* aP = GetSchRoundRectPath(addBtnRect, 6.0f);
    SolidBrush aBr(hAddProfileBtn ? ClrTealHover : ClrTeal); g.FillPath(&aBr, aP); delete aP; g.DrawString(L"+ Add Blocking Profile", -1, &fBold, addBtnRect, &fC, &bWhite);

    float cardGap = 25.0f;
    float cardW = (w - (cardGap * 3.0f)) / 2.0f; 
    float cardH = 190.0f; 
    float startX = x + cardGap; float startY = y + 110.0f - sch_cScroll;
    Region oldClip; g.GetClip(&oldClip); g.SetClip(RectF(x, y + 105.0f, w, h - 105.0f));

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        float cX = startX + (i % 2) * (cardW + cardGap); float cY = startY + (i / 2) * (cardH + cardGap);
        if (cY > y + h || cY + cardH < y + 100.0f) continue; 
        
        RectF cardRect(cX, cY, cardW, cardH); GraphicsPath* cP = GetSchRoundRectPath(cardRect, 8.0f); 
        g.FillPath(&bWhite, cP); g.DrawPath(g_profiles[i].isActive ? &pTeal : &pThin, cP); delete cP;

        g.DrawString(L"\xE82D", -1, &fIcon, RectF(cX + 18.0f, cY + 18.0f, 30.0f, 30.0f), &fL, g_profiles[i].isActive ? &bTeal : &bGray); 
        g.DrawString(g_profiles[i].profileName.c_str(), -1, &fCardTitle, RectF(cX + 55.0f, cY + 18.0f, cardW - 70.0f, 30.0f), &fL, &bDark);
        
        wstring statStr = L"Blocked: " + to_wstring(g_profiles[i].blockedWebsites.size()) + L" Web, " + to_wstring(g_profiles[i].blockedApps.size()) + L" App";
        g.DrawString(statStr.c_str(), -1, &fNorm, RectF(cX + 18.0f, cY + 60.0f, cardW - 36.0f, 20.0f), &fL, &bGray);

        wstring modeName = (g_profiles[i].lockMode == 1) ? L"Mode: Parents Control" : ((g_profiles[i].lockMode == 2) ? L"Mode: Long Text Unlock" : L"Mode: Self Control");
        g.DrawString(modeName.c_str(), -1, &fSmallBold, RectF(cX + 18.0f, cY + 85.0f, cardW - 36.0f, 20.0f), &fL, &bTeal);

        RectF togRect(cX + 18.0f, cY + 130.0f, 55.0f, 30.0f); GraphicsPath* tp = GetSchRoundRectPath(togRect, 15.0f);
        SolidBrush tBg(g_profiles[i].isActive ? ClrGreen : ClrGrayText); g.FillPath(&tBg, tp); delete tp;
        g.FillEllipse(&bWhite, g_profiles[i].isActive ? (cX + 18.0f + 55.0f - 28.0f) : (cX + 18.0f + 2.0f), cY + 130.0f + 2.0f, 26.0f, 26.0f);

        wstring toggleTxt = g_profiles[i].isActive ? L"Active" : L"Inactive";
        if (g_profiles[i].isActive && g_profiles[i].lockMode == 0 && g_profiles[i].lockEndTime > 0) toggleTxt = L"Locked (Timer)";
        else if (g_profiles[i].isActive && g_profiles[i].lockMode == 0 && g_profiles[i].lockEndTime == 0) toggleTxt = L"Locked (Auto)";
        g.DrawString(toggleTxt.c_str(), -1, &fBold, RectF(cX + 85.0f, cY + 130.0f, 120.0f, 30.0f), &fL, g_profiles[i].isActive ? &bTeal : &bDark);

        RectF editRect(cX + cardW - 145.0f, cY + 130.0f, 70.0f, 34.0f); GraphicsPath* ep = GetSchRoundRectPath(editRect, 6.0f);
        SolidBrush eBr(g_profiles[i].hEdit ? ClrBgHover : ClrBg); g.FillPath(&eBr, ep); g.DrawPath(&pThin, ep); delete ep; g.DrawString(L"Edit", -1, &fBold, editRect, &fC, &bDark);

        RectF delRect(cX + cardW - 65.0f, cY + 130.0f, 48.0f, 34.0f); GraphicsPath* dp = GetSchRoundRectPath(delRect, 6.0f);
        if (g_profiles[i].isActive) {
            g.FillPath(&bGray, dp); g.DrawPath(&pThin, dp); delete dp; g.DrawString(L"\xE74D", -1, &fIcon, delRect, &fC, &bWhite);
        } else {
            SolidBrush dBr(g_profiles[i].hDel ? ClrRed : ClrWhite); g.FillPath(&dBr, dp); g.DrawPath(&pThin, dp); delete dp; g.DrawString(L"\xE74D", -1, &fIcon, delRect, &fC, g_profiles[i].hDel ? &bWhite : &bRed);
        }
    }
    g.SetClip(&oldClip);

    // =========================================================================
    // 🟢 TOP TAB DESIGN OVERLAY: FULL-WINDOW EDIT PROFILE
    // =========================================================================
    if (editingProfileIdx != -1) {
        float ovX = x, ovY = y, ovW = w, ovH = h;
        SolidBrush bgEditor(Color(255, 248, 250, 252)); g.FillRectangle(&bgEditor, ovX, ovY, ovW, ovH);

        float topH = 75.0f; SolidBrush topBg(Color(255, 255, 255, 255)); g.FillRectangle(&topBg, ovX, ovY, ovW, topH);
        Pen pDiv(Color(255, 220, 228, 238), 1.0f); g.DrawLine(&pDiv, ovX, ovY + topH, ovX + ovW, ovY + topH);

        struct TopNavItem { const wchar_t* icon; const wchar_t* label; };
        TopNavItem navItems[4] = { { L"\xE713", L"Basic & Time" }, { L"\xE7BA", L"Quick Settings" }, { L"\xE71D", L"Custom Lists" }, { L"\xE72E", L"Adult Block" } };
        float tabW = 160.0f; float startTabX = ovX + 25.0f;
        for (int i = 0; i < 4; i++) {
            float tX = startTabX + (i * (tabW + 15.0f)); g_ehb.subTabRects[i] = RectF(tX, ovY + 20.0f, tabW, 40.0f);
            bool isActive = (s_activeSubTab == i); bool isHov = (g_ehb.hSubTab == i);
            if (isActive) { SolidBrush activeBg(Color(30, 12, 168, 176)); GraphicsPath* tp = GetSchRoundRectPath(g_ehb.subTabRects[i], 8.0f); g.FillPath(&activeBg, tp); delete tp; SolidBrush accent(ClrTeal); g.FillRectangle(&accent, tX + 15.0f, ovY + 71.0f, tabW - 30.0f, 4.0f); } 
            else if (isHov) { SolidBrush hovBg(Color(255, 240, 243, 248)); GraphicsPath* tp = GetSchRoundRectPath(g_ehb.subTabRects[i], 8.0f); g.FillPath(&hovBg, tp); delete tp; }
            SolidBrush iconClr(isActive ? ClrTeal : Color(255, 130, 140, 150)); SolidBrush labelClr(isActive ? ClrTeal : Color(255, 80, 80, 80));
            g.DrawString(navItems[i].icon, -1, &fSmallIcon, RectF(tX + 12.0f, ovY + 28.0f, 24.0f, 24.0f), &fL, &iconClr); g.DrawString(navItems[i].label, -1, &fBold, RectF(tX + 40.0f, ovY + 30.0f, tabW - 45.0f, 20.0f), &fL, &labelClr);
        }

        float contX = ovX + 30.0f; float contW = ovW - 60.0f; float contentY = ovY + topH + 25.0f;
        float cardX = contX; float cardW_inner = contW;
        float footerY = ovY + ovH - 68.0f; SolidBrush footerBg(Color(255, 255, 255, 255)); g.FillRectangle(&footerBg, ovX, footerY, ovW, 68.0f); g.DrawLine(&pDiv, ovX, footerY, ovX + ovW, footerY);

        g_ehb.backBtn = RectF(); g_ehb.nextBtn = RectF();
        if (s_activeSubTab > 0) {
            g_ehb.backBtn = RectF(contX, footerY + 14.0f, 105.0f, 38.0f); GraphicsPath* bbp = GetSchRoundRectPath(g_ehb.backBtn, 8.0f); SolidBrush bbBr(g_ehb.hBack ? ClrBgHover : Color(255, 240, 243, 248)); g.FillPath(&bbBr, bbp); g.DrawPath(&pDiv, bbp); delete bbp; 
            g.DrawString(L"Back", -1, &fBold, g_ehb.backBtn, &fC, &bDark);
        }
        if (s_activeSubTab < 3) {
            float nextX = (s_activeSubTab > 0) ? contX + 120.0f : contX; g_ehb.nextBtn = RectF(nextX, footerY + 14.0f, 105.0f, 38.0f); GraphicsPath* nbp = GetSchRoundRectPath(g_ehb.nextBtn, 8.0f); SolidBrush nbBr(g_ehb.hNext ? ClrTealHover : ClrTeal); g.FillPath(&nbBr, nbp); delete nbp; 
            g.DrawString(L"Next", -1, &fBold, g_ehb.nextBtn, &fC, &bWhite);
        }
        g_ehb.cancelBtn = RectF(ovX + ovW - 275.0f, footerY + 14.0f, 110.0f, 38.0f); GraphicsPath* cvp = GetSchRoundRectPath(g_ehb.cancelBtn, 8.0f); SolidBrush cvBr(g_ehb.hCancel ? ClrBgHover : Color(255, 240, 243, 248)); g.FillPath(&cvBr, cvp); g.DrawPath(&pDiv, cvp); delete cvp; 
        g.DrawString(L"Cancel", -1, &fBold, g_ehb.cancelBtn, &fC, &bDark);
        
        g_ehb.saveBtn = RectF(ovX + ovW - 150.0f, footerY + 14.0f, 120.0f, 38.0f); GraphicsPath* svp = GetSchRoundRectPath(g_ehb.saveBtn, 8.0f); SolidBrush svBr(g_ehb.hSave ? ClrTealHover : ClrTeal); g.FillPath(&svBr, svp); delete svp; 
        g.DrawString(L"Save", -1, &fBold, g_ehb.saveBtn, &fC, &bWhite);
        
        g.SetClip(RectF(ovX, ovY + topH + 1.0f, ovW, footerY - (ovY + topH + 1.0f)));

        if (s_activeSubTab == 0) {
            g.DrawString(L"Profile Name", -1, &fSmall, RectF(cardX + 18.0f, contentY + 10.0f, 100.0f, 22.0f), &fL, &bGray);
            g_ehb.nameInp = RectF(cardX + 125.0f, contentY + 6.0f, 230.0f, 36.0f);
            GraphicsPath* np = GetSchRoundRectPath(g_ehb.nameInp, 7.0f); g.FillPath(activeInput == 1 ? &bWhite : &bBg, np); g.DrawPath(activeInput == 1 ? &pTeal : &pDiv, np); delete np;
            if (inpProfileName.empty() && activeInput != 1) { g.DrawString(L"e.g. Study Time", -1, &fNorm, RectF(g_ehb.nameInp.X+10.0f, g_ehb.nameInp.Y, g_ehb.nameInp.Width-10.0f, g_ehb.nameInp.Height), &fL, &bGray); } else { g.DrawString(inpProfileName.c_str(), -1, &fNorm, RectF(g_ehb.nameInp.X+10.0f, g_ehb.nameInp.Y, g_ehb.nameInp.Width-10.0f, g_ehb.nameInp.Height), &fL, &bDark); if (activeInput == 1 && (GetTickCount()/500)%2==0) { Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpProfileName.c_str(), -1, &fNorm, PointF(0,0), &bR); g.FillRectangle(&bDark, g_ehb.nameInp.X+12.0f+(inpProfileName.empty()?0:bR.Width), g_ehb.nameInp.Y+8.0f, 1.5f, 20.0f); } }
            
            g.DrawString(L"Lock Mode", -1, &fSmall, RectF(cardX + 375.0f, contentY + 10.0f, 90.0f, 22.0f), &fL, &bGray);
            g_ehb.modeDrop = RectF(cardX + 460.0f, contentY + 6.0f, 210.0f, 36.0f); GraphicsPath* mdp = GetSchRoundRectPath(g_ehb.modeDrop, 7.0f); SolidBrush dropBg(hoverSchModeDropdown ? ClrBgHover : ClrWhite); g.FillPath(&dropBg, mdp); g.DrawPath(&pDiv, mdp); delete mdp;
            wstring curModeTxt = (tempLockMode == 1) ? L"Parents Control" : ((tempLockMode == 2) ? L"Long Text Unlock" : L"Self Control");
            g.DrawString(curModeTxt.c_str(), -1, &fNorm, RectF(g_ehb.modeDrop.X+12.0f, g_ehb.modeDrop.Y, g_ehb.modeDrop.Width-34.0f, g_ehb.modeDrop.Height), &fL, &bDark); g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(g_ehb.modeDrop.X+g_ehb.modeDrop.Width-30.0f, g_ehb.modeDrop.Y, 28.0f, g_ehb.modeDrop.Height), &fC, &bTeal);

            contentY += 80.0f; 
            
            g.DrawString(L"Active Days", -1, &fSmall, RectF(cardX + 18.0f, contentY + 10.0f, 90.0f, 22.0f), &fL, &bGray);
            wstring dLabels[] = {L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"};
            for (int d = 0; d < 7; d++) {
                g_ehb.days[d] = RectF(cardX + 115.0f + (d * 45.0f), contentY + 5.0f, 38.0f, 38.0f); GraphicsPath* dP = GetSchRoundRectPath(g_ehb.days[d], 8.0f); SolidBrush dBr(editDays[d] ? ClrTeal : (g_ehb.hDay == d ? Color(255,235,248,250) : ClrWhite)); g.FillPath(&dBr, dP); g.DrawPath(editDays[d] ? &pTeal : &pDiv, dP); delete dP; g.DrawString(dLabels[d].c_str(), -1, &fSmallBold, g_ehb.days[d], &fC, editDays[d] ? &bWhite : &bDark);
            }
            g.DrawString(L"Session Time", -1, &fSmall, RectF(cardX + 18.0f, contentY + 65.0f, 95.0f, 22.0f), &fL, &bGray);
            auto DrawModernTimeBox = [&](float tx, float ty, const wstring& lbl, int h, int m, RectF& hBox, RectF& mBox, RectF& ampmBtn, bool hH, bool hM, bool hAmPm) {
                g.DrawString(lbl.c_str(), -1, &fSmallBold, RectF(tx, ty, 45.0f, 34.0f), &fC, &bGray); int dispH = h % 12; if (dispH == 0) dispH = 12; wstring ampmStr = (h >= 12) ? L"PM" : L"AM";
                hBox = RectF(tx + 48.0f, ty, 44.0f, 34.0f); GraphicsPath hp; AddRoundedRectPath(hp, hBox.X, hBox.Y, hBox.Width, hBox.Height, 7.0f); SolidBrush hbBr(hH ? ClrBgHover : ClrWhite); g.FillPath(&hbBr, &hp); g.DrawPath(&pDiv, &hp); g.DrawString((dispH < 10 ? L"0" + to_wstring(dispH) : to_wstring(dispH)).c_str(), -1, &fBold, hBox, &fC, &bDark);
                g.DrawString(L":", -1, &fBold, RectF(tx + 93.0f, ty, 12.0f, 34.0f), &fC, &bDark); mBox = RectF(tx + 105.0f, ty, 44.0f, 34.0f); GraphicsPath mp; AddRoundedRectPath(mp, mBox.X, mBox.Y, mBox.Width, mBox.Height, 7.0f); SolidBrush mbBr(hM ? ClrBgHover : ClrWhite); g.FillPath(&mbBr, &mp); g.DrawPath(&pDiv, &mp); g.DrawString((m < 10 ? L"0" + to_wstring(m) : to_wstring(m)).c_str(), -1, &fBold, mBox, &fC, &bDark);
                ampmBtn = RectF(tx + 155.0f, ty, 44.0f, 34.0f); GraphicsPath ap; AddRoundedRectPath(ap, ampmBtn.X, ampmBtn.Y, ampmBtn.Width, ampmBtn.Height, 7.0f); SolidBrush aBr(hAmPm ? ClrTealHover : (h >= 12 ? Color(230, 12, 168, 176) : ClrWhite)); g.FillPath(&aBr, &ap); g.DrawPath(h >= 12 ? &pTeal : &pDiv, &ap); g.DrawString(ampmStr.c_str(), -1, &fBold, ampmBtn, &fC, (h >= 12) ? &bWhite : &bDark);
            };
            DrawModernTimeBox(cardX + 115.0f, contentY + 58.0f, L"Start", editStH, editStM, g_ehb.stH_Box, g_ehb.stM_Box, g_ehb.stAmPm, g_ehb.hStH, g_ehb.hStM, g_ehb.hStAmPm); g.DrawString(L"\xE72A", -1, &fSmallIcon, RectF(cardX + 340.0f, contentY + 61.0f, 24.0f, 28.0f), &fC, &bGray); DrawModernTimeBox(cardX + 365.0f, contentY + 58.0f, L"End", editEnH, editEnM, g_ehb.enH_Box, g_ehb.enM_Box, g_ehb.enAmPm, g_ehb.hEnH, g_ehb.hEnM, g_ehb.hEnAmPm);
        }
        else if (s_activeSubTab == 1) {
            float cbWidth = (cardW_inner - 30.0f) / 2.0f;
            auto DrawCb = [&](RectF& outHitbox, float cx, float cy, const wstring& label, const wstring& desc, bool val, bool hov) {
                outHitbox = RectF(cx, cy, cbWidth - 15.0f, 52.0f); if (hov) { GraphicsPath* hp = GetSchRoundRectPath(outHitbox, 8.0f); g.FillPath(&bBgHover, hp); delete hp; }
                RectF togBg(cx + 12.0f, cy + 15.0f, 44.0f, 24.0f); GraphicsPath* tp = GetSchRoundRectPath(togBg, 12.0f); SolidBrush tBg(val ? ClrTeal : Color(255, 200, 210, 220)); g.FillPath(&tBg, tp); delete tp; g.FillEllipse(&bWhite, val ? togBg.X + togBg.Width - 22.0f : togBg.X + 2.0f, togBg.Y + 2.0f, 20.0f, 20.0f);
                g.DrawString(label.c_str(), -1, &fBold, RectF(cx + 62.0f, cy + 8.0f, cbWidth - 75.0f, 22.0f), &fL, val ? &bTeal : &bDark); g.DrawString(desc.c_str(),  -1, &fSmall, RectF(cx + 62.0f, cy + 30.0f, cbWidth - 75.0f, 18.0f), &fL, &bGray);
            };
            DrawCb(g_ehb.toggleInternet, cardX + 15.0f, contentY + 10.0f, L"Block Internet", L"Disables all network access", editBlockInt, g_ehb.hoverToggleInternet); 
            DrawCb(g_ehb.toggleUninstall, cardX + 15.0f + cbWidth, contentY + 10.0f, L"Block Uninstall", L"Locks Task Manager & apps", editBlockUninst, g_ehb.hoverToggleUninstall);

            contentY += 80.0f; 
            
            g.DrawString(L"Quick Content Filters (Chrome, Edge, Firefox, Brave)", -1, &fBold, RectF(cardX + 18.0f, contentY, cardW_inner - 36.0f, 24.0f), &fL, &bDark);

            float qbX = cardX + 18.0f; float qbY = contentY + 35.0f; float qbW = 138.0f; float qbH = 44.0f; float qbGap = 14.0f; s_quickBlockRects.resize(s_quickBlocks.size()); const wchar_t* qbIcons[4] = { L"\xE714", L"\xE8F3", L"\xE90A", L"\xE718" };
            const wchar_t* qbFullLabels[4] = { L"YouTube Shorts", L"Facebook Reels", L"Web & YT Ads", L"Instagram Reels" };
            for (size_t qi = 0; qi < s_quickBlocks.size(); ++qi) {
                RectF qbRect(qbX + qi * (qbW + qbGap), qbY, qbW, qbH); s_quickBlockRects[qi] = qbRect; bool alreadyAdded = false;
                if (editingProfileIdx >= 0) { if (qi == 0) alreadyAdded = g_profiles[editingProfileIdx].quickBlockYouTubeShorts; else if (qi == 1) alreadyAdded = g_profiles[editingProfileIdx].quickBlockFacebookReels; else if (qi == 2) alreadyAdded = g_profiles[editingProfileIdx].quickBlockAds; else if (qi == 3) alreadyAdded = g_profiles[editingProfileIdx].quickBlockInstagramReels; }
                GraphicsPath* qp = GetSchRoundRectPath(qbRect, 8.0f); if (alreadyAdded) { g.FillPath(&bTeal, qp); } else { SolidBrush qbBg(s_quickBlocks[qi].hovered ? ClrBgHover : ClrWhite); g.FillPath(&qbBg, qp); g.DrawPath(&pDiv, qp); } delete qp;
                SolidBrush* txtClr = alreadyAdded ? &bWhite : &bDark; SolidBrush iconClrQb(alreadyAdded ? Color(255,255,255,255) : Color(255,12,168,176));
                g.DrawString(qbIcons[qi], -1, &fSmallIcon, RectF(qbRect.X + 8.0f, qbRect.Y, 20.0f, qbH), &fL, &iconClrQb);
                g.DrawString(qbFullLabels[qi], -1, &fSmallBold, RectF(qbRect.X + 30.0f, qbRect.Y, qbW - 60.0f, qbH), &fL, txtClr);
                RectF togBgQ(qbRect.X + qbW - 36.0f, qbRect.Y + 12.0f, 28.0f, 16.0f); GraphicsPath* tpQ = GetSchRoundRectPath(togBgQ, 8.0f); SolidBrush tBgQ(alreadyAdded ? Color(255,255,255,255) : Color(80,255,255,255)); g.FillPath(&tBgQ, tpQ); delete tpQ; SolidBrush tDotQ(alreadyAdded ? ClrTeal : Color(180,150,160,170)); g.FillEllipse(&tDotQ, alreadyAdded ? togBgQ.X + togBgQ.Width - 14.0f : togBgQ.X + 2.0f, togBgQ.Y + 2.0f, 12.0f, 12.0f);
            }
        }
        else if (s_activeSubTab == 2) {
            vector<SchBlockItem>* cWebs = nullptr; vector<SchBlockItem>* cApps = nullptr; if(editingProfileIdx >= 0) { cWebs = &g_profiles[editingProfileIdx].blockedWebsites; cApps = &g_profiles[editingProfileIdx].blockedApps; }
            float listAreaH = footerY - contentY - 15.0f;
            float colGap = 20.0f; float colW = (cardW_inner - (colGap * 3.0f)) / 2.0f;

            auto DrawListCol = [&](int colIdx, float colX, const wstring& title, const wstring& icon, const wstring& ph, wstring& inpStr, int inpIdx, vector<SchBlockItem>* list, RectF& outInp, RectF* outCombo, RectF& outAdd, bool hovCombo, bool hovAdd, vector<pair<RectF,int>>& outDel) {
                g.DrawString(icon.c_str(), -1, &fSmallIcon, RectF(colX, contentY + 10.0f, 22.0f, 26.0f), &fL, &bTeal); g.DrawString(title.c_str(), -1, &fBold, RectF(colX + 26.0f, contentY + 8.0f, colW - 26.0f, 28.0f), &fL, &bDark);
                wstring countTxt = list ? (to_wstring(list->size()) + L" item" + (list->size() != 1 ? L"s" : L"")) : L"0 items"; SolidBrush countBg(ClrTeal); RectF countR(colX + colW - 60.0f, contentY + 12.0f, 55.0f, 22.0f); GraphicsPath* cp = GetSchRoundRectPath(countR, 11.0f); g.FillPath(&countBg, cp); delete cp; g.DrawString(countTxt.c_str(), -1, &fSmall, countR, &fC, &bWhite); g.DrawLine(&pDiv, colX, contentY + 42.0f, colX + colW, contentY + 42.0f);
                float inpY = contentY + 52.0f; float comboW = outCombo ? 36.0f : 0.0f; float addW = 72.0f; float gap = 6.0f; float inpW = colW - comboW - addW - (outCombo ? gap * 2.0f : gap);
                outInp = RectF(colX, inpY, inpW, 38.0f); GraphicsPath* ip = GetSchRoundRectPath(outInp, 8.0f); SolidBrush inpBg(activeInput == inpIdx ? Color(255,255,255,255) : Color(255,248,250,253)); g.FillPath(&inpBg, ip); g.DrawPath(activeInput == inpIdx ? &pTeal : &pDiv, ip); delete ip;
                if(inpStr.empty() && activeInput != inpIdx) { g.DrawString(ph.c_str(), -1, &fSmall, RectF(outInp.X+10.0f, outInp.Y, outInp.Width-20.0f, outInp.Height), &fL, &bGray); } else { g.DrawString(inpStr.c_str(), -1, &fSmall, RectF(outInp.X+10.0f, outInp.Y, outInp.Width-20.0f, outInp.Height), &fL, &bDark); if(activeInput == inpIdx && (GetTickCount()/500)%2==0) { Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpStr.c_str(), -1, &fSmall, PointF(0,0), &bR); g.FillRectangle(&bDark, outInp.X+10.0f+(inpStr.empty()?0:bR.Width), outInp.Y+9.0f, 1.5f, 20.0f); } }
                float nextX = outInp.X + outInp.Width + gap;
                if(outCombo) { *outCombo = RectF(nextX, inpY, comboW, 38.0f); GraphicsPath* cbp = GetSchRoundRectPath(*outCombo, 8.0f); SolidBrush cbBr(hovCombo ? ClrBgHover : Color(255,248,250,253)); g.FillPath(&cbBr, cbp); g.DrawPath(&pDiv, cbp); delete cbp; g.DrawString(L"\xE70D", -1, &fSmallIcon, *outCombo, &fC, &bTeal); nextX += comboW + gap; }
                outAdd = RectF(nextX, inpY, addW, 38.0f); GraphicsPath* ap = GetSchRoundRectPath(outAdd, 8.0f); SolidBrush aBr(hovAdd ? ClrTealHover : ClrTeal); g.FillPath(&aBr, ap); delete ap; g.DrawString(L"+ Add", -1, &fSmallBold, outAdd, &fC, &bWhite);
                outDel.clear(); float listStartY = inpY + 48.0f; float boxH = listAreaH - 130.0f; if (colIdx == 1) boxH -= 50.0f;
                g_ehb.listAreas[colIdx] = RectF(colX, listStartY, colW, boxH); SolidBrush listBg(Color(255, 250, 252, 255)); GraphicsPath* lbgp = GetSchRoundRectPath(g_ehb.listAreas[colIdx], 8.0f); g.FillPath(&listBg, lbgp); g.DrawPath(&pDiv, lbgp); delete lbgp;
                if(list && !list->empty()) {
                    s_listScrollMax[colIdx] = (std::max)(0.0f, (list->size() * 42.0f) - boxH + 10.0f); s_listScrollC[colIdx] += (s_listScrollT[colIdx] - s_listScrollC[colIdx]) * 0.15f; g.SetClip(g_ehb.listAreas[colIdx]); float itemY = listStartY + 6.0f - s_listScrollC[colIdx];
                    for(size_t i = 0; i < list->size(); ++i) {
                        auto& item = (*list)[i];
                        if (itemY + 38.0f > listStartY && itemY < listStartY + boxH) {
                            RectF rowR(colX + 8.0f, itemY, colW - 16.0f, 38.0f); if (i % 2 == 0) { SolidBrush rowBg(Color(30, 12, 168, 176)); GraphicsPath* rp = GetSchRoundRectPath(rowR, 5.0f); g.FillPath(&rowBg, rp); delete rp; }
                            SolidBrush dotClr(ClrTeal); g.FillEllipse(&dotClr, rowR.X + 8.0f, rowR.Y + 14.0f, 7.0f, 7.0f); g.DrawString(item.name.c_str(), -1, &fSmall, RectF(rowR.X + 22.0f, rowR.Y, rowR.Width - 55.0f, rowR.Height), &fL, &bDark);
                            RectF delR(rowR.X + rowR.Width - 32.0f, rowR.Y + 3.0f, 30.0f, 32.0f); outDel.push_back({delR, (int)i}); SolidBrush crBr(item.isHoveredCross ? ClrRed : Color(150, 150, 160, 175)); g.DrawString(L"\xE711", -1, &fSmallIcon, delR, &fC, &crBr);
                        } itemY += 42.0f;
                    }
                    g.SetClip(RectF(ovX, ovY + topH + 1.0f, ovW, footerY - (ovY + topH + 1.0f)));
                    if (s_listScrollMax[colIdx] > 0) { float thumbH = (std::max)(24.0f, boxH * (boxH / (boxH + s_listScrollMax[colIdx]))); float thumbY = listStartY + (s_listScrollC[colIdx] / s_listScrollMax[colIdx]) * (boxH - thumbH); RectF thumbR(colX + colW - 7.0f, thumbY, 5.0f, thumbH); GraphicsPath thP; AddRoundedRectPath(thP, thumbR.X, thumbR.Y, thumbR.Width, thumbR.Height, 3.0f); SolidBrush thB(Color(120, 12, 168, 176)); g.FillPath(&thB, &thP); }
                } else { g.DrawString(L"\xE71D", -1, &fTitle, RectF(colX, listStartY + boxH/2 - 40.0f, colW, 40.0f), &fC, &bGray); g.DrawString(L"No items added yet", -1, &fSmall, RectF(colX, listStartY + boxH/2 + 5.0f, colW, 24.0f), &fC, &bGray); s_listScrollMax[colIdx] = 0.0f; s_listScrollT[colIdx] = 0.0f; }
            };
            float startColX = cardX + colGap; DrawListCol(0, startColX, L"Websites", L"\xE774", L"e.g. facebook.com", inpWeb, 2, cWebs, g_ehb.webInp, &g_ehb.webCombo, g_ehb.addWeb, hoverSchWebCombo, g_ehb.hAddWeb, g_ehb.webDel); DrawListCol(1, startColX + colW + colGap, L"Applications", L"\xE7EF", L"e.g. vlc.exe", inpApp, 3, cApps, g_ehb.appInp, &g_ehb.appCombo, g_ehb.addApp, hoverSchAppCombo, g_ehb.hAddApp, g_ehb.appDel);
            float appColX = startColX + colW + colGap; float btnW = (colW - colGap) / 3.0f; float btnY = contentY + listAreaH - 45.0f;
            g_ehb.btnAddExe = RectF(appColX, btnY, btnW - 5.0f, 36.0f); GraphicsPath* p1 = GetSchRoundRectPath(g_ehb.btnAddExe, 8.0f); SolidBrush b1(g_ehb.hBtnAddExe ? Color(255,75,155,5) : Color(255, 90, 170, 20)); g.FillPath(&b1, p1); delete p1; g.DrawString(L"Add .exe", -1, &fSmall, g_ehb.btnAddExe, &fC, &bWhite);
            g_ehb.btnAddStore = RectF(appColX + btnW + 2.0f, btnY, btnW - 5.0f, 36.0f); GraphicsPath* p2 = GetSchRoundRectPath(g_ehb.btnAddStore, 8.0f); SolidBrush b2(g_ehb.hBtnAddStore ? Color(255,75,155,5) : Color(255, 90, 170, 20)); g.FillPath(&b2, p2); delete p2; g.DrawString(L"Store App", -1, &fSmall, g_ehb.btnAddStore, &fC, &bWhite);
            g_ehb.btnAddTitle = RectF(appColX + btnW * 2.0f + 4.0f, btnY, btnW - 5.0f, 36.0f); GraphicsPath* p3 = GetSchRoundRectPath(g_ehb.btnAddTitle, 8.0f); SolidBrush b3(g_ehb.hBtnAddTitle ? Color(255,75,155,5) : Color(255, 90, 170, 20)); g.FillPath(&b3, p3); delete p3; g.DrawString(L"By Title", -1, &fSmall, g_ehb.btnAddTitle, &fC, &bWhite);
        }
        else if (s_activeSubTab == 3) {
            float listAreaH = footerY - contentY - 15.0f;
            float colGap = 20.0f; float colW = (cardW_inner - (colGap * 3.0f)) / 2.0f; float startColX = cardX + colGap;
            g.DrawString(L"\xE72E", -1, &fSmallIcon, RectF(startColX, contentY + 10.0f, 22.0f, 26.0f), &fL, &bTeal); g.DrawString(L"Safe Browsing Rules", -1, &fBold, RectF(startColX + 28.0f, contentY + 8.0f, colW, 28.0f), &fL, &bDark); g.DrawLine(&pDiv, startColX, contentY + 42.0f, startColX + colW, contentY + 42.0f);
            bool cA = false, cH = false, cR = false, cD = false; if(editingProfileIdx >= 0) { cA = g_profiles[editingProfileIdx].schAdultWeb; cH = g_profiles[editingProfileIdx].schHardcore; cR = g_profiles[editingProfileIdx].schRomantic; cD = g_profiles[editingProfileIdx].schStrictDns; }
            struct RuleInfo { const wchar_t* icon; const wchar_t* label; const wchar_t* desc; const wchar_t* badge; };
            RuleInfo rules[4] = { { L"\xE774", L"Block Adult Websites", L"Resource list of 1000+ adult sites", L"STRONG" }, { L"\xE787", L"Block Hardcore Keywords", L"Filters porn, NSFW, explicit terms", L"STRICT" }, { L"\xE8F3", L"Block Romantic Content", L"Softcore, suggestive, item songs", L"MILD" }, { L"\xE8F1", L"Strict DNS & SafeSearch", L"Forces safe search on all browsers", L"DNS" } };
            bool* ruleVals[4] = { &cA, &cH, &cR, &cD }; RectF* ruleHitboxes[4] = { &g_ehb.cbAdultWeb, &g_ehb.cbHardcore, &g_ehb.cbRomantic, &g_ehb.cbStrictDns }; bool ruleHovs[4] = { g_ehb.hCbAdultWeb, g_ehb.hCbHardcore, g_ehb.hCbRomantic, g_ehb.hCbStrictDns };
            for (int ri = 0; ri < 4; ri++) {
                float ry = contentY + 50.0f + ri * 58.0f; *ruleHitboxes[ri] = RectF(startColX, ry, colW, 52.0f); if (ruleHovs[ri]) { GraphicsPath* hp = GetSchRoundRectPath(*ruleHitboxes[ri], 8.0f); SolidBrush hovBg(ClrBgHover); g.FillPath(&hovBg, hp); delete hp; }
                RectF togBg(startColX + 10.0f, ry + 15.0f, 44.0f, 24.0f); GraphicsPath* tp = GetSchRoundRectPath(togBg, 12.0f); SolidBrush tBg(*ruleVals[ri] ? ClrTeal : Color(255, 200, 210, 220)); g.FillPath(&tBg, tp); delete tp; g.FillEllipse(&bWhite, *ruleVals[ri] ? togBg.X + togBg.Width - 22.0f : togBg.X + 2.0f, togBg.Y + 2.0f, 20.0f, 20.0f);
                g.DrawString(rules[ri].icon, -1, &fSmallIcon, RectF(startColX + 62.0f, ry + 5.0f, 22.0f, 22.0f), &fL, *ruleVals[ri] ? &bTeal : &bGray); g.DrawString(rules[ri].label, -1, &fBold, RectF(startColX + 88.0f, ry + 4.0f, colW - 140.0f, 22.0f), &fL, *ruleVals[ri] ? &bTeal : &bDark); g.DrawString(rules[ri].desc, -1, &fSmall, RectF(startColX + 88.0f, ry + 26.0f, colW - 140.0f, 18.0f), &fL, &bGray);
                Color badgeClr = *ruleVals[ri] ? Color(255, 12, 168, 176) : Color(255, 200, 210, 220); RectF bdgR(startColX + colW - 52.0f, ry + 16.0f, 46.0f, 20.0f); GraphicsPath* bdgP = GetSchRoundRectPath(bdgR, 10.0f); SolidBrush bdgBg(badgeClr); g.FillPath(&bdgBg, bdgP); delete bdgP; SolidBrush bdgTxt(*ruleVals[ri] ? Color(255,255,255,255) : Color(255,120,130,145)); g.DrawString(rules[ri].badge, -1, &fSmall, bdgR, &fC, &bdgTxt);
                if (ri < 3) g.DrawLine(&pDiv, startColX + 10.0f, ry + 52.0f, startColX + colW - 10.0f, ry + 52.0f);
            }
            float rightColX = startColX + colW + colGap; g.DrawString(L"\xE721", -1, &fSmallIcon, RectF(rightColX, contentY + 10.0f, 22.0f, 26.0f), &fL, &bTeal); g.DrawString(L"Custom Bad Words", -1, &fBold, RectF(rightColX + 28.0f, contentY + 8.0f, colW, 28.0f), &fL, &bDark); g.DrawLine(&pDiv, rightColX, contentY + 42.0f, rightColX + colW, contentY + 42.0f);
            float inpY = contentY + 52.0f; float addW = 72.0f; float gap = 6.0f; float inpW = colW - addW - gap; g_ehb.keyInp = RectF(rightColX, inpY, inpW, 38.0f); GraphicsPath* ip = GetSchRoundRectPath(g_ehb.keyInp, 8.0f); SolidBrush inpBg2(activeInput == 4 ? Color(255,255,255,255) : Color(255,248,250,253)); g.FillPath(&inpBg2, ip); g.DrawPath(activeInput == 4 ? &pTeal : &pDiv, ip); delete ip;
            if(inpKey.empty() && activeInput != 4) { g.DrawString(L"e.g. badword", -1, &fSmall, RectF(g_ehb.keyInp.X+10.0f, g_ehb.keyInp.Y, g_ehb.keyInp.Width-20.0f, g_ehb.keyInp.Height), &fL, &bGray); } else { g.DrawString(inpKey.c_str(), -1, &fSmall, RectF(g_ehb.keyInp.X+10.0f, g_ehb.keyInp.Y, g_ehb.keyInp.Width-20.0f, g_ehb.keyInp.Height), &fL, &bDark); if(activeInput == 4 && (GetTickCount()/500)%2==0) { Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpKey.c_str(), -1, &fSmall, PointF(0,0), &bR); g.FillRectangle(&bDark, g_ehb.keyInp.X+10.0f+(inpKey.empty()?0:bR.Width), g_ehb.keyInp.Y+9.0f, 1.5f, 20.0f); } }
            g_ehb.addKey = RectF(g_ehb.keyInp.X + g_ehb.keyInp.Width + gap, inpY, addW, 38.0f); GraphicsPath* ap2 = GetSchRoundRectPath(g_ehb.addKey, 8.0f); SolidBrush aBr2(g_ehb.hAddKey ? ClrTealHover : ClrTeal); g.FillPath(&aBr2, ap2); delete ap2; g.DrawString(L"+ Add", -1, &fSmallBold, g_ehb.addKey, &fC, &bWhite);
            g_ehb.keyDel.clear(); float listStartY = inpY + 48.0f; float boxH = listAreaH - 110.0f; g_ehb.listAreas[2] = RectF(rightColX, listStartY, colW, boxH); SolidBrush listBg2(Color(255, 250, 252, 255)); GraphicsPath* lbgp2 = GetSchRoundRectPath(g_ehb.listAreas[2], 8.0f); g.FillPath(&listBg2, lbgp2); g.DrawPath(&pDiv, lbgp2); delete lbgp2;
            vector<SchBlockItem>* aList = nullptr; if(editingProfileIdx >= 0) aList = &g_profiles[editingProfileIdx].adultCustomKeywords;
            if(aList && !aList->empty()) {
                s_listScrollMax[2] = (std::max)(0.0f, (aList->size() * 42.0f) - boxH + 10.0f); s_listScrollC[2] += (s_listScrollT[2] - s_listScrollC[2]) * 0.15f; g.SetClip(g_ehb.listAreas[2]); float itemY = listStartY + 6.0f - s_listScrollC[2];
                for(size_t i = 0; i < aList->size(); ++i) {
                    auto& item = (*aList)[i];
                    if (itemY + 38.0f > listStartY && itemY < listStartY + boxH) {
                        RectF rowR(rightColX + 8.0f, itemY, colW - 16.0f, 38.0f); if (i % 2 == 0) { SolidBrush rowBg(Color(30, 12, 168, 176)); GraphicsPath* rp = GetSchRoundRectPath(rowR, 5.0f); g.FillPath(&rowBg, rp); delete rp; }
                        SolidBrush dotClr(ClrTeal); g.FillEllipse(&dotClr, rowR.X + 8.0f, rowR.Y + 14.0f, 7.0f, 7.0f); g.DrawString(item.name.c_str(), -1, &fSmall, RectF(rowR.X + 22.0f, rowR.Y, rowR.Width - 55.0f, rowR.Height), &fL, &bDark);
                        RectF delR(rowR.X + rowR.Width - 32.0f, rowR.Y + 3.0f, 30.0f, 32.0f); g_ehb.keyDel.push_back({delR, (int)i}); SolidBrush crBr(item.isHoveredCross ? ClrRed : Color(150, 150, 160, 175)); g.DrawString(L"\xE711", -1, &fSmallIcon, delR, &fC, &crBr);
                    } itemY += 42.0f;
                } g.SetClip(RectF(ovX, ovY + topH + 1.0f, ovW, footerY - (ovY + topH + 1.0f)));
                if (s_listScrollMax[2] > 0) { float thumbH = (std::max)(24.0f, boxH * (boxH / (boxH + s_listScrollMax[2]))); float thumbY = listStartY + (s_listScrollC[2] / s_listScrollMax[2]) * (boxH - thumbH); RectF thumbR(rightColX + colW - 7.0f, thumbY, 5.0f, thumbH); GraphicsPath thP; AddRoundedRectPath(thP, thumbR.X, thumbR.Y, thumbR.Width, thumbR.Height, 3.0f); SolidBrush thB(Color(120, 12, 168, 176)); g.FillPath(&thB, &thP); }
            } else { s_listScrollMax[2] = 0.0f; s_listScrollT[2] = 0.0f; }
        }
        g.SetClip(&oldClip);

        // ================== DROPDOWNS OVERLAYS ==================
        if (s_activeSubTab == 0 && isSchModeDropdownOpen) {
            RectF mlR(g_ehb.modeDrop.X, g_ehb.modeDrop.Y + 40.0f, 215.0f, 130.0f); GraphicsPath* mlP = GetSchRoundRectPath(mlR, 8.0f); g.FillPath(&bWhite, mlP); g.DrawPath(&pDiv, mlP); delete mlP;
            g_ehb.modeOpt[0] = RectF(mlR.X+2.0f, mlR.Y+2.0f, mlR.Width-4.0f, 40.0f); g_ehb.modeOpt[1] = RectF(mlR.X+2.0f, mlR.Y+44.0f, mlR.Width-4.0f, 40.0f); g_ehb.modeOpt[2] = RectF(mlR.X+2.0f, mlR.Y+86.0f, mlR.Width-4.0f, 40.0f);
            SolidBrush o1Br(g_ehb.hOptSelf ? ClrBgHover : ClrWhite); g.FillRectangle(&o1Br, g_ehb.modeOpt[0]); g.DrawString(L"\xE713  Self Control", -1, &fNorm, RectF(mlR.X+15.0f, mlR.Y+2.0f, mlR.Width-20.0f, 40.0f), &fL, &bDark);
            SolidBrush o2Br(g_ehb.hOptParents ? ClrBgHover : ClrWhite); g.FillRectangle(&o2Br, g_ehb.modeOpt[1]); g.DrawString(L"\xE7EF  Parents Control", -1, &fNorm, RectF(mlR.X+15.0f, mlR.Y+44.0f, mlR.Width-20.0f, 40.0f), &fL, &bDark);
            SolidBrush o3Br(g_ehb.hOptLongText ? ClrBgHover : ClrWhite); g.FillRectangle(&o3Br, g_ehb.modeOpt[2]); g.DrawString(L"\xE8A1  Long Text Unlock", -1, &fNorm, RectF(mlR.X+15.0f, mlR.Y+86.0f, mlR.Width-20.0f, 40.0f), &fL, &bDark);
        }
        
        auto DrawDynamicDropdown = [&](RectF btnRect, vector<wstring>& opts, vector<RectF>& outOpts, int hovIdx) {
            RectF lR(btnRect.X - 120.0f, btnRect.Y + 36.0f, 165.0f, (float)(opts.size() * 36 + 10)); GraphicsPath* lP = GetSchRoundRectPath(lR, 8.0f); g.FillPath(&bWhite, lP); g.DrawPath(&pDiv, lP); delete lP; outOpts.clear(); float iY = lR.Y + 5.0f;
            for(size_t i=0; i<opts.size(); ++i) { RectF optRect(lR.X+2.0f, iY, lR.Width-4.0f, 36.0f); outOpts.push_back(optRect); SolidBrush oBr(hovIdx == (int)i ? ClrBgHover : ClrWhite); g.FillRectangle(&oBr, optRect); g.DrawString(opts[i].c_str(), -1, &fSmall, RectF(lR.X+12.0f, iY, lR.Width-18.0f, 36.0f), &fL, &bDark); iY += 36.0f; }
        };

        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen) DrawDynamicDropdown(g_ehb.webCombo, schCommonWebsites, g_ehb.webOpts, hoverSchWebOptIdx);
            if (isSchAppComboOpen) DrawDynamicDropdown(g_ehb.appCombo, schCommonApps, g_ehb.appOpts, hoverSchAppOptIdx);
            if (isSchStoreComboOpen) {
                RectF lR(g_ehb.btnAddStore.X, g_ehb.btnAddStore.Y - (float)(schCommonStoreApps.size() * 36 + 10), 165.0f, (float)(schCommonStoreApps.size() * 36 + 10)); GraphicsPath* lP = GetSchRoundRectPath(lR, 8.0f); g.FillPath(&bWhite, lP); g.DrawPath(&pDiv, lP); delete lP; g_ehb.storeOpts.clear(); float iY = lR.Y + 5.0f;
                for(size_t i=0; i<schCommonStoreApps.size(); ++i) { RectF optRect(lR.X+2.0f, iY, lR.Width-4.0f, 36.0f); g_ehb.storeOpts.push_back(optRect); SolidBrush oBr(hoverSchStoreOptIdx == (int)i ? ClrBgHover : ClrWhite); g.FillRectangle(&oBr, optRect); g.DrawString(schCommonStoreApps[i].c_str(), -1, &fSmall, RectF(lR.X+12.0f, iY, lR.Width-18.0f, 36.0f), &fL, &bDark); iY += 36.0f; }
            }
        }
    }

    if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
        SolidBrush overlayBg(ClrOverlay); g.FillRectangle(&overlayBg, x, y, w, h);
        float ovW = s_showTextUnlockOverlay ? 750.0f : 600.0f; float ovH = s_showTextUnlockOverlay ? 500.0f : 320.0f;
        float ovX = x + (w - ovW) / 2.0f; float ovY = y + (h - ovH) / 2.0f;
        RectF ovRect(ovX, ovY, ovW, ovH); GraphicsPath* op = GetSchRoundRectPath(ovRect, 10.0f); g.FillPath(&bBg, op); g.DrawPath(&pThin, op); delete op;

        if (s_showTimeOverlay) {
            g.DrawString(L"SET FOCUS DURATION (SELF CONTROL)", -1, &fTitle, RectF(ovX, ovY + 30.0f, ovW, 35.0f), &fC, &bDark);
            g.DrawString(L"Months:", -1, &fBold, RectF(ovX + 60.0f, ovY + 100.0f, 70.0f, 40.0f), &fL, &bDark); DrawSchOverlaySpinner(g, ovX + 140.0f, ovY + 100.0f, to_wstring(s_focusMonths), s_hTimeMoM, s_hTimeMoP, &fIcon, &fBold);
            g.DrawString(L"Days:", -1, &fBold, RectF(ovX + 330.0f, ovY + 100.0f, 60.0f, 40.0f), &fL, &bDark); DrawSchOverlaySpinner(g, ovX + 390.0f, ovY + 100.0f, to_wstring(s_focusDays), s_hTimeDM, s_hTimeDP, &fIcon, &fBold);
            g.DrawString(L"Hours:", -1, &fBold, RectF(ovX + 60.0f, ovY + 170.0f, 70.0f, 40.0f), &fL, &bDark); DrawSchOverlaySpinner(g, ovX + 140.0f, ovY + 170.0f, to_wstring(s_focusHours), s_hTimeHM, s_hTimeHP, &fIcon, &fBold);
            g.DrawString(L"Mins:", -1, &fBold, RectF(ovX + 330.0f, ovY + 170.0f, 60.0f, 40.0f), &fL, &bDark); DrawSchOverlaySpinner(g, ovX + 390.0f, ovY + 170.0f, to_wstring(s_focusMins), s_hTimeMM, s_hTimeMP, &fIcon, &fBold);
            RectF cancelRect(ovX + 90.0f, ovY + 250.0f, 160.0f, 45.0f); GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 6.0f); SolidBrush cancelBrush(s_hTimeCancel ? ClrBgHover : ClrWhite); g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp; g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);
            RectF startRect(ovX + 310.0f, ovY + 250.0f, 200.0f, 45.0f); GraphicsPath* sp = GetSchRoundRectPath(startRect, 6.0f); SolidBrush startBrush(s_hTimeStart ? ClrTealHover : ClrTeal); g.FillPath(&startBrush, sp); delete sp; g.DrawString(L"Start Profile", -1, &fBold, startRect, &fC, &bWhite);
        } else if (s_showPassOverlay) {
            wstring titleTxt = s_isStoppingFocus ? L"ENTER PARENTS PASSWORD TO STOP" : L"SET PARENTS PASSWORD"; g.DrawString(titleTxt.c_str(), -1, &fTitle, RectF(ovX, ovY + 30.0f, ovW, 35.0f), &fC, &bDark);
            s_isPassInputActive = true; // Always active — window খুললেই type করা যাবে
            RectF passInpRect(ovX + 60.0f, ovY + 110.0f, ovW - 120.0f, 45.0f); GraphicsPath* pp = GetSchRoundRectPath(passInpRect, 6.0f); g.FillPath(&bWhite, pp); g.DrawPath(&pTeal, pp); delete pp;
            wstring displayPass = wstring(s_inputPassText.length(), L'*');
            if (s_inputPassText.empty()) {
                // Placeholder + blinking cursor একসাথে
                g.DrawString(L"Type password here...", -1, &fNorm, passInpRect, &fC, &bGray);
                if ((GetTickCount() / 500) % 2 == 0) {
                    float cursorX = passInpRect.X + passInpRect.Width / 2.0f - 1.0f;
                    g.FillRectangle(&bDark, cursorX, passInpRect.Y + 10.0f, 1.5f, 25.0f);
                }
            } else {
                // Stars + cursor text এর পরে
                g.DrawString(displayPass.c_str(), -1, &fTitle, RectF(ovX + 70.0f, ovY + 115.0f, ovW - 140.0f, 35.0f), &fL, &bDark);
                if ((GetTickCount() / 500) % 2 == 0) {
                    Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(displayPass.c_str(), -1, &fTitle, PointF(0,0), &bR);
                    float curX = ovX + 72.0f + bR.Width;
                    g.FillRectangle(&bDark, curX, ovY + 120.0f, 1.5f, 25.0f);
                }
            }
            RectF cancelRect(ovX + 60.0f, ovY + 190.0f, 160.0f, 45.0f); GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 6.0f); SolidBrush cancelBrush(s_hPassCancel ? ClrBgHover : ClrWhite); g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp; g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);
            RectF confRect(ovX + 260.0f, ovY + 190.0f, 200.0f, 45.0f); GraphicsPath* sp = GetSchRoundRectPath(confRect, 6.0f); SolidBrush confBrush(s_hPassConfirm ? ClrTealHover : ClrTeal); g.FillPath(&confBrush, sp); delete sp; g.DrawString(L"Confirm", -1, &fBold, confRect, &fC, &bWhite);
        } else if (s_showTextUnlockOverlay) {
            g.DrawString(L"EXACT TEXT UNLOCK MODE", -1, &fTitle, RectF(ovX, ovY + 30.0f, ovW, 35.0f), &fC, &bDark);
            RectF targetBox(ovX + 30.0f, ovY + 80.0f, ovW - 60.0f, 140.0f); GraphicsPath* tbp = GetSchRoundRectPath(targetBox, 6.0f); g.FillPath(&bWhite, tbp); g.DrawPath(&pThin, tbp); delete tbp; g.DrawString(s_targetUnlockText.c_str(), -1, &fNorm, RectF(targetBox.X+12.0f, targetBox.Y+12.0f, targetBox.Width-24.0f, targetBox.Height-24.0f), &fTL, &bGray);
            RectF typeBox(ovX + 30.0f, ovY + 240.0f, ovW - 60.0f, 170.0f); GraphicsPath* tp = GetSchRoundRectPath(typeBox, 6.0f); g.FillPath(s_isTypingActive ? &bWhite : &bBg, tp); g.DrawPath(s_isTypingActive ? &pTeal : &pThin, tp); delete tp;
            g.DrawString(s_currentTypingText.c_str(), -1, &fNorm, RectF(typeBox.X + 12.0f, typeBox.Y + 12.0f, typeBox.Width - 24.0f, typeBox.Height - 24.0f), &fTL, &bDark);
            if (s_isTypingActive && (GetTickCount() / 500) % 2 == 0) {
                Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(s_currentTypingText.c_str(), -1, &fNorm, PointF(0,0), &bR);
                float lineH = 22.0f; float maxW = typeBox.Width - 24.0f;
                int lines = (bR.Width > 0 && maxW > 0) ? (int)(bR.Width / maxW) : 0;
                float cursorX = typeBox.X + 12.0f + fmod(bR.Width, maxW);
                float cursorY = typeBox.Y + 12.0f + lines * lineH;
                g.FillRectangle(&bDark, cursorX, cursorY, 1.5f, 18.0f);
            }
            RectF cancelRect(ovX + 160.0f, ovY + 430.0f, 160.0f, 45.0f); GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 6.0f); SolidBrush cancelBrush(s_hTextUnlockCancel ? ClrBgHover : ClrWhite); g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp; g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);
            RectF confRect(ovX + 360.0f, ovY + 430.0f, 220.0f, 45.0f); GraphicsPath* sp = GetSchRoundRectPath(confRect, 6.0f); SolidBrush confBrush((s_currentTypingText == s_targetUnlockText) ? (s_hTextUnlockConfirm ? ClrTealHover : ClrTeal) : ClrDisabled); g.FillPath(&confBrush, sp); delete sp; g.DrawString(L"Unlock Profile", -1, &fBold, confRect, &fC, &bWhite);
        }
    }
}

// ==========================================
// --- MOUSE MOVE LOGIC ---
// ==========================================
void ProcessScheduleBlocksMouseMove(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    hAddProfileBtn = false; s_hTimeMoM=false; s_hTimeMoP=false; s_hTimeDM=false; s_hTimeDP=false; s_hTimeHM=false; s_hTimeHP=false; s_hTimeMM=false; s_hTimeMP=false; s_hTimeStart=false; s_hTimeCancel=false; s_hPassInput=false; s_hPassConfirm=false; s_hPassCancel=false; s_hTextUnlockConfirm=false; s_hTextUnlockCancel=false;
    g_ehb.hSubTab = -1;

    if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
        float ovW = s_showTextUnlockOverlay ? 750.0f : 600.0f; float ovH = s_showTextUnlockOverlay ? 500.0f : 320.0f; float ovX = s_cx + (s_cw - ovW) / 2.0f; float ovY = s_cy + (s_ch - ovH) / 2.0f;
        if (s_showTimeOverlay) {
            if (RectF(ovX+140.0f, ovY+100.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeMoM=true; if (RectF(ovX+250.0f, ovY+100.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeMoP=true;
            if (RectF(ovX+390.0f, ovY+100.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeDM=true; if (RectF(ovX+500.0f, ovY+100.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeDP=true;
            if (RectF(ovX+140.0f, ovY+170.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeHM=true; if (RectF(ovX+250.0f, ovY+170.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeHP=true;
            if (RectF(ovX+390.0f, ovY+170.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeMM=true; if (RectF(ovX+500.0f, ovY+170.0f, 40.0f, 40.0f).Contains(x,y)) s_hTimeMP=true;
            if (RectF(ovX+90.0f, ovY+250.0f, 160.0f, 45.0f).Contains(x,y)) s_hTimeCancel=true; if (RectF(ovX+310.0f, ovY+250.0f, 200.0f, 45.0f).Contains(x,y)) s_hTimeStart=true;
        } else if (s_showPassOverlay) {
            if (RectF(ovX+60.0f, ovY+110.0f, ovW-120.0f, 45.0f).Contains(x,y)) s_hPassInput=true; if (RectF(ovX+60.0f, ovY+190.0f, 160.0f, 45.0f).Contains(x,y)) s_hPassCancel=true; if (RectF(ovX+260.0f, ovY+190.0f, 200.0f, 45.0f).Contains(x,y)) s_hPassConfirm=true;
        } else if (s_showTextUnlockOverlay) {
            if (RectF(ovX+160.0f, ovY+430.0f, 160.0f, 45.0f).Contains(x,y)) s_hTextUnlockCancel=true; if (RectF(ovX+360.0f, ovY+430.0f, 220.0f, 45.0f).Contains(x,y)) s_hTextUnlockConfirm=true;
        }
        return;
    }

    if (editingProfileIdx != -1) {
        g_ehb.hOptSelf = false; g_ehb.hOptParents = false; g_ehb.hOptLongText = false; hoverSchWebOptIdx = -1; hoverSchAppOptIdx = -1; hoverSchStoreOptIdx = -1; g_ehb.hSave = false; g_ehb.hCancel = false; g_ehb.hNext = false; g_ehb.hBack = false;
        g_ehb.hCbAdultWeb = false; g_ehb.hCbHardcore = false; g_ehb.hCbRomantic = false; g_ehb.hCbStrictDns = false;

        for (int i = 0; i < 4; i++) { if (g_ehb.subTabRects[i].Contains(x, y)) g_ehb.hSubTab = i; }

        if (s_activeSubTab == 0 && isSchModeDropdownOpen) {
            if (g_ehb.modeOpt[0].Contains(x, y)) g_ehb.hOptSelf = true; if (g_ehb.modeOpt[1].Contains(x, y)) g_ehb.hOptParents = true; if (g_ehb.modeOpt[2].Contains(x, y)) g_ehb.hOptLongText = true; return;
        }
        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen) { for(size_t i=0; i<g_ehb.webOpts.size(); ++i) { if(g_ehb.webOpts[i].Contains(x,y)) { hoverSchWebOptIdx=i; return; } } }
            if (isSchAppComboOpen) { for(size_t i=0; i<g_ehb.appOpts.size(); ++i) { if(g_ehb.appOpts[i].Contains(x,y)) { hoverSchAppOptIdx=i; return; } } }
            if (isSchStoreComboOpen) { for(size_t i=0; i<g_ehb.storeOpts.size(); ++i) { if(g_ehb.storeOpts[i].Contains(x,y)) { hoverSchStoreOptIdx=i; return; } } }
            g_ehb.hBtnAddExe = false; g_ehb.hBtnAddStore = false; g_ehb.hBtnAddTitle = false;
        }

        if(g_ehb.saveBtn.Contains(x,y)) g_ehb.hSave = true; if(g_ehb.cancelBtn.Contains(x,y)) g_ehb.hCancel = true; if(g_ehb.nextBtn.Contains(x,y)) g_ehb.hNext = true; if(g_ehb.backBtn.Contains(x,y)) g_ehb.hBack = true;
        g_ehb.hDay = -1; g_ehb.hStH=false; g_ehb.hStM=false; g_ehb.hStAmPm=false; g_ehb.hEnH=false; g_ehb.hEnM=false; g_ehb.hEnAmPm=false; g_ehb.hoverToggleInternet=false; g_ehb.hoverToggleUninstall=false;
        hoverSchModeDropdown=false; hoverSchWebCombo=false; hoverSchAppCombo=false; hoverSchStoreCombo=false; g_ehb.hAddWeb=false; g_ehb.hAddApp=false; g_ehb.hAddKey=false;
        for (auto& qb : s_quickBlocks) qb.hovered = false;
        if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) { for(auto& it : g_profiles[editingProfileIdx].blockedWebsites) it.isHoveredCross=false; for(auto& it : g_profiles[editingProfileIdx].blockedApps) it.isHoveredCross=false; for(auto& it : g_profiles[editingProfileIdx].adultCustomKeywords) it.isHoveredCross=false; }

        if (s_activeSubTab == 0) {
            if(g_ehb.modeDrop.Contains(x, y)) hoverSchModeDropdown = true;
            for(int d=0; d<7; d++) { if(g_ehb.days[d].Contains(x,y)) g_ehb.hDay = d; }
            if(g_ehb.stH_Box.Contains(x,y)) g_ehb.hStH = true; if(g_ehb.stM_Box.Contains(x,y)) g_ehb.hStM = true; if(g_ehb.stAmPm.Contains(x,y)) g_ehb.hStAmPm = true;
            if(g_ehb.enH_Box.Contains(x,y)) g_ehb.hEnH = true; if(g_ehb.enM_Box.Contains(x,y)) g_ehb.hEnM = true; if(g_ehb.enAmPm.Contains(x,y)) g_ehb.hEnAmPm = true;
        } 
        else if (s_activeSubTab == 1) {
            g_ehb.hoverToggleInternet = g_ehb.toggleInternet.Contains(x, y);
            g_ehb.hoverToggleUninstall = g_ehb.toggleUninstall.Contains(x, y);
            if (editingProfileIdx >= 0) {
                for (size_t qi = 0; qi < s_quickBlocks.size() && qi < s_quickBlockRects.size(); ++qi) {
                    s_quickBlocks[qi].hovered = s_quickBlockRects[qi].Contains(x, y);
                }
            }
        }
        else if (s_activeSubTab == 2) {
            if(g_ehb.webCombo.Contains(x,y)) hoverSchWebCombo = true; if(g_ehb.appCombo.Contains(x,y)) hoverSchAppCombo = true;
            if (g_ehb.btnAddExe.Contains(x, y)) g_ehb.hBtnAddExe = true; if (g_ehb.btnAddStore.Contains(x, y)) g_ehb.hBtnAddStore = true; if (g_ehb.btnAddTitle.Contains(x, y)) g_ehb.hBtnAddTitle = true;

            activeInput = 0; if (g_ehb.webInp.Contains(x,y)) activeInput = 2; if (g_ehb.appInp.Contains(x,y)) activeInput = 3;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                if (g_ehb.listAreas[0].Contains(x, y)) { for(auto& p : g_ehb.webDel) { if(p.first.Contains(x,y)) g_profiles[editingProfileIdx].blockedWebsites[p.second].isHoveredCross = true; } }
                if (g_ehb.listAreas[1].Contains(x, y)) { for(auto& p : g_ehb.appDel) { if(p.first.Contains(x,y)) g_profiles[editingProfileIdx].blockedApps[p.second].isHoveredCross = true; } }
            }
        }
        else if (s_activeSubTab == 3) { 
            if (g_ehb.cbAdultWeb.Contains(x, y)) g_ehb.hCbAdultWeb = true;
            if (g_ehb.cbHardcore.Contains(x, y)) g_ehb.hCbHardcore = true;
            if (g_ehb.cbRomantic.Contains(x, y)) g_ehb.hCbRomantic = true;
            if (g_ehb.cbStrictDns.Contains(x, y)) g_ehb.hCbStrictDns = true;
            
            activeInput = 0; if (g_ehb.keyInp.Contains(x,y)) activeInput = 4;
            if (g_ehb.addKey.Contains(x, y)) g_ehb.hAddKey = true;

            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                if (g_ehb.listAreas[2].Contains(x, y)) { for(auto& p : g_ehb.keyDel) { if(p.first.Contains(x,y)) g_profiles[editingProfileIdx].adultCustomKeywords[p.second].isHoveredCross = true; } }
            }
        }
        return;
    }

    if (RectF(s_cx + s_cw - 230.0f, s_cy + 20.0f, 210.0f, 45.0f).Contains(x, y)) hAddProfileBtn = true;
    if (s_scrollbarDragging) {
        float maxScroll = (std::max)(0.0f, (ceil((float)g_profiles.size() / 2.0f) * 215.0f) - (s_ch - 105.0f));
        float thumbH = (std::max)(28.0f, (s_ch - 105.0f) * ((s_ch - 105.0f) / ((s_ch - 105.0f) + maxScroll)));
        float thumbRange = (s_ch - 105.0f) - thumbH; float dy = y - s_scrollbarDragStartY;
        float newScroll = s_scrollbarDragStartScroll + (thumbRange > 0 ? dy / thumbRange * maxScroll : 0);
        sch_tScroll = (std::max)(0.0f, (std::min)(newScroll, maxScroll)); return;
    }
    float cardGap = 25.0f; float cardW = (s_cw - (cardGap * 3.0f)) / 2.0f; float cardH = 190.0f; float startX = s_cx + cardGap; float startY = s_cy + 110.0f - sch_cScroll;
    for (size_t i = 0; i < g_profiles.size(); ++i) {
        float cX = startX + (i % 2) * (cardW + cardGap); float cY = startY + (i / 2) * (cardH + cardGap); if (cY > s_cy + s_ch || cY + cardH < s_cy + 100.0f) continue;
        g_profiles[i].hToggle = RectF(cX + 18.0f, cY + 130.0f, 60.0f, 30.0f).Contains(x, y) || RectF(cX + 85.0f, cY + 130.0f, 60.0f, 30.0f).Contains(x, y);
        g_profiles[i].hEdit = RectF(cX + cardW - 145.0f, cY + 130.0f, 70.0f, 34.0f).Contains(x, y);
        if(!g_profiles[i].isActive) g_profiles[i].hDel = RectF(cX + cardW - 65.0f, cY + 130.0f, 48.0f, 34.0f).Contains(x, y); else g_profiles[i].hDel = false; 
    }
}

// ==========================================
// --- MOUSE BUTTON DOWN & UP ---
// ==========================================
void ProcessScheduleBlocksMouseDown(float x, float y) { std::lock_guard<std::mutex> lock(g_schMutex); }
void ProcessScheduleBlocksMouseUp(float x, float y) { std::lock_guard<std::mutex> lock(g_schMutex); s_scrollbarDragging = false; }

// ==========================================
// --- MOUSE CLICK LOGIC ---
// ==========================================
void ProcessScheduleBlocksMouseClick(float x, float y) {
    std::unique_lock<std::mutex> lock(g_schMutex);
    
    if (s_showTimeOverlay) {
        if (s_hTimeMoM && s_focusMonths > 0) s_focusMonths--; if (s_hTimeMoP && s_focusMonths < 12) s_focusMonths++;
        if (s_hTimeDM && s_focusDays > 0) s_focusDays--; if (s_hTimeDP && s_focusDays < 30) s_focusDays++;
        if (s_hTimeHM && s_focusHours > 0) s_focusHours--; if (s_hTimeHP && s_focusHours < 23) s_focusHours++;
        if (s_hTimeMM) { s_focusMins -= 5; if (s_focusMins < 0) s_focusMins = 55; } if (s_hTimeMP) { s_focusMins = (s_focusMins + 5) % 60; }
        if (s_hTimeCancel) s_showTimeOverlay = false;
        if (s_hTimeStart && activeActionProfileIdx >= 0) { 
            g_profiles[activeActionProfileIdx].isActive = true; 
            g_profiles[activeActionProfileIdx].lockEndTime = std::time(nullptr) + (s_focusMonths * 30 * 24 * 3600) + (s_focusDays * 24 * 3600) + (s_focusHours * 3600) + (s_focusMins * 60);
            ApplyProfileBlocking(); s_showTimeOverlay = false; 
            LogHistoryToHiddenFolderSch(L"Started Schedule: " + g_profiles[activeActionProfileIdx].profileName); SaveProfiles();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
        } return;
    }
    
    // 🟢 FIXED: Parent Password Overlay Safety Check & Validation
    if (s_showPassOverlay) {
        s_isPassInputActive = true; if (s_hPassCancel) { s_showPassOverlay = false; s_inputPassText = L""; }
        if (s_hPassConfirm && !s_inputPassText.empty()) {
            if (editingProfileIdx != -1) { 
                // 🟢 FIXED: Force Pre-set Password in Editing Mode Before Saving
                if (editingProfileIdx == -2) { FocusProfile np; np.lockMode = tempLockMode; g_profiles.push_back(np); editingProfileIdx = g_profiles.size() - 1; }
                g_profiles[editingProfileIdx].parentsPassword = s_inputPassText;
                
                // Complete the save process
                g_profiles[editingProfileIdx].profileName = inpProfileName;
                g_profiles[editingProfileIdx].lockMode = tempLockMode; 
                for(int d=0; d<7; d++) g_profiles[editingProfileIdx].activeDays[d] = editDays[d];
                g_profiles[editingProfileIdx].startHour = editStH; g_profiles[editingProfileIdx].startMin = editStM; 
                g_profiles[editingProfileIdx].endHour = editEnH; g_profiles[editingProfileIdx].endMin = editEnM;
                g_profiles[editingProfileIdx].blockInternet = editBlockInt; g_profiles[editingProfileIdx].blockUninstall = editBlockUninst;
                
                LogHistoryToHiddenFolderSch(L"Saved Profile: " + inpProfileName); 
                editingProfileIdx = -1; SaveProfiles(); 
                s_showPassOverlay = false; s_inputPassText = L"";
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
            } else if (activeActionProfileIdx >= 0) {
                // Starting/Stopping Profile Lock
                if (!s_isStoppingFocus) { 
                    g_profiles[activeActionProfileIdx].parentsPassword = s_inputPassText; g_profiles[activeActionProfileIdx].isActive = true; 
                    ApplyProfileBlocking(); 
                    s_showPassOverlay = false; s_inputPassText = L""; SaveProfiles();
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                } else { 
                    if (g_profiles[activeActionProfileIdx].parentsPassword == s_inputPassText) { 
                        g_profiles[activeActionProfileIdx].isActive = false; 
                        ApplyProfileBlocking(); 
                        s_showPassOverlay = false; s_inputPassText = L""; SaveProfiles();
                        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                    } else {
                        MessageBeep(MB_ICONERROR); // Wrong password warning
                    }
                }
            }
        } return;
    }
    
    if (s_showTextUnlockOverlay) {
        if (s_hTextUnlockCancel) s_showTextUnlockOverlay = false;
        if (s_hTextUnlockConfirm && s_currentTypingText == s_targetUnlockText && activeActionProfileIdx >= 0) { 
            g_profiles[activeActionProfileIdx].isActive = false; 
            ApplyProfileBlocking(); 
            s_showTextUnlockOverlay = false; s_currentTypingText = L""; SaveProfiles(); 
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
        } return;
    }

    if (editingProfileIdx != -1) {
        for (int i = 0; i < 4; i++) { if (g_ehb.subTabRects[i].Contains(x, y)) { s_activeSubTab = i; activeInput = (i == 0) ? 1 : 0; isSchModeDropdownOpen = false; isSchWebComboOpen = false; isSchAppComboOpen = false; isSchStoreComboOpen = false; return; } }
        if (g_ehb.hNext && s_activeSubTab < 3) { s_activeSubTab++; activeInput = (s_activeSubTab == 0) ? 1 : 0; return; }
        if (g_ehb.hBack && s_activeSubTab > 0) { s_activeSubTab--; activeInput = (s_activeSubTab == 0) ? 1 : 0; return; }

        bool dropdownClosed = false;

        if (s_activeSubTab == 0) {
            if (isSchModeDropdownOpen && !hoverSchModeDropdown && !g_ehb.hOptSelf && !g_ehb.hOptParents && !g_ehb.hOptLongText) { isSchModeDropdownOpen = false; dropdownClosed = true; } 
            else if (isSchModeDropdownOpen) { if (g_ehb.hOptSelf) tempLockMode = 0; if (g_ehb.hOptParents) tempLockMode = 1; if (g_ehb.hOptLongText) tempLockMode = 2; isSchModeDropdownOpen = false; return; }
        }

        if (s_activeSubTab == 2) {
            // Dropdown toggle: directly check position, not hover bool (hover can be stale)
            if (g_ehb.webCombo.Contains(x, y)) { isSchWebComboOpen = !isSchWebComboOpen; isSchAppComboOpen = false; isSchStoreComboOpen = false; return; }
            if (g_ehb.appCombo.Contains(x, y)) { isSchAppComboOpen = !isSchAppComboOpen; isSchWebComboOpen = false; isSchStoreComboOpen = false; return; }

            if (g_ehb.hBtnAddExe && editingProfileIdx >= 0) {
                lock.unlock(); 
                
                OPENFILENAMEW ofn; wchar_t szFile[260] = { 0 }; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hParentWnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t); ofn.lpstrFilter = L"Executables\0*.exe\0All\0*.*\0"; ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                
                bool fileSelected = (GetOpenFileNameW(&ofn) == TRUE);
                
                lock.lock(); 
                
                if (fileSelected) { 
                    wstring fullPath = ofn.lpstrFile; size_t pos = fullPath.find_last_of(L"\\/"); wstring exeName = (pos != wstring::npos) ? fullPath.substr(pos + 1) : fullPath; 
                    g_profiles[editingProfileIdx].blockedApps.push_back({exeName, false}); 
                }
                return;
            }
            if (g_ehb.hBtnAddStore) { isSchStoreComboOpen = !isSchStoreComboOpen; isSchWebComboOpen = false; isSchAppComboOpen = false; return; } 

            // Option selection when open
            if (isSchWebComboOpen) { 
                if (hoverSchWebOptIdx != -1 && editingProfileIdx >= 0) { g_profiles[editingProfileIdx].blockedWebsites.push_back({schCommonWebsites[hoverSchWebOptIdx], false}); }
                isSchWebComboOpen = false; return; 
            }
            if (isSchAppComboOpen) { 
                if (hoverSchAppOptIdx != -1 && editingProfileIdx >= 0) { g_profiles[editingProfileIdx].blockedApps.push_back({schCommonApps[hoverSchAppOptIdx], false}); }
                isSchAppComboOpen = false; return; 
            }
            if (isSchStoreComboOpen) { 
                if (hoverSchStoreOptIdx != -1 && editingProfileIdx >= 0) { g_profiles[editingProfileIdx].blockedApps.push_back({schCommonStoreApps[hoverSchStoreOptIdx], false}); }
                isSchStoreComboOpen = false; return; 
            }
        }

        if (dropdownClosed) return;

        if (g_ehb.hCancel) { editingProfileIdx = -1; return; }
        
        if (g_ehb.hSave) {
            if (inpProfileName.empty()) inpProfileName = L"Custom Profile";
            
            // 🟢 FIXED: Parent Pre-set Password Enforcement
            if (tempLockMode == 1) {
                wstring existingPass = L"";
                if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                    existingPass = g_profiles[editingProfileIdx].parentsPassword;
                }
                if (existingPass.empty()) {
                    s_showPassOverlay = true;
                    s_isStoppingFocus = false; // Overlay behaves as initial setter
                    s_inputPassText = L"";
                    return; // Hold Save Process until pass confirmed
                }
            }

            if (editingProfileIdx == -2) { g_profiles.back().profileName = inpProfileName; } else if (editingProfileIdx >= 0) { g_profiles[editingProfileIdx].profileName = inpProfileName; }
            if(editingProfileIdx >= 0) {
                g_profiles[editingProfileIdx].lockMode = tempLockMode; for(int d=0; d<7; d++) g_profiles[editingProfileIdx].activeDays[d] = editDays[d];
                g_profiles[editingProfileIdx].startHour = editStH; g_profiles[editingProfileIdx].startMin = editStM; g_profiles[editingProfileIdx].endHour = editEnH; g_profiles[editingProfileIdx].endMin = editEnM;
                g_profiles[editingProfileIdx].blockInternet = editBlockInt; g_profiles[editingProfileIdx].blockUninstall = editBlockUninst;
            }
            LogHistoryToHiddenFolderSch(L"Saved Profile: " + inpProfileName); editingProfileIdx = -1; SaveProfiles(); return;
        }

        if (s_activeSubTab == 0) {
            if (hoverSchModeDropdown) { isSchModeDropdownOpen = true; return; }
            if(g_ehb.hDay != -1) editDays[g_ehb.hDay] = !editDays[g_ehb.hDay];
            if(g_ehb.hStH) { editStH = (editStH + 1) % 24; } if(g_ehb.hStM) { editStM = (editStM + 5) % 60; } if(g_ehb.hStAmPm) { editStH = (editStH + 12) % 24; } 
            if(g_ehb.hEnH) { editEnH = (editEnH + 1) % 24; } if(g_ehb.hEnM) { editEnM = (editEnM + 5) % 60; } if(g_ehb.hEnAmPm) { editEnH = (editEnH + 12) % 24; } 
            activeInput = g_ehb.nameInp.Contains(x,y) ? 1 : 0;
        } 
        else if (s_activeSubTab == 1) {
            if(g_ehb.hoverToggleInternet) editBlockInt = !editBlockInt; if(g_ehb.hoverToggleUninstall) editBlockUninst = !editBlockUninst;
            if (editingProfileIdx >= 0) {
                for (size_t qi = 0; qi < s_quickBlocks.size() && qi < s_quickBlockRects.size(); ++qi) {
                    if (s_quickBlockRects[qi].Contains(x, y)) {
                        if (qi == 0) g_profiles[editingProfileIdx].quickBlockYouTubeShorts = !g_profiles[editingProfileIdx].quickBlockYouTubeShorts;
                        else if (qi == 1) g_profiles[editingProfileIdx].quickBlockFacebookReels = !g_profiles[editingProfileIdx].quickBlockFacebookReels;
                        else if (qi == 2) {
                            g_profiles[editingProfileIdx].quickBlockAds = !g_profiles[editingProfileIdx].quickBlockAds;
                            ManageAdGuardExtension(g_profiles[editingProfileIdx].quickBlockAds);
                        }
                        else if (qi == 3) g_profiles[editingProfileIdx].quickBlockInstagramReels = !g_profiles[editingProfileIdx].quickBlockInstagramReels;
                    }
                }
            }
        }
        else if (s_activeSubTab == 2) {
            activeInput = 0; if (g_ehb.webInp.Contains(x,y)) activeInput = 2; if (g_ehb.appInp.Contains(x,y)) activeInput = 3;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                if (g_ehb.hAddWeb && !inpWeb.empty()) { g_profiles[editingProfileIdx].blockedWebsites.push_back({inpWeb, false}); inpWeb = L""; }
                if (g_ehb.hAddApp && !inpApp.empty()) { if (inpApp.length() < 4 || inpApp.substr(inpApp.length() - 4) != L".exe") inpApp += L".exe"; g_profiles[editingProfileIdx].blockedApps.push_back({inpApp, false}); inpApp = L""; }
                
                if (g_ehb.listAreas[0].Contains(x, y)) { 
                    auto& webs = g_profiles[editingProfileIdx].blockedWebsites; 
                    for(auto& p : g_ehb.webDel) { if(p.first.Contains(x,y)) { webs.erase(webs.begin() + p.second); break; } } 
                }
                if (g_ehb.listAreas[1].Contains(x, y)) { 
                    auto& apps = g_profiles[editingProfileIdx].blockedApps; 
                    for(auto& p : g_ehb.appDel) { if(p.first.Contains(x,y)) { apps.erase(apps.begin() + p.second); break; } } 
                }
            }
        }
        else if (s_activeSubTab == 3) {
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                if(g_ehb.hCbAdultWeb) g_profiles[editingProfileIdx].schAdultWeb = !g_profiles[editingProfileIdx].schAdultWeb;
                if(g_ehb.hCbHardcore) g_profiles[editingProfileIdx].schHardcore = !g_profiles[editingProfileIdx].schHardcore;
                if(g_ehb.hCbRomantic) g_profiles[editingProfileIdx].schRomantic = !g_profiles[editingProfileIdx].schRomantic;
                if(g_ehb.hCbStrictDns) g_profiles[editingProfileIdx].schStrictDns = !g_profiles[editingProfileIdx].schStrictDns;
                
                activeInput = 0; if (g_ehb.keyInp.Contains(x,y)) activeInput = 4;
                if (g_ehb.hAddKey && !inpKey.empty()) { g_profiles[editingProfileIdx].adultCustomKeywords.push_back({inpKey, false}); inpKey = L""; }
                
                if (g_ehb.listAreas[2].Contains(x, y)) { 
                    auto& keys = g_profiles[editingProfileIdx].adultCustomKeywords; 
                    for(auto& p : g_ehb.keyDel) { if(p.first.Contains(x,y)) { keys.erase(keys.begin() + p.second); break; } } 
                }
            }
        }
        return;
    }

    if (hAddProfileBtn) {
        editingProfileIdx = -2; s_activeSubTab = 0; inpProfileName = L""; inpWeb = L""; inpApp = L""; inpKey = L""; tempLockMode = 0;
        for(int d=0; d<7; d++) editDays[d] = false; editStH = 9; editStM = 0; editEnH = 17; editEnM = 0; editBlockInt = false; editBlockUninst = true;
        
        s_listScrollT[0] = s_listScrollT[1] = s_listScrollT[2] = 0; 
        s_listScrollC[0] = s_listScrollC[1] = s_listScrollC[2] = 0;
        
        FocusProfile np; np.profileName = L""; np.lockMode = 0; g_profiles.push_back(np); editingProfileIdx = g_profiles.size() - 1; activeInput = 1; return;
    }

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        if (g_profiles[i].hToggle) { 
            activeActionProfileIdx = i;
            if (!g_profiles[i].isActive) {
                if (g_profiles[i].lockMode == 0) { s_showTimeOverlay = true; }
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = false; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) { g_profiles[i].isActive = true; ApplyProfileBlocking(); SaveProfiles(); if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); }
            } else {
                if (g_profiles[i].lockMode == 0) {
                    if (g_profiles[i].lockEndTime > 0) {
                        MessageBeep(MB_ICONWARNING); 
                    } else {
                        bool hasActiveSchedule = false;
                        time_t t = std::time(nullptr); tm* now = std::localtime(&t); int currentDay = now->tm_wday; int prevDay = (currentDay + 6) % 7; int currentTotalMins = now->tm_hour * 60 + now->tm_min;
                        int startTotalMins = g_profiles[i].startHour * 60 + g_profiles[i].startMin; int endTotalMins = g_profiles[i].endHour * 60 + g_profiles[i].endMin;
                        
                        if (startTotalMins <= endTotalMins) {
                            if (g_profiles[i].activeDays[currentDay]) { hasActiveSchedule = (currentTotalMins >= startTotalMins && currentTotalMins < endTotalMins); }
                        } else {
                            if (g_profiles[i].activeDays[currentDay] && currentTotalMins >= startTotalMins) { hasActiveSchedule = true; }
                            if (g_profiles[i].activeDays[prevDay] && currentTotalMins < endTotalMins) { hasActiveSchedule = true; }
                        }
                        
                        if (hasActiveSchedule) {
                            MessageBeep(MB_ICONWARNING); 
                        } else {
                            g_profiles[i].isActive = false; ApplyProfileBlocking(); SaveProfiles(); if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                        }
                    }
                } 
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = true; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) { s_showTextUnlockOverlay = true; s_currentTypingText = L""; s_isTypingActive = true; }
            }
        }
        if (g_profiles[i].hEdit) {
            editingProfileIdx = i; s_activeSubTab = 0; inpProfileName = g_profiles[i].profileName; tempLockMode = g_profiles[i].lockMode;
            for(int d=0; d<7; d++) editDays[d] = g_profiles[i].activeDays[d];
            editStH = g_profiles[i].startHour; editStM = g_profiles[i].startMin; editEnH = g_profiles[i].endHour; editEnM = g_profiles[i].endMin; editBlockInt = g_profiles[i].blockInternet; editBlockUninst = g_profiles[i].blockUninstall;
            
            s_listScrollT[0] = s_listScrollT[1] = s_listScrollT[2] = 0; 
            s_listScrollC[0] = s_listScrollC[1] = s_listScrollC[2] = 0;
            
            inpWeb = L""; inpApp = L""; inpKey = L""; activeInput = 1;
        }
        
        if (g_profiles[i].hDel && !g_profiles[i].isActive) {
            int pIdx = i;
            lock.unlock(); 
            int res = MessageBoxA(NULL, "Are you sure you want to delete this profile?", "Delete Profile", MB_YESNO | MB_ICONWARNING);
            lock.lock();   

            if (res == IDYES) { 
                if (pIdx < g_profiles.size()) {
                    g_profiles.erase(g_profiles.begin() + pIdx); 
                    SaveProfiles();
                    ApplyProfileBlocking();
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                }
            }
            break; 
        }
    }
}

// ==========================================
// --- KEYBOARD LOGIC ---
// ==========================================
void ProcessScheduleBlocksKeyPress(wchar_t c) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    if (s_showPassOverlay && s_isPassInputActive) { if (c >= 32 && c <= 126 && s_inputPassText.length() < 20) s_inputPassText += c; } 
    else if (s_showTextUnlockOverlay && s_isTypingActive) { if (c >= 32 && c <= 126 && s_currentTypingText.length() < 1000) s_currentTypingText += c; } 
    else if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size() && activeInput != 0) {
        if (c >= 32 && c <= 126) {
            if (activeInput == 1 && inpProfileName.length() < 30) inpProfileName += c;
            if (activeInput == 2 && inpWeb.length() < 40) inpWeb += c;
            if (activeInput == 3 && inpApp.length() < 40) inpApp += c;
            if (activeInput == 4 && inpKey.length() < 40) inpKey += c;
        }
    }
}

void ProcessScheduleBlocksKeyDown(WPARAM key) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    if (key == VK_ESCAPE) { 
        if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) { s_showTimeOverlay = false; s_showPassOverlay = false; s_showTextUnlockOverlay = false; return; }
        if (editingProfileIdx != -1) { editingProfileIdx = -1; return; }
    }
    if (s_showPassOverlay && s_isPassInputActive) { if (key == VK_BACK && !s_inputPassText.empty()) s_inputPassText.pop_back(); } 
    else if (s_showTextUnlockOverlay && s_isTypingActive) { if (key == VK_BACK && !s_currentTypingText.empty()) s_currentTypingText.pop_back(); } 
    else if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
        if (key == VK_BACK) {
            if (activeInput == 1 && !inpProfileName.empty()) inpProfileName.pop_back();
            if (activeInput == 2 && !inpWeb.empty()) inpWeb.pop_back();
            if (activeInput == 3 && !inpApp.empty()) inpApp.pop_back();
            if (activeInput == 4 && !inpKey.empty()) inpKey.pop_back();
        }
        else if (key == VK_RETURN) {
            if (activeInput == 2 && !inpWeb.empty()) { g_profiles[editingProfileIdx].blockedWebsites.push_back({inpWeb, false}); inpWeb = L""; }
            if (activeInput == 3 && !inpApp.empty()) { if (inpApp.length() < 4 || inpApp.substr(inpApp.length() - 4) != L".exe") inpApp += L".exe"; g_profiles[editingProfileIdx].blockedApps.push_back({inpApp, false}); inpApp = L""; }
            if (activeInput == 4 && !inpKey.empty()) { g_profiles[editingProfileIdx].adultCustomKeywords.push_back({inpKey, false}); inpKey = L""; }
        }
    }
}

void ProcessScheduleBlocksMouseWheel(float x, float y, int delta) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    UINT scrollLines = 3; SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
    float scrollStep = (float)scrollLines * 15.0f; int steps = (delta > 0) ? 1 : -1;

    if (editingProfileIdx != -1) {
        if (s_activeSubTab == 2) {
            for (int i = 0; i < 2; i++) {
                if (g_ehb.listAreas[i].Contains(x, y)) {
                    s_listScrollT[i] -= steps * scrollStep; s_listScrollT[i] = (std::max)(0.0f, (std::min)(s_listScrollT[i], s_listScrollMax[i]));
                }
            }
        } else if (s_activeSubTab == 3) {
            if (g_ehb.listAreas[2].Contains(x, y)) {
                s_listScrollT[2] -= steps * scrollStep; s_listScrollT[2] = (std::max)(0.0f, (std::min)(s_listScrollT[2], s_listScrollMax[2]));
            }
        }
        isSchModeDropdownOpen = false; isSchWebComboOpen = false; isSchAppComboOpen = false; isSchStoreComboOpen = false;
        return;
    }
    sch_tScroll -= steps * scrollStep;
    float totalRows = ceil((float)g_profiles.size() / 2.0f); float maxScroll = (std::max)(0.0f, (totalRows * 215.0f) - (s_ch - 105.0f));
    sch_tScroll = (std::max)(0.0f, (std::min)(sch_tScroll, maxScroll));
}
// ─── Dashboard shortcut: সরাসরি "Add New Profile" form খোলা ───────────────
void TriggerAddNewProfile() {
    editingProfileIdx = -2;
    s_activeSubTab = 0;
    inpProfileName = L""; inpWeb = L""; inpApp = L""; inpKey = L"";
    tempLockMode = 0;
    for (int d = 0; d < 7; d++) editDays[d] = false;
    editStH = 9; editStM = 0; editEnH = 17; editEnM = 0;
    editBlockInt = false; editBlockUninst = true;
    s_listScrollT[0] = s_listScrollT[1] = s_listScrollT[2] = 0;
    s_listScrollC[0] = s_listScrollC[1] = s_listScrollC[2] = 0;
    FocusProfile np; np.profileName = L""; np.lockMode = 0;
    g_profiles.push_back(np);
    editingProfileIdx = (int)g_profiles.size() - 1;
    activeInput = 1;
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}
