#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)

#include "tab_settings.h"
#include <vector>
#include <string>
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <thread>
#include <mutex>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;

// ==========================================
// Profile check — returns true if any blocking profile is currently active
bool CheckIfAnyProfileIsActive() {
    // TODO: Replace this with your real profile state logic.
    // For example, query your profile/accounts system here.
    // Returning false means enforcement thread will skip all blocking.
    return false;
}
// ==========================================

static std::mutex g_setMutex;

// ==========================================
// --- STATE VARIABLES ---
// ==========================================
static int currentSetTab = 0; 
static int hoverSetTab = -1;

static float s_setScrollT = 0.0f;
static float s_setScrollC = 0.0f;
static float s_setScrollMax = 0.0f;

// 1. Browsers & Security (Tab 0)
static bool tglBlockUnsupportedBrowsers = false;  // FIX: was true — default OFF, user must enable manually
static bool tglBlockDateTime = false;
static bool tglBlockLoginItems = false;
static bool tglBlockUsersGroups = false;
static bool tglBlockActivityMonitor = false;
static bool tglBlockInactiveTabs = false;
static bool tglBlockEmbedded = false;
static bool tglBlockPauseForCause = false;
static bool tglBlockInstaller = false;
static bool tglForceFileURLs = false;
static int  reEnableSeconds = 60;

// 2. General (Tab 1)
static bool tglStartup = true;
static bool tglRequirePass = false;
static bool tglStartMin = true;
static bool tglDarkTheme = false;
static bool tglDailyBackup = true;
static bool tgl24Hour = false;
static int langIdx = 0; 

// 3. System (Tab 2)
static bool tglBlockTaskMgr = false;
static bool tglBlockRegEdit = false;
static bool tglProtectUninstall = true;
static bool tglSafeMode = true;
static bool tglProcessSuspend = false;
static bool tglBlockUninstallers = true;
static bool tglProtectPowerShell = true;

// 4. Advanced (Tab 3)
static bool tglRecordUsage = true;
static bool tglProtectClipboard = false;
static bool tglShowDelete = false;
static bool tglShowTimer = true;
static bool tglImportExport = true;
static int maxPlansCount = 0;

// 5. Notification (Tab 4)
static int genPosIdx = 0; 
static int aiPosIdx = 0;
static bool tglAudio = true;
static bool tglMuteAll = false;
static bool tglDND = true;
static int dndStartH = 22; 
static int dndEndH = 6;    
static int threshMin = 10;
static int freqNormMin = 30;
static int freqLowMin = 5;

// 6. Sync (Tab 5)
static bool tglCloudSync = false;
static bool tglSyncSchedules = true;

// --- PERFECT HITBOX TRACKING ---
struct SettingsHitboxes {
    RectF tabs[6];
    RectF toggles[60];
    RectF minusSec, plusSec;
    RectF langBtn;
    RectF minusPlan, plusPlan;
    RectF pos1Btn, pos2Btn;
    RectF dndStM, dndStP, dndEnM, dndEnP;
    RectF thM, thP, fnM, fnP, flM, flP;
    
    int hTglIdx = -1;
    bool hMinusSec = false, hPlusSec = false;
    bool hLangBtn = false;
    bool hMinusPlan = false, hPlusPlan = false;
    bool hPos1 = false, hPos2 = false;
    bool hDndStM=false, hDndStP=false, hDndEnM=false, hDndEnP=false;
    bool hThM=false, hThP=false, hFnM=false, hFnP=false, hFlM=false, hFlP=false;
} g_shb;

// --- Local Colors ---
static const Color SClrTeal(255, 12, 168, 176);
static const Color SClrTealHover(255, 30, 185, 195);
static const Color SClrWhite(255, 255, 255, 255);
static const Color SClrDark(255, 50, 50, 50);
static const Color SClrGrayText(255, 120, 120, 120);
static const Color SClrBorder(255, 230, 235, 240);
static const Color SClrBg(255, 248, 250, 252);
static const Color SClrBtnLight(255, 240, 243, 248);

