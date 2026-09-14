// ════════════════════════════════════════════════════════════════════
// tab_family_link.cpp  —  RasFocus Family Link (Complete Rewrite v2)
//
// Fixes:
//   • PIN input cursor এখন দেখা যায় (blinking caret)
//   • Connect button hit-test সঠিক
//   • Parent Gmail, relation type field যোগ
//   • Top-bar এ connected PIN + parent info দেখা যাবে
//   • Unlink / Debug button যোগ
//   • সুন্দর card-based UI
// ════════════════════════════════════════════════════════════════════

// tab_family_link.h already handles the correct include order:
//   winsock2.h -> ws2tcpip.h -> windows.h -> objbase.h -> propidl.h -> gdiplus.h
// So we just include our header first, then the extras this .cpp needs.
#include "tab_family_link.h"
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <codecvt>
#include <locale>
#include <thread>
#include <atomic>
#include <algorithm>
#include <shellapi.h>
#include <tlhelp32.h>
#include <powrprof.h>
#include <iphlpapi.h>
#include <uiautomation.h>
#include <comdef.h>
#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "UIAutomationCore.lib")
using namespace Gdiplus;
using namespace std;

// ── Main project থেকে extern ──
extern string g_currentPackage;
extern bool   g_isPremiumUser;
extern string SendFirestoreRequest(const string& method, const string& path, const string& payload);
extern string GetHardwareID();
extern string g_loggedInUserUid;

// ── Firebase project ──
static const string FB_PROJECT = "rasfocus-c746d";
static const string FB_BASE    = "/v1/projects/" + FB_PROJECT + "/databases/(default)/documents/";

// ════════════════════════════════════════════════════════════════════
// GLOBAL PARENT CONTROL STATE
// ════════════════════════════════════════════════════════════════════
bool   g_isLinkedToParent        = false;
string g_parentUid               = "";

bool   g_parentLockAllTabs       = false;
bool   g_parentForceAdultBlock   = false;
bool   g_parentForceReelsBlock   = false;
bool   g_parentForceShortsBlock  = false;

bool   g_parentAppControlEnabled = false;
string g_parentAppMode           = "BLOCK";
string g_parentAllowedAppsCSV    = "";
string g_parentBlockedAppsCSV    = "";

bool   g_parentWebBlockEnabled   = false;
string g_parentBlockedWebsCSV    = "";

bool   g_parentBlockTaskManager  = false;
bool   g_parentBlockSettings     = false;
bool   g_parentBlockFileManager  = false;
string g_parentBlockedFoldersCSV = "";

bool   g_parentInternetFasting   = false;
int    g_parentPowerAction       = 0;

int         g_parentTimeLimitMinutes = 0;
ULONGLONG   g_parentTimeLimitStart   = 0;

// ── Timed Lock System ──────────────────────────────────────────────
// lock_until_epoch: Unix time ms যতক্ষণ না হয় PC locked থাকবে
// lock_type: "lock" | "sleep" | "shutdown"
// lock_active: parent set করেছে কিনা
long long   g_lockUntilEpoch     = 0;   // ms
string      g_lockType           = "";  // "lock" | "sleep" | "shutdown"
bool        g_lockActive         = false;
// local tracking
static bool fl_timedLockApplied  = false;  // এই cycle-এ action নেওয়া হয়েছে কিনা
static ULONGLONG fl_lastLockTick = 0;      // শেষবার lock call দেওয়া হয়েছিল কখন

// ════════════════════════════════════════════════════════════════════
// INTERNAL UI STATE
// ════════════════════════════════════════════════════════════════════

// ── PIN input ──
static wchar_t  fl_pinCode[7]       = L"";
static bool     fl_isPinFocused     = false;
static ULONGLONG fl_caretTick       = 0;   // blinking caret এর জন্য

// ── Gmail input ──
static wchar_t  fl_gmailInput[128]  = L"";
static bool     fl_isGmailFocused   = false;

// ── Relation selector ──
// 0=Not selected, 1=Parent/Guardian, 2=Father, 3=Mother, 4=Friend, 5=Other
static int      fl_relationIdx      = 0;
static const wchar_t* fl_relations[] = {
    L"Select...", L"Parent / Guardian", L"Father", L"Mother", L"Friend", L"Other"
};
static int      fl_relationCount    = 6;
static bool     fl_showRelationDrop = false;
static bool     fl_hoverRelDrop     = false;

// ── Saved parent info ──
static wstring  fl_savedGmail       = L"";
static int      fl_savedRelation    = 0;

// ── Button hover states ──
static bool     fl_hoverConnectBtn  = false;
static bool     fl_hoverUnlinkBtn   = false;
static bool     fl_hoverDebugBtn    = false;

// ── Connection state ──
// 0=Idle, 1=Connecting, 2=Success, 3=Error
static int      fl_connectionState  = 0;
static wstring  fl_statusMsg        = L"";
static HWND     fl_hwnd             = NULL;

// ── Saved PIN (for top-bar display) ──
static wstring  fl_connectedPin     = L"";

// ── Polling timer ──
static int      fl_pollIntervalSec  = 5;
static int      fl_pollTick         = 0;

// ── Power / fasting state ──
static int      fl_lastPowerAction  = 0;
static bool     fl_fastingApplied   = false;

// ── Cached layout rects for hit-testing ──
struct FLRects {
    // PIN input box
    float pinX, pinY, pinW, pinH;
    // Gmail input box
    float gmailX, gmailY, gmailW, gmailH;
    // Relation dropdown
    float relX, relY, relW, relH;
    // Connect button
    float btnX, btnY, btnW, btnH;
    // Unlink button (connected view)
    float unlinkX, unlinkY, unlinkW, unlinkH;
    // Debug button
    float debugX, debugY, debugW, debugH;
    // Relation dropdown items (when open)
    float dropItemH;
} fl_rects = {};

// ════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ════════════════════════════════════════════════════════════════════

static string ExtractJsonStr(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    size_t sv = json.find("\"stringValue\": \"", pos);
    size_t bv = json.find("\"booleanValue\": ",  pos);
    size_t iv = json.find("\"integerValue\": \"", pos);
    if (sv != string::npos && (bv == string::npos || sv < bv) && (iv == string::npos || sv < iv)) {
        sv += 16;
        size_t end = json.find("\"", sv);
        if (end != string::npos) return json.substr(sv, end - sv);
    } else if (bv != string::npos && (sv == string::npos || bv < sv)) {
        bv += 16;
        size_t end = json.find_first_of(",\n}", bv);
        if (end != string::npos) return json.substr(bv, end - bv);
    } else if (iv != string::npos) {
        iv += 17;
        size_t end = json.find("\"", iv);
        if (end != string::npos) return json.substr(iv, end - iv);
    }
    return "";
}

static bool FindJsonField(const string& json, const string& key, string& out) {
    string search = "\"" + key + "\"";
    if (json.find(search) == string::npos) return false;
    out = ExtractJsonStr(json, key);
    return true;
}

static vector<string> CSVToVector(const string& csv) {
    vector<string> result;
    if (csv.empty()) return result;
    stringstream ss(csv);
    string item;
    while (getline(ss, item, ',')) {
        size_t s = item.find_first_not_of(" \t\r\n");
        size_t e = item.find_last_not_of(" \t\r\n");
        if (s != string::npos) result.push_back(item.substr(s, e - s + 1));
    }
    return result;
}

