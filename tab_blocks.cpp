#include "tab_blocks.h"

#include "tab_schedule_blocks.h"

#include "tab_adult.h"

#include "tab_device_block.h"

#include <vector>

#include <string>

#include <commdlg.h> 

#include <psapi.h>

#include <tlhelp32.h> 

#include <thread>

#include <algorithm> // For transform & sort

#include <iostream>

#include <sstream>

#include <fstream>

#include <shlobj.h>

#include <ctime>

#include <cctype> // For word count



using namespace Gdiplus;

using namespace std;



// --- Premium Gate (defined in upgrade.cpp) ---
extern bool g_isPremiumUser;
extern bool g_showUpgradePopup;

// ── Family Link: Parent Remote Control ──
extern bool g_parentLockAllTabs;       // tab_family_link.cpp থেকে আসছে
extern bool g_parentForceAdultBlock;   // tab_family_link.cpp থেকে আসছে



// --- Dynamic Window Size Cache ---

static float s_contentX = 0.0f;

static float s_contentY = 0.0f;

static float s_contentW = 800.0f; 

static float s_contentH = 600.0f;



// --- Smooth Scroll States ---

static float tWebScrollY = 0.0f, cWebScrollY = 0.0f;

static float tAppScrollY = 0.0f, cAppScrollY = 0.0f;

static float tStoreScrollY = 0.0f, cStoreScrollY = 0.0f;



// --- Main Navigation States ---

// 0=Simple Blocks, 1=Schedule Blocks, 2=Adult Block, 3=Device Block

static int currentBlockTab = 0; 

static int hoverBlockTab = -1;



// --- Focus Control States ---

static bool isFocusActive = false; 

static int controlMode = 0; // 0 = Self, 1 = Parents (Premium), 2 = Long Text (Premium)

static bool hoverControlDropdown = false;

static bool isControlDropdownOpen = false;

static bool hoverOptSelf = false;

static bool hoverOptParents = false;

static bool hoverOptLongText = false;

static bool hoverStartFocusBtn = false;

static time_t focusEndTimeBlocks = 0;



// --- Quotes States ---

static bool showQuotes = true;

static bool hoverQuotesCheckbox = false;

static int quoteLanguage = 0; // 0 = Bangla, 1 = English

static bool hoverLangDropdown = false;

static bool isLangDropdownOpen = false;

static bool hoverOptBn = false;

static bool hoverOptEn = false;



struct Quote { wstring bn; wstring en; };

vector<Quote> motivationalQuotes = {

    {L"সময়ের মূল্য বোঝো, জীবন তোমার মূল্য বুঝবে।", L"Understand the value of time, life will understand your value."},

    {L"সফলতা আসে ফোকাস থেকে, ডিস্ট্রাকশন থেকে নয়।", L"Success comes from focus, not from distraction."},

    {L"আজকের সময় নষ্ট মানে, কালকের স্বপ্ন নষ্ট।", L"Wasting time today means ruining tomorrow's dreams."},

    {L"যে নিজের মনকে নিয়ন্ত্রণ করতে পারে, সে পৃথিবী জয় করতে পারে।", L"He who can control his mind can conquer the world."},

    {L"বড় কিছু পেতে হলে ছোট আনন্দগুলো ত্যাগ করতে হয়।", L"To achieve something big, you have to sacrifice small pleasures."},

    {L"তোমার আজকের ফোকাস নির্ধারণ করবে তোমার কালকের ভবিষ্যৎ।", L"Your focus today determines your future tomorrow."},

    {L"যা তোমাকে তোমার লক্ষ্য থেকে দূরে সরিয়ে নেয়, তাকে এখনই না বলো।", L"Say no to whatever takes you away from your goals."},

    {L"সাময়িক আনন্দ নয়, স্থায়ী সফলতার দিকে নজর দাও।", L"Focus on permanent success, not temporary pleasure."}

};



// --- Overlays States ---

static bool showTimeOverlay = false;

static int focusHours = 1; static int focusMins = 0;

static bool hTimeHM = false, hTimeHP = false, hTimeMM = false, hTimeMP = false; 

static bool hTimeStart = false, hTimeCancel = false;



static bool showPassOverlay = false;

static wstring inputPassText = L"";

static bool isPassInputActive = true, hPassInput = false;

static bool hPassConfirm = false, hPassCancel = false;

static bool isStoppingFocus = false; 



// --- Long Text Overlay States ---

static bool showLongTextOverlay = false;

static wstring inputLongText = L"";

static bool isLongTextInputActive = true, hLongTextInput = false;

static bool hLongTextConfirm = false, hLongTextCancel = false;



// --- Store Apps Overlay ---

static bool showStoreOverlay = false;

static bool hoverStoreClose = false;

static int hoverStoreAddIdx = -1;

vector<wstring> systemStoreApps = {};



// --- Window Title Overlay ---

static bool showTitleOverlay = false;

static wstring inputTitleText = L"";

static bool isTitleInputActive = true, hTitleInput = false;

static bool hTitleAdd = false, hTitleCancel = false;



// --- Simple Blocks States ---

static int simpleBlockMode = 1; // 0: Allow, 1: Block

static bool hoverModeDropdown = false, isModeDropdownOpen = false;

static bool hoverOptAllow = false, hoverOptBlock = false;



// Websites & Apps

static wstring webInputText = L"";

static bool isWebInputActive = false, hoverWebInput = false, hoverWebAddBtn = false;

static bool hoverWebCombo = false, isWebComboOpen = false;

static int hoverWebOptIdx = -1;

vector<wstring> commonWebsites = { L"facebook.com", L"youtube.com", L"instagram.com", L"tiktok.com", L"reddit.com", L"twitter.com" };



static wstring appInputText = L"";

static bool isAppInputActive = false, hoverAppInput = false, hoverAppAddBtn = false;

static bool hoverAppCombo = false, isAppComboOpen = false;

static int hoverAppOptIdx = -1;

vector<wstring> commonApps = { L"chrome.exe", L"msedge.exe", L"firefox.exe", L"telegram.exe", L"whatsapp.exe", L"discord.exe", L"vlc.exe" };



static bool hoverAddExe = false, hoverAddStoreApp = false, hoverAddWindowTitle = false;



struct BlockItem { wstring name; bool isHoveredCross; bool isSystemLocked; };

static vector<BlockItem> webList;

static vector<BlockItem> appList;



// --- Colors ---

static const Color SClrTeal(255, 12, 168, 176);

static const Color SClrTealHover(255, 30, 185, 195);

static const Color SClrWhite(255, 255, 255, 255);

static const Color SClrDark(255, 50, 50, 50);

static const Color SClrGrayText(255, 120, 120, 120);

static const Color SClrBorder(255, 220, 225, 230);

static const Color SClrBg(255, 248, 250, 252); 

static const Color SClrBgHover(255, 235, 248, 250); 

static const Color SClrGreen(255, 90, 170, 20); 

static const Color SClrGreenHover(255, 100, 190, 25);

static const Color SClrRed(255, 231, 76, 60);

static const Color SClrOverlay(180, 0, 0, 0); 

static const Color SClrDisabled(255, 200, 200, 200);

// Premium gold colors
static const Color SClrGold(255, 255, 193, 7);

static const Color SClrGoldDark(255, 180, 130, 0);

static const Color SClrPremiumBadgeBg(255, 255, 248, 220);

static const Color SClrPremiumBadgeBorder(255, 255, 193, 7);



// ==========================================

// UTILS

// ==========================================

int CountWords(const wstring& text) {

    int count = 0;

    bool inWord = false;

    for (wchar_t c : text) {

        if (iswspace(c) || c == L'\n' || c == L'\r') {

            inWord = false;

        } else {

            if (!inWord) count++;

            inWord = true;

        }

    }

    return count;

}



// ==========================================

// PREMIUM HELPER — draws a small gold crown badge next to a rect
// Call after drawing the button/label you want to mark as Premium.
// ==========================================
static void DrawPremiumCrown(Graphics& g, float rightEdgeX, float topY, Font* fIcon) {

    // Crown icon from Segoe MDL2 Assets: U+E2CC (medal/crown-like)
    // Use U+E2CC or U+E735 — pick whichever renders well in your environment.
    FontFamily ffIcons(L"Segoe MDL2 Assets");

    Font fCrown(&ffIcons, 13, FontStyleRegular, UnitPixel);

    SolidBrush goldBrush(SClrGold);

    StringFormat fmtC;

    fmtC.SetAlignment(StringAlignmentCenter);

    fmtC.SetLineAlignment(StringAlignmentCenter);

    // Draw a small gold rounded badge behind the crown
    RectF badgeRect(rightEdgeX, topY - 1.0f, 20.0f, 18.0f);

    SolidBrush badgeBg(SClrPremiumBadgeBg);

    Pen badgeBorder(SClrGold, 1.0f);

    GraphicsPath* bp = new GraphicsPath();

    float d = 4.0f;
    bp->AddArc(badgeRect.X, badgeRect.Y, d, d, 180, 90);
    bp->AddArc(badgeRect.X + badgeRect.Width - d, badgeRect.Y, d, d, 270, 90);
    bp->AddArc(badgeRect.X + badgeRect.Width - d, badgeRect.Y + badgeRect.Height - d, d, d, 0, 90);
    bp->AddArc(badgeRect.X, badgeRect.Y + badgeRect.Height - d, d, d, 90, 90);
    bp->CloseFigure();

    g.FillPath(&badgeBg, bp);

    g.DrawPath(&badgeBorder, bp);

    delete bp;

    // U+E2CC = crown in Segoe MDL2 Assets
    g.DrawString(L"\xE2CC", -1, &fCrown, badgeRect, &fmtC, &goldBrush);

}



// ==========================================

// DATA PERSISTENCE (SAVE / LOAD)

// ==========================================

wstring GetAppDataFolder() {

    wchar_t path[MAX_PATH];

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {

        wstring fullPath = wstring(path) + L"\\RasFocus";

        CreateDirectoryW(fullPath.c_str(), NULL);

        SetFileAttributesW(fullPath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        return fullPath;

    }

    return L"";

}



void SaveBlocksData() {

    wstring path = GetAppDataFolder() + L"\\blocks_data.dat";

    string narrowPathOut(path.begin(), path.end());

    ofstream out(narrowPathOut.c_str(), ios::binary);

    if(!out) return;

    

    out.write((char*)&simpleBlockMode, sizeof(simpleBlockMode));

    out.write((char*)&controlMode, sizeof(controlMode));

    out.write((char*)&showQuotes, sizeof(showQuotes));

    out.write((char*)&quoteLanguage, sizeof(quoteLanguage));

    out.write((char*)&focusHours, sizeof(focusHours));

    out.write((char*)&focusMins, sizeof(focusMins));

    out.write((char*)&focusEndTimeBlocks, sizeof(focusEndTimeBlocks));

    

    bool isActive = isFocusActive;

    out.write((char*)&isActive, sizeof(isActive));



    size_t wSize = webList.size();

    out.write((char*)&wSize, sizeof(wSize));

    for(auto& w : webList) {

        size_t len = w.name.length();

        out.write((char*)&len, sizeof(len));

        out.write((char*)w.name.data(), len * sizeof(wchar_t));

        out.write((char*)&w.isSystemLocked, sizeof(w.isSystemLocked));

    }



    size_t aSize = appList.size();

    out.write((char*)&aSize, sizeof(aSize));

    for(auto& a : appList) {

        size_t len = a.name.length();

        out.write((char*)&len, sizeof(len));

        out.write((char*)a.name.data(), len * sizeof(wchar_t));

        out.write((char*)&a.isSystemLocked, sizeof(a.isSystemLocked));

    }

}



void LoadBlocksData() {

    wstring path = GetAppDataFolder() + L"\\blocks_data.dat";

    string narrowPathIn(path.begin(), path.end());

    ifstream in(narrowPathIn.c_str(), ios::binary);

    if(!in) return;

    

    in.read((char*)&simpleBlockMode, sizeof(simpleBlockMode));

    in.read((char*)&controlMode, sizeof(controlMode));

    in.read((char*)&showQuotes, sizeof(showQuotes));

    in.read((char*)&quoteLanguage, sizeof(quoteLanguage));

    in.read((char*)&focusHours, sizeof(focusHours));

    in.read((char*)&focusMins, sizeof(focusMins));

    in.read((char*)&focusEndTimeBlocks, sizeof(focusEndTimeBlocks));



    bool savedFocus = false;

    if(in.read((char*)&savedFocus, sizeof(savedFocus))) {

        if (savedFocus) {

            time_t now = std::time(nullptr);

            if (now < focusEndTimeBlocks) {

                isFocusActive = true;

            }

        }

    }



    size_t wSize = 0;

    if(in.read((char*)&wSize, sizeof(wSize))) {

        webList.clear();

        for(size_t i=0; i<wSize; i++) {

            size_t len=0; in.read((char*)&len, sizeof(len));

            wstring name(len, L'\0');

            in.read((char*)name.data(), len * sizeof(wchar_t));

            bool sysLocked = false;

            in.read((char*)&sysLocked, sizeof(sysLocked));

            webList.push_back({name, false, sysLocked});

        }

    }

    

    size_t aSize = 0;

    if(in.read((char*)&aSize, sizeof(aSize))) {

        appList.clear();

        for(size_t i=0; i<aSize; i++) {

            size_t len=0; in.read((char*)&len, sizeof(len));

            wstring name(len, L'\0');

            in.read((char*)name.data(), len * sizeof(wchar_t));

            bool sysLocked = false;

            in.read((char*)&sysLocked, sizeof(sysLocked));

            appList.push_back({name, false, sysLocked});

        }

    }

}



// --- Helpers ---

static GraphicsPath* GetBlockRoundRectPath(RectF rect, int radius) {

    GraphicsPath* path = new GraphicsPath();

    float d = radius * 2.0f;

    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);

    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);

    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);

    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);

    path->CloseFigure(); return path;

}