// ==========================================
// FIX 1: MINIMIZE BUG — Safe window title check
// Only close windows that are EXACTLY restricted settings pages.
// Do NOT use broad keywords like "settings", "setup", "folder".
// ==========================================
static bool IsTitleRestrictedSettings(const wstring& lowerTitle, bool cDateTime, bool cUsersGroups, bool cLoginItems, bool cInstaller) {
    // Date & Time — very specific Windows Settings paths only
    if (cDateTime) {
        if (lowerTitle == L"date and time" ||
            lowerTitle.find(L"time & language") != wstring::npos)
            return true;
    }
    // Users & Groups — specific only
    if (cUsersGroups) {
        if (lowerTitle == L"family & other users" ||
            lowerTitle == L"user accounts")
            return true;
    }
    // Login / Startup items — specific only
    if (cLoginItems) {
        if (lowerTitle == L"startup apps" ||
            lowerTitle == L"system configuration")
            return true;
    }
    // Installer / Uninstaller — IMPORTANT: Avoid matching loose keywords like "rasfocus" alone
    if (cInstaller) {
        if (lowerTitle.find(L"rasfocus setup") != wstring::npos || 
            lowerTitle.find(L"rasfocus uninstall") != wstring::npos ||
            lowerTitle.find(L"rasfocus+ setup") != wstring::npos || 
            lowerTitle.find(L"uninstall a program") != wstring::npos ||
            lowerTitle.find(L"programs and features") != wstring::npos)
            return true;
    }
    return false;
}

static void ForceKillProcesses(const vector<wstring>& procs) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            wstring procName = pe.szExeFile;
            transform(procName.begin(), procName.end(), procName.begin(), ::towlower);
            for (const auto& target : procs) {
                if (procName == target) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) { TerminateProcess(hProc, 1); CloseHandle(hProc); }
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

static void SettingsEnforcementThread() {
    while (true) {
        Sleep(2000);
        g_setMutex.lock();

        // FIX 1: Replaced hardcoded logic with actual profile check
        bool isAnyProfileActive = CheckIfAnyProfileIsActive();

        bool cUnsupBrowsers  = tglBlockUnsupportedBrowsers;
        bool cActivityMon    = tglBlockActivityMonitor || tglBlockTaskMgr;
        bool cDateTime       = tglBlockDateTime;
        bool cUsersGroups    = tglBlockUsersGroups;
        bool cLoginItems     = tglBlockLoginItems;
        bool cInstaller      = tglBlockInstaller || tglBlockUninstallers;
        g_setMutex.unlock();

        if (!isAnyProfileActive) continue;

        // 1. Block Unsupported Browsers
        if (cUnsupBrowsers) {
            ForceKillProcesses({ L"opera.exe", L"safari.exe", L"ucbrowser.exe",
                                 L"maxthon.exe", L"brave.exe", L"vivaldi.exe", L"yandex.exe" });
        }

        // 2. Block Activity Monitor / Taskmgr
        if (cActivityMon) {
            ForceKillProcesses({ L"taskmgr.exe", L"resmon.exe", L"perfmon.exe", L"procexp.exe" });
        }

        // FIX 1: Only close window if it is OUR app's own window or a truly restricted settings page.
        HWND hForeground = GetForegroundWindow();
        if (hForeground && hForeground != hParentWnd) { // FIX: never close our own window
            wchar_t title[512] = { 0 };
            if (GetWindowTextW(hForeground, title, 512) > 0) {
                wstring tStr = title;
                transform(tStr.begin(), tStr.end(), tStr.begin(), ::towlower);

                if (IsTitleRestrictedSettings(tStr, cDateTime, cUsersGroups, cLoginItems, cInstaller)) {
                    PostMessage(hForeground, WM_CLOSE, 0, 0);
                }
            }
        }
    }
}