static string ToLower(string s) {
    transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static bool PointIn(float px, float py, float rx, float ry, float rw, float rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

// ════════════════════════════════════════════════════════════════════
// FIREBASE: POLL PARENT COMMANDS
// ════════════════════════════════════════════════════════════════════
static void PollParentCommands() {
    if (!g_isLinkedToParent) return;
    string hwId    = GetHardwareID();
    string getPath = FB_BASE + "parent_commands/" + hwId;
    string resp    = SendFirestoreRequest("GET", getPath, "");
    if (resp.empty() || resp.find("\"error\"") != string::npos) return;

    string v;
    v = ExtractJsonStr(resp, "lock_all_tabs");        g_parentLockAllTabs = (v == "true");
    v = ExtractJsonStr(resp, "force_adult_block");    g_parentForceAdultBlock = (v == "true");
    v = ExtractJsonStr(resp, "force_reels_block");    g_parentForceReelsBlock = (v == "true");
    v = ExtractJsonStr(resp, "force_shorts_block");   g_parentForceShortsBlock = (v == "true");
    v = ExtractJsonStr(resp, "app_control_enabled");  g_parentAppControlEnabled = (v == "true");
    v = ExtractJsonStr(resp, "app_mode");             if (!v.empty()) g_parentAppMode = v;
    if (FindJsonField(resp, "allowed_apps_csv",   v)) g_parentAllowedAppsCSV = v;
    if (FindJsonField(resp, "blocked_apps_csv",   v)) g_parentBlockedAppsCSV = v;
    v = ExtractJsonStr(resp, "web_block_enabled");    g_parentWebBlockEnabled = (v == "true");
    if (FindJsonField(resp, "blocked_webs_csv",   v)) g_parentBlockedWebsCSV = v;
    v = ExtractJsonStr(resp, "block_task_manager");   g_parentBlockTaskManager = (v == "true");
    v = ExtractJsonStr(resp, "block_settings");       g_parentBlockSettings = (v == "true");
    v = ExtractJsonStr(resp, "block_file_manager");   g_parentBlockFileManager = (v == "true");
    if (FindJsonField(resp, "blocked_folders_csv",v)) g_parentBlockedFoldersCSV = v;
    v = ExtractJsonStr(resp, "internet_fasting");     g_parentInternetFasting = (v == "true");
    v = ExtractJsonStr(resp, "power_action");         if (!v.empty()) g_parentPowerAction = atoi(v.c_str());
    v = ExtractJsonStr(resp, "time_limit_minutes");
    if (!v.empty()) {
        int newLimit = atoi(v.c_str());
        if (newLimit != g_parentTimeLimitMinutes) {
            g_parentTimeLimitMinutes = newLimit;
            g_parentTimeLimitStart   = GetTickCount64();
        }
    }

    // ── Timed Lock fields ──
    v = ExtractJsonStr(resp, "lock_until_epoch");
    if (!v.empty()) {
        long long newEpoch = atoll(v.c_str());
        if (newEpoch != g_lockUntilEpoch) {
            g_lockUntilEpoch    = newEpoch;
            fl_timedLockApplied = false;   // নতুন epoch → নতুন lock cycle শুরু
        }
    } else {
        g_lockUntilEpoch    = 0;
        fl_timedLockApplied = false;
    }
    v = ExtractJsonStr(resp, "lock_type");
    if (!v.empty()) g_lockType = v;

    // lock_active: epoch > now এবং parent clear করেনি
    bool nowActive = (g_lockUntilEpoch > 0);
    if (!nowActive && g_lockActive) {
        // Parent unlock করেছে — fl_timedLockApplied reset
        fl_timedLockApplied = false;
    }
    g_lockActive = nowActive;

    if (fl_hwnd) InvalidateRect(fl_hwnd, NULL, FALSE);
}

// ════════════════════════════════════════════════════════════════════
// FIREBASE: INIT PARENT COMMANDS DOCUMENT
// ════════════════════════════════════════════════════════════════════
static void InitParentCommandsDocument(const string& hwId, const string& parentUid) {
    string cmdPath  = FB_BASE + "parent_commands/" + hwId;
    string checkResp = SendFirestoreRequest("GET", cmdPath, "");
    if (checkResp.find("NOT_FOUND") == string::npos &&
        checkResp.find("\"error\"")  == string::npos) {
        string patchPath = cmdPath + "?updateMask.fieldPaths=parent_uid"
                                     "&updateMask.fieldPaths=child_device_id";
        string payload = "{\"fields\":{"
            "\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"},"
            "\"child_device_id\":{\"stringValue\":\"" + hwId + "\"}"
            "}}";
        SendFirestoreRequest("PATCH", patchPath, payload);
        return;
    }
    string initPayload = "{\"fields\":{"
        "\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"},"
        "\"child_device_id\":{\"stringValue\":\"" + hwId + "\"},"
        "\"lock_all_tabs\":{\"booleanValue\":false},"
        "\"force_adult_block\":{\"booleanValue\":false},"
        "\"force_reels_block\":{\"booleanValue\":false},"
        "\"force_shorts_block\":{\"booleanValue\":false},"
        "\"app_control_enabled\":{\"booleanValue\":false},"
        "\"app_mode\":{\"stringValue\":\"BLOCK\"},"
        "\"allowed_apps_csv\":{\"stringValue\":\"\"},"
        "\"blocked_apps_csv\":{\"stringValue\":\"\"},"
        "\"web_block_enabled\":{\"booleanValue\":false},"
        "\"blocked_webs_csv\":{\"stringValue\":\"\"},"
        "\"block_task_manager\":{\"booleanValue\":false},"
        "\"block_settings\":{\"booleanValue\":false},"
        "\"block_file_manager\":{\"booleanValue\":false},"
        "\"blocked_folders_csv\":{\"stringValue\":\"\"},"
        "\"internet_fasting\":{\"booleanValue\":false},"
        "\"power_action\":{\"integerValue\":\"0\"},"
        "\"time_limit_minutes\":{\"integerValue\":\"0\"}"
        "}}";
    SendFirestoreRequest("PATCH", cmdPath, initPayload);
}

// ════════════════════════════════════════════════════════════════════
// PARENT CONTROL DETECTION — tab_adult.cpp এর verified logic exact copy
// (adultsites.txt / resource ID 105 দিয়ে site list load হয়)
// ════════════════════════════════════════════════════════════════════

// ── Adult sites list (tab_adult এর মতোই resource + file fallback) ──
static vector<wstring> fl_adultWebsites;

static void FL_LoadAdultSites() {
    if (!fl_adultWebsites.empty()) return;

    // 1st: resource ID 105 থেকে (tab_adult এর মতোই)
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(105), RT_RCDATA);
    if (hRes) {
        HGLOBAL hData = LoadResource(NULL, hRes);
        DWORD size    = SizeofResource(NULL, hRes);
        const char* data = (const char*)LockResource(hData);
        if (data && size > 0) {
            string fc(data, size); stringstream ss(fc); string line;
            while (getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty())
                    fl_adultWebsites.push_back(wstring(line.begin(), line.end()));
            }
        }
    }

    // 2nd: assets/adultsites.txt fallback (exe পাশের folder)
    if (fl_adultWebsites.empty()) {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        wstring dir(exePath);
        size_t sl = dir.rfind(L'\\');
        if (sl != wstring::npos) dir = dir.substr(0, sl);
        wstring txtPath = dir + L"\\assets\\adultsites.txt";
        wifstream in(txtPath.c_str());
        in.imbue(locale(in.getloc(), new codecvt_utf8<wchar_t>));
        if (in.is_open()) {
            wstring line;
            while (getline(in, line)) {
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (!line.empty()) fl_adultWebsites.push_back(line);
            }
        }
    }

    // hardcoded fallback (tab_adult এর মতোই)
    if (fl_adultWebsites.empty()) {
        fl_adultWebsites = {
            L"pornhub.com", L"xvideos.com", L"xnxx.com",
            L"xhamster.com", L"redtube.com"
        };
    }
}

// ── UIA instance (tab_adult এর GetBrowserURL_Fallback exact copy) ──
static IUIAutomation* fl_pAutomation = NULL;

static wstring FL_GetBrowserURL(HWND hBrowser) {
    wstring url = L"";
    if (!fl_pAutomation) return url;
    IUIAutomationElement* pEl = NULL;
    if (SUCCEEDED(fl_pAutomation->ElementFromHandle(hBrowser, &pEl)) && pEl) {
        IUIAutomationCondition* pCond = NULL; IUIAutomationElement* pEdit = NULL;
        VARIANT v; v.vt = VT_I4; v.lVal = UIA_EditControlTypeId;
        fl_pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &pCond);
        if (pCond) { pEl->FindFirst(TreeScope_Descendants, pCond, &pEdit); pCond->Release(); }
        if (!pEdit) {
            VARIANT vn; vn.vt = VT_BSTR; vn.bstrVal = SysAllocString(L"Address and search bar");
            fl_pAutomation->CreatePropertyCondition(UIA_NamePropertyId, vn, &pCond);
            if (pCond) { pEl->FindFirst(TreeScope_Descendants, pCond, &pEdit); pCond->Release(); }
            SysFreeString(vn.bstrVal);
        }
        if (pEdit) {
            VARIANT vv; VariantInit(&vv);
            if (SUCCEEDED(pEdit->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &vv))
                && vv.vt == VT_BSTR && vv.bstrVal)
                url = wstring(vv.bstrVal);
            VariantClear(&vv); pEdit->Release();
        }
        pEl->Release();
    }
    return url;
}