static wstring toLowerW_Blocks(wstring str) {

    for (auto& c : str) c = towlower(c); return str;

}



// --- Smart Tab Closer (ONLY TAB CLOSE) ---

void CloseActiveTabOnly(HWND hBrowser) { 

    if (GetForegroundWindow() == hBrowser) {

        keybd_event(VK_CONTROL, 0, 0, 0); 

        keybd_event('W', 0, 0, 0); 

        keybd_event('W', 0, KEYEVENTF_KEYUP, 0); 

        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); 

    }

}



// --- Smart Tab Closer (TAB CLOSE + MINIMIZE) ---

void CloseActiveTabAndMinimize(HWND hBrowser) { 

    if (GetForegroundWindow() == hBrowser) {

        keybd_event(VK_CONTROL, 0, 0, 0); 

        keybd_event('W', 0, 0, 0); 

        keybd_event('W', 0, KEYEVENTF_KEYUP, 0); 

        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); 

        Sleep(50); 

    }

    ShowWindow(hBrowser, SW_MINIMIZE); 

}



void EnforceSystemApps() {

    for (auto it = appList.begin(); it != appList.end(); ) {

        if (it->isSystemLocked) it = appList.erase(it);

        else ++it;

    }

    if (simpleBlockMode == 0) {

        appList.insert(appList.begin(), {L"explorer.exe", false, true});

        appList.insert(appList.begin(), {L"svchost.exe", false, true});

        appList.insert(appList.begin(), {L"RasFocus.exe", false, true});

        appList.insert(appList.begin(), {L"Taskmgr.exe", false, true});

        appList.insert(appList.begin(), {L"chrome.exe", false, true}); 

        appList.insert(appList.begin(), {L"msedge.exe", false, true});

    }

    SaveBlocksData();

}



// ==========================================

// CRASH-FREE SAFE POPUP THREAD LOGIC

// ==========================================

struct BlocksPopupData { wstring quote; };



LRESULT CALLBACK BlocksPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    if (msg == WM_PAINT) {

        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);

        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);

        RECT rect; GetClientRect(hwnd, &rect); RectF bgRect(0, 0, rect.right, rect.bottom);



        SolidBrush bgBrush(Color(255, 20, 80, 40)); 

        g.FillRectangle(&bgBrush, bgRect);

        Pen border(SClrTeal, 4.0f); g.DrawRectangle(&border, 2.0f, 2.0f, rect.right-4.0f, rect.bottom-4.0f);



        FontFamily ff(L"Segoe UI"); Font fQ(&ff, 32, FontStyleBold, UnitPixel);

        SolidBrush whiteBrush(Color(255, 255, 255, 255));

        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);



        BlocksPopupData* pData = (BlocksPopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        if (pData) {

            g.DrawString(pData->quote.c_str(), -1, &fQ, RectF(20.0f, 20.0f, rect.right - 40.0f, rect.bottom - 40.0f), &fmtC, &whiteBrush);

        }

        EndPaint(hwnd, &ps); return 0;

    }

    if (msg == WM_TIMER && wParam == 1) { 

        KillTimer(hwnd, 1); DestroyWindow(hwnd); PostQuitMessage(0); return 0; 

    }

    if (msg == WM_DESTROY) {

        BlocksPopupData* pData = (BlocksPopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        if(pData) delete pData;

    }

    return DefWindowProc(hwnd, msg, wParam, lParam);

}



void SafeBlocksPopupThread(wstring quote) {

    WNDCLASS wc = {0}; 

    wc.lpfnWndProc = BlocksPopupWndProc; 

    wc.hInstance = GetModuleHandle(NULL); 

    wc.lpszClassName = "RasFocusBlocksPopupClass"; 

    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); 

    RegisterClass(&wc);



    int screenW = GetSystemMetrics(SM_CXSCREEN);

    int w = 1000; int h = 250;

    HWND hPopup = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 

        "RasFocusBlocksPopupClass", "Alert", WS_POPUP, (screenW-w)/2, 80, w, h, NULL, NULL, GetModuleHandle(NULL), NULL);

    

    if (hPopup) {

        BlocksPopupData* data = new BlocksPopupData{ quote };

        SetWindowLongPtr(hPopup, GWLP_USERDATA, (LONG_PTR)data);

        SetLayeredWindowAttributes(hPopup, 0, 240, LWA_ALPHA); 

        ShowWindow(hPopup, SW_SHOW); 

        SetForegroundWindow(hPopup); 

        SetTimer(hPopup, 1, 6000, NULL); 

        

        MSG msg;

        while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    }

}



void TriggerGlobalBlockAlert(bool isWarning = false, wstring customMsg = L"") {

    wstring finalQuote = L"Focus Active! Access Denied.";

    if (isWarning) {

        finalQuote = customMsg;

    } else if (showQuotes) {

        int rIdx = rand() % motivationalQuotes.size();

        finalQuote = (quoteLanguage == 0) ? motivationalQuotes[rIdx].bn : motivationalQuotes[rIdx].en;

    }

    thread t(SafeBlocksPopupThread, finalQuote);

    t.detach();

}



// ==========================================

// GLOBAL KEYLOGGER FOR BLOCKS TAB

// ==========================================

HHOOK hKeyboardHookBlocks = NULL;

string globalKeyBufferBlocks = "";



LRESULT CALLBACK KeyboardHookProcBlocks(int nCode, WPARAM wParam, LPARAM lParam) {

    if (nCode >= 0 && wParam == WM_KEYDOWN && isFocusActive) {

        KBDLLHOOKSTRUCT* kbdStruct = (KBDLLHOOKSTRUCT*)lParam;

        DWORD vkCode = kbdStruct->vkCode;



        if ((vkCode >= 'A' && vkCode <= 'Z') || (vkCode >= '0' && vkCode <= '9') || vkCode == VK_SPACE || vkCode == VK_OEM_PERIOD) {

            char c = MapVirtualKey(vkCode, MAPVK_VK_TO_CHAR);

            if (vkCode == VK_OEM_PERIOD) c = '.'; 

            globalKeyBufferBlocks += tolower(c);

            if (globalKeyBufferBlocks.length() > 100) globalKeyBufferBlocks.erase(0, 1);



            wstring wBuffer(globalKeyBufferBlocks.begin(), globalKeyBufferBlocks.end());

            bool shouldBlock = false;



            for (const auto& web : webList) {

                wstring lowerWeb = toLowerW_Blocks(web.name);

                size_t dotPos = lowerWeb.find(L".");

                wstring coreName = (dotPos != wstring::npos) ? lowerWeb.substr(0, dotPos) : lowerWeb;

                if (coreName.length() > 2 && wBuffer.find(coreName) != wstring::npos) {

                    shouldBlock = true; break;

                }

            }



            if (!shouldBlock && simpleBlockMode == 1) {

                for (const auto& app : appList) {

                    wstring lowerApp = toLowerW_Blocks(app.name);

                    size_t dotPos = lowerApp.find(L".");

                    wstring coreName = (dotPos != wstring::npos) ? lowerApp.substr(0, dotPos) : lowerApp;

                    if (coreName.length() > 2 && wBuffer.find(coreName) != wstring::npos) {

                        shouldBlock = true; break;

                    }

                }

            }



            if (shouldBlock) {

                globalKeyBufferBlocks = ""; 

                HWND hActive = GetForegroundWindow();

                if (hActive) { CloseActiveTabOnly(hActive); }

                TriggerGlobalBlockAlert();

            }

        } 

        else if (vkCode == VK_BACK) {

            if (!globalKeyBufferBlocks.empty()) globalKeyBufferBlocks.pop_back();

        }

    }

    return CallNextHookEx(hKeyboardHookBlocks, nCode, wParam, lParam);

}



void StartBlocksKeyloggerThread() {

    hKeyboardHookBlocks = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProcBlocks, NULL, 0);

    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

}



// --- REAL BLOCKING ENGINE ---

bool ShouldKillProcessBlocks(const wstring& processName, const wstring& windowTitle) {

    if (!isFocusActive) return false;



    wstring lowerTitle = toLowerW_Blocks(windowTitle);

    wstring lowerProcess = toLowerW_Blocks(processName);

    

    if (lowerTitle.find(L"task manager") != wstring::npos || lowerTitle.find(L"taskmgr") != wstring::npos) {

        TriggerGlobalBlockAlert(true, L"Focus is Active. Task Manager is blocked!");

        return true;

    }

    if (lowerTitle.find(L"uninstall") != wstring::npos || lowerTitle.find(L"programs and features") != wstring::npos || lowerTitle.find(L"অ্যাপস ও বৈশিষ্ট্য") != wstring::npos) {

        TriggerGlobalBlockAlert(true, L"Focus is Active. Uninstallation is blocked!");

        return true;

    }



    if (simpleBlockMode == 1) { 

        for (const auto& app : appList) {

            wstring lowerApp = toLowerW_Blocks(app.name);

            if (lowerProcess == lowerApp || lowerProcess.find(lowerApp) != wstring::npos || lowerTitle.find(lowerApp) != wstring::npos) {

                TriggerGlobalBlockAlert();

                return true;

            }

        }

    } 

    else if (simpleBlockMode == 0) {

        bool isAllowed = false;

        for (const auto& app : appList) {

            wstring lowerApp = toLowerW_Blocks(app.name);

            if (lowerProcess == lowerApp || lowerTitle.find(lowerApp) != wstring::npos) {

                isAllowed = true; break;

            }

        }

        if (!isAllowed) {

            if (windowTitle.length() > 0 && processName != L"explorer.exe") {

                TriggerGlobalBlockAlert(true, L"Focus is Active. Only allowed apps can run.");

                return true; 

            }

        }

    }

    return false;

}



BOOL CALLBACK EnumWindowsProcBlocker(HWND hwnd, LPARAM lParam) {

    if (!IsWindowVisible(hwnd)) return TRUE;

    

    wchar_t windowTitle[256]; GetWindowTextW(hwnd, windowTitle, 256);

    DWORD processId; GetWindowThreadProcessId(hwnd, &processId);

    

    if (processId == GetCurrentProcessId()) return TRUE; 



    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, FALSE, processId);

    if (hProcess) {

        wchar_t processName[MAX_PATH]; HMODULE hMod; DWORD cbNeeded;

        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {

            GetModuleBaseNameW(hProcess, hMod, processName, sizeof(processName)/sizeof(wchar_t));

            wstring pName(processName); wstring wTitle(windowTitle);

            if (ShouldKillProcessBlocks(pName, wTitle)) {

                PostMessage(hwnd, WM_CLOSE, 0, 0); 

                TerminateProcess(hProcess, 0); 

            }

        }

        CloseHandle(hProcess);

    }

    return TRUE;

}



void BackgroundBlockerThread() {

    int processKillTimer = 0;

    while (true) {

        if (isFocusActive || g_parentLockAllTabs) {

            HWND hActive = GetForegroundWindow();

            if (hActive) {

                wchar_t windowTitle[512];

                if (GetWindowTextW(hActive, windowTitle, 512) > 0) {

                    wstring lowerTitle = toLowerW_Blocks(windowTitle);

                    bool shouldBlockTab = false;

                    for (const auto& web : webList) {

                        wstring lowerWeb = toLowerW_Blocks(web.name);

                        size_t dotPos = lowerWeb.find(L".");

                        wstring coreName = (dotPos != wstring::npos) ? lowerWeb.substr(0, dotPos) : lowerWeb;

                        if (coreName.length() > 2 && lowerTitle.find(coreName) != wstring::npos) {

                            shouldBlockTab = true; break;

                        }

                    }

                    if (shouldBlockTab) {

                        CloseActiveTabAndMinimize(hActive); 

                        TriggerGlobalBlockAlert();          

                        Sleep(1500); 

                    }

                }

            }

            processKillTimer += 50;

            if (processKillTimer >= 2000) {

                EnumWindows(EnumWindowsProcBlocker, 0); 

                processKillTimer = 0;

            }

        }

        Sleep(50);

    }

}



static bool threadStarted = false;

void StartBlockerThread() {

    if (!threadStarted) {

        thread t(BackgroundBlockerThread); t.detach();

        thread kl(StartBlocksKeyloggerThread); kl.detach(); 

        threadStarted = true;

    }

}



void RefreshRunningApps() {

    systemStoreApps.clear();

    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hProcessSnap == INVALID_HANDLE_VALUE) return;

    

    PROCESSENTRY32W pe32; pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hProcessSnap, &pe32)) {

        do {

            wstring exeName = pe32.szExeFile;

            if (exeName != L"svchost.exe" && exeName != L"conhost.exe" && exeName != L"System" && exeName.length() > 4) {

                bool exists = false;

                for (const auto& app : systemStoreApps) { if (app == exeName) { exists = true; break; } }

                if (!exists && systemStoreApps.size() < 100) systemStoreApps.push_back(exeName);

            }

        } while (Process32NextW(hProcessSnap, &pe32));

    }

    CloseHandle(hProcessSnap);

    

    vector<wstring> priorities = {L"chrome", L"msedge", L"firefox", L"telegram", L"whatsapp", L"discord", L"vlc", L"spotify", L"netflix", L"zoom", L"skype"};

    auto getPriority = [&](const wstring& name) {

        wstring lower = toLowerW_Blocks(name);

        for (const auto& p : priorities) {

            if (lower.find(p) != wstring::npos) return 0;

        }

        return 1;

    };

    

    std::sort(systemStoreApps.begin(), systemStoreApps.end(), [&](const wstring& a, const wstring& b) {

        int pA = getPriority(a); int pB = getPriority(b);

        if(pA != pB) return pA < pB;

        return a < b;

    });



    if(systemStoreApps.empty()) systemStoreApps.push_back(L"No active apps found");

}



static void DrawBlocksOverlaySpinner(Graphics& g, float x, float y, const wstring& valStr, bool hM, bool hP, Font* fIcon, Font* fBold) {

    SolidBrush brushBtn(SClrBorder); SolidBrush brushBtnHover(SClrGrayText);

    SolidBrush brushWhite(SClrWhite); SolidBrush brushDark(SClrDark);

    Pen penBorder(SClrBorder, 1.5f);

    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);



    RectF mRect(x, y, 32.0f, 36.0f); RectF tRect(x + 32.0f, y, 60.0f, 36.0f); RectF pRect(x + 92.0f, y, 32.0f, 36.0f);



    g.FillRectangle(hM ? &brushBtnHover : &brushBtn, mRect); g.DrawRectangle(&penBorder, mRect.X, mRect.Y, mRect.Width, mRect.Height);

    g.DrawString(L"\xE738", -1, fIcon, mRect, &fmtC, &brushDark);

    g.FillRectangle(&brushWhite, tRect); g.DrawRectangle(&penBorder, tRect.X, tRect.Y, tRect.Width, tRect.Height);

    g.DrawString(valStr.c_str(), -1, fBold, tRect, &fmtC, &brushDark);

    g.FillRectangle(hP ? &brushBtnHover : &brushBtn, pRect); g.DrawRectangle(&penBorder, pRect.X, pRect.Y, pRect.Width, pRect.Height);

    g.DrawString(L"\xE710", -1, fIcon, pRect, &fmtC, &brushDark);

}



// ==========================================

// MAIN DRAWING FUNCTION

// ==========================================

void DrawBlocksTab(Graphics& g, float contentX, float contentY, float contentW, float contentH) {

    static bool isBlocksDataLoaded = false;

    if (!isBlocksDataLoaded) {

        LoadBlocksData();

        isBlocksDataLoaded = true;

    }



    StartBlockerThread(); 

    

    // Smooth scroll interpolations

    cWebScrollY += (tWebScrollY - cWebScrollY) * 0.2f;

    if (abs(tWebScrollY - cWebScrollY) < 0.5f) cWebScrollY = tWebScrollY;

    cAppScrollY += (tAppScrollY - cAppScrollY) * 0.2f;

    if (abs(tAppScrollY - cAppScrollY) < 0.5f) cAppScrollY = tAppScrollY;

    cStoreScrollY += (tStoreScrollY - cStoreScrollY) * 0.2f;

    if (abs(tStoreScrollY - cStoreScrollY) < 0.5f) cStoreScrollY = tStoreScrollY;



    s_contentX = contentX; s_contentY = contentY; s_contentW = contentW; s_contentH = contentH;



    FontFamily ff(L"Segoe UI");

    Font fTopTab(&ff, 14, FontStyleBold, UnitPixel);

    Font fTitle(&ff, 24, FontStyleBold, UnitPixel); 

    Font fNormal(&ff, 15, FontStyleRegular, UnitPixel); Font fBold(&ff, 15, FontStyleBold, UnitPixel);

    Font fInfo(&ff, 13, FontStyleItalic, UnitPixel); 

    Font fSmallerBold(&ff, 12, FontStyleBold, UnitPixel);

    FontFamily ffIcons(L"Segoe MDL2 Assets");

    Font fIcon(&ffIcons, 22, FontStyleRegular, UnitPixel); Font fSmallIcon(&ffIcons, 14, FontStyleRegular, UnitPixel);

    

    SolidBrush brushTeal(SClrTeal); SolidBrush brushDark(SClrDark); SolidBrush brushGray(SClrGrayText); 

    SolidBrush brushWhite(SClrWhite); SolidBrush brushBg(SClrBg); SolidBrush brushRed(SClrRed);

    Pen penBorder(SClrBorder, 1.5f); Pen penTeal(SClrTeal, 2.0f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);



    // ==========================================

    // --- HEADER: 4 TABS ---
    // Tab 1 (Schedule Blocks) and Tab 3 (Device Block) are Premium.
    // ==========================================

    g.FillRectangle(&brushWhite, contentX, contentY, contentW, 60.0f); 



    float tabH = 40.0f;

    float tabW = (contentW - 60.0f) / 4.0f - 7.5f;

    float tabGap = 10.0f;

    float tab1X = contentX + 20.0f;

    float tab2X = tab1X + tabW + tabGap;

    float tab3X = tab2X + tabW + tabGap;

    float tab4X = tab3X + tabW + tabGap;

    float tabY  = contentY + 10.0f;



    // Tab backgrounds

    SolidBrush bTab1(currentBlockTab == 0 ? Color(255,12,168,176) : (hoverBlockTab==0 ? Color(255,230,230,230) : Color(255,245,245,245)));

    // Schedule Blocks — Premium tab: gold tint when not active
    SolidBrush bTab2(currentBlockTab == 1 ? Color(255,12,168,176) : (hoverBlockTab==1 ? Color(255,255,240,180) : Color(255,255,250,220)));

    SolidBrush bTab3(currentBlockTab == 2 ? Color(255,12,168,176) : (hoverBlockTab==2 ? Color(255,230,230,230) : Color(255,245,245,245)));

    // Device Block — Premium tab: gold tint when not active
    SolidBrush bTab4(currentBlockTab == 3 ? Color(255,12,168,176) : (hoverBlockTab==3 ? Color(255,255,240,180) : Color(255,255,250,220)));



    SolidBrush bT1(currentBlockTab==0 ? Color(255,255,255,255) : Color(255,100,100,100));

    SolidBrush bT2(currentBlockTab==1 ? Color(255,255,255,255) : Color(255,140,100,0));   // gold text when inactive

    SolidBrush bT3(currentBlockTab==2 ? Color(255,255,255,255) : Color(255,100,100,100));

    SolidBrush bT4(currentBlockTab==3 ? Color(255,255,255,255) : Color(255,140,100,0));   // gold text when inactive



    // Tab 0 — Simple Blocks (FREE)
    g.FillRectangle(&bTab1, tab1X, tabY, tabW, tabH);

    g.DrawString(L"Simple Blocks", -1, &fTopTab, RectF(tab1X, tabY, tabW, tabH), &fmtC, &bT1);



    // Tab 1 — Schedule Blocks (PREMIUM) — draw tab + crown badge
    g.FillRectangle(&bTab2, tab2X, tabY, tabW, tabH);

    // Draw text slightly left of center to leave room for crown
    StringFormat fmtLCenter; fmtLCenter.SetAlignment(StringAlignmentCenter); fmtLCenter.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Schedule Blocks", -1, &fTopTab, RectF(tab2X, tabY, tabW - 22.0f, tabH), &fmtLCenter, &bT2);

    if (!g_isPremiumUser) DrawPremiumCrown(g, tab2X + tabW - 24.0f, tabY + 11.0f, &fSmallIcon);



    // Tab 2 — Adult Block (FREE)
    g.FillRectangle(&bTab3, tab3X, tabY, tabW, tabH);

    g.DrawString(L"Adult Block", -1, &fTopTab, RectF(tab3X, tabY, tabW, tabH), &fmtC, &bT3);



    // Tab 3 — Device Block (PREMIUM) — draw tab + crown badge
    g.FillRectangle(&bTab4, tab4X, tabY, tabW, tabH);

    g.DrawString(L"Device Block", -1, &fTopTab, RectF(tab4X, tabY, tabW - 22.0f, tabH), &fmtLCenter, &bT4);

    if (!g_isPremiumUser) DrawPremiumCrown(g, tab4X + tabW - 24.0f, tabY + 11.0f, &fSmallIcon);



    float bodyY = contentY + 60.0f;

    g.FillRectangle(&brushBg, contentX, bodyY, contentW, contentH - 60.0f); 



    float boxX = contentX + 30.0f;

    float boxW = contentW - 60.0f;

    float boxH = contentH - 60.0f - 40.0f; 

    GraphicsPath* boxPath = GetBlockRoundRectPath(RectF(boxX, bodyY + 20.0f, boxW, boxH), 6);

    g.FillPath(&brushWhite, boxPath); g.DrawPath(&penBorder, boxPath); delete boxPath;



    float ctrlDropX = boxX + 30.0f; float ctrlDropY = bodyY + 40.0f;

    float modeDropX = boxX + 150.0f; float modeDropY = ctrlDropY + 75.0f;

    float webComboX = boxX + 30.0f + ((boxW - 90.0f) / 2.0f) - 105.0f;

    float webComboY = modeDropY + 95.0f;



    // ==========================================

    // TAB 0: SIMPLE BLOCKS

    // ==========================================

    if (currentBlockTab == 0) { 

        float rowY = bodyY + 40.0f;



        // --- Control Mode Dropdown ---
        // "Self Control" is FREE.
        // "Parents Control" and "Long Text Control" are PREMIUM.
        RectF ctrlDrop(ctrlDropX, rowY, 160.0f, 40.0f);

        GraphicsPath* cdp = GetBlockRoundRectPath(ctrlDrop, 4);

        SolidBrush cDropBg((!isFocusActive && hoverControlDropdown) ? SClrBgHover : (isFocusActive ? SClrBg : SClrWhite));

        g.FillPath(&cDropBg, cdp); g.DrawPath(&penBorder, cdp); delete cdp;

        

        wstring ctrlTxt = (controlMode == 0) ? L"Self Control" : ((controlMode == 1) ? L"Parents Control" : L"Long Text Control");

        g.DrawString(ctrlTxt.c_str(), -1, &fBold, RectF(ctrlDrop.X + 15.0f, ctrlDrop.Y, ctrlDrop.Width - 30.0f, ctrlDrop.Height), &fmtL, isFocusActive ? &brushGray : &brushDark);

        g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(ctrlDrop.X + ctrlDrop.Width - 30.0f, ctrlDrop.Y, 30.0f, ctrlDrop.Height), &fmtC, &brushGray);



        RectF startBtn(boxX + 205.0f, rowY, 140.0f, 40.0f);

        

        if (isFocusActive && controlMode == 0) {

            if (std::time(nullptr) >= focusEndTimeBlocks) { 

                isFocusActive = false;

                SaveBlocksData(); 

            }

        }



        SolidBrush sbBrush(isFocusActive ? (hoverStartFocusBtn ? Color(255,200,50,50) : SClrRed) : (hoverStartFocusBtn ? SClrGreenHover : SClrGreen));

        GraphicsPath* sbp = GetBlockRoundRectPath(startBtn, 4); g.FillPath(&sbBrush, sbp); delete sbp;

        

        wstring startTextStr = L"Start Focus";

        if (isFocusActive) {

            if (controlMode == 0) {

                time_t left = focusEndTimeBlocks - std::time(nullptr);

                int mLeft = (left / 60) + 1;

                startTextStr = L"Locked (" + to_wstring(mLeft) + L"m)";

            } else {

                startTextStr = L"Stop Focus";

            }

        }

        g.DrawString(startTextStr.c_str(), -1, &fBold, startBtn, &fmtC, &brushWhite);

        

        if (isFocusActive) {

            g.DrawString(L"FOCUS ACTIVE: Uninstallation & Task Manager restricted.", -1, &fInfo, RectF(boxX + 360.0f, rowY, 500.0f, 40.0f), &fmtL, &brushRed);

        }



        rowY += 60.0f; g.DrawLine(&penBorder, boxX + 30.0f, rowY, boxX + boxW - 30.0f, rowY); rowY += 15.0f;



        SolidBrush activeTextBrush(isFocusActive ? SClrGrayText : SClrDark);

        SolidBrush activeTealBrush(isFocusActive ? SClrGrayText : SClrTeal);

        SolidBrush activeInputBg(isFocusActive ? SClrBg : SClrWhite);



        g.DrawString(L"Select Mode:", -1, &fBold, RectF(boxX + 30.0f, rowY, 120.0f, 36.0f), &fmtL, &activeTextBrush);

        RectF dropRect(modeDropX, rowY, 200.0f, 36.0f);

        GraphicsPath* dp = GetBlockRoundRectPath(dropRect, 4);

        SolidBrush dropBg(isFocusActive ? SClrBg : (hoverModeDropdown ? SClrBgHover : SClrWhite));

        g.FillPath(&dropBg, dp); g.DrawPath(&penBorder, dp); delete dp;



        wstring modeTxt = (simpleBlockMode == 0) ? L"Allow Apps & Web" : L"Block Apps & Web";

        g.DrawString(modeTxt.c_str(), -1, &fNormal, RectF(dropRect.X + 15.0f, dropRect.Y, dropRect.Width - 30.0f, dropRect.Height), &fmtL, &activeTealBrush);

        g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(dropRect.X + dropRect.Width - 30.0f, dropRect.Y, 30.0f, dropRect.Height), &fmtC, &brushGray);



        float colW = (boxW - 90.0f) / 2.0f; float leftColX = boxX + 30.0f; float rightColX = boxX + 60.0f + colW; float secY = rowY + 50.0f;



        // LEFT COLUMN: WEBSITES

        g.DrawString(L"Websites", -1, &fTitle, RectF(leftColX, secY, colW, 40.0f), &fmtL, &brushDark); 

        

        RectF webInpRect(leftColX, secY + 45.0f, colW - 110.0f, 36.0f); 

        GraphicsPath* wp = GetBlockRoundRectPath(webInpRect, 4);

        g.FillPath(&brushWhite, wp); g.DrawPath((isWebInputActive ? &penTeal : &penBorder), wp); delete wp;

        

        if (webInputText.empty() && !isWebInputActive) g.DrawString(L"e.g. facebook.com", -1, &fNormal, RectF(webInpRect.X + 10.0f, webInpRect.Y, webInpRect.Width, webInpRect.Height), &fmtL, &brushGray);

        else {

            g.DrawString(webInputText.c_str(), -1, &fNormal, RectF(webInpRect.X + 10.0f, webInpRect.Y, webInpRect.Width, webInpRect.Height), &fmtL, &brushDark);

            if (isWebInputActive && (GetTickCount() / 500) % 2 == 0) {

                Graphics gTemp(GetDesktopWindow()); RectF bRect; gTemp.MeasureString(webInputText.c_str(), -1, &fNormal, PointF(0,0), &bRect);

                float cursorX = webInputText.empty() ? webInpRect.X + 10.0f : webInpRect.X + 10.0f + bRect.Width;

                g.FillRectangle(&brushDark, cursorX, webInpRect.Y + 8.0f, 1.5f, 20.0f);

            }

        }



        RectF wComboRect(webComboX, secY + 45.0f, 30.0f, 36.0f);

        GraphicsPath* wcp = GetBlockRoundRectPath(wComboRect, 4);

        SolidBrush wComboBrush(isFocusActive ? SClrDisabled : (hoverWebCombo ? SClrBorder : SClrWhite));

        g.FillPath(&wComboBrush, wcp); g.DrawPath(&penBorder, wcp); delete wcp;

        g.DrawString(L"\xE70D", -1, &fSmallIcon, wComboRect, &fmtC, &brushDark);



        RectF wAddRect(leftColX + colW - 70.0f, secY + 45.0f, 70.0f, 36.0f);

        GraphicsPath* wAddP = GetBlockRoundRectPath(wAddRect, 4);

        SolidBrush wAddBrush(hoverWebAddBtn ? SClrTealHover : SClrTeal);

        g.FillPath(&wAddBrush, wAddP); delete wAddP;

        g.DrawString(L"+ Add", -1, &fBold, wAddRect, &fmtC, &brushWhite);



        RectF webTable(leftColX, secY + 90.0f, colW, 160.0f);

        g.FillRectangle(&brushBg, webTable); g.DrawRectangle(&penBorder, webTable.X, webTable.Y, webTable.Width, webTable.Height);

        

        Region oldClip; g.GetClip(&oldClip);

        g.SetClip(webTable);

        float itemY = webTable.Y + 5.0f - cWebScrollY;

        for (size_t i = 0; i < webList.size(); ++i) {

            if (itemY > webTable.Y - 30.0f && itemY < webTable.Y + webTable.Height) {

                g.DrawString(webList[i].name.c_str(), -1, &fNormal, RectF(leftColX + 10.0f, itemY, colW - 40.0f, 30.0f), &fmtL, &brushDark);

                SolidBrush crossBrush(isFocusActive ? SClrGrayText : (webList[i].isHoveredCross ? SClrRed : SClrGrayText));

                g.DrawString(L"\xE711", -1, &fSmallIcon, RectF(leftColX + colW - 30.0f, itemY, 30.0f, 30.0f), &fmtC, &crossBrush);

                g.DrawLine(&penBorder, leftColX + 5.0f, itemY + 30.0f, leftColX + colW - 5.0f, itemY + 30.0f);

            }

            itemY += 30.0f;

        }

        g.SetClip(&oldClip);

        

        if (webList.size() * 30.0f > webTable.Height) {

            float maxScroll = webList.size() * 30.0f - webTable.Height + 10.0f;

            float thumbH = max(20.0f, (webTable.Height / (webList.size() * 30.0f)) * webTable.Height);

            float thumbY = webTable.Y + (cWebScrollY / maxScroll) * (webTable.Height - thumbH);

            g.FillRectangle(&brushGray, webTable.X + webTable.Width - 4.0f, thumbY, 4.0f, thumbH);

        }



        // RIGHT COLUMN: APPS

        float qY = secY; 

        SolidBrush cbBrush(showQuotes ? (isFocusActive ? SClrGrayText : SClrTeal) : SClrWhite);

        RectF cbRect(rightColX, qY + 2.0f, 16.0f, 16.0f);

        g.FillRectangle(&cbBrush, cbRect); g.DrawRectangle(&penBorder, cbRect.X, cbRect.Y, cbRect.Width, cbRect.Height);

        if (showQuotes) g.DrawString(L"\xE73E", -1, &fSmallIcon, cbRect, &fmtC, &brushWhite);

        g.DrawString(L"Motivational Quotes", -1, &fNormal, RectF(rightColX + 25.0f, qY, 150.0f, 20.0f), &fmtL, &activeTextBrush);



        RectF langRect(rightColX + 180.0f, qY - 2.0f, 100.0f, 24.0f);

        g.FillRectangle(&activeInputBg, langRect); g.DrawRectangle(&penBorder, langRect.X, langRect.Y, langRect.Width, langRect.Height);

        wstring langTxt = (quoteLanguage == 0) ? L"Bangla" : L"English";

        g.DrawString(langTxt.c_str(), -1, &fNormal, RectF(langRect.X+5.0f, langRect.Y, 70.0f, 24.0f), &fmtL, &activeTextBrush);

        g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(langRect.X+75.0f, langRect.Y, 25.0f, 24.0f), &fmtC, &brushGray);



        g.DrawString(L"Applications", -1, &fTitle, RectF(rightColX, secY + 25.0f, colW, 40.0f), &fmtL, &brushDark);

        RectF appInpRect(rightColX, secY + 65.0f, colW - 110.0f, 36.0f);

        GraphicsPath* ap = GetBlockRoundRectPath(appInpRect, 4);

        g.FillPath(&brushWhite, ap); g.DrawPath((isAppInputActive ? &penTeal : &penBorder), ap); delete ap;

        

        if (appInputText.empty() && !isAppInputActive) g.DrawString(L"e.g. telegram.exe", -1, &fNormal, RectF(appInpRect.X + 10.0f, appInpRect.Y, appInpRect.Width, appInpRect.Height), &fmtL, &brushGray);

        else {

            g.DrawString(appInputText.c_str(), -1, &fNormal, RectF(appInpRect.X + 10.0f, appInpRect.Y, appInpRect.Width, appInpRect.Height), &fmtL, &brushDark);

            if (isAppInputActive && (GetTickCount() / 500) % 2 == 0) {

                Graphics gTemp(GetDesktopWindow()); RectF bRect; gTemp.MeasureString(appInputText.c_str(), -1, &fNormal, PointF(0,0), &bRect);

                float cursorX = appInputText.empty() ? appInpRect.X + 10.0f : appInpRect.X + 10.0f + bRect.Width;

                g.FillRectangle(&brushDark, cursorX, appInpRect.Y + 8.0f, 1.5f, 20.0f);

            }

        }



        float aComboX = rightColX + colW - 105.0f;

        RectF aComboRect(aComboX, secY + 65.0f, 30.0f, 36.0f);

        GraphicsPath* acp = GetBlockRoundRectPath(aComboRect, 4);

        SolidBrush aComboBrush(isFocusActive ? SClrDisabled : (hoverAppCombo ? SClrBorder : SClrWhite));

        g.FillPath(&aComboBrush, acp); g.DrawPath(&penBorder, acp); delete acp;

        g.DrawString(L"\xE70D", -1, &fSmallIcon, aComboRect, &fmtC, &brushDark);



        RectF aAddRect(rightColX + colW - 70.0f, secY + 65.0f, 70.0f, 36.0f);

        GraphicsPath* aAddP = GetBlockRoundRectPath(aAddRect, 4);

        SolidBrush aAddBrush(hoverAppAddBtn ? SClrTealHover : SClrTeal);

        g.FillPath(&aAddBrush, aAddP); delete aAddP;

        g.DrawString(L"+ Add", -1, &fBold, aAddRect, &fmtC, &brushWhite);



        RectF appTable(rightColX, secY + 110.0f, colW, 140.0f);

        g.FillRectangle(&brushBg, appTable); g.DrawRectangle(&penBorder, appTable.X, appTable.Y, appTable.Width, appTable.Height);

        

        g.GetClip(&oldClip);

        g.SetClip(appTable);

        float aItemY = appTable.Y + 5.0f - cAppScrollY;

        for (size_t i = 0; i < appList.size(); ++i) {

            if (aItemY > appTable.Y - 30.0f && aItemY < appTable.Y + appTable.Height) {

                g.DrawString(appList[i].name.c_str(), -1, &fNormal, RectF(rightColX + 10.0f, aItemY, colW - 40.0f, 30.0f), &fmtL, &brushDark);

                if (!appList[i].isSystemLocked) { 

                    SolidBrush crossBrush(isFocusActive ? SClrGrayText : (appList[i].isHoveredCross ? SClrRed : SClrGrayText));

                    g.DrawString(L"\xE711", -1, &fSmallIcon, RectF(rightColX + colW - 30.0f, aItemY, 30.0f, 30.0f), &fmtC, &crossBrush);

                } else {

                    g.DrawString(L"\xE72E", -1, &fSmallIcon, RectF(rightColX + colW - 30.0f, aItemY, 30.0f, 30.0f), &fmtC, &brushTeal); 

                }

                g.DrawLine(&penBorder, rightColX + 5.0f, aItemY + 30.0f, rightColX + colW - 5.0f, aItemY + 30.0f);

            }

            aItemY += 30.0f;

        }

        g.SetClip(&oldClip);



        if (appList.size() * 30.0f > appTable.Height) {

            float maxScroll = appList.size() * 30.0f - appTable.Height + 10.0f;

            float thumbH = max(20.0f, (appTable.Height / (appList.size() * 30.0f)) * appTable.Height);

            float thumbY = appTable.Y + (cAppScrollY / maxScroll) * (appTable.Height - thumbH);

            g.FillRectangle(&brushGray, appTable.X + appTable.Width - 4.0f, thumbY, 4.0f, thumbH);

        }



        // ---- Three Action Buttons: Add Exe (FREE), Add Store (PREMIUM), Add Title (PREMIUM) ----

        float btnW = (colW - 20.0f) / 3.0f;

        float btnY = secY + 260.0f;



        // Button 1 — Add Exe (FREE)

        SolidBrush greenBtn(hoverAddExe ? SClrGreenHover : SClrGreen); 

        RectF b1(rightColX, btnY, btnW, 40.0f);

        GraphicsPath* p1 = GetBlockRoundRectPath(b1, 4); g.FillPath(&greenBtn, p1); delete p1;

        g.DrawString(L"Add Exe", -1, &fSmallerBold, b1, &fmtC, &brushWhite); 



        // Button 2 — Add Store (PREMIUM): dimmed when not premium

        {

            bool isPrem = g_isPremiumUser;

            Color btnClr = isPrem ? (hoverAddStoreApp ? Color(255,100,190,25) : Color(255,90,170,20))

                                  : Color(255,180,155,80); // muted gold-ish when locked

            SolidBrush storeBtn(btnClr);

            RectF b2(rightColX + btnW + 10.0f, btnY, btnW, 40.0f);

            GraphicsPath* p2 = GetBlockRoundRectPath(b2, 4); g.FillPath(&storeBtn, p2); delete p2;

            g.DrawString(L"Add Store", -1, &fSmallerBold, RectF(b2.X, b2.Y, b2.Width - (isPrem ? 0.0f : 18.0f), b2.Height), &fmtC, &brushWhite);

            if (!isPrem) DrawPremiumCrown(g, b2.X + b2.Width - 22.0f, b2.Y + 11.0f, &fSmallIcon);

        }



        // Button 3 — Add Title (PREMIUM): dimmed when not premium

        {

            bool isPrem = g_isPremiumUser;

            Color btnClr = isPrem ? (hoverAddWindowTitle ? Color(255,100,190,25) : Color(255,90,170,20))

                                  : Color(255,180,155,80);

            SolidBrush titleBtn(btnClr);

            RectF b3(rightColX + (btnW * 2) + 20.0f, btnY, btnW, 40.0f);

            GraphicsPath* p3 = GetBlockRoundRectPath(b3, 4); g.FillPath(&titleBtn, p3); delete p3;

            g.DrawString(L"Add Title", -1, &fSmallerBold, RectF(b3.X, b3.Y, b3.Width - (isPrem ? 0.0f : 18.0f), b3.Height), &fmtC, &brushWhite);

            if (!isPrem) DrawPremiumCrown(g, b3.X + b3.Width - 22.0f, b3.Y + 11.0f, &fSmallIcon);

        }



        // --- DRAW DROPDOWNS ---

        if (isLangDropdownOpen && !isFocusActive) {

            RectF lDrop(langRect.X, langRect.Y + 26.0f, 100.0f, 50.0f);

            g.FillRectangle(&brushWhite, lDrop); g.DrawRectangle(&penBorder, lDrop.X, lDrop.Y, lDrop.Width, lDrop.Height);

            SolidBrush bBg(hoverOptBn ? SClrBgHover : SClrWhite);

            g.FillRectangle(&bBg, RectF(lDrop.X+1.0f, lDrop.Y+1.0f, lDrop.Width-2.0f, 24.0f));

            g.DrawString(L"Bangla", -1, &fNormal, RectF(lDrop.X+5.0f, lDrop.Y+1.0f, lDrop.Width, 24.0f), &fmtL, &brushDark);

            SolidBrush eBg(hoverOptEn ? SClrBgHover : SClrWhite);

            g.FillRectangle(&eBg, RectF(lDrop.X+1.0f, lDrop.Y+25.0f, lDrop.Width-2.0f, 24.0f));

            g.DrawString(L"English", -1, &fNormal, RectF(lDrop.X+5.0f, lDrop.Y+25.0f, lDrop.Width, 24.0f), &fmtL, &brushDark);

        }



        if (isWebComboOpen && !isFocusActive) {

            float listY = webComboY + 38.0f;

            RectF listRect(webComboX - 120.0f, listY, 150.0f, commonWebsites.size() * 30.0f + 10.0f);

            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);

            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;

            float itemY2 = listY + 5.0f;

            for (size_t i = 0; i < commonWebsites.size(); ++i) {

                SolidBrush optBg(hoverWebOptIdx == i ? SClrBgHover : SClrWhite);

                g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itemY2, listRect.Width - 4.0f, 30.0f));

                g.DrawString(commonWebsites[i].c_str(), -1, &fNormal, RectF(listRect.X + 10.0f, itemY2, listRect.Width, 30.0f), &fmtL, &brushDark);

                itemY2 += 30.0f;

            }

        }



        if (isAppComboOpen && !isFocusActive) {

            float listY = secY + 103.0f;

            RectF listRect(aComboX - 120.0f, listY, 150.0f, commonApps.size() * 30.0f + 10.0f);

            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);

            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;

            float itemY2 = listY + 5.0f;

            for (size_t i = 0; i < commonApps.size(); ++i) {

                SolidBrush optBg(hoverAppOptIdx == i ? SClrBgHover : SClrWhite);

                g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itemY2, listRect.Width - 4.0f, 30.0f));

                g.DrawString(commonApps[i].c_str(), -1, &fNormal, RectF(listRect.X + 10.0f, itemY2, listRect.Width, 30.0f), &fmtL, &brushDark);

                itemY2 += 30.0f;

            }

        }



        if (isModeDropdownOpen && !isFocusActive) {

            float listY = modeDropY + 38.0f;

            RectF listRect(modeDropX, listY, 200.0f, 80.0f);

            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);

            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;

            // Option 1: Allow
            bool opt1Selected = (simpleBlockMode == 0);
            Color opt1Color = hoverOptAllow ? SClrBgHover : (opt1Selected ? Color(255, 220, 245, 243) : SClrWhite);
            SolidBrush opt1Bg(opt1Color);
            g.FillRectangle(&opt1Bg, RectF(listRect.X + 2.0f, listY + 2.0f, listRect.Width - 4.0f, 38.0f));
            SolidBrush opt1Txt(opt1Selected ? SClrTeal : SClrDark);
            g.DrawString(L"Allow Apps & Web", -1, &fNormal, RectF(listRect.X + 15.0f, listY + 2.0f, listRect.Width, 38.0f), &fmtL, &opt1Txt);

            // Option 2: Block
            bool opt2Selected = (simpleBlockMode == 1);
            Color opt2Color = hoverOptBlock ? SClrBgHover : (opt2Selected ? Color(255, 220, 245, 243) : SClrWhite);
            SolidBrush opt2Bg(opt2Color);
            g.FillRectangle(&opt2Bg, RectF(listRect.X + 2.0f, listY + 40.0f, listRect.Width - 4.0f, 38.0f));
            SolidBrush opt2Txt(opt2Selected ? SClrTeal : SClrDark);
            g.DrawString(L"Block Apps & Web", -1, &fNormal, RectF(listRect.X + 15.0f, listY + 40.0f, listRect.Width, 38.0f), &fmtL, &opt2Txt);

        }



        // Control Mode Dropdown — with Premium badges on Parents & Long Text options

        if (isControlDropdownOpen && !isFocusActive) {

            float listY = ctrlDropY + 42.0f;

            RectF listRect(ctrlDropX, listY, 160.0f, 118.0f);

            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);

            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;



            // Option 0: Self Control (FREE)
            SolidBrush opt1Bg(hoverOptSelf ? SClrBgHover : SClrWhite);

            g.FillRectangle(&opt1Bg, RectF(listRect.X + 2.0f, listY + 2.0f, listRect.Width - 4.0f, 38.0f));

            g.DrawString(L"Self Control", -1, &fBold, RectF(listRect.X + 15.0f, listY + 2.0f, listRect.Width, 38.0f), &fmtL, &brushDark);



            // Option 1: Parents Control (PREMIUM)
            SolidBrush opt2Bg(hoverOptParents ? SClrBgHover : SClrWhite);

            g.FillRectangle(&opt2Bg, RectF(listRect.X + 2.0f, listY + 40.0f, listRect.Width - 4.0f, 38.0f));

            SolidBrush parentsTxtBrush(g_isPremiumUser ? SClrDark : SClrGoldDark);

            g.DrawString(L"Parents Control", -1, &fBold, RectF(listRect.X + 15.0f, listY + 40.0f, listRect.Width - 30.0f, 38.0f), &fmtL, &parentsTxtBrush);

            if (!g_isPremiumUser) DrawPremiumCrown(g, listRect.X + listRect.Width - 24.0f, listY + 50.0f, &fSmallIcon);



            // Option 2: Long Text Control (PREMIUM)
            SolidBrush opt3Bg(hoverOptLongText ? SClrBgHover : SClrWhite);

            g.FillRectangle(&opt3Bg, RectF(listRect.X + 2.0f, listY + 78.0f, listRect.Width - 4.0f, 38.0f));

            SolidBrush longTxtBrush(g_isPremiumUser ? SClrDark : SClrGoldDark);

            g.DrawString(L"Long Text", -1, &fBold, RectF(listRect.X + 15.0f, listY + 78.0f, listRect.Width - 30.0f, 38.0f), &fmtL, &longTxtBrush);

            if (!g_isPremiumUser) DrawPremiumCrown(g, listRect.X + listRect.Width - 24.0f, listY + 88.0f, &fSmallIcon);

        }

    } 

    // ==========================================

    // TAB 1: SCHEDULE BLOCKS (PREMIUM GATE)

    // ==========================================

    else if (currentBlockTab == 1) {

        if (!g_isPremiumUser) {

            // Draw a friendly locked screen instead of the real tab content

            SolidBrush premBg(Color(255, 255, 251, 230));

            g.FillRectangle(&premBg, boxX, bodyY + 20.0f, boxW, boxH);



            FontFamily ffIc(L"Segoe MDL2 Assets");

            Font fBigCrown(&ffIc, 56, FontStyleRegular, UnitPixel);

            SolidBrush goldBrush(SClrGold);

            SolidBrush darkBrush(SClrDark);

            SolidBrush grayBrush(SClrGrayText);

            StringFormat fmtCC; fmtCC.SetAlignment(StringAlignmentCenter); fmtCC.SetLineAlignment(StringAlignmentCenter);



            float cx = boxX + boxW / 2.0f;

            float cy = bodyY + 20.0f + boxH / 2.0f - 40.0f;



            // Big crown

            g.DrawString(L"\xE2CC", -1, &fBigCrown, RectF(cx - 40.0f, cy - 70.0f, 80.0f, 80.0f), &fmtCC, &goldBrush);



            // Title

            Font fPremTitle(&ff, 22, FontStyleBold, UnitPixel);

            g.DrawString(L"Schedule Blocks is a Premium Feature", -1, &fPremTitle, RectF(boxX, cy + 20.0f, boxW, 36.0f), &fmtCC, &darkBrush);



            // Description

            Font fPremDesc(&ff, 14, FontStyleRegular, UnitPixel);

            g.DrawString(L"Block distractions on a schedule — automatically, hands-free.", -1, &fPremDesc, RectF(boxX, cy + 62.0f, boxW, 28.0f), &fmtCC, &grayBrush);



            // Upgrade button

            RectF upgBtn(cx - 90.0f, cy + 105.0f, 180.0f, 44.0f);

            SolidBrush upgBg(SClrGold);

            GraphicsPath* upgP = GetBlockRoundRectPath(upgBtn, 6);

            g.FillPath(&upgBg, upgP); delete upgP;

            Font fUpgBtn(&ff, 15, FontStyleBold, UnitPixel);

            SolidBrush upgTxt(Color(255, 50, 30, 0));

            g.DrawString(L"Upgrade to Premium", -1, &fUpgBtn, upgBtn, &fmtCC, &upgTxt);

        } else {

            DrawScheduleBlocksTab(g, boxX, bodyY + 20.0f, boxW, boxH);

        }

    }

    // ==========================================

    // TAB 2: ADULT BLOCK (FREE)

    // ==========================================

    else if (currentBlockTab == 2) {

        DrawAdultBlockTab(g, boxX, bodyY + 20.0f, boxW, boxH);

    }

    // ==========================================

    // TAB 3: DEVICE BLOCK (PREMIUM GATE)

    // ==========================================

    else if (currentBlockTab == 3) {

        if (!g_isPremiumUser) {

            SolidBrush premBg(Color(255, 255, 251, 230));

            g.FillRectangle(&premBg, boxX, bodyY + 20.0f, boxW, boxH);



            FontFamily ffIc(L"Segoe MDL2 Assets");

            Font fBigCrown(&ffIc, 56, FontStyleRegular, UnitPixel);

            SolidBrush goldBrush(SClrGold);

            SolidBrush darkBrush(SClrDark);

            SolidBrush grayBrush(SClrGrayText);

            StringFormat fmtCC; fmtCC.SetAlignment(StringAlignmentCenter); fmtCC.SetLineAlignment(StringAlignmentCenter);



            float cx = boxX + boxW / 2.0f;

            float cy = bodyY + 20.0f + boxH / 2.0f - 40.0f;



            g.DrawString(L"\xE2CC", -1, &fBigCrown, RectF(cx - 40.0f, cy - 70.0f, 80.0f, 80.0f), &fmtCC, &goldBrush);



            Font fPremTitle(&ff, 22, FontStyleBold, UnitPixel);

            g.DrawString(L"Device Block is a Premium Feature", -1, &fPremTitle, RectF(boxX, cy + 20.0f, boxW, 36.0f), &fmtCC, &darkBrush);



            Font fPremDesc(&ff, 14, FontStyleRegular, UnitPixel);

            g.DrawString(L"Block internet, USB, and hardware-level distractions system-wide.", -1, &fPremDesc, RectF(boxX, cy + 62.0f, boxW, 28.0f), &fmtCC, &grayBrush);



            RectF upgBtn(cx - 90.0f, cy + 105.0f, 180.0f, 44.0f);

            SolidBrush upgBg(SClrGold);

            GraphicsPath* upgP = GetBlockRoundRectPath(upgBtn, 6);

            g.FillPath(&upgBg, upgP); delete upgP;

            Font fUpgBtn(&ff, 15, FontStyleBold, UnitPixel);

            SolidBrush upgTxt(Color(255, 50, 30, 0));

            g.DrawString(L"Upgrade to Premium", -1, &fUpgBtn, upgBtn, &fmtCC, &upgTxt);

        } else {

            DrawDeviceBlockTab(g, boxX, bodyY + 20.0f, boxW, boxH);

        }

    }

    

    // ==========================================

    // FULL SCREEN OVERLAYS (Simple Blocks only)

    // ==========================================

    if (showTimeOverlay || showPassOverlay || showStoreOverlay || showTitleOverlay || showLongTextOverlay) {

        SolidBrush overlayBg(SClrOverlay);

        g.FillRectangle(&overlayBg, contentX, contentY, contentW, contentH);



        float ovW = (showStoreOverlay || showLongTextOverlay) ? 600.0f : 400.0f; 

        float ovH = (showStoreOverlay) ? 450.0f : (showLongTextOverlay ? 400.0f : 220.0f);

        float ovX = contentX + (contentW - ovW) / 2.0f;

        float ovY = contentY + (contentH - ovH) / 2.0f;



        RectF ovRect(ovX, ovY, ovW, ovH);

        GraphicsPath* op = GetBlockRoundRectPath(ovRect, 8);

        g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;



        if (showStoreOverlay) {

            g.DrawString(L"ADD MICROSOFT STORE APPS", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);

            g.DrawLine(&penBorder, ovX + 30.0f, ovY + 60.0f, ovX + ovW - 30.0f, ovY + 60.0f);

            

            RectF clipRect(ovX + 10.0f, ovY + 65.0f, ovW - 20.0f, ovH - 120.0f);

            Region oldClip; g.GetClip(&oldClip);

            g.SetClip(clipRect);



            float listY = ovY + 70.0f - cStoreScrollY;

            for (size_t i = 0; i < systemStoreApps.size(); ++i) {

                if (listY > ovY - 20.0f && listY < ovY + ovH) {

                    RectF addBtn(ovX + 30.0f, listY + 5.0f, 60.0f, 30.0f);

                    GraphicsPath* ap = GetBlockRoundRectPath(addBtn, 4);

                    SolidBrush aBr(hoverStoreAddIdx == i ? SClrGreenHover : SClrGreen);

                    g.FillPath(&aBr, ap); delete ap;

                    g.DrawString(L"Add", -1, &fBold, addBtn, &fmtC, &brushWhite);

                    g.DrawString(systemStoreApps[i].c_str(), -1, &fNormal, RectF(ovX + 110.0f, listY, 300.0f, 40.0f), &fmtL, &brushDark);

                }

                listY += 45.0f;

            }

            g.SetClip(&oldClip);

            

            float totalH = systemStoreApps.size() * 45.0f;

            float visibleH = ovH - 120.0f;

            if (totalH > visibleH) {

                float maxScroll = totalH - visibleH;

                float thumbH = max(20.0f, (visibleH / totalH) * visibleH);

                float thumbY = ovY + 70.0f + (cStoreScrollY / maxScroll) * (visibleH - thumbH);

                g.FillRectangle(&brushGray, ovX + ovW - 8.0f, thumbY, 4.0f, thumbH);

            }



            RectF closeBtn(ovX + ovW - 120.0f, ovY + ovH - 50.0f, 90.0f, 35.0f);

            GraphicsPath* cp = GetBlockRoundRectPath(closeBtn, 4);

            SolidBrush cBr(hoverStoreClose ? SClrGreenHover : SClrGreen);

            g.FillPath(&cBr, cp); delete cp;

            g.DrawString(L"Close", -1, &fBold, closeBtn, &fmtC, &brushWhite);

        }

        else if (showTitleOverlay) {

            g.DrawString(L"ENTER WINDOW TITLE", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);

            RectF titleInpRect(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f);

            GraphicsPath* pp = GetBlockRoundRectPath(titleInpRect, 4);

            Pen pTealTitle(SClrTeal, 2.0f);

            g.FillPath(&brushWhite, pp); g.DrawPath(isTitleInputActive ? &pTealTitle : &penBorder, pp); delete pp;

            if (inputTitleText.empty() && !isTitleInputActive) g.DrawString(L"e.g. Google Chrome", -1, &fNormal, titleInpRect, &fmtC, &brushGray);

            else {

                g.DrawString(inputTitleText.c_str(), -1, &fNormal, RectF(ovX + 50.0f, ovY + 85.0f, ovW - 100.0f, 30.0f), &fmtL, &brushDark);

                if (isTitleInputActive && (GetTickCount() / 500) % 2 == 0) {

                     Graphics gTemp(GetDesktopWindow()); RectF bRect;

                     gTemp.MeasureString(inputTitleText.c_str(), -1, &fNormal, PointF(0,0), &bRect);

                     float cursorX = ovX + 52.0f + bRect.Width;

                     if(inputTitleText.empty()) cursorX = ovX + 52.0f;

                     g.FillRectangle(&brushDark, cursorX, ovY + 90.0f, 1.5f, 20.0f);

                }

            }

            RectF cancelRect(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f);

            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);

            SolidBrush cancelBrush(hTitleCancel ? SClrBgHover : SClrWhite);

            g.FillPath(&cancelBrush, cp); g.DrawPath(&penBorder, cp); delete cp;

            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF confRect(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f);

            GraphicsPath* sp = GetBlockRoundRectPath(confRect, 4);

            SolidBrush confBrush(hTitleAdd ? SClrTealHover : SClrTeal);

            g.FillPath(&confBrush, sp); delete sp;

            g.DrawString(L"Add Title", -1, &fBold, confRect, &fmtC, &brushWhite);

        }

        else if (showTimeOverlay) {

            g.DrawString(L"SET FOCUS DURATION", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);

            g.DrawString(L"Hours:", -1, &fBold, RectF(ovX + 50.0f, ovY + 80.0f, 60.0f, 36.0f), &fmtL, &brushDark);

            DrawBlocksOverlaySpinner(g, ovX + 110.0f, ovY + 80.0f, to_wstring(focusHours), hTimeHM, hTimeHP, &fIcon, &fBold);

            g.DrawString(L"Mins:", -1, &fBold, RectF(ovX + 250.0f, ovY + 80.0f, 50.0f, 36.0f), &fmtL, &brushDark);

            DrawBlocksOverlaySpinner(g, ovX + 300.0f, ovY + 80.0f, to_wstring(focusMins), hTimeMM, hTimeMP, &fIcon, &fBold);

            RectF cancelRect(ovX + 50.0f, ovY + 150.0f, 140.0f, 40.0f);

            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);

            SolidBrush cancelBrush(hTimeCancel ? SClrBgHover : SClrWhite);

            g.FillPath(&cancelBrush, cp); g.DrawPath(&penBorder, cp); delete cp;

            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF startRect(ovX + 210.0f, ovY + 150.0f, 140.0f, 40.0f);

            GraphicsPath* sp = GetBlockRoundRectPath(startRect, 4);

            SolidBrush startBrush(hTimeStart ? SClrTealHover : SClrTeal);

            g.FillPath(&startBrush, sp); delete sp;

            g.DrawString(L"Start Focus", -1, &fBold, startRect, &fmtC, &brushWhite);

        }

        else if (showPassOverlay) {

            wstring titleTxt = isStoppingFocus ? L"ENTER PASSWORD TO STOP" : L"ENTER PARENT'S PASSWORD";

            g.DrawString(titleTxt.c_str(), -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);

            RectF passInpRect(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f);

            GraphicsPath* pp = GetBlockRoundRectPath(passInpRect, 4);

            Pen pTealPass(SClrTeal, 2.0f);

            g.FillPath(&brushWhite, pp); g.DrawPath(isPassInputActive ? &pTealPass : &penBorder, pp); delete pp;

            wstring displayPass = wstring(inputPassText.length(), L'*');

            if (inputPassText.empty() && !isPassInputActive) g.DrawString(L"Type password here...", -1, &fNormal, passInpRect, &fmtC, &brushGray);

            else {

                g.DrawString(displayPass.c_str(), -1, &fTitle, RectF(ovX + 50.0f, ovY + 85.0f, ovW - 100.0f, 30.0f), &fmtL, &brushDark);

                if (isPassInputActive && (GetTickCount() / 500) % 2 == 0) {

                     Graphics gTemp(GetDesktopWindow()); RectF bRect;

                     gTemp.MeasureString(displayPass.c_str(), -1, &fTitle, PointF(0,0), &bRect);

                     float cursorX = ovX + 52.0f + bRect.Width;

                     if(displayPass.empty()) cursorX = ovX + 52.0f;

                     g.FillRectangle(&brushDark, cursorX, ovY + 90.0f, 1.5f, 20.0f);

                }

            }

            RectF cancelRect(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f);

            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);

            SolidBrush cancelBrush(hPassCancel ? SClrBgHover : SClrWhite);

            g.FillPath(&cancelBrush, cp); g.DrawPath(&penBorder, cp); delete cp;

            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF confRect(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f);

            GraphicsPath* sp = GetBlockRoundRectPath(confRect, 4);

            SolidBrush confBrush(hPassConfirm ? SClrTealHover : SClrTeal);

            g.FillPath(&confBrush, sp); delete sp;

            g.DrawString(L"Confirm", -1, &fBold, confRect, &fmtC, &brushWhite);

        }

        else if (showLongTextOverlay) {

            int words = CountWords(inputLongText);

            g.DrawString(L"TYPE 200 WORDS TO STOP FOCUS", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);

            wstring wordCountText = L"Word Count: " + to_wstring(words) + L" / 200";

            SolidBrush countBrush(words >= 200 ? SClrGreen : SClrRed);

            g.DrawString(wordCountText.c_str(), -1, &fBold, RectF(ovX, ovY + 60.0f, ovW, 20.0f), &fmtC, &countBrush);

            RectF textInpRect(ovX + 40.0f, ovY + 90.0f, ovW - 80.0f, 200.0f);

            GraphicsPath* pp = GetBlockRoundRectPath(textInpRect, 4);

            Pen pTealBox(SClrTeal, 2.0f);

            g.FillPath(&brushWhite, pp); 

            g.DrawPath(isLongTextInputActive ? &pTealBox : &penBorder, pp); delete pp;

            StringFormat fmtWrap; fmtWrap.SetAlignment(StringAlignmentNear); fmtWrap.SetLineAlignment(StringAlignmentNear);

            if (inputLongText.empty() && !isLongTextInputActive) {

                g.DrawString(L"Start typing here... Type at least 200 words explaining why you are stopping this session.", -1, &fNormal, RectF(textInpRect.X+10.0f, textInpRect.Y+10.0f, textInpRect.Width-20.0f, textInpRect.Height-20.0f), &fmtWrap, &brushGray);

            } else {

                g.DrawString(inputLongText.c_str(), -1, &fNormal, RectF(textInpRect.X+10.0f, textInpRect.Y+10.0f, textInpRect.Width-20.0f, textInpRect.Height-20.0f), &fmtWrap, &brushDark);

                if (isLongTextInputActive && (GetTickCount() / 500) % 2 == 0) {

                    g.FillRectangle(&brushTeal, textInpRect.X + textInpRect.Width - 15.0f, textInpRect.Y + textInpRect.Height - 15.0f, 10.0f, 10.0f);

                }

            }

            RectF cancelRect(ovX + 100.0f, ovY + 320.0f, 140.0f, 40.0f);

            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);

            SolidBrush cancelBrush(hLongTextCancel ? SClrBgHover : SClrWhite);

            g.FillPath(&cancelBrush, cp); g.DrawPath(&penBorder, cp); delete cp;

            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF confRect(ovX + 360.0f, ovY + 320.0f, 140.0f, 40.0f);

            GraphicsPath* sp = GetBlockRoundRectPath(confRect, 4);

            bool canConfirm = (words >= 200);

            SolidBrush confBrush(canConfirm ? (hLongTextConfirm ? SClrTealHover : SClrTeal) : SClrDisabled);

            g.FillPath(&confBrush, sp); delete sp;

            g.DrawString(L"Confirm Stop", -1, &fBold, confRect, &fmtC, &brushWhite);

        }

    }

}



// ==========================================

// MOUSE MOVE LOGIC

// ==========================================

void ProcessBlocksMouseMove(float x, float y) {

    float contentX = s_contentX, contentY = s_contentY;

    float contentW = s_contentW, contentH = s_contentH;



    hTimeHM = false; hTimeHP = false; hTimeMM = false; hTimeMP = false; hTimeStart = false; hTimeCancel = false;

    hPassInput = false; hPassConfirm = false; hPassCancel = false;

    hLongTextInput = false; hLongTextConfirm = false; hLongTextCancel = false;

    hoverStoreClose = false; hoverStoreAddIdx = -1;

    hTitleInput = false; hTitleAdd = false; hTitleCancel = false;

    hoverControlDropdown = false; hoverModeDropdown = false; hoverWebCombo = false; hoverAppCombo = false;

    hoverLangDropdown = false; hoverOptBn = false; hoverOptEn = false;

    hoverQuotesCheckbox = false; hoverWebInput = false; hoverWebAddBtn = false;

    hoverAppInput = false; hoverAppAddBtn = false; hoverAddExe = false; 

    hoverAddStoreApp = false; hoverAddWindowTitle = false;

    hoverAppOptIdx = -1;

    hoverOptSelf = false; hoverOptParents = false; hoverOptLongText = false;

    hoverStartFocusBtn = false; 



    for (auto& item : webList) item.isHoveredCross = false;

    for (auto& item : appList) item.isHoveredCross = false;



    if (showTimeOverlay || showPassOverlay || showStoreOverlay || showTitleOverlay || showLongTextOverlay) {

        float ovW = (showStoreOverlay || showLongTextOverlay) ? 600.0f : 400.0f; 

        float ovH = (showStoreOverlay) ? 450.0f : (showLongTextOverlay ? 400.0f : 220.0f);

        float ovX = contentX + (contentW - ovW) / 2.0f;

        float ovY = contentY + (contentH - ovH) / 2.0f;



        if (showStoreOverlay) {

            float listY = ovY + 70.0f - cStoreScrollY;

            for (size_t i = 0; i < systemStoreApps.size(); ++i) {

                if (listY > ovY + 60.0f && listY < ovY + ovH - 60.0f)

                    if (RectF(ovX + 30.0f, listY + 5.0f, 60.0f, 30.0f).Contains(x, y)) hoverStoreAddIdx = i;

                listY += 45.0f;

            }

            if (RectF(ovX + ovW - 120.0f, ovY + ovH - 50.0f, 90.0f, 35.0f).Contains(x, y)) hoverStoreClose = true;

        }

        else if (showTitleOverlay) {

            if (RectF(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f).Contains(x, y)) hTitleInput = true;

            if (RectF(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hTitleCancel = true;

            if (RectF(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f).Contains(x, y)) hTitleAdd = true;

        }

        else if (showTimeOverlay) {

            if (RectF(ovX + 110.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeHM = true;

            if (RectF(ovX + 110.0f + 92.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeHP = true;

            if (RectF(ovX + 300.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeMM = true;

            if (RectF(ovX + 300.0f + 92.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeMP = true;

            if (RectF(ovX + 50.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hTimeCancel = true;

            if (RectF(ovX + 210.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hTimeStart = true;

        }

        else if (showPassOverlay) {

            if (RectF(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f).Contains(x, y)) hPassInput = true;

            if (RectF(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hPassCancel = true;

            if (RectF(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f).Contains(x, y)) hPassConfirm = true;

        }

        else if (showLongTextOverlay) {

            if (RectF(ovX + 40.0f, ovY + 90.0f, ovW - 80.0f, 200.0f).Contains(x, y)) hLongTextInput = true;

            if (RectF(ovX + 100.0f, ovY + 320.0f, 140.0f, 40.0f).Contains(x, y)) hLongTextCancel = true;

            if (RectF(ovX + 360.0f, ovY + 320.0f, 140.0f, 40.0f).Contains(x, y)) hLongTextConfirm = true;

        }

        return; 

    }



    // --- Tab hover ---

    hoverBlockTab = -1;

    float tabH = 40.0f;

    float tabW = (contentW - 60.0f) / 4.0f - 7.5f;

    float tabGap = 10.0f;

    float tab1X = contentX + 20.0f;

    float tab2X = tab1X + tabW + tabGap;

    float tab3X = tab2X + tabW + tabGap;

    float tab4X = tab3X + tabW + tabGap;

    float tabY  = contentY + 10.0f;

    

    if (y >= contentY && y <= contentY + 60.0f) {

        if (RectF(tab1X, tabY, tabW, tabH).Contains(x, y)) hoverBlockTab = 0;

        else if (RectF(tab2X, tabY, tabW, tabH).Contains(x, y)) hoverBlockTab = 1;

        else if (RectF(tab3X, tabY, tabW, tabH).Contains(x, y)) hoverBlockTab = 2;

        else if (RectF(tab4X, tabY, tabW, tabH).Contains(x, y)) hoverBlockTab = 3;

    }



    if (currentBlockTab == 0) {

        float bodyY = contentY + 60.0f;

        float boxX = contentX + 30.0f;

        float boxW = contentW - 60.0f;

        float ctrlDropX = boxX + 30.0f;

        float ctrlDropY = bodyY + 40.0f;

        float modeDropX = boxX + 150.0f;

        float modeDropY = ctrlDropY + 75.0f;

        float webComboX = boxX + 30.0f + ((boxW - 90.0f) / 2.0f) - 105.0f;

        float webComboY = modeDropY + 95.0f;

        float colW = (boxW - 90.0f) / 2.0f;

        float rightColX = boxX + 60.0f + colW;

        float secY = modeDropY + 50.0f;

        float qY = secY; 



        if (isLangDropdownOpen && !isFocusActive) {

            RectF lDrop(rightColX + 180.0f, qY + 24.0f, 100.0f, 50.0f);

            if (RectF(lDrop.X, lDrop.Y, lDrop.Width, 24.0f).Contains(x, y)) { hoverOptBn = true; return; }

            if (RectF(lDrop.X, lDrop.Y+25.0f, lDrop.Width, 24.0f).Contains(x, y)) { hoverOptEn = true; return; }

        }

        if (isControlDropdownOpen && !isFocusActive) {

            float listY = ctrlDropY + 42.0f;

            if (RectF(ctrlDropX + 2.0f, listY + 2.0f, 156.0f, 38.0f).Contains(x, y)) { hoverOptSelf = true; return; }

            if (RectF(ctrlDropX + 2.0f, listY + 40.0f, 156.0f, 38.0f).Contains(x, y)) { hoverOptParents = true; return; }

            if (RectF(ctrlDropX + 2.0f, listY + 78.0f, 156.0f, 38.0f).Contains(x, y)) { hoverOptLongText = true; return; }

        }

        if (isModeDropdownOpen && !isFocusActive) {

            float listY = modeDropY + 38.0f;

            if (RectF(modeDropX + 2.0f, listY + 2.0f, 196.0f, 38.0f).Contains(x, y)) { hoverOptAllow = true; return; }

            if (RectF(modeDropX + 2.0f, listY + 40.0f, 196.0f, 38.0f).Contains(x, y)) { hoverOptBlock = true; return; }

        }

        if (isWebComboOpen && !isFocusActive) {

            float listY = webComboY + 38.0f;

            float itemY = listY + 5.0f;

            for (size_t i = 0; i < commonWebsites.size(); ++i) {

                if (RectF(webComboX - 120.0f + 2.0f, itemY, 146.0f, 30.0f).Contains(x, y)) { hoverWebOptIdx = i; return; }

                itemY += 30.0f;

            }

        }

        if (isAppComboOpen && !isFocusActive) {

            float listY = secY + 103.0f;

            float aComboX = rightColX + colW - 105.0f;

            float itemY = listY + 5.0f;

            for (size_t i = 0; i < commonApps.size(); ++i) {

                if (RectF(aComboX - 120.0f + 2.0f, itemY, 146.0f, 30.0f).Contains(x, y)) { hoverAppOptIdx = i; return; }

                itemY += 30.0f;

            }

        }



        if (isFocusActive && controlMode == 0 && std::time(nullptr) < focusEndTimeBlocks) {

            hoverStartFocusBtn = false;

        } else {

            if (RectF(boxX + 205.0f, ctrlDropY, 140.0f, 40.0f).Contains(x, y)) hoverStartFocusBtn = true;

        }



        if (!isFocusActive) {

            if (RectF(ctrlDropX, ctrlDropY, 160.0f, 40.0f).Contains(x, y)) hoverControlDropdown = true;

            if (RectF(modeDropX, modeDropY, 200.0f, 36.0f).Contains(x, y)) hoverModeDropdown = true;

            if (RectF(rightColX, qY + 2.0f, 150.0f, 20.0f).Contains(x, y)) hoverQuotesCheckbox = true;

            if (RectF(rightColX + 180.0f, qY - 2.0f, 100.0f, 24.0f).Contains(x, y)) hoverLangDropdown = true;

            if (RectF(webComboX, webComboY, 30.0f, 36.0f).Contains(x, y)) hoverWebCombo = true;

            float aComboX = rightColX + colW - 105.0f;

            if (RectF(aComboX, secY + 65.0f, 30.0f, 36.0f).Contains(x, y)) hoverAppCombo = true;

        }



        float leftColX = boxX + 30.0f;

        if (RectF(leftColX, secY + 45.0f, colW - 110.0f, 36.0f).Contains(x, y)) hoverWebInput = true;

        if (RectF(leftColX + colW - 70.0f, secY + 45.0f, 70.0f, 36.0f).Contains(x, y)) hoverWebAddBtn = true;

        if (RectF(rightColX, secY + 65.0f, colW - 110.0f, 36.0f).Contains(x, y)) hoverAppInput = true;

        if (RectF(rightColX + colW - 70.0f, secY + 65.0f, 70.0f, 36.0f).Contains(x, y)) hoverAppAddBtn = true;

        

        float btnW = (colW - 20.0f) / 3.0f;

        float btnY = secY + 260.0f;

        if (RectF(rightColX, btnY, btnW, 40.0f).Contains(x, y)) hoverAddExe = true;

        if (RectF(rightColX + btnW + 10.0f, btnY, btnW, 40.0f).Contains(x, y)) hoverAddStoreApp = true;

        if (RectF(rightColX + (btnW * 2) + 20.0f, btnY, btnW, 40.0f).Contains(x, y)) hoverAddWindowTitle = true;



        if (!isFocusActive) {

            float itemY = secY + 90.0f + 5.0f - cWebScrollY;

            for (size_t i = 0; i < webList.size(); ++i) {

                if (itemY > secY + 90.0f - 30.0f && itemY < secY + 90.0f + 160.0f)

                    if (RectF(leftColX + colW - 30.0f, itemY, 30.0f, 30.0f).Contains(x, y)) webList[i].isHoveredCross = true;

                itemY += 30.0f;

            }

            float aItemY = secY + 110.0f + 5.0f - cAppScrollY;

            for (size_t i = 0; i < appList.size(); ++i) {

                if (!appList[i].isSystemLocked && aItemY > secY + 110.0f - 30.0f && aItemY < secY + 110.0f + 140.0f)

                    if (RectF(rightColX + colW - 30.0f, aItemY, 30.0f, 30.0f).Contains(x, y)) appList[i].isHoveredCross = true;

                aItemY += 30.0f;

            }

        }

    }

    else if (currentBlockTab == 1) {

        if (g_isPremiumUser) ProcessScheduleBlocksMouseMove(x, y);

    }

    else if (currentBlockTab == 2) {

        ProcessAdultBlockMouseMove(x, y);

    }

    else if (currentBlockTab == 3) {

        if (g_isPremiumUser) ProcessDeviceBlockMouseMove(x, y);

    }

}



// ==========================================

// MOUSE CLICK LOGIC

// ==========================================

void ProcessBlocksMouseClick(float x, float y) {

    if (showStoreOverlay) {

        if (hoverStoreAddIdx != -1 && hoverStoreAddIdx < (int)systemStoreApps.size()) { 

            if (systemStoreApps[hoverStoreAddIdx] != L"No active apps found") {

                appList.push_back({systemStoreApps[hoverStoreAddIdx], false, false}); 

                SaveBlocksData();

            }

        }

        if (hoverStoreClose) showStoreOverlay = false;

        return;

    }

    if (showTitleOverlay) {

        isTitleInputActive = hTitleInput;

        if (hTitleCancel) { showTitleOverlay = false; inputTitleText = L""; }

        if (hTitleAdd) {

            if (!inputTitleText.empty()) {

                appList.push_back({inputTitleText + L" (Window)", false, false}); 

                SaveBlocksData();

            }

            showTitleOverlay = false; inputTitleText = L"";

        }

        return;

    }

    if (showLongTextOverlay) {

        isLongTextInputActive = hLongTextInput;

        if (hLongTextCancel) { showLongTextOverlay = false; inputLongText = L""; }

        if (hLongTextConfirm && CountWords(inputLongText) >= 200) {

            isFocusActive = false;

            showLongTextOverlay = false; inputLongText = L""; 

            SaveBlocksData();

        }

        return;

    }

    if (showTimeOverlay) {

        if (hTimeHM && focusHours > 0) focusHours--;

        if (hTimeHP && focusHours < 23) focusHours++;

        if (hTimeMM) { focusMins -= 5; if (focusMins < 0) focusMins = 55; }

        if (hTimeMP) { focusMins = (focusMins + 5) % 60; }

        if (hTimeCancel) showTimeOverlay = false;

        if (hTimeStart) { 

            isFocusActive = true; 

            focusEndTimeBlocks = std::time(nullptr) + (focusHours * 3600) + (focusMins * 60);

            showTimeOverlay = false; 

            SaveBlocksData();

        }

        return;

    }

    if (showPassOverlay) {

        isPassInputActive = hPassInput;

        if (hPassCancel) { showPassOverlay = false; inputPassText = L""; }

        if (hPassConfirm && !inputPassText.empty()) {

            isFocusActive = !isStoppingFocus;

            showPassOverlay = false; inputPassText = L""; 

            SaveBlocksData();

        }

        return;

    }



    // Tab switch

    if (hoverBlockTab != -1) { 

        // Tabs 1 (Schedule) and 3 (Device): if not premium, just switch the tab
        // (the draw function will show the locked screen with upgrade button).
        currentBlockTab = hoverBlockTab; 

        isModeDropdownOpen = false; isControlDropdownOpen = false;

        isWebComboOpen = false; isAppComboOpen = false;

        isLangDropdownOpen = false;

        return; 

    }



    if (currentBlockTab == 0) {

        if (hoverStartFocusBtn) {

            isWebInputActive = false; isAppInputActive = false; hoverStartFocusBtn = false; 

            if (isFocusActive) {

                if (controlMode == 1) { isStoppingFocus = true; showPassOverlay = true; isPassInputActive = true; }

                else if (controlMode == 2) { showLongTextOverlay = true; isLongTextInputActive = true; }

                else { isFocusActive = false; SaveBlocksData(); }

            } else {

                if (controlMode == 0 || controlMode == 2) { showTimeOverlay = true; }

                else { isStoppingFocus = false; showPassOverlay = true; isPassInputActive = true; }

            }

            return;

        }



        bool closedAnyDropdown = false;

        

        if (isLangDropdownOpen && !hoverLangDropdown && !hoverOptBn && !hoverOptEn) {

            isLangDropdownOpen = false; closedAnyDropdown = true;

        } else if (isLangDropdownOpen) {

            if (hoverOptBn) quoteLanguage = 0;

            if (hoverOptEn) quoteLanguage = 1;

            isLangDropdownOpen = false; SaveBlocksData(); return;

        }

        if (isControlDropdownOpen && !hoverControlDropdown && !hoverOptSelf && !hoverOptParents && !hoverOptLongText) {

            isControlDropdownOpen = false; closedAnyDropdown = true;

        } else if (isControlDropdownOpen) {

            if (hoverOptSelf) {

                controlMode = 0;

                isControlDropdownOpen = false; SaveBlocksData(); return;

            }

            // Parents Control — Premium gate
            if (hoverOptParents) {

                if (!g_isPremiumUser) { g_showUpgradePopup = true; isControlDropdownOpen = false; return; }

                controlMode = 1;

                isControlDropdownOpen = false; SaveBlocksData(); return;

            }

            // Long Text Control — Premium gate
            if (hoverOptLongText) {

                if (!g_isPremiumUser) { g_showUpgradePopup = true; isControlDropdownOpen = false; return; }

                controlMode = 2;

                isControlDropdownOpen = false; SaveBlocksData(); return;

            }

            isControlDropdownOpen = false; SaveBlocksData(); return;

        }

        if (isModeDropdownOpen && !hoverModeDropdown && !hoverOptAllow && !hoverOptBlock) {

            isModeDropdownOpen = false; closedAnyDropdown = true;

        } else if (isModeDropdownOpen) {

            if (hoverOptAllow) { simpleBlockMode = 0; EnforceSystemApps(); }

            if (hoverOptBlock) { simpleBlockMode = 1; EnforceSystemApps(); }

            isModeDropdownOpen = false; SaveBlocksData(); return;

        }

        if (isWebComboOpen && !hoverWebCombo && hoverWebOptIdx == -1) {

            isWebComboOpen = false; closedAnyDropdown = true;

        } else if (isWebComboOpen) {

            if (hoverWebOptIdx != -1) { webList.push_back({commonWebsites[hoverWebOptIdx], false}); SaveBlocksData(); }

            isWebComboOpen = false; return;

        }

        if (isAppComboOpen && !hoverAppCombo && hoverAppOptIdx == -1) {

            isAppComboOpen = false; closedAnyDropdown = true;

        } else if (isAppComboOpen) {

            if (hoverAppOptIdx != -1) { appList.push_back({commonApps[hoverAppOptIdx], false, false}); SaveBlocksData(); }

            isAppComboOpen = false; return;

        }

        if (closedAnyDropdown) return; 



        if (hoverControlDropdown && !isFocusActive) { isControlDropdownOpen = true; isModeDropdownOpen = false; isWebComboOpen = false; isAppComboOpen = false; isLangDropdownOpen = false; return; }

        if (hoverModeDropdown && !isFocusActive) { isModeDropdownOpen = true; isControlDropdownOpen = false; isWebComboOpen = false; isAppComboOpen = false; isLangDropdownOpen = false; return; }

        if (hoverWebCombo && !isFocusActive) { isWebComboOpen = true; isControlDropdownOpen = false; isModeDropdownOpen = false; isAppComboOpen = false; isLangDropdownOpen = false; return; }

        if (hoverAppCombo && !isFocusActive) { isAppComboOpen = true; isControlDropdownOpen = false; isModeDropdownOpen = false; isWebComboOpen = false; isLangDropdownOpen = false; return; }

        if (hoverLangDropdown && !isFocusActive) { isLangDropdownOpen = true; isControlDropdownOpen = false; isModeDropdownOpen = false; isWebComboOpen = false; isAppComboOpen = false; return; }

        if (hoverQuotesCheckbox && !isFocusActive) { showQuotes = !showQuotes; SaveBlocksData(); return; }



        isWebInputActive = hoverWebInput;

        isAppInputActive = hoverAppInput;



        if (hoverWebAddBtn && !webInputText.empty()) { webList.push_back({webInputText, false}); webInputText = L""; SaveBlocksData(); }

        if (hoverAppAddBtn && !appInputText.empty()) { appList.push_back({appInputText, false, false}); appInputText = L""; SaveBlocksData(); }



        if (hoverAddExe) {

            OPENFILENAMEW ofn; wchar_t szFile[260] = { 0 };

            ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = GetActiveWindow(); 

            ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); ofn.lpstrFilter = L"Executable Files\0*.exe\0All Files\0*.*\0";

            ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; 

            if (GetOpenFileNameW(&ofn) == TRUE) {

                wstring filePath = ofn.lpstrFile; size_t pos = filePath.find_last_of(L"\\/");

                if(pos != wstring::npos) filePath = filePath.substr(pos+1);

                appList.push_back({filePath, false, false}); SaveBlocksData();

            }

        }

        // Add Store — Premium gate

        if (hoverAddStoreApp) {

            if (!g_isPremiumUser) { g_showUpgradePopup = true; return; }

            RefreshRunningApps(); tStoreScrollY = cStoreScrollY = 0; showStoreOverlay = true;

        }

        // Add Title — Premium gate

        if (hoverAddWindowTitle) {

            if (!g_isPremiumUser) { g_showUpgradePopup = true; return; }

            showTitleOverlay = true; isTitleInputActive = true;

        }



        if (!isFocusActive) {

            bool listChanged = false;

            for (auto it = webList.begin(); it != webList.end(); ) { 

                if (it->isHoveredCross) { it = webList.erase(it); listChanged = true; } else ++it; 

            }

            for (auto it = appList.begin(); it != appList.end(); ) { 

                if (it->isHoveredCross && !it->isSystemLocked) { it = appList.erase(it); listChanged = true; } else ++it; 

            }

            if (listChanged) SaveBlocksData();

        }

    }

    else if (currentBlockTab == 1) {

        // Premium locked screen "Upgrade" button hit-test

        if (!g_isPremiumUser) {

            float boxX = s_contentX + 30.0f;

            float boxW = s_contentW - 60.0f;

            float boxH = s_contentH - 60.0f - 40.0f;

            float bodyY = s_contentY + 60.0f;

            float cx = boxX + boxW / 2.0f;

            float cy = bodyY + 20.0f + boxH / 2.0f - 40.0f;

            RectF upgBtn(cx - 90.0f, cy + 105.0f, 180.0f, 44.0f);

            if (upgBtn.Contains(x, y)) { g_showUpgradePopup = true; }

            return;

        }

        ProcessScheduleBlocksMouseClick(x, y);

    }

    else if (currentBlockTab == 2) {

        ProcessAdultBlockMouseClick(x, y);

    }

    else if (currentBlockTab == 3) {

        // Premium locked screen "Upgrade" button hit-test

        if (!g_isPremiumUser) {

            float boxX = s_contentX + 30.0f;

            float boxW = s_contentW - 60.0f;

            float boxH = s_contentH - 60.0f - 40.0f;

            float bodyY = s_contentY + 60.0f;

            float cx = boxX + boxW / 2.0f;

            float cy = bodyY + 20.0f + boxH / 2.0f - 40.0f;

            RectF upgBtn(cx - 90.0f, cy + 105.0f, 180.0f, 44.0f);

            if (upgBtn.Contains(x, y)) { g_showUpgradePopup = true; }

            return;

        }

        ProcessDeviceBlockMouseClick(x, y);

    }

}



// ==========================================

// KEY PRESS LOGIC

// ==========================================

void ProcessBlocksKeyPress(wchar_t c) {

    if (showLongTextOverlay && isLongTextInputActive) {

        if ((c >= 32 && c <= 126) || c == L'\n' || c == L'\r') {

            if (inputLongText.length() < 2500) inputLongText += c;

        }

    } else if (showPassOverlay && isPassInputActive) {

        if (c >= 32 && c <= 126 && inputPassText.length() < 20) inputPassText += c;

    } else if (showTitleOverlay && isTitleInputActive) {

        if (c >= 32 && c <= 126 && inputTitleText.length() < 40) inputTitleText += c;

    } else if (!showTimeOverlay && !showPassOverlay && !showStoreOverlay && !showTitleOverlay && !showLongTextOverlay) {

        if (currentBlockTab == 0) {

            if (isWebInputActive && c >= 32 && c <= 126 && webInputText.length() < 40) webInputText += c;

            if (isAppInputActive && c >= 32 && c <= 126 && appInputText.length() < 40) appInputText += c;

        } else if (currentBlockTab == 1 && g_isPremiumUser) {

            ProcessScheduleBlocksKeyPress(c);

        } else if (currentBlockTab == 2) {

            ProcessAdultBlockKeyPress(c);

        } else if (currentBlockTab == 3 && g_isPremiumUser) {

            ProcessDeviceBlockKeyPress(c);

        }

    }

}



// ==========================================

// KEY DOWN LOGIC

// ==========================================

void ProcessBlocksKeyDown(WPARAM key) {

    if (key == VK_ESCAPE) {

        if (showPassOverlay || showTitleOverlay || showTimeOverlay || showStoreOverlay || showLongTextOverlay) {

            showPassOverlay = false; showTitleOverlay = false; showTimeOverlay = false;

            showStoreOverlay = false; showLongTextOverlay = false;

            inputPassText = L""; inputTitleText = L""; inputLongText = L"";

            return;

        }

    }



    if (showLongTextOverlay && isLongTextInputActive) {

        if (key == VK_BACK && !inputLongText.empty()) inputLongText.pop_back();

    } else if (showPassOverlay && isPassInputActive) {

        if (key == VK_BACK && !inputPassText.empty()) inputPassText.pop_back();

    } else if (showTitleOverlay && isTitleInputActive) {

        if (key == VK_BACK && !inputTitleText.empty()) inputTitleText.pop_back();

    } else if (!showTimeOverlay && !showPassOverlay && !showStoreOverlay && !showTitleOverlay && !showLongTextOverlay) {

        if (currentBlockTab == 0) {

            if (isWebInputActive) {

                if (key == VK_BACK && !webInputText.empty()) webInputText.pop_back();

                else if (key == VK_RETURN && !webInputText.empty()) {

                    webList.push_back({webInputText, false}); webInputText = L""; SaveBlocksData();

                }

            }

            if (isAppInputActive) {

                if (key == VK_BACK && !appInputText.empty()) appInputText.pop_back();

                else if (key == VK_RETURN && !appInputText.empty()) {

                    appList.push_back({appInputText, false, false}); appInputText = L""; SaveBlocksData();

                }

            }

        } else if (currentBlockTab == 1 && g_isPremiumUser) {

            ProcessScheduleBlocksKeyDown(key);

        } else if (currentBlockTab == 2) {

            ProcessAdultBlockKeyDown(key);

        } else if (currentBlockTab == 3 && g_isPremiumUser) {

            ProcessDeviceBlockKeyDown(key);

        }

    }

}



// ==========================================

// MOUSE WHEEL LOGIC

// ==========================================

void ProcessBlocksMouseWheel(float x, float y, int delta) {

    int steps = (delta > 0) ? 1 : -1;



    if (showStoreOverlay) {

        tStoreScrollY -= steps * 60.0f;

        float visibleStoreH = 450.0f - 70.0f - 60.0f; 

        float maxStoreScroll = max(0.0f, (float)systemStoreApps.size() * 45.0f - visibleStoreH);

        tStoreScrollY = max(0.0f, min(tStoreScrollY, maxStoreScroll));

        return;

    }

    

    if (!showTimeOverlay && !showPassOverlay && !showTitleOverlay && !showLongTextOverlay) {

        if (currentBlockTab == 0) {

            float boxX = s_contentX + 30.0f;

            float boxW = s_contentW - 60.0f;

            float colW = (boxW - 90.0f) / 2.0f;

            float leftColX = boxX + 30.0f;

            float rightColX = boxX + 60.0f + colW;

            float secY = s_contentY + 65.0f + 40.0f + 75.0f + 50.0f; 

            

            RectF webTable(leftColX, secY + 90.0f, colW, 160.0f);

            if (webTable.Contains(x, y)) {

                tWebScrollY -= steps * 40.0f; 

                float maxWebScroll = max(0.0f, (float)webList.size() * 30.0f - 160.0f + 10.0f);

                tWebScrollY = max(0.0f, min(tWebScrollY, maxWebScroll));

            }

            RectF appTable(rightColX, secY + 110.0f, colW, 140.0f);

            if (appTable.Contains(x, y)) {

                tAppScrollY -= steps * 40.0f; 

                float maxAppScroll = max(0.0f, (float)appList.size() * 30.0f - 140.0f + 10.0f);

                tAppScrollY = max(0.0f, min(tAppScrollY, maxAppScroll));

            }

        } else if (currentBlockTab == 1 && g_isPremiumUser) {

            ProcessScheduleBlocksMouseWheel(x, y, delta);

        } else if (currentBlockTab == 2) {

            ProcessAdultBlockMouseWheel(x, y, delta);

        } else if (currentBlockTab == 3 && g_isPremiumUser) {

            ProcessDeviceBlockMouseWheel(x, y, delta);

        }

    }

}

void SetBlockSubTab(int subTab) {
    currentBlockTab = subTab;
}

// Dashboard shortcut: sub-tab + specific action একসাথে trigger করা
// action: 0=শুধু tab change, 1=Add Profile form (Schedule), 2=Allow mode (Simple)
void SetBlockSubTabWithAction(int subTab, int action) {
    currentBlockTab = subTab;
    if (action == 1 && subTab == 1) {
        TriggerAddNewProfile(); // Schedule → Add Profile form
    } else if (action == 2 && subTab == 0) {
        simpleBlockMode = 0;   // Simple → Allow mode
    }
}