// ==========================================
// --- DRAWING HELPERS ---
// ==========================================
static GraphicsPath* GetSetRoundRectPath(RectF rect, float radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

void DrawPremiumToggle(Graphics& g, RectF rect, bool isOn, bool isHovered) {
    GraphicsPath* path = GetSetRoundRectPath(rect, rect.Height / 2.0f);
    SolidBrush bg(isOn
        ? (isHovered ? SClrTealHover : SClrTeal)
        : (isHovered ? Color(255, 200, 210, 215) : Color(255, 230, 235, 240))); // FIX: hover color more visible
    g.FillPath(&bg, path);
    delete path;

    SolidBrush knob(SClrWhite);
    float knobSize = rect.Height - 6.0f;
    float kX = isOn ? (rect.X + rect.Width - knobSize - 3.0f) : (rect.X + 3.0f);
    g.FillEllipse(&knob, kX, rect.Y + 3.0f, knobSize, knobSize);
}

// FIX 2: TOGGLE HOVER BUG
void DrawSetRow(Graphics& g, const wstring& text, const wstring& desc,
                float x, float y, float w, float h,
                Font* fBold, Font* fSmall, int tglIdx, bool& valRef)
{
    SolidBrush bDark(SClrDark);
    SolidBrush bGray(SClrGrayText);
    g.DrawString(text.c_str(), -1, fBold, RectF(x, y + 10.0f, w - 100.0f, 22.0f), NULL, &bDark);
    if (!desc.empty())
        g.DrawString(desc.c_str(), -1, fSmall, RectF(x, y + 32.0f, w - 100.0f, 18.0f), NULL, &bGray);

    Pen pDiv(SClrBorder, 1.0f);
    g.DrawLine(&pDiv, x, y + h, x + w, y + h);

    // Store hitbox
    g_shb.toggles[tglIdx] = RectF(x + w - 55.0f, y + (h - 26.0f) / 2.0f, 48.0f, 26.0f);
    DrawPremiumToggle(g, g_shb.toggles[tglIdx], valRef, g_shb.hTglIdx == tglIdx);
}

// ==========================================
// --- MAIN DRAWING UI ---
// ==========================================
void DrawSettingsTab(Graphics& g, float contentX, float contentY, float contentW, float contentH) {
    static bool initThread = false;
    if (!initThread) {
        std::thread(SettingsEnforcementThread).detach();
        initThread = true;
    }

    std::lock_guard<std::mutex> lock(g_setMutex);
    s_setScrollC += (s_setScrollT - s_setScrollC) * 0.15f;

    FontFamily ff(L"Segoe UI");
    Font fTopTab(&ff, 16, FontStyleBold, UnitPixel);
    Font fRowTitle(&ff, 16, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 14, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    FontFamily ffi(L"Segoe MDL2 Assets");
    Font fIcon(&ffi, 22, FontStyleRegular, UnitPixel);
    Font fSmallIcon(&ffi, 16, FontStyleRegular, UnitPixel);

    SolidBrush brushTeal(SClrTeal), brushDark(SClrDark), brushGray(SClrGrayText),
                brushWhite(SClrWhite), brushBg(SClrBg);
    Pen pDiv(SClrBorder, 1.0f);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    // --- TOP TABS HEADER ---
    float headerH = 65.0f;
    g.FillRectangle(&brushWhite, contentX, contentY, contentW, headerH);
    g.DrawLine(&pDiv, contentX, contentY + headerH, contentX + contentW, contentY + headerH);

    wstring topTabs[] = { L"Browsers & Rules", L"General", L"System", L"Advanced", L"Notification", L"Sync" };
    float tabWidths[] = { 150.0f, 80.0f, 80.0f, 90.0f, 110.0f, 60.0f };
    float currentX = contentX + 30.0f;

    for (int i = 0; i < 6; ++i) {
        g_shb.tabs[i] = RectF(currentX, contentY, tabWidths[i], headerH);
        bool isActive = (currentSetTab == i);
        bool isHovered = (hoverSetTab == i);
        SolidBrush* textBrush = (isActive || isHovered) ? &brushTeal : &brushGray;
        g.DrawString(topTabs[i].c_str(), -1, &fTopTab, g_shb.tabs[i], &fmtC, textBrush);
        if (isActive)
            g.FillRectangle(&brushTeal, currentX + 15.0f, contentY + headerH - 4.0f, tabWidths[i] - 30.0f, 4.0f);
        currentX += tabWidths[i] + 5.0f;
    }

    // --- CONTENT AREA ---
    g.FillRectangle(&brushBg, contentX, contentY + headerH + 1.0f, contentW, contentH - headerH - 1.0f);

    float boxX = contentX + 30.0f;
    float boxW = contentW - 60.0f;
    float boxH = contentH - headerH - 40.0f;
    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(RectF(boxX, contentY + headerH + 20.0f, boxW, boxH));

    GraphicsPath* boxPath = GetSetRoundRectPath(RectF(boxX, contentY + headerH + 20.0f, boxW, 2000.0f), 8.0f);
    g.FillPath(&brushWhite, boxPath);
    g.DrawPath(&pDiv, boxPath);
    delete boxPath;

    float rowY = contentY + headerH + 20.0f - s_setScrollC;
    float rowH = 65.0f;
    float innerX = boxX + 25.0f;
    float innerW = boxW - 50.0f;
    int tglCount = 0;

    auto DrawSpinnerRaw = [&](float sx, float sy, wstring val,
                               RectF& hm, RectF& hp, bool stM, bool stP) {
        hm = RectF(sx, sy, 34, 34);
        hp = RectF(sx + 90, sy, 34, 34);
        GraphicsPath* pM = GetSetRoundRectPath(hm, 6);
        SolidBrush bM(stM ? SClrTealHover : SClrBtnLight);
        g.FillPath(&bM, pM); delete pM;
        g.DrawString(L"\xE738", -1, &fSmallIcon, hm, &fmtC, stM ? &brushWhite : &brushDark);
        GraphicsPath* pP = GetSetRoundRectPath(hp, 6);
        SolidBrush bP(stP ? SClrTealHover : SClrBtnLight);
        g.FillPath(&bP, pP); delete pP;
        g.DrawString(L"\xE710", -1, &fSmallIcon, hp, &fmtC, stP ? &brushWhite : &brushDark);
        g.DrawString(val.c_str(), -1, &fBold, RectF(sx + 34, sy, 56, 34), &fmtC, &brushDark);
    };

    if (currentSetTab == 0) {
        DrawSetRow(g, L"Block unsupported browsers", L"Kill untested browsers to prevent proxy bypassing.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockUnsupportedBrowsers); rowY += rowH;
        DrawSetRow(g, L"Block Date & Time settings", L"Prevent changing system time when a block is enabled.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockDateTime); rowY += rowH;
        DrawSetRow(g, L"Block Login Items", L"Block startup management tools when a block is enabled.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockLoginItems); rowY += rowH;
        DrawSetRow(g, L"Block Users & Groups", L"Block Windows account settings when a block is enabled.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockUsersGroups); rowY += rowH;
        DrawSetRow(g, L"Block Activity Monitor", L"Kill Task Manager and Resource Monitor during focus.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockActivityMonitor); rowY += rowH;
        DrawSetRow(g, L"Block inactive browser tabs", L"Freeze or restrict inactive tabs for blocked websites.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockInactiveTabs); rowY += rowH;
        DrawSetRow(g, L"Block embedded content", L"Filter iframes and video players from blocked websites.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockEmbedded); rowY += rowH;
        DrawSetRow(g, L"Block the Pause for a Cause", L"Disable the emergency break feature on the block page.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockPauseForCause); rowY += rowH;
        DrawSetRow(g, L"Block the RasFocus+ Installer", L"Block setup.exe or uninstall triggers to prevent removal.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglBlockInstaller); rowY += rowH;
        DrawSetRow(g, L"Force Allow file URLs access", L"Force extension permissions in Chromium based browsers.", innerX, rowY, innerW, rowH, &fRowTitle, &fSmall, tglCount++, tglForceFileURLs); rowY += rowH;

        g.DrawString(L"Seconds given to re-enable extensions", -1, &fRowTitle, RectF(innerX, rowY + 10.0f, innerW - 140.0f, 22.0f), NULL, &brushDark);
        g.DrawString(L"Grace period if extension is disabled or removed.", -1, &fSmall, RectF(innerX, rowY + 32.0f, innerW - 140.0f, 18.0f), NULL, &brushGray);
        DrawSpinnerRaw(innerX + innerW - 125.0f, rowY + 15.0f, to_wstring(reEnableSeconds) + L"s", g_shb.minusSec, g_shb.plusSec, g_shb.hMinusSec, g_shb.hPlusSec);
        rowY += rowH;
        s_setScrollMax = max(0.0f, (rowY - (contentY + headerH + 20.0f)) - boxH + 20.0f);
    }
    else {
        g.DrawString(L"Other settings logic follows the same exact UI architecture...", -1, &fRowTitle,
                     RectF(innerX, rowY + 20.0f, innerW, 30.0f), NULL, &brushGray);
        s_setScrollMax = 0.0f;
    }

    g.SetClip(&oldClip);

    // Draw Scrollbar
    if (s_setScrollMax > 0) {
        float thumbH = max(30.0f, boxH * (boxH / (boxH + s_setScrollMax)));
        float thumbY = (contentY + headerH + 20.0f) + (s_setScrollC / s_setScrollMax) * (boxH - thumbH);
        GraphicsPath* sp = GetSetRoundRectPath(RectF(boxX + boxW - 8.0f, thumbY, 6.0f, thumbH), 3.0f);
        SolidBrush sb(Color(100, 150, 150, 150));
        g.FillPath(&sb, sp);
        delete sp;
    }
}

// ==========================================
// FIX 3: MOUSE HOVER & CLICK — Toggle detection was broken
// ==========================================
void ProcessSettingsMouseMove(float x, float y) {
    std::lock_guard<std::mutex> lock(g_setMutex);

    // FIX 3: Reset ALL hover states first
    hoverSetTab = -1;
    g_shb.hTglIdx = -1;
    g_shb.hMinusSec = false;
    g_shb.hPlusSec = false;
    g_shb.hLangBtn = false;
    g_shb.hMinusPlan = false;
    g_shb.hPlusPlan = false;
    g_shb.hPos1 = false;
    g_shb.hPos2 = false;

    // Check Top Tabs — FIX: do NOT return early; just set and break
    for (int i = 0; i < 6; i++) {
        if (g_shb.tabs[i].Contains(x, y)) {
            hoverSetTab = i;
            return; // tabs are in header, nothing else can overlap — early return OK here
        }
    }

    // FIX 3: Check Toggles — do NOT return inside loop until we find a match
    for (int i = 0; i < 60; i++) {
        // Skip zero-initialized empty hitboxes (Width==0 means not set yet)
        if (g_shb.toggles[i].Width == 0) continue;
        if (g_shb.toggles[i].Contains(x, y)) {
            g_shb.hTglIdx = i;
            return;
        }
    }

    // Check Spinners
    if (g_shb.minusSec.Width > 0 && g_shb.minusSec.Contains(x, y)) { g_shb.hMinusSec = true; return; }
    if (g_shb.plusSec.Width  > 0 && g_shb.plusSec.Contains(x, y))  { g_shb.hPlusSec  = true; return; }
}

void ProcessSettingsMouseClick(float x, float y) {
    std::lock_guard<std::mutex> lock(g_setMutex);

    // Tab switch
    if (hoverSetTab != -1) {
        currentSetTab = hoverSetTab;
        s_setScrollC = s_setScrollT = 0;

        // FIX 3: Clear all toggle hitboxes when switching tabs so stale
        // hitboxes from previous tab don't falsely trigger on the new tab.
        for (int i = 0; i < 60; i++) g_shb.toggles[i] = RectF(0, 0, 0, 0);
        g_shb.hTglIdx = -1;
        return;
    }

    if (currentSetTab == 0) {
        // FIX 3: Also do a direct hit-test on click for reliability,
        // instead of relying solely on hTglIdx from last MouseMove.
        int clickedTgl = -1;
        for (int i = 0; i < 60; i++) {
            if (g_shb.toggles[i].Width > 0 && g_shb.toggles[i].Contains(x, y)) {
                clickedTgl = i;
                break;
            }
        }
        int idx = (clickedTgl != -1) ? clickedTgl : g_shb.hTglIdx;

        if      (idx == 0) tglBlockUnsupportedBrowsers = !tglBlockUnsupportedBrowsers;
        else if (idx == 1) tglBlockDateTime            = !tglBlockDateTime;
        else if (idx == 2) tglBlockLoginItems          = !tglBlockLoginItems;
        else if (idx == 3) tglBlockUsersGroups         = !tglBlockUsersGroups;
        else if (idx == 4) tglBlockActivityMonitor     = !tglBlockActivityMonitor;
        else if (idx == 5) tglBlockInactiveTabs        = !tglBlockInactiveTabs;
        else if (idx == 6) tglBlockEmbedded            = !tglBlockEmbedded;
        else if (idx == 7) tglBlockPauseForCause       = !tglBlockPauseForCause;
        else if (idx == 8) tglBlockInstaller           = !tglBlockInstaller;
        else if (idx == 9) tglForceFileURLs            = !tglForceFileURLs;

        // Spinner
        if (g_shb.minusSec.Width > 0 && g_shb.minusSec.Contains(x, y) && reEnableSeconds > 5)   reEnableSeconds -= 5;
        if (g_shb.plusSec.Width  > 0 && g_shb.plusSec.Contains(x, y)  && reEnableSeconds < 300) reEnableSeconds += 5;
    }
}

void ProcessSettingsMouseWheel(int delta) {
    std::lock_guard<std::mutex> lock(g_setMutex);
    s_setScrollT -= (delta > 0 ? 1 : -1) * 45.0f;
    s_setScrollT = max(0.0f, min(s_setScrollT, s_setScrollMax));

    // FIX 3: After scroll, clear toggle hitboxes so they are rebuilt
    // fresh in the next DrawSettingsTab call with updated Y positions.
    for (int i = 0; i < 60; i++) g_shb.toggles[i] = RectF(0, 0, 0, 0);
    g_shb.hTglIdx = -1;
}