// ── tab_adult এর closeActiveTab exact copy ──
static void FL_CloseActiveTab() {
    keybd_event(VK_CONTROL, 0, 0, 0); keybd_event('W', 0, 0, 0);
    keybd_event('W', 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

static wstring FL_ToLower(wstring s) {
    transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// ════════════════════════════════════════════════════════════════════
// PARENT DETECTION THREAD
// tab_adult AdultBackgroundThread এর detection block exact copy —
// শুধু parent flags (g_parentForce*) দিয়ে trigger হয়
// ════════════════════════════════════════════════════════════════════
static bool fl_detectionThreadStarted = false;

static void FL_ParentDetectionThread() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                     IID_IUIAutomation, (void**)&fl_pAutomation);

    FL_LoadAdultSites();

    wstring lastTitle = L"";

    while (true) {
        Sleep(500); // tab_adult এর মতোই 500ms

        if (!g_isLinkedToParent) continue;

        // ── Lock All Tabs ──
        // tab_adult এর closeActiveTab logic — foreground browser-এ Ctrl+W
        if (g_parentLockAllTabs) {
            HWND ha = GetForegroundWindow();
            if (ha) {
                wchar_t t[256]; GetWindowTextW(ha, t, 256);
                wstring lt = FL_ToLower(wstring(t));
                if (lt.find(L"chrome")  != wstring::npos ||
                    lt.find(L"edge")    != wstring::npos  ||
                    lt.find(L"brave")   != wstring::npos  ||
                    lt.find(L"firefox") != wstring::npos  ||
                    lt.find(L"opera")   != wstring::npos) {
                    FL_CloseActiveTab();
                }
            }
            continue; // lock হলে adult check দরকার নেই
        }

        // ── Adult / Reels / Shorts — tab_adult AdultBackgroundThread exact logic ──
        bool doAdult = g_parentForceAdultBlock;
        bool doReels = g_parentForceReelsBlock || g_parentForceShortsBlock;
        if (!doAdult && !doReels) continue;

        // tab_adult এর exact detection block:
        HWND ha = GetForegroundWindow();
        if (ha) {
            wchar_t t[256]; GetWindowTextW(ha, t, 256); wstring title(t);
            if (!title.empty() && title != lastTitle) {
                lastTitle = title;
                wstring lt = FL_ToLower(title);
                bool blk = false;

                // browser window কিনা (tab_adult এর মতো title দিয়ে check)
                if (lt.find(L"chrome") != wstring::npos ||
                    lt.find(L"edge")   != wstring::npos ||
                    lt.find(L"brave")  != wstring::npos) {

                    wstring url = FL_GetBrowserURL(ha);
                    wstring lu  = FL_ToLower(url);
                    bool ub = false;

                    // Adult site check (tab_adult cbAdultWeb block exact copy)
                    if (doAdult && !ub)
                        for (const auto& s : fl_adultWebsites)
                            if (url.find(s) != wstring::npos || lt.find(s) != wstring::npos)
                                { ub = true; break; }

                    // Reels/Shorts check (tab_adult cbFbReels block exact copy)
                    if (!ub && doReels) {
                        if (lu.find(L"facebook.com/reel")  != wstring::npos ||
                            lu.find(L"instagram.com/reels")!= wstring::npos ||
                            lu.find(L"youtube.com/shorts") != wstring::npos)
                            ub = true;
                    }

                    if (ub) { FL_CloseActiveTab(); Sleep(280); lastTitle = L""; }
                }
            }
        }
    }
}

static void FL_EnsureDetectionThreadRunning() {
    if (!fl_detectionThreadStarted) {
        fl_detectionThreadStarted = true;
        thread(FL_ParentDetectionThread).detach();
    }
}

// ════════════════════════════════════════════════════════════════════
// INTERNET FASTING — UAC-free approach (netsh advfirewall rule)
// ════════════════════════════════════════════════════════════════════
static void FL_ApplyInternetFasting(bool enable) {
    // Firewall rule দিয়ে সব outbound block করি — elevated process হলে কাজ করবে
    // Rule name unique রাখি যাতে toggle করতে পারি
    const char* ruleName = "RasFocusInternetFasting";
    if (enable) {
        // প্রথমে পুরনো rule delete করি (যদি থাকে)
        string delCmd = string("/c netsh advfirewall firewall delete rule name=\"") + ruleName + "\" >nul 2>&1";
        // তারপর outbound block rule add করি
        string addCmd = string("/c netsh advfirewall firewall add rule name=\"") + ruleName +
                        "\" dir=out action=block protocol=any enable=yes >nul 2>&1";
        ShellExecuteA(NULL, "open", "cmd.exe", delCmd.c_str(), NULL, SW_HIDE);
        Sleep(300);
        ShellExecuteA(NULL, "open", "cmd.exe", addCmd.c_str(), NULL, SW_HIDE);
    } else {
        // Block rule সরিয়ে দিই
        string delCmd = string("/c netsh advfirewall firewall delete rule name=\"") + ruleName + "\" >nul 2>&1";
        ShellExecuteA(NULL, "open", "cmd.exe", delCmd.c_str(), NULL, SW_HIDE);
    }
}


// ════════════════════════════════════════════════════════════════════
// ENFORCEMENT (system-level — browser detection thread এ আলাদা)
// ════════════════════════════════════════════════════════════════════
void FamilyLink_EnforceParentCommands(HWND hWnd) {
    if (!g_isLinkedToParent) return;

    // Detection thread চালু রাখি (adult/reels/shorts/lock_all_tabs সব ওখানে)
    FL_EnsureDetectionThreadRunning();

    // ── 1. Task Manager ───────────────────────────────────────────
    if (g_parentBlockTaskManager) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                string name = pe.szExeFile;
                transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "taskmgr.exe") {
                    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (ph) { TerminateProcess(ph, 1); CloseHandle(ph); }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // ── 3. Windows Settings ───────────────────────────────────────
    if (g_parentBlockSettings) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                string name = pe.szExeFile;
                transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "systemsettings.exe" || name == "immersivecontrolpanel.exe") {
                    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (ph) { TerminateProcess(ph, 1); CloseHandle(ph); }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            DWORD val = 1;
            RegSetValueExA(hKey, "NoControlPanel", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
            RegSetValueExA(hKey, "NoSetFolders",   0, REG_DWORD, (BYTE*)&val, sizeof(val));
            RegCloseKey(hKey);
        }
    } else {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueA(hKey, "NoControlPanel");
            RegDeleteValueA(hKey, "NoSetFolders");
            RegCloseKey(hKey);
        }
    }

    // ── 4. File Manager ───────────────────────────────────────────
    if (g_parentBlockFileManager) {
        HWND hFE = NULL;
        while ((hFE = FindWindowExA(NULL, hFE, "CabinetWClass", NULL)) != NULL)
            PostMessage(hFE, WM_CLOSE, 0, 0);
    }

    // ── 5. Folder Block ───────────────────────────────────────────
    if (!g_parentBlockedFoldersCSV.empty()) {
        vector<string> blockedFolders = CSVToVector(g_parentBlockedFoldersCSV);
        HWND hFE = NULL;
        while ((hFE = FindWindowExA(NULL, hFE, "CabinetWClass", NULL)) != NULL) {
            char title[512] = {};
            GetWindowTextA(hFE, title, sizeof(title));
            string t = ToLower(string(title));
            for (const auto& folder : blockedFolders) {
                if (!folder.empty() && t.find(ToLower(folder)) != string::npos) {
                    PostMessage(hFE, WM_CLOSE, 0, 0); break;
                }
            }
        }
    }

    // ── 6. App Control ────────────────────────────────────────────
    if (g_parentAppControlEnabled) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        DWORD myPid = GetCurrentProcessId();
        static const vector<string> systemSafe = {
            "explorer.exe","svchost.exe","csrss.exe","dwm.exe","lsass.exe",
            "services.exe","smss.exe","wininit.exe","winlogon.exe","spoolsv.exe",
            "fontdrvhost.exe","sihost.exe","taskhostw.exe","ctfmon.exe",
            "audiodg.exe","searchindexer.exe","registry","system",
            "conhost.exe","applicationframehost.exe"
        };
        if (Process32First(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == myPid) continue;
                string name = ToLower(string(pe.szExeFile));
                bool isSysApp = (find(systemSafe.begin(), systemSafe.end(), name) != systemSafe.end());
                if (isSysApp) continue;
                bool shouldKill = false;
                if (g_parentAppMode == "ALLOW") {
                    vector<string> allowed = CSVToVector(g_parentAllowedAppsCSV);
                    bool isAllowed = false;
                    for (const auto& a : allowed) {
                        string la = ToLower(a);
                        if (la.find('.') == string::npos) la += ".exe";
                        if (name == la) { isAllowed = true; break; }
                    }
                    bool isBrowser = (name == "chrome.exe" || name == "msedge.exe" ||
                                      name == "firefox.exe" || name == "brave.exe" ||
                                      name == "opera.exe"   || name == "vivaldi.exe");
                    if (!isAllowed && !isBrowser) shouldKill = true;
                } else {
                    vector<string> blocked = CSVToVector(g_parentBlockedAppsCSV);
                    for (const auto& a : blocked) {
                        string la = ToLower(a);
                        if (la.find('.') == string::npos) la += ".exe";
                        if (name == la) { shouldKill = true; break; }
                    }
                }
                if (shouldKill) {
                    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (ph) { TerminateProcess(ph, 1); CloseHandle(ph); }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // ── 7. Website Block ──────────────────────────────────────────
    if (g_parentWebBlockEnabled && !g_parentBlockedWebsCSV.empty()) {
        vector<string> blockedWebs = CSVToVector(g_parentBlockedWebsCSV);
        HWND hActive = GetForegroundWindow();
        if (hActive && hActive != hWnd) {
            char title[512] = {};
            if (GetWindowTextA(hActive, title, sizeof(title)) > 0) {
                string t = ToLower(string(title));
                for (const auto& site : blockedWebs) {
                    string s = ToLower(site);
                    size_t dotPos = s.find('.');
                    string domain = (dotPos != string::npos) ? s.substr(0, dotPos) : s;
                    if (!domain.empty() && t.find(domain) != string::npos) {
                        keybd_event(VK_CONTROL, 0, 0, 0);
                        keybd_event('W', 0, 0, 0);
                        keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
                        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                        Sleep(50);
                        ShowWindow(hActive, SW_MINIMIZE);
                        break;
                    }
                }
            }
        }
    }

    // ── 8. Internet Fasting (UAC-free via firewall rule) ──────────
    if (g_parentInternetFasting && !fl_fastingApplied) {
        fl_fastingApplied = true;
        thread([]() { FL_ApplyInternetFasting(true); }).detach();
    } else if (!g_parentInternetFasting && fl_fastingApplied) {
        fl_fastingApplied = false;
        thread([]() { FL_ApplyInternetFasting(false); }).detach();
    }

    // ── 9. Power Action (instant — no timer) ─────────────────────
    if (g_parentPowerAction != 0 && g_parentPowerAction != fl_lastPowerAction) {
        fl_lastPowerAction = g_parentPowerAction;
        if (g_parentPowerAction == 1) LockWorkStation();
        else if (g_parentPowerAction == 2) SetSuspendState(FALSE, FALSE, FALSE);
        else if (g_parentPowerAction == 3) {
            SHELLEXECUTEINFOA sei = { sizeof(sei) };
            sei.lpVerb = "runas"; sei.lpFile = "cmd.exe";
            sei.lpParameters = "/c shutdown /s /t 30"; sei.nShow = SW_HIDE;
            ShellExecuteExA(&sei);
        }
        string hwId = GetHardwareID();
        string patchPath = FB_BASE + "parent_commands/" + hwId +
                           "?updateMask.fieldPaths=power_action";
        string payload = "{\"fields\":{\"power_action\":{\"integerValue\":\"0\"}}}";
        thread([patchPath, payload]() {
            SendFirestoreRequest("PATCH", patchPath, payload);
        }).detach();
    }

    // ── 10. Timed Lock Enforcement ────────────────────────────────
    // g_lockUntilEpoch (ms) পর্যন্ত PC locked থাকবে।
    // প্রতি enforcement cycle-এ: time শেষ না হলে lock করো।
    // Parent unlock করলে g_lockActive = false হয়ে যাবে।
    if (g_lockActive && g_lockUntilEpoch > 0) {
        // বর্তমান system time (ms) পাই
        FILETIME ft; GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ui; ui.LowPart = ft.dwLowDateTime; ui.HighPart = ft.dwHighDateTime;
        long long nowMs = (long long)(ui.QuadPart / 10000LL) - 11644473600000LL;

        if (nowMs < g_lockUntilEpoch) {
            // সময় এখনো শেষ হয়নি — প্রতি 2 সেকেন্ডে lock enforce করো
            ULONGLONG nowTick = GetTickCount64();
            if (nowTick - fl_lastLockTick >= 2000) {
                fl_lastLockTick = nowTick;
                if (g_lockType == "sleep") {
                    SetSuspendState(FALSE, FALSE, FALSE);
                } else if (g_lockType == "shutdown") {
                    // shutdown এ বারবার call করা ঠিক না — একবার দিলেই হয়
                    if (!fl_timedLockApplied) {
                        fl_timedLockApplied = true;
                        SHELLEXECUTEINFOA sei = { sizeof(sei) };
                        sei.lpVerb = "runas"; sei.lpFile = "cmd.exe";
                        sei.lpParameters = "/c shutdown /s /t 0"; sei.nShow = SW_HIDE;
                        ShellExecuteExA(&sei);
                    }
                } else {
                    // default: lock
                    LockWorkStation();
                }
            }
        } else {
            // সময় শেষ — lock_until_epoch Firebase থেকে clear করো
            g_lockActive    = false;
            g_lockUntilEpoch = 0;
            string hwId = GetHardwareID();
            string clrPath = FB_BASE + "parent_commands/" + hwId +
                             "?updateMask.fieldPaths=lock_until_epoch"
                             "&updateMask.fieldPaths=lock_type";
            string clrPayload = "{\"fields\":{"
                "\"lock_until_epoch\":{\"integerValue\":\"0\"},"
                "\"lock_type\":{\"stringValue\":\"\"}"
                "}}";
            thread([clrPath, clrPayload]() {
                SendFirestoreRequest("PATCH", clrPath, clrPayload);
            }).detach();
        }
    }

    // ── 10. Screen Time — time শেষ হলে lock, তারপর timer reset নয় ──
    // ফলে প্রতিবার lock হবে না — parent নতুন সময় set না করলে locked থাকবে।
    if (g_parentTimeLimitMinutes > 0 && g_parentTimeLimitStart > 0) {
        ULONGLONG elapsed = (GetTickCount64() - g_parentTimeLimitStart) / 60000ULL;
        if (elapsed >= (ULONGLONG)g_parentTimeLimitMinutes) {
            // Timer reset করি না — parent নতুন time_limit_minutes set না করলে
            // LockWorkStation প্রতি poll-tick এ ডাকা হবে না।
            // g_parentTimeLimitStart = 0 করে দিই, Firestore poll-এ নতুন value
            // না আসা পর্যন্ত আর lock হবে না।
            g_parentTimeLimitStart   = 0;
            g_parentTimeLimitMinutes = 0;
            LockWorkStation();
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// DRAW HELPER: Field Label
// ════════════════════════════════════════════════════════════════════
static void DrawFieldLabel(Graphics& g, FontFamily& ff, float x, float y, float w,
                           const wchar_t* text) {
    Gdiplus::Font fLabel(&ff, 11, FontStyleBold, UnitPixel);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush col(Color(255, 80, 90, 110));
    g.DrawString(text, -1, &fLabel, RectF(x, y, w, 18.0f), &fmtL, &col);
}

// ════════════════════════════════════════════════════════════════════
// DRAW HELPER: Input Box
// ════════════════════════════════════════════════════════════════════
static void DrawInputBox(Graphics& g, FontFamily& ff, float x, float y, float w, float h,
                         const wchar_t* text, const wchar_t* placeholder,
                         bool focused, bool showCaret) {
    // Shadow
    SolidBrush shadow(Color(18, 0, 130, 150));
    g.FillRectangle(&shadow, x + 2.0f, y + 3.0f, w, h);

    // Background
    GraphicsPath path;
    AddRoundRect(path, x, y, w, h, 8.0f);
    SolidBrush bg(Color(255, 255, 255, 255));
    g.FillPath(&bg, &path);

    // Border — focused: teal glow, else light gray
    if (focused) {
        // Outer glow
        GraphicsPath glowPath;
        AddRoundRect(glowPath, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 10.0f);
        Pen glow(Color(60, 0, 160, 175), 3.0f);
        g.DrawPath(&glow, &glowPath);
        Pen border(Color(255, 0, 155, 170), 2.0f);
        g.DrawPath(&border, &path);
    } else {
        Pen border(Color(255, 210, 215, 225), 1.5f);
        g.DrawPath(&border, &path);
    }

    // Text
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);
    fmtL.SetTrimming(StringTrimmingEllipsisCharacter);

    Gdiplus::Font fInput(&ff, 14, FontStyleRegular, UnitPixel);
    wstring display(text);
    float textX = x + 12.0f;
    float textW = w - 24.0f;

    if (display.empty() && !focused) {
        SolidBrush ph(Color(255, 185, 190, 200));
        g.DrawString(placeholder, -1, &fInput,
            RectF(textX, y, textW, h), &fmtL, &ph);
    } else {
        SolidBrush txt(Color(255, 30, 35, 50));
        g.DrawString(display.c_str(), -1, &fInput,
            RectF(textX, y, textW, h), &fmtL, &txt);

        // Blinking caret
        if (focused && showCaret) {
            // Measure text width to position caret
            RectF bounds;
            g.MeasureString(display.c_str(), (int)display.size(), &fInput,
                RectF(textX, y, textW, h), &fmtL, &bounds);
            float caretX = textX + bounds.Width + 1.0f;
            if (caretX > x + w - 8.0f) caretX = x + w - 8.0f;
            float caretTop    = y + h * 0.2f;
            float caretBottom = y + h * 0.8f;
            Pen caretPen(Color(255, 0, 150, 165), 2.0f);
            g.DrawLine(&caretPen, caretX, caretTop, caretX, caretBottom);
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// DRAW HELPER: Button
// ════════════════════════════════════════════════════════════════════
static void DrawButton(Graphics& g, FontFamily& ff, float x, float y, float w, float h,
                       const wchar_t* label, Color bgColor, Color hoverColor,
                       bool hovered, bool disabled = false) {
    // Shadow
    if (!disabled) {
        SolidBrush shadow(Color(40, 0, 0, 0));
        GraphicsPath sp; AddRoundRect(sp, x + 2.0f, y + 3.0f, w, h, 9.0f);
        g.FillPath(&shadow, &sp);
    }

    GraphicsPath path; AddRoundRect(path, x, y, w, h, 9.0f);
    Color useColor = disabled ? Color(255, 180, 185, 195) : (hovered ? hoverColor : bgColor);
    SolidBrush bg(useColor);
    g.FillPath(&bg, &path);

    // Subtle top highlight
    if (!disabled) {
        LinearGradientBrush shine(
            PointF(x, y), PointF(x, y + h * 0.4f),
            Color(50, 255, 255, 255), Color(0, 255, 255, 255));
        GraphicsPath shinePath; AddRoundRect(shinePath, x, y, w, h * 0.45f, 9.0f);
        g.FillPath(&shine, &shinePath);
    }

    Gdiplus::Font fBtn(&ff, 13, FontStyleBold, UnitPixel);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    SolidBrush txt(Color(255, 255, 255, 255));
    g.DrawString(label, -1, &fBtn, RectF(x, y, w, h), &fmtC, &txt);
}

// ════════════════════════════════════════════════════════════════════
// DRAW HELPER: Status Badge
// ════════════════════════════════════════════════════════════════════
static void DrawStatusBadge(Graphics& g, FontFamily& ff, float x, float y,
                            const wchar_t* text, Color bg, Color txtColor) {
    Gdiplus::Font fBadge(&ff, 11, FontStyleBold, UnitPixel);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    RectF bounds;
    g.MeasureString(text, -1, &fBadge, RectF(0,0,300,30), &fmtL, &bounds);
    float badgeW = bounds.Width + 18.0f;
    float badgeH = 22.0f;
    GraphicsPath path; AddRoundRect(path, x, y, badgeW, badgeH, 5.0f);
    SolidBrush bgBrush(bg);
    g.FillPath(&bgBrush, &path);
    SolidBrush txtBrush(txtColor);
    g.DrawString(text, -1, &fBadge, RectF(x + 9.0f, y, badgeW - 18.0f, badgeH), &fmtL, &txtBrush);
}

// ════════════════════════════════════════════════════════════════════
// DRAW: CONNECTED VIEW
// ════════════════════════════════════════════════════════════════════
static void DrawConnectedView(Graphics& g, float x, float y, float w, float h,
                              FontFamily& ff) {
    StringFormat fmtC, fmtL;
    fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    Gdiplus::Font fTitle (&ff, 20, FontStyleBold,    UnitPixel);
    Gdiplus::Font fDesc  (&ff, 12, FontStyleRegular, UnitPixel);
    Gdiplus::Font fBold  (&ff, 12, FontStyleBold,    UnitPixel);
    Gdiplus::Font fSmall (&ff, 11, FontStyleRegular, UnitPixel);

    // ── Top status bar ──────────────────────────────────────────────
    float barH = 52.0f;
    // Gradient background
    LinearGradientBrush barBg(
        PointF(x, y), PointF(x + w, y),
        Color(255, 0, 135, 148), Color(255, 0, 100, 115));
    g.FillRectangle(&barBg, x, y, w, barH);

    // Connected icon (shield)
    Gdiplus::Font fIcon(&ff, 20, FontStyleBold, UnitPixel);
    SolidBrush white(Color(255, 255, 255, 255));
    g.DrawString(L"✓", -1, &fIcon, RectF(x + 16.0f, y, 36.0f, barH), &fmtL, &white);

    // Title
    g.DrawString(L"Family Link — Connected", -1, &fTitle,
        RectF(x + 56.0f, y, w - 300.0f, barH), &fmtL, &white);

    // Parent info (PIN + Gmail)
    wstring pinInfo = L"PIN: " + fl_connectedPin;
    if (!fl_savedGmail.empty()) pinInfo += L"  •  " + fl_savedGmail;
    if (fl_savedRelation > 0) {
        pinInfo += L"  •  ";
        pinInfo += fl_relations[fl_savedRelation];
    }
    Gdiplus::Font fPinInfo(&ff, 11, FontStyleRegular, UnitPixel);
    SolidBrush whiteAlpha(Color(200, 255, 255, 255));
    g.DrawString(pinInfo.c_str(), -1, &fPinInfo,
        RectF(x + 56.0f, y + barH * 0.58f, w - 270.0f, barH * 0.42f), &fmtL, &whiteAlpha);

    // Unlink + Debug buttons in top bar
    float btnH2 = 30.0f;
    float btnY2 = y + (barH - btnH2) / 2.0f;

    // Debug button (refresh / re-link)
    float dbgW = 90.0f;
    float dbgX = x + w - 16.0f - dbgW;
    fl_rects.debugX = dbgX; fl_rects.debugY = btnY2;
    fl_rects.debugW = dbgW; fl_rects.debugH = btnH2;

    GraphicsPath dbgPath; AddRoundRect(dbgPath, dbgX, btnY2, dbgW, btnH2, 6.0f);
    SolidBrush dbgBg(fl_hoverDebugBtn
        ? Color(255, 255, 255, 255)
        : Color(60, 255, 255, 255));
    g.FillPath(&dbgBg, &dbgPath);
    Gdiplus::Font fDbg(&ff, 11, FontStyleBold, UnitPixel);
    SolidBrush dbgTxt(fl_hoverDebugBtn
        ? Color(255, 0, 120, 135)
        : Color(255, 255, 255, 255));
    g.DrawString(L"⟳ Refresh", -1, &fDbg, RectF(dbgX, btnY2, dbgW, btnH2), &fmtC, &dbgTxt);

    // Unlink button
    float ulW = 75.0f;
    float ulX = dbgX - 10.0f - ulW;
    fl_rects.unlinkX = ulX; fl_rects.unlinkY = btnY2;
    fl_rects.unlinkW = ulW; fl_rects.unlinkH = btnH2;

    GraphicsPath ulPath; AddRoundRect(ulPath, ulX, btnY2, ulW, btnH2, 6.0f);
    SolidBrush ulBg(fl_hoverUnlinkBtn
        ? Color(255, 220, 50, 50)
        : Color(60, 230, 60, 60));
    g.FillPath(&ulBg, &ulPath);
    Gdiplus::Font fUl(&ff, 11, FontStyleBold, UnitPixel);
    SolidBrush ulTxt(Color(255, 255, 255, 255));
    g.DrawString(L"Unlink", -1, &fUl, RectF(ulX, btnY2, ulW, btnH2), &fmtC, &ulTxt);

    // ── Controls grid ───────────────────────────────────────────────
    float gridY = y + barH + 14.0f;
    float cardW = min(w - 32.0f, 720.0f);
    float cardX = x + (w - cardW) / 2.0f;

    // Card background
    GraphicsPath cardBg; AddRoundRect(cardBg, cardX, gridY, cardW, h - barH - 28.0f, 12.0f);
    SolidBrush cardFill(Color(255, 255, 255, 255));
    g.FillPath(&cardFill, &cardBg);
    Pen cardBorder(Color(255, 220, 225, 235), 1.5f);
    g.DrawPath(&cardBorder, &cardBg);

    // Screen time string
    wstring tlStr = L"No limit";
    if (g_parentTimeLimitMinutes > 0) {
        ULONGLONG elapsed   = (GetTickCount64() - g_parentTimeLimitStart) / 60000ULL;
        ULONGLONG remaining = (elapsed < (ULONGLONG)g_parentTimeLimitMinutes)
                              ? (ULONGLONG)g_parentTimeLimitMinutes - elapsed : 0ULL;
        tlStr = to_wstring(remaining) + L" min left";
    }

    // Timed lock countdown string
    wstring lockTimerStr = L"Off";
    bool    lockIsActive = false;
    if (g_lockActive && g_lockUntilEpoch > 0) {
        FILETIME ft; GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ui; ui.LowPart = ft.dwLowDateTime; ui.HighPart = ft.dwHighDateTime;
        long long nowMs  = (long long)(ui.QuadPart / 10000LL) - 11644473600000LL;
        long long remMs  = g_lockUntilEpoch - nowMs;
        if (remMs > 0) {
            long long remSec = remMs / 1000LL;
            long long remMin = remSec / 60LL;
            long long remH   = remMin / 60LL;
            remMin %= 60; remSec %= 60;
            wchar_t tbuf[64];
            if (remH > 0)
                swprintf_s(tbuf, L"%lluh %02llum %02llus left", remH, remMin, remSec);
            else if (remMin > 0)
                swprintf_s(tbuf, L"%llum %02llus left", remMin, remSec);
            else
                swprintf_s(tbuf, L"%llus left", remSec);

            // Lock type label
            wstring ltLabel = L"🔒 LOCK";
            if (g_lockType == "sleep")    ltLabel = L"😴 SLEEP";
            if (g_lockType == "shutdown") ltLabel = L"⛔ SHUTDOWN";
            lockTimerStr = ltLabel + L" — " + wstring(tbuf);
            lockIsActive = true;
        } else {
            lockTimerStr = L"Unlocking…";
        }
    }

    // Short parent UID
    wchar_t parentW[64] = {};
    string shortP = g_parentUid.size() > 14
        ? g_parentUid.substr(0, 7) + "..." + g_parentUid.substr(g_parentUid.size() - 5)
        : g_parentUid;
    MultiByteToWideChar(CP_UTF8, 0, shortP.c_str(), -1, parentW, 63);

    // App mode
    wstring appModeW = L"Off";
    if (g_parentAppControlEnabled) {
        wchar_t buf[64] = {};
        MultiByteToWideChar(CP_UTF8, 0, g_parentAppMode.c_str(), -1, buf, 63);
        appModeW = wstring(buf) + L" mode";
    }

    struct Row { wstring label; wstring value; ARGB valueColor; };
    Row rows[] = {
        { L"Parent UID",    parentW,                                      Color::MakeARGB(255,50,55,70)   },
        { L"Tab Lock",      g_parentLockAllTabs     ? L"🔒 LOCKED" : L"Off", g_parentLockAllTabs     ? Color::MakeARGB(255,215,45,45) : Color::MakeARGB(255,130,135,150) },
        { L"Adult Block",   g_parentForceAdultBlock ? L"🔴 FORCED" : L"Off", g_parentForceAdultBlock ? Color::MakeARGB(255,215,45,45) : Color::MakeARGB(255,130,135,150) },
        { L"Reels",         g_parentForceReelsBlock ? L"🔴 ON"     : L"Off", g_parentForceReelsBlock ? Color::MakeARGB(255,215,80,45) : Color::MakeARGB(255,130,135,150) },
        { L"Shorts",        g_parentForceShortsBlock? L"🔴 ON"     : L"Off", g_parentForceShortsBlock? Color::MakeARGB(255,215,80,45) : Color::MakeARGB(255,130,135,150) },
        { L"App Control",   appModeW.c_str(),                               g_parentAppControlEnabled? Color::MakeARGB(255,0,125,175):Color::MakeARGB(255,130,135,150) },
        { L"Web Block",     g_parentWebBlockEnabled ? L"🔴 ON"     : L"Off", g_parentWebBlockEnabled ? Color::MakeARGB(255,215,80,45) : Color::MakeARGB(255,130,135,150) },
        { L"Task Manager",  g_parentBlockTaskManager? L"🔒 BLOCKED": L"Off", g_parentBlockTaskManager? Color::MakeARGB(255,175,45,45) : Color::MakeARGB(255,130,135,150) },
        { L"Settings",      g_parentBlockSettings   ? L"🔒 BLOCKED": L"Off", g_parentBlockSettings   ? Color::MakeARGB(255,175,45,45) : Color::MakeARGB(255,130,135,150) },
        { L"File Manager",  g_parentBlockFileManager? L"🔒 BLOCKED": L"Off", g_parentBlockFileManager? Color::MakeARGB(255,175,45,45) : Color::MakeARGB(255,130,135,150) },
        { L"Net Fasting",   g_parentInternetFasting ? L"⚡ ACTIVE" : L"Off", g_parentInternetFasting ? Color::MakeARGB(255,220,115,0) : Color::MakeARGB(255,130,135,150) },
        { L"Screen Time",   tlStr.c_str(),                                   g_parentTimeLimitMinutes>0 ? Color::MakeARGB(255,225,115,0) : Color::MakeARGB(255,130,135,150) },
        { L"Timed Lock",    lockTimerStr.c_str(),                             lockIsActive ? Color::MakeARGB(255,210,40,40) : Color::MakeARGB(255,130,135,150) },
    };

    int nRows  = sizeof(rows) / sizeof(rows[0]);
    int halfN  = (nRows + 1) / 2;
    float colW = (cardW - 40.0f) / 2.0f;
    float rowH = 34.0f;
    float startY2 = gridY + 12.0f;

    for (int i = 0; i < nRows; i++) {
        int col = i / halfN;
        int row = i % halfN;
        float rx = cardX + 20.0f + col * colW;
        float ry = startY2 + row * rowH;

        // Alternating row tint
        if (row % 2 == 0) {
            SolidBrush rowBg(Color(255, 248, 250, 253));
            GraphicsPath rowPath; AddRoundRect(rowPath, rx - 6.0f, ry + 1.0f, colW - 8.0f, rowH - 2.0f, 5.0f);
            g.FillPath(&rowBg, &rowPath);
        }

        SolidBrush labelCol(Color(255, 100, 108, 128));
        g.DrawString(rows[i].label.c_str(), -1, &fDesc,
            RectF(rx, ry, 120.0f, rowH), &fmtL, &labelCol);

        SolidBrush valCol(rows[i].valueColor);
        g.DrawString(rows[i].value.c_str(), -1, &fBold,
            RectF(rx + 125.0f, ry, colW - 135.0f, rowH), &fmtL, &valCol);
    }

    // Sync note
    float noteY2 = startY2 + halfN * rowH + 8.0f;
    SolidBrush noteCol(Color(255, 160, 165, 180));
    g.DrawString(L"Controls sync automatically every 5 seconds from parent device.",
        -1, &fSmall, RectF(x, noteY2, w, 18.0f), &fmtC, &noteCol);
}

// ════════════════════════════════════════════════════════════════════
// DRAW: NOT CONNECTED VIEW (Setup Form)
// ════════════════════════════════════════════════════════════════════
static void DrawSetupView(Graphics& g, float x, float y, float w, float h,
                          FontFamily& ff) {
    StringFormat fmtC, fmtL;
    fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    Gdiplus::Font fTitle (&ff, 22, FontStyleBold,    UnitPixel);
    Gdiplus::Font fDesc  (&ff, 12, FontStyleRegular, UnitPixel);
    Gdiplus::Font fSmall (&ff, 11, FontStyleRegular, UnitPixel);

    // ── Centered card ───────────────────────────────────────────────
    float cardW = min(w - 40.0f, 480.0f);
    float cardH = 390.0f;
    float cardX = x + (w - cardW) / 2.0f;
    float cardY = y + (h - cardH) / 2.0f - 10.0f;

    // Card shadow
    SolidBrush shadowBrush(Color(25, 0, 80, 100));
    GraphicsPath shadowPath; AddRoundRect(shadowPath, cardX + 4.0f, cardY + 6.0f, cardW, cardH, 16.0f);
    g.FillPath(&shadowBrush, &shadowPath);

    // Card body
    GraphicsPath cardPath; AddRoundRect(cardPath, cardX, cardY, cardW, cardH, 16.0f);
    SolidBrush cardFill(Color(255, 255, 255, 255));
    g.FillPath(&cardFill, &cardPath);
    Pen cardBorder(Color(255, 215, 220, 230), 1.5f);
    g.DrawPath(&cardBorder, &cardPath);

    // Card header strip
    GraphicsPath headerPath;
    headerPath.AddArc(cardX, cardY, 32.0f, 32.0f, 180.0f, 90.0f);
    headerPath.AddArc(cardX + cardW - 32.0f, cardY, 32.0f, 32.0f, 270.0f, 90.0f);
    headerPath.AddLine(cardX + cardW, cardY + 56.0f, cardX, cardY + 56.0f);
    headerPath.CloseFigure();
    LinearGradientBrush headerBg(
        PointF(cardX, cardY), PointF(cardX + cardW, cardY),
        Color(255, 0, 140, 155), Color(255, 0, 105, 120));
    g.FillPath(&headerBg, &headerPath);

    // Header icon + text
    Gdiplus::Font fHIcon(&ff, 22, FontStyleBold, UnitPixel);
    SolidBrush white(Color(255, 255, 255, 255));
    g.DrawString(L"🔗", -1, &fHIcon, RectF(cardX + 18.0f, cardY, 40.0f, 56.0f), &fmtL, &white);

    Gdiplus::Font fHTitle(&ff, 16, FontStyleBold, UnitPixel);
    g.DrawString(L"Family Link Setup", -1, &fHTitle,
        RectF(cardX + 62.0f, cardY, cardW - 80.0f, 56.0f), &fmtL, &white);
    Gdiplus::Font fHSub(&ff, 11, FontStyleRegular, UnitPixel);
    SolidBrush whiteAlpha(Color(200, 255, 255, 255));
    g.DrawString(L"Connect this device to a parent account",
        -1, &fHSub, RectF(cardX + 62.0f, cardY + 28.0f, cardW - 80.0f, 28.0f), &fmtL, &whiteAlpha);

    // ── Form body ───────────────────────────────────────────────────
    float formX  = cardX + 28.0f;
    float formW  = cardW - 56.0f;
    float fieldY = cardY + 70.0f;
    float fieldH = 44.0f;
    float gap    = 56.0f;

    bool showCaret = ((GetTickCount64() - fl_caretTick) % 1000) < 500;

    // ── Field 1: 6-digit PIN ──
    DrawFieldLabel(g, ff, formX, fieldY, formW, L"6-digit Pairing Code  *");
    fieldY += 20.0f;
    fl_rects.pinX = formX; fl_rects.pinY = fieldY;
    fl_rects.pinW = formW; fl_rects.pinH = fieldH;

    // Draw PIN with spaced circles / digits
    {
        // Input box
        GraphicsPath pinBoxPath; AddRoundRect(pinBoxPath, formX, fieldY, formW, fieldH, 8.0f);

        // Shadow
        SolidBrush pShadow(Color(18, 0, 130, 150));
        g.FillRectangle(&pShadow, formX + 2.0f, fieldY + 3.0f, formW, fieldH);

        SolidBrush pFill(Color(255, 255, 255, 255));
        g.FillPath(&pFill, &pinBoxPath);

        if (fl_isPinFocused) {
            GraphicsPath glowPath; AddRoundRect(glowPath, formX - 2.0f, fieldY - 2.0f, formW + 4.0f, fieldH + 4.0f, 10.0f);
            Pen glow(Color(60, 0, 160, 175), 3.0f);
            g.DrawPath(&glow, &glowPath);
            Pen border(Color(255, 0, 155, 170), 2.0f);
            g.DrawPath(&border, &pinBoxPath);
        } else {
            Pen border(Color(255, 210, 215, 225), 1.5f);
            g.DrawPath(&border, &pinBoxPath);
        }

        // Draw 6 digit slots
        int pinLen = lstrlenW(fl_pinCode);
        float slotW = (formW - 40.0f) / 6.0f;
        float slotStartX = formX + 20.0f;
        Gdiplus::Font fPinDigit(&ff, 20, FontStyleBold, UnitPixel);
        Gdiplus::Font fPinDot  (&ff, 28, FontStyleBold, UnitPixel);

        for (int i = 0; i < 6; i++) {
            float sx = slotStartX + i * slotW;
            float sy = fieldY;

            // Slot underline
            float ulY2 = fieldY + fieldH - 8.0f;
            bool slotActive = (fl_isPinFocused && i == pinLen);
            Pen ulPen(slotActive ? Color(255, 0, 155, 170) : Color(255, 200, 210, 220), 2.0f);
            g.DrawLine(&ulPen, sx + 4.0f, ulY2, sx + slotW - 8.0f, ulY2);

            if (i < pinLen) {
                // Show digit
                wchar_t ch[2] = { fl_pinCode[i], L'\0' };
                SolidBrush digitBrush(Color(255, 30, 35, 55));
                g.DrawString(ch, -1, &fPinDigit,
                    RectF(sx, sy, slotW, fieldH), &fmtC, &digitBrush);
            } else if (i == pinLen && fl_isPinFocused && showCaret) {
                // Blinking cursor in empty slot
                float cx2 = sx + slotW / 2.0f - 1.0f;
                Pen caretP(Color(255, 0, 150, 165), 2.0f);
                g.DrawLine(&caretP, cx2, fieldY + 10.0f, cx2, fieldY + fieldH - 10.0f);
            } else {
                // Empty placeholder dot
                SolidBrush dotBrush(Color(255, 210, 215, 225));
                g.DrawString(L"·", -1, &fPinDot,
                    RectF(sx, sy - 4.0f, slotW, fieldH), &fmtC, &dotBrush);
            }
        }
    }

    fieldY += fieldH + gap - 18.0f;

    // ── Field 2: Parent Gmail ──
    DrawFieldLabel(g, ff, formX, fieldY, formW, L"Parent's Gmail / Email");
    fieldY += 20.0f;
    fl_rects.gmailX = formX; fl_rects.gmailY = fieldY;
    fl_rects.gmailW = formW; fl_rects.gmailH = fieldH;
    DrawInputBox(g, ff, formX, fieldY, formW, fieldH,
        fl_gmailInput, L"parent@gmail.com",
        fl_isGmailFocused, showCaret);

    fieldY += fieldH + gap - 18.0f;

    // ── Field 3: Relation dropdown ──
    DrawFieldLabel(g, ff, formX, fieldY, formW, L"Your relation to this device user");
    fieldY += 20.0f;
    fl_rects.relX = formX; fl_rects.relY = fieldY;
    fl_rects.relW = formW; fl_rects.relH = fieldH;

    // Dropdown box
    {
        GraphicsPath relPath; AddRoundRect(relPath, formX, fieldY, formW, fieldH, 8.0f);
        SolidBrush relFill(Color(255, 255, 255, 255));
        g.FillPath(&relFill, &relPath);

        if (fl_showRelationDrop || fl_hoverRelDrop) {
            Pen relBorder(Color(255, 0, 155, 170), 2.0f);
            g.DrawPath(&relBorder, &relPath);
        } else {
            Pen relBorder(Color(255, 210, 215, 225), 1.5f);
            g.DrawPath(&relBorder, &relPath);
        }

        Gdiplus::Font fRel(&ff, 14, FontStyleRegular, UnitPixel);
        bool isPlaceholder = (fl_relationIdx == 0);
        SolidBrush relTxt(isPlaceholder ? Color(255, 175, 180, 195) : Color(255, 30, 35, 55));
        g.DrawString(fl_relations[fl_relationIdx], -1, &fRel,
            RectF(formX + 12.0f, fieldY, formW - 40.0f, fieldH), &fmtL, &relTxt);

        // Chevron
        Gdiplus::Font fChev(&ff, 11, FontStyleBold, UnitPixel);
        SolidBrush chevCol(Color(255, 130, 140, 160));
        g.DrawString(fl_showRelationDrop ? L"▲" : L"▼", -1, &fChev,
            RectF(formX + formW - 30.0f, fieldY, 20.0f, fieldH), &fmtC, &chevCol);

        // Dropdown items
        if (fl_showRelationDrop) {
            float dropY = fieldY + fieldH;
            float itemH = 36.0f;
            fl_rects.dropItemH = itemH;

            // Dropdown panel shadow
            SolidBrush dropShadow(Color(30, 0, 0, 0));
            g.FillRectangle(&dropShadow, formX + 4.0f, dropY + 4.0f, formW, itemH * (fl_relationCount - 1));

            // Dropdown panel bg
            GraphicsPath dropPath;
            dropPath.StartFigure();
            dropPath.AddLine(formX, dropY, formX + formW, dropY);
            dropPath.AddArc(formX + formW - 16.0f, dropY + itemH * (fl_relationCount - 1) - 16.0f, 16.0f, 16.0f, 0.0f, 90.0f);
            dropPath.AddArc(formX, dropY + itemH * (fl_relationCount - 1) - 16.0f, 16.0f, 16.0f, 90.0f, 90.0f);
            dropPath.CloseFigure();
            SolidBrush dropBg(Color(255, 252, 253, 255));
            g.FillPath(&dropBg, &dropPath);
            Pen dropBorder(Color(255, 0, 155, 170), 1.5f);
            g.DrawPath(&dropBorder, &dropPath);

            for (int i = 1; i < fl_relationCount; i++) {
                float iy = dropY + (i - 1) * itemH;
                // Hover highlight
                POINT cursor;
                GetCursorPos(&cursor);
                // (hover handled via fl_hoverRelDrop + fl_relationIdx tracking in MouseMove)

                if (fl_relationIdx == i) {
                    SolidBrush selBg(Color(255, 235, 250, 252));
                    g.FillRectangle(&selBg, formX + 1.0f, iy, formW - 2.0f, itemH);
                }

                SolidBrush itemTxt(fl_relationIdx == i
                    ? Color(255, 0, 135, 150)
                    : Color(255, 45, 50, 65));
                g.DrawString(fl_relations[i], -1, &fRel,
                    RectF(formX + 18.0f, iy, formW - 36.0f, itemH), &fmtL, &itemTxt);

                if (i < fl_relationCount - 1) {
                    Pen divider(Color(255, 230, 235, 242), 1.0f);
                    g.DrawLine(&divider, formX + 14.0f, iy + itemH - 0.5f, formX + formW - 14.0f, iy + itemH - 0.5f);
                }
            }
        }
    }

    fieldY += fieldH + 24.0f;

    // ── Connect Button ──
    float btnW2 = formW;
    float btnH2 = 46.0f;
    float btnX2 = formX;
    float btnY2 = fieldY;
    fl_rects.btnX = btnX2; fl_rects.btnY = btnY2;
    fl_rects.btnW = btnW2; fl_rects.btnH = btnH2;

    bool isConnecting = (fl_connectionState == 1);
    const wchar_t* btnLabel = isConnecting ? L"Connecting…" : L"Connect to Parent ›";
    DrawButton(g, ff, btnX2, btnY2, btnW2, btnH2, btnLabel,
        Color(255, 0, 148, 163), Color(255, 0, 122, 137),
        fl_hoverConnectBtn, isConnecting);

    // ── Status message ──
    if (!fl_statusMsg.empty()) {
        float smY = btnY2 + btnH2 + 10.0f;
        ARGB smARGB = (fl_connectionState == 2) ? Color::MakeARGB(255, 0, 170, 80)
                    : (fl_connectionState == 1) ? Color::MakeARGB(255, 0, 148, 163)
                                                : Color::MakeARGB(255, 210, 40, 40);
        SolidBrush smBrush(smARGB);
        Gdiplus::Font fSm(&ff, 12, FontStyleBold, UnitPixel);
        g.DrawString(fl_statusMsg.c_str(), -1, &fSm,
            RectF(cardX, smY, cardW, 22.0f), &fmtC, &smBrush);
    }

    // ── Hint below card ──
    float hintY = cardY + cardH + (fl_statusMsg.empty() ? 16.0f : 42.0f);
    SolidBrush hintCol(Color(255, 155, 160, 175));
    g.DrawString(
        L"Ask your parent to open RasFocus app  →  Family Link  →  Generate Code",
        -1, &fSmall, RectF(x, hintY, w, 18.0f), &fmtC, &hintCol);
}

// ════════════════════════════════════════════════════════════════════
// MAIN DRAW
// ════════════════════════════════════════════════════════════════════
void DrawFamilyLinkTab(Graphics& g, float x, float y, float w, float h) {
    // Background gradient
    LinearGradientBrush bgBrush(
        PointF(x, y), PointF(x, y + h),
        Color(255, 243, 247, 252), Color(255, 234, 240, 248));
    g.FillRectangle(&bgBrush, x, y, w, h);

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    // ── Premium check ──
    bool hasAccess = (g_currentPackage == "PREMIUM"  ||
                      g_currentPackage == "PARENTAL" ||
                      g_currentPackage == "TRIAL");
    if (!hasAccess) {
        Gdiplus::Font fH(&ff, 22, FontStyleBold, UnitPixel);
        Gdiplus::Font fD(&ff, 13, FontStyleRegular, UnitPixel);
        SolidBrush colDark(Color(255, 50, 55, 70));
        SolidBrush colGray(Color(255, 120, 125, 140));
        g.DrawString(L"Family Link — Premium Feature", -1, &fH,
            RectF(x, y + 150.0f, w, 40.0f), &fmtC, &colDark);
        g.DrawString(L"Upgrade to RasFocus+ Pro or Parental package to use Family Link.",
            -1, &fD, RectF(x, y + 202.0f, w, 30.0f), &fmtC, &colGray);
        return;
    }

    if (g_isLinkedToParent)
        DrawConnectedView(g, x, y, w, h, ff);
    else
        DrawSetupView(g, x, y, w, h, ff);
}

// ════════════════════════════════════════════════════════════════════
// MOUSE MOVE
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY) {
    bool redraw = false;

    if (g_isLinkedToParent) {
        bool nh = PointIn(mx, my, fl_rects.unlinkX, fl_rects.unlinkY, fl_rects.unlinkW, fl_rects.unlinkH);
        bool db = PointIn(mx, my, fl_rects.debugX,  fl_rects.debugY,  fl_rects.debugW,  fl_rects.debugH);
        if (nh != fl_hoverUnlinkBtn || db != fl_hoverDebugBtn) { fl_hoverUnlinkBtn = nh; fl_hoverDebugBtn = db; redraw = true; }
    } else {
        bool nb = PointIn(mx, my, fl_rects.btnX,   fl_rects.btnY,   fl_rects.btnW,   fl_rects.btnH);
        bool nr = PointIn(mx, my, fl_rects.relX,   fl_rects.relY,   fl_rects.relW,   fl_rects.relH);
        if (nb != fl_hoverConnectBtn || nr != fl_hoverRelDrop) { fl_hoverConnectBtn = nb; fl_hoverRelDrop = nr; redraw = true; }
    }

    if (redraw && fl_hwnd) InvalidateRect(fl_hwnd, NULL, FALSE);
}

// ════════════════════════════════════════════════════════════════════
// MOUSE CLICK
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd) {
    fl_hwnd = hWnd;

    // ── Connected view clicks ──
    if (g_isLinkedToParent) {
        // Unlink
        if (PointIn(mx, my, fl_rects.unlinkX, fl_rects.unlinkY, fl_rects.unlinkW, fl_rects.unlinkH)) {
            // Reset state
            g_isLinkedToParent = false;
            g_parentUid        = "";
            fl_connectedPin    = L"";
            // Reset all control globals
            g_parentLockAllTabs = g_parentForceAdultBlock = g_parentForceReelsBlock =
            g_parentForceShortsBlock = g_parentAppControlEnabled = g_parentWebBlockEnabled =
            g_parentBlockTaskManager = g_parentBlockSettings = g_parentBlockFileManager =
            g_parentInternetFasting = false;
            g_parentPowerAction       = 0;
            g_parentTimeLimitMinutes  = 0;
            g_parentTimeLimitStart    = 0;
            fl_pollTick               = 0;
            fl_fastingApplied         = false;
            fl_lastPowerAction        = 0;
            fl_connectionState        = 0;
            fl_statusMsg              = L"";
            ZeroMemory(fl_pinCode, sizeof(fl_pinCode));
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }
        // Debug/Refresh — re-poll immediately
        if (PointIn(mx, my, fl_rects.debugX, fl_rects.debugY, fl_rects.debugW, fl_rects.debugH)) {
            fl_statusMsg = L"Refreshing…";
            InvalidateRect(hWnd, NULL, FALSE);
            thread([]() { PollParentCommands(); }).detach();
            return;
        }
        return;
    }

    // ── Setup view clicks ──

    // Close dropdown if click outside
    if (fl_showRelationDrop) {
        // Check if click is inside a dropdown item
        if (PointIn(mx, my, fl_rects.relX, fl_rects.relY + fl_rects.relH,
                    fl_rects.relW, fl_rects.dropItemH * (fl_relationCount - 1))) {
            int idx = (int)((my - (fl_rects.relY + fl_rects.relH)) / fl_rects.dropItemH);
            if (idx >= 0 && idx < fl_relationCount - 1) fl_relationIdx = idx + 1;
            fl_showRelationDrop = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }
        fl_showRelationDrop = false;
        InvalidateRect(hWnd, NULL, FALSE);
    }

    // PIN box
    bool clickedPin = PointIn(mx, my, fl_rects.pinX, fl_rects.pinY, fl_rects.pinW, fl_rects.pinH);
    bool clickedGmail = PointIn(mx, my, fl_rects.gmailX, fl_rects.gmailY, fl_rects.gmailW, fl_rects.gmailH);
    bool clickedRel = PointIn(mx, my, fl_rects.relX, fl_rects.relY, fl_rects.relW, fl_rects.relH);
    bool clickedBtn = PointIn(mx, my, fl_rects.btnX, fl_rects.btnY, fl_rects.btnW, fl_rects.btnH);

    if (clickedPin)   { fl_isPinFocused = true;  fl_isGmailFocused = false; fl_caretTick = GetTickCount64(); }
    if (clickedGmail) { fl_isGmailFocused = true; fl_isPinFocused  = false; fl_caretTick = GetTickCount64(); }
    if (clickedRel)   { fl_showRelationDrop = !fl_showRelationDrop; }
    if (!clickedPin && !clickedGmail && !clickedRel && !clickedBtn) {
        fl_isPinFocused = false; fl_isGmailFocused = false;
    }

    if (!clickedBtn) { InvalidateRect(hWnd, NULL, FALSE); return; }

    // ── Connect button ──
    if (fl_connectionState == 1) return; // already connecting

    wstring currentPin(fl_pinCode);
    if (currentPin.length() != 6) {
        fl_connectionState = 3;
        fl_statusMsg = L"Please enter a complete 6-digit PIN.";
        fl_isPinFocused = true;
        fl_caretTick = GetTickCount64();
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }

    fl_connectionState = 1;
    fl_statusMsg       = L"Verifying PIN with server…";
    fl_isPinFocused    = false;
    InvalidateRect(hWnd, NULL, FALSE);
    UpdateWindow(hWnd);

    string pinStr(currentPin.begin(), currentPin.end());
    wstring gmailCopy(fl_gmailInput);
    int     relCopy  = fl_relationIdx;
    HWND    capturedHwnd = hWnd;

    thread([pinStr, gmailCopy, relCopy, capturedHwnd]() {

        // Step 1: Verify pairing code
        string getPairPath = FB_BASE + "pairing_codes/" + pinStr;
        string pairResp    = SendFirestoreRequest("GET", getPairPath, "");

        if (pairResp.empty() ||
            pairResp.find("\"error\"")   != string::npos ||
            pairResp.find("NOT_FOUND")   != string::npos) {
            fl_connectionState = 3;
            fl_statusMsg       = L"Invalid or expired PIN. Please try again.";
            InvalidateRect(capturedHwnd, NULL, FALSE);
            return;
        }

        string parentUid = ExtractJsonStr(pairResp, "parent_uid");
        if (parentUid.empty()) {
            fl_connectionState = 3;
            fl_statusMsg       = L"Server error: parent_uid missing.";
            InvalidateRect(capturedHwnd, NULL, FALSE);
            return;
        }

        string hwId = GetHardwareID();

        // Convert gmail wstring to string
        string gmailStr(gmailCopy.begin(), gmailCopy.end());

        // Step 2: Save device info
        string devPatchPath = FB_BASE + "devices/" + hwId +
            "?updateMask.fieldPaths=parent_uid"
            "&updateMask.fieldPaths=linked_at"
            "&updateMask.fieldPaths=parent_gmail"
            "&updateMask.fieldPaths=relation_type";
        char timeStr[32];
        sprintf_s(timeStr, "%lld", (long long)time(nullptr));
        // relation label
        string relLabel = "";
        if (relCopy > 0 && relCopy < 6) {
            wstring relW(fl_relations[relCopy]);
            relLabel = string(relW.begin(), relW.end());
        }
        string devPayload = "{\"fields\":{"
            "\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"},"
            "\"linked_at\":{\"stringValue\":\"" + string(timeStr) + "\"},"
            "\"parent_gmail\":{\"stringValue\":\"" + gmailStr + "\"},"
            "\"relation_type\":{\"stringValue\":\"" + relLabel + "\"}"
            "}}";
        SendFirestoreRequest("PATCH", devPatchPath, devPayload);

        // Step 3: Init parent_commands doc
        InitParentCommandsDocument(hwId, parentUid);

        // Step 4: Mark PIN used
        string usedPatch = FB_BASE + "pairing_codes/" + pinStr +
                           "?updateMask.fieldPaths=used";
        string usedPayload = "{\"fields\":{\"used\":{\"booleanValue\":true}}}";
        SendFirestoreRequest("PATCH", usedPatch, usedPayload);

        // Step 5: Update state
        g_parentUid         = parentUid;
        g_isLinkedToParent  = true;
        fl_connectionState  = 2;
        fl_statusMsg        = L"Successfully connected to parent!";
        fl_connectedPin     = wstring(pinStr.begin(), pinStr.end());
        fl_savedGmail       = gmailCopy;
        fl_savedRelation    = relCopy;

        // Step 6: First poll
        PollParentCommands();

        InvalidateRect(capturedHwnd, NULL, FALSE);

    }).detach();

    InvalidateRect(hWnd, NULL, FALSE);
}

// ════════════════════════════════════════════════════════════════════
// KEYBOARD INPUT
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkChar(wchar_t c) {
    bool needRedraw = false;

    if (fl_isPinFocused) {
        int len = lstrlenW(fl_pinCode);
        if (c == L'\b') {
            if (len > 0) { fl_pinCode[len - 1] = L'\0'; needRedraw = true; }
        } else if (c >= L'0' && c <= L'9') {
            if (len < 6) { fl_pinCode[len] = c; fl_pinCode[len + 1] = L'\0'; needRedraw = true; }
        }
        // Reset caret blink on keypress
        fl_caretTick = GetTickCount64();
    }

    if (fl_isGmailFocused) {
        int len = lstrlenW(fl_gmailInput);
        if (c == L'\b') {
            if (len > 0) { fl_gmailInput[len - 1] = L'\0'; needRedraw = true; }
        } else if (c >= L' ' && c != L'\t') {
            if (len < 126) { fl_gmailInput[len] = c; fl_gmailInput[len + 1] = L'\0'; needRedraw = true; }
        }
        fl_caretTick = GetTickCount64();
    }

    if (needRedraw && fl_hwnd) InvalidateRect(fl_hwnd, NULL, FALSE);
}

void ProcessFamilyLinkKeyDown(WPARAM wp) {
    if (wp == VK_TAB) {
        // Tab: cycle between fields
        if (fl_isPinFocused) {
            fl_isPinFocused   = false;
            fl_isGmailFocused = true;
        } else if (fl_isGmailFocused) {
            fl_isGmailFocused   = false;
            fl_showRelationDrop = false;
        } else {
            fl_isPinFocused = true;
        }
        fl_caretTick = GetTickCount64();
        if (fl_hwnd) InvalidateRect(fl_hwnd, NULL, FALSE);
    }
    if (wp == VK_RETURN) {
        // Enter on PIN field: move to gmail
        if (fl_isPinFocused && lstrlenW(fl_pinCode) == 6) {
            fl_isPinFocused   = false;
            fl_isGmailFocused = true;
            fl_caretTick      = GetTickCount64();
        }
        if (fl_hwnd) InvalidateRect(fl_hwnd, NULL, FALSE);
    }
    if (wp == VK_ESCAPE) {
        fl_isPinFocused     = false;
        fl_isGmailFocused   = false;
        fl_showRelationDrop = false;
        if (fl_hwnd) InvalidateRect(fl_hwnd, NULL, FALSE);
    }
}

// ════════════════════════════════════════════════════════════════════
// TIMER — caret blink + poll ticker
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkTimer(UINT_PTR timerId, HWND hWnd) {
    if (hWnd) fl_hwnd = hWnd;

    // Caret blinking — redraw if any input is focused
    if (fl_isPinFocused || fl_isGmailFocused)
        InvalidateRect(hWnd, NULL, FALSE);

    // Live countdown redraw — প্রতি timer tick-এ redraw যাতে countdown সেকেন্ড-by-সেকেন্ড আপডেট হয়
    if (g_lockActive && g_lockUntilEpoch > 0)
        InvalidateRect(hWnd, NULL, FALSE);

    if (!g_isLinkedToParent) return;

    fl_pollTick++;
    if (fl_pollTick >= fl_pollIntervalSec) {
        fl_pollTick = 0;
        thread([]() { PollParentCommands(); }).detach();
    }
}