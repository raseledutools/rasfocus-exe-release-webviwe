#include "tab_device_block.h"
#include <vector>
#include <string>
#include <powrprof.h>
#include <tlhelp32.h>
#include <chrono>
#include <ctime>
#pragma comment(lib, "PowrProf.lib")

// --- Premium Feature Gate ---
extern bool g_isPremiumUser;
extern bool g_showUpgradePopup;

// ── Family Link: Parent Remote Control ──
extern bool g_parentLockAllTabs;       // tab_family_link.cpp থেকে আসছে

using namespace Gdiplus;
using namespace std;

// --- State Variables ---
static float s_cx = 0, s_cy = 0, s_cw = 800, s_ch = 600;

// ──────────────────────────────────────────────
// CARD 1: INTERNET FASTING
// ──────────────────────────────────────────────
static bool isFastingActive = false;
static int fastingModeIdx = 0; // 0: Until manually stopped, 1: 1 Hour, 2: 2 Hours
static bool isFastingDropOpen = false;
static bool hovFastingDrop = false;
static int hovFastingOpt = -1;
static bool hovFastingBtn = false;
static DWORD fastingStartTick = 0; // GetTickCount() when fasting started

// Emergency Unlock PIN (for Internet Fasting)
static bool isEmergencyUnlockMode = false;
static wchar_t emergencyPin[5] = { 0 };       // stored 4-digit PIN
static wchar_t enteredPin[5] = { 0 };          // currently typed
static int enteredPinLen = 0;
static bool hovSetPinBtn = false;
static bool hovUnlockBtn = false;
static bool isPinDialogOpen = false;
static bool pinError = false;

// ──────────────────────────────────────────────
// CARD 2: POWER MANAGEMENT
// ──────────────────────────────────────────────
static int powerActionIdx = 0; // 0: Lock PC, 1: Sleep, 2: Shutdown
static int powerTimerIdx = 0;  // 0: Immediate, 1: In 15 mins, 2: In 1 Hour
static bool isPowerActionDropOpen = false, hovPowerActionDrop = false;
static bool isPowerTimerDropOpen = false, hovPowerTimerDrop = false;
static int hovPowerActionOpt = -1, hovPowerTimerOpt = -1;
static bool hovPowerBtn = false;

// ──────────────────────────────────────────────
// CARD 3: FOCUS SESSION (Pomodoro)
// ──────────────────────────────────────────────
static bool isFocusSessionActive = false;
static int focusDurationIdx = 0; // 0: 25 min, 1: 45 min, 2: 60 min, 3: 90 min
static bool isFocusDurDropOpen = false, hovFocusDurDrop = false;
static int hovFocusDurOpt = -1;
static bool hovFocusBtn = false;
static DWORD focusStartTick = 0;
static bool focusDisabledInternet = false; // did session auto-disable internet?

// ──────────────────────────────────────────────
// CARD 4: DAILY USAGE LIMIT
// ──────────────────────────────────────────────
static bool isDailyLimitEnabled = false;
static int dailyLimitIdx = 0;    // 0: 1hr, 1: 2hr, 2: 3hr, 3: 4hr, 4: 6hr
static bool isDailyLimDropOpen = false, hovDailyLimDrop = false;
static int hovDailyLimOpt = -1;
static bool hovDailyLimBtn = false;
static DWORD dailyUsedSeconds = 0;        // seconds used today
static DWORD dailySessionStart = 0;       // tick when counting started

// ──────────────────────────────────────────────
// CARD 5: APP BLOCKER
// ──────────────────────────────────────────────
static bool isAppBlockerEnabled = false;
static bool hovAppBlockerToggle = false;
// Distraction app process names
static vector<wstring> blockedApps = {
    L"chrome.exe", L"firefox.exe", L"msedge.exe",
    L"YouTube.exe", L"spotify.exe", L"steam.exe",
    L"Discord.exe", L"WhatsApp.exe"
};
static int hovAppItem = -1;
static bool hovAddAppBtn = false;

// ──────────────────────────────────────────────
// CARD 6: BREAK REMINDER
// ──────────────────────────────────────────────
static bool isBreakReminderEnabled = false;
static int breakIntervalIdx = 0; // 0: 20min, 1: 30min, 2: 45min, 3: 60min
static bool isBreakIntDropOpen = false, hovBreakIntDrop = false;
static int hovBreakIntOpt = -1;
static bool hovBreakReminderToggle = false;
static DWORD lastBreakTick = 0;

// ──────────────────────────────────────────────
// Option Lists
// ──────────────────────────────────────────────
vector<wstring> fastingModes   = { L"Until Manually Stopped", L"1 Hour Fasting", L"2 Hours Fasting" };
vector<wstring> powerActions   = { L"Lock PC", L"Sleep Mode", L"Shutdown PC" };
vector<wstring> powerTimers    = { L"Immediately", L"In 15 Minutes", L"In 1 Hour" };
vector<wstring> focusDurations = { L"25 Minutes (Pomodoro)", L"45 Minutes", L"60 Minutes", L"90 Minutes" };
vector<wstring> dailyLimits    = { L"1 Hour / Day", L"2 Hours / Day", L"3 Hours / Day", L"4 Hours / Day", L"6 Hours / Day" };
vector<wstring> breakIntervals = { L"Every 20 Minutes", L"Every 30 Minutes", L"Every 45 Minutes", L"Every 1 Hour" };

// --- Colors ---
static const Color ClrTeal(255, 12, 168, 176);
static const Color ClrTealHover(255, 30, 185, 195);
static const Color ClrTealLight(255, 220, 248, 250);
static const Color ClrWhite(255, 255, 255, 255);
static const Color ClrDark(255, 50, 50, 50);
static const Color ClrGrayText(255, 120, 120, 120);
static const Color ClrBorder(255, 220, 225, 230);
static const Color ClrBg(255, 248, 250, 252);
static const Color ClrBgHover(255, 235, 248, 250);
static const Color ClrRed(255, 231, 76, 60);
static const Color ClrRedLight(255, 255, 235, 232);
static const Color ClrGreen(255, 90, 170, 20);
static const Color ClrGreenHover(255, 70, 150, 10);
static const Color ClrOrange(255, 230, 126, 34);
static const Color ClrOrangeLight(255, 255, 243, 224);
static const Color ClrPurple(255, 142, 68, 173);
static const Color ClrPurpleLight(255, 245, 235, 250);

// ──────────────────────────────────────────────
// HELPERS
// ──────────────────────────────────────────────
static GraphicsPath* GetRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

static void DrawCard(Graphics& g, float x, float y, float w, float h, Color accent = Color(0,0,0,0)) {
    GraphicsPath* cp = GetRoundRectPath(RectF(x, y, w, h), 10);
    SolidBrush bWhite(ClrWhite);
    Pen pBorder(ClrBorder, 1.5f);
    g.FillPath(&bWhite, cp);
    g.DrawPath(&pBorder, cp);
    delete cp;
    // Left accent bar
    if (accent.GetA() > 0) {
        GraphicsPath* bar = GetRoundRectPath(RectF(x, y + 20.0f, 4.0f, h - 40.0f), 2);
        SolidBrush bAccent(accent);
        g.FillPath(&bAccent, bar);
        delete bar;
    }
}

static void DrawDropdown(Graphics& g, FontFamily& ff, RectF rect, const wstring& label, bool isOpen, bool hov, bool disabled = false) {
    GraphicsPath* fdp = GetRoundRectPath(rect, 4);
    SolidBrush fdBg(disabled ? ClrBg : (hov ? ClrBgHover : ClrWhite));
    Pen pBorder(isOpen ? ClrTeal : ClrBorder, isOpen ? 2.0f : 1.5f);
    g.FillPath(&fdBg, fdp);
    g.DrawPath(&pBorder, fdp);
    delete fdp;
    Font fNormal(&ff, 14, FontStyleRegular, UnitPixel);
    Font fIconSmall(&ff, 13, FontStyleRegular, UnitPixel);
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fArrow(&ffIcons, 12, FontStyleRegular, UnitPixel);
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    SolidBrush bText(disabled ? ClrGrayText : ClrDark);
    SolidBrush bGray(ClrGrayText);
    g.DrawString(label.c_str(), -1, &fNormal, RectF(rect.X + 10.0f, rect.Y, rect.Width - 30.0f, rect.Height), &fL, &bText);
    g.DrawString(L"\xE70D", -1, &fArrow, RectF(rect.X + rect.Width - 28.0f, rect.Y, 26.0f, rect.Height), &fC, &bGray);
}

static void DrawDropList(Graphics& g, FontFamily& ff, RectF dropRect, const vector<wstring>& items, int hovIdx, float extraOffsetY = 0) {
    float listY = dropRect.Y + dropRect.Height + 2.0f + extraOffsetY;
    RectF listRect(dropRect.X, listY, dropRect.Width, items.size() * 36.0f);
    GraphicsPath* listP = GetRoundRectPath(listRect, 6);
    SolidBrush bWhite(ClrWhite);
    Pen pBorder(ClrTeal, 1.5f);
    g.FillPath(&bWhite, listP);
    g.DrawPath(&pBorder, listP);
    delete listP;
    Font fNormal(&ff, 14, FontStyleRegular, UnitPixel);
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    float itemY = listY;
    for (size_t i = 0; i < items.size(); ++i) {
        SolidBrush optBg((int)i == hovIdx ? ClrTealLight : ClrWhite);
        SolidBrush optText((int)i == hovIdx ? ClrTeal : ClrDark);
        g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itemY + 1.0f, listRect.Width - 4.0f, 34.0f));
        g.DrawString(items[i].c_str(), -1, &fNormal, RectF(listRect.X + 14.0f, itemY, listRect.Width, 36.0f), &fL, &optText);
        itemY += 36.0f;
    }
}

static void DrawStatusBadge(Graphics& g, FontFamily& ff, float x, float y, const wchar_t* text, Color bgColor, Color textColor) {
    Font fSmall(&ff, 12, FontStyleBold, UnitPixel);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    RectF badgeRect(x, y, 90.0f, 22.0f);
    GraphicsPath* bp = GetRoundRectPath(badgeRect, 11);
    SolidBrush bBg(bgColor);
    g.FillPath(&bBg, bp);
    delete bp;
    SolidBrush bText(textColor);
    g.DrawString(text, -1, &fSmall, badgeRect, &fC, &bText);
}

// Format seconds → "MM:SS" or "HH:MM:SS"
static wstring FormatCountdown(DWORD totalSec) {
    DWORD h = totalSec / 3600;
    DWORD m = (totalSec % 3600) / 60;
    DWORD s = totalSec % 60;
    wchar_t buf[16];
    if (h > 0) swprintf_s(buf, L"%02d:%02d:%02d", h, m, s);
    else       swprintf_s(buf, L"%02d:%02d", m, s);
    return wstring(buf);
}

// Draw a progress ring arc (0.0–1.0)
static void DrawProgressRing(Graphics& g, float cx, float cy, float r, float progress, Color fgColor, Color bgColor) {
    float thick = 6.0f;
    Pen bgPen(bgColor, thick); bgPen.SetStartCap(LineCapRound); bgPen.SetEndCap(LineCapRound);
    g.DrawEllipse(&bgPen, cx - r, cy - r, r * 2, r * 2);
    if (progress > 0.0f) {
        Pen fgPen(fgColor, thick); fgPen.SetStartCap(LineCapRound); fgPen.SetEndCap(LineCapRound);
        float sweep = 360.0f * min(progress, 1.0f);
        g.DrawArc(&fgPen, cx - r, cy - r, r * 2, r * 2, -90.0f, sweep);
    }
}

// Kill a process by name
static void KillProcessByName(const wstring& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

// ──────────────────────────────────────────────
// MAIN DRAW
// ──────────────────────────────────────────────
void DrawDeviceBlockTab(Graphics& g, float cx, float cy, float cw, float ch) {
    s_cx = cx; s_cy = cy; s_cw = cw; s_ch = ch;

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fTitle(&ff, 22, FontStyleBold, UnitPixel);
    Font fCardTitle(&ff, 16, FontStyleBold, UnitPixel);
    Font fNormal(&ff, 14, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fIconBig(&ffIcons, 28, FontStyleRegular, UnitPixel);
    Font fIconMed(&ffIcons, 18, FontStyleRegular, UnitPixel);

    SolidBrush bTeal(ClrTeal); SolidBrush bDark(ClrDark);
    SolidBrush bGray(ClrGrayText); SolidBrush bWhite(ClrWhite);
    SolidBrush bBg(ClrBg);
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fR; fR.SetAlignment(StringAlignmentFar);   fR.SetLineAlignment(StringAlignmentCenter);

    float bx = cx + 30.0f;
    float by = cy + 30.0f;
    float bw = cw - 60.0f;

    // ── Page Header ──
    g.DrawString(L"Device Level Blockers", -1, &fTitle, RectF(bx, by, bw, 30.0f), &fL, &bDark);
    g.DrawString(L"Distraction-proof your work sessions with internet fasting, smart timers & app blocking.", -1, &fNormal, RectF(bx, by + 32.0f, bw, 20.0f), &fL, &bGray);

    // ── Scrollable layout: 3 rows of 2 cards ──
    float cardY    = by + 65.0f;
    float cardW    = (bw - 20.0f) / 2.0f;
    float cardH    = 270.0f;
    float lx       = bx;
    float rx       = bx + cardW + 20.0f;
    float row2Y    = cardY + cardH + 20.0f;
    float row3Y    = row2Y + cardH + 20.0f;

    // ============================================================
    // ROW 1 LEFT: INTERNET FASTING
    // ============================================================
    DrawCard(g, lx, cardY, cardW, cardH, ClrTeal);
    g.DrawString(L"\xE774", -1, &fIconBig, RectF(lx + 18.0f, cardY + 18.0f, 36.0f, 36.0f), &fL, &bTeal);
    g.DrawString(L"Internet Fasting", -1, &fCardTitle, RectF(lx + 58.0f, cardY + 18.0f, cardW - 80.0f, 30.0f), &fL, &bDark);
    if (isFastingActive)
        DrawStatusBadge(g, ff, lx + cardW - 102.0f, cardY + 20.0f, L"ACTIVE", ClrTeal, ClrWhite);
    g.DrawString(L"Disable all network connections to force offline deep work.", -1, &fSmall, RectF(lx + 18.0f, cardY + 56.0f, cardW - 36.0f, 34.0f), &fL, &bGray);

    // Countdown when active
    if (isFastingActive && fastingModeIdx > 0) {
        DWORD elapsed = (GetTickCount() - fastingStartTick) / 1000;
        DWORD totalSec = (fastingModeIdx == 1) ? 3600 : 7200;
        DWORD remaining = (elapsed < totalSec) ? (totalSec - elapsed) : 0;
        float prog = (float)elapsed / (float)totalSec;
        DrawProgressRing(g, lx + cardW - 52.0f, cardY + 140.0f, 28.0f, prog, ClrTeal, ClrBorder);
        Font fCountdown(&ff, 11, FontStyleBold, UnitPixel);
        SolidBrush bTeal2(ClrTeal);
        wstring countdown = FormatCountdown(remaining);
        g.DrawString(countdown.c_str(), -1, &fCountdown, RectF(lx + cardW - 80.0f, cardY + 175.0f, 56.0f, 18.0f), &fC, &bTeal2);
    }

    // Duration dropdown
    g.DrawString(L"Duration:", -1, &fBold, RectF(lx + 18.0f, cardY + 98.0f, 80.0f, 20.0f), &fL, &bDark);
    RectF fDropRect(lx + 18.0f, cardY + 122.0f, cardW - 36.0f, 34.0f);
    DrawDropdown(g, ff, fDropRect, fastingModes[fastingModeIdx], isFastingDropOpen, hovFastingDrop, isFastingActive);

    // Emergency PIN row (shown when active)
    if (isFastingActive) {
        g.DrawString(L"\xE72E", -1, &fIconMed, RectF(lx + 18.0f, cardY + 170.0f, 22.0f, 22.0f), &fL, &bGray);
        SolidBrush bOrange(ClrOrange);
        g.DrawString(L"PIN required to stop fasting", -1, &fSmall, RectF(lx + 44.0f, cardY + 164.0f, cardW - 100.0f, 24.0f), &fL, &bOrange);
        // Unlock button
        RectF unlockBtnRect(lx + 18.0f, cardY + 198.0f, (cardW - 46.0f) / 2.0f - 5.0f, 36.0f);
        GraphicsPath* ubp = GetRoundRectPath(unlockBtnRect, 4);
        SolidBrush uBg(hovUnlockBtn ? ClrOrange : ClrOrangeLight);
        g.FillPath(&uBg, ubp); delete ubp;
        SolidBrush bOText(hovUnlockBtn ? ClrWhite : ClrOrange);
        g.DrawString(L"Unlock with PIN", -1, &fSmall, unlockBtnRect, &fC, &bOText);
        // Stop (no PIN needed if PIN not set)
        RectF stopBtnRect(lx + 18.0f + (cardW - 46.0f) / 2.0f + 5.0f, cardY + 198.0f, (cardW - 46.0f) / 2.0f - 5.0f, 36.0f);
        GraphicsPath* sbp = GetRoundRectPath(stopBtnRect, 4);
        SolidBrush sBg(hovFastingBtn ? Color(255, 200, 50, 50) : ClrRedLight);
        g.FillPath(&sBg, sbp); delete sbp;
        SolidBrush bSText(hovFastingBtn ? ClrWhite : ClrRed);
        g.DrawString(L"Force Stop", -1, &fSmall, stopBtnRect, &fC, &bSText);
    } else {
        // Start button
        RectF fBtnRect(lx + 18.0f, cardY + 210.0f, cardW - 36.0f, 38.0f);
        GraphicsPath* fbp = GetRoundRectPath(fBtnRect, 5);
        SolidBrush fBtnBrush(hovFastingBtn ? ClrGreenHover : ClrGreen);
        g.FillPath(&fBtnBrush, fbp); delete fbp;
        g.DrawString(L"\xE774  Start Internet Fasting", -1, &fBold, fBtnRect, &fC, &bWhite);
    }

    // ============================================================
    // ROW 1 RIGHT: POWER MANAGEMENT
    // ============================================================
    DrawCard(g, rx, cardY, cardW, cardH, ClrRed);
    SolidBrush bRed(ClrRed);
    g.DrawString(L"\xE7E8", -1, &fIconBig, RectF(rx + 18.0f, cardY + 18.0f, 36.0f, 36.0f), &fL, &bRed);
    g.DrawString(L"Power Management", -1, &fCardTitle, RectF(rx + 58.0f, cardY + 18.0f, cardW - 80.0f, 30.0f), &fL, &bDark);
    g.DrawString(L"Enforce breaks by locking, sleeping, or shutting down your PC on schedule.", -1, &fSmall, RectF(rx + 18.0f, cardY + 56.0f, cardW - 36.0f, 34.0f), &fL, &bGray);

    float halfDropW = (cardW - 52.0f) / 2.0f;
    g.DrawString(L"Action:", -1, &fBold, RectF(rx + 18.0f, cardY + 98.0f, 60.0f, 20.0f), &fL, &bDark);
    g.DrawString(L"When:", -1, &fBold, RectF(rx + 28.0f + halfDropW, cardY + 98.0f, 50.0f, 20.0f), &fL, &bDark);
    RectF pActDropRect(rx + 18.0f, cardY + 122.0f, halfDropW, 34.0f);
    RectF pTimDropRect(rx + 28.0f + halfDropW, cardY + 122.0f, halfDropW, 34.0f);
    DrawDropdown(g, ff, pActDropRect, powerActions[powerActionIdx], isPowerActionDropOpen, hovPowerActionDrop);
    DrawDropdown(g, ff, pTimDropRect, powerTimers[powerTimerIdx], isPowerTimerDropOpen, hovPowerTimerDrop);

    // Schedule label
    g.DrawString(L"\xE787", -1, &fIconMed, RectF(rx + 18.0f, cardY + 170.0f, 22.0f, 22.0f), &fL, &bGray);
    wchar_t schedBuf[80];
    swprintf_s(schedBuf, L"%ls  %ls", powerActions[powerActionIdx].c_str(), powerTimers[powerTimerIdx].c_str());
    g.DrawString(schedBuf, -1, &fSmall, RectF(rx + 44.0f, cardY + 164.0f, cardW - 80.0f, 24.0f), &fL, &bGray);

    RectF pBtnRect(rx + 18.0f, cardY + 210.0f, cardW - 36.0f, 38.0f);
    GraphicsPath* pbp = GetRoundRectPath(pBtnRect, 5);
    SolidBrush pBtnBrush(hovPowerBtn ? Color(255, 200, 50, 50) : ClrRed);
    g.FillPath(&pBtnBrush, pbp); delete pbp;
    g.DrawString(L"\xE7E8  Apply Power Action", -1, &fBold, pBtnRect, &fC, &bWhite);

    // ============================================================
    // ROW 2 LEFT: FOCUS SESSION (Pomodoro)
    // ============================================================
    DrawCard(g, lx, row2Y, cardW, cardH, ClrGreen);
    SolidBrush bGreen(ClrGreen);
    g.DrawString(L"\xE916", -1, &fIconBig, RectF(lx + 18.0f, row2Y + 18.0f, 36.0f, 36.0f), &fL, &bGreen);
    g.DrawString(L"Focus Session", -1, &fCardTitle, RectF(lx + 58.0f, row2Y + 18.0f, cardW - 80.0f, 30.0f), &fL, &bDark);
    if (isFocusSessionActive)
        DrawStatusBadge(g, ff, lx + cardW - 102.0f, row2Y + 20.0f, L"RUNNING", ClrGreen, ClrWhite);
    g.DrawString(L"Pomodoro-style deep work timer. Internet auto-disables and restores.", -1, &fSmall, RectF(lx + 18.0f, row2Y + 56.0f, cardW - 36.0f, 34.0f), &fL, &bGray);

    // Duration dropdown
    g.DrawString(L"Duration:", -1, &fBold, RectF(lx + 18.0f, row2Y + 98.0f, 80.0f, 20.0f), &fL, &bDark);
    RectF focDurDropRect(lx + 18.0f, row2Y + 122.0f, cardW - 36.0f, 34.0f);
    DrawDropdown(g, ff, focDurDropRect, focusDurations[focusDurationIdx], isFocusDurDropOpen, hovFocusDurDrop, isFocusSessionActive);

    // Countdown + ring when active
    if (isFocusSessionActive) {
        int durations[] = { 25*60, 45*60, 60*60, 90*60 };
        DWORD totalSec = durations[focusDurationIdx];
        DWORD elapsed  = (GetTickCount() - focusStartTick) / 1000;
        DWORD remaining = (elapsed < totalSec) ? (totalSec - elapsed) : 0;
        float prog = (float)elapsed / (float)totalSec;
        DrawProgressRing(g, lx + cardW - 50.0f, row2Y + 180.0f, 26.0f, prog, ClrGreen, ClrBorder);
        Font fCountdown(&ff, 11, FontStyleBold, UnitPixel);
        SolidBrush bGreen2(ClrGreen);
        wstring countdown = FormatCountdown(remaining);
        g.DrawString(countdown.c_str(), -1, &fCountdown, RectF(lx + cardW - 77.0f, row2Y + 205.0f, 54.0f, 18.0f), &fC, &bGreen2);
    }

    RectF focBtnRect(lx + 18.0f, row2Y + 210.0f, cardW - 36.0f, 38.0f);
    if (isFocusSessionActive) focBtnRect = RectF(lx + 18.0f, row2Y + 210.0f, cardW - 100.0f, 38.0f);
    GraphicsPath* focbp = GetRoundRectPath(focBtnRect, 5);
    SolidBrush focBtnBrush(isFocusSessionActive ? (hovFocusBtn ? Color(255, 200, 50, 50) : ClrRed) : (hovFocusBtn ? ClrGreenHover : ClrGreen));
    g.FillPath(&focBtnBrush, focbp); delete focbp;
    g.DrawString(isFocusSessionActive ? L"\xE71A  Stop Session" : L"\xE916  Start Focus Session", -1, &fBold, focBtnRect, &fC, &bWhite);

    // ============================================================
    // ROW 2 RIGHT: DAILY USAGE LIMIT
    // ============================================================
    DrawCard(g, rx, row2Y, cardW, cardH, ClrOrange);
    SolidBrush bOrange2(ClrOrange);
    g.DrawString(L"\xE787", -1, &fIconBig, RectF(rx + 18.0f, row2Y + 18.0f, 36.0f, 36.0f), &fL, &bOrange2);
    g.DrawString(L"Daily Usage Limit", -1, &fCardTitle, RectF(rx + 58.0f, row2Y + 18.0f, cardW - 80.0f, 30.0f), &fL, &bDark);
    if (isDailyLimitEnabled)
        DrawStatusBadge(g, ff, rx + cardW - 102.0f, row2Y + 20.0f, L"ON", ClrOrange, ClrWhite);
    g.DrawString(L"Auto-cut internet after your daily quota. Resets at midnight.", -1, &fSmall, RectF(rx + 18.0f, row2Y + 56.0f, cardW - 36.0f, 34.0f), &fL, &bGray);

    // Limit dropdown
    g.DrawString(L"Daily Limit:", -1, &fBold, RectF(rx + 18.0f, row2Y + 98.0f, 90.0f, 20.0f), &fL, &bDark);
    RectF dailyLimDropRect(rx + 18.0f, row2Y + 122.0f, cardW - 36.0f, 34.0f);
    DrawDropdown(g, ff, dailyLimDropRect, dailyLimits[dailyLimitIdx], isDailyLimDropOpen, hovDailyLimDrop, isDailyLimitEnabled);

    // Usage progress bar
    if (isDailyLimitEnabled) {
        int limSeconds[] = { 3600, 7200, 10800, 14400, 21600 };
        DWORD elapsed = isDailyLimitEnabled ? (dailyUsedSeconds + (dailySessionStart > 0 ? (GetTickCount() - dailySessionStart) / 1000 : 0)) : 0;
        float usedFrac = min((float)elapsed / (float)limSeconds[dailyLimitIdx], 1.0f);
        float pbX = rx + 18.0f, pbY = row2Y + 170.0f, pbW = cardW - 36.0f, pbH = 10.0f;
        GraphicsPath* pbBg = GetRoundRectPath(RectF(pbX, pbY, pbW, pbH), 5);
        SolidBrush pbBgBrush(ClrBorder); g.FillPath(&pbBgBrush, pbBg); delete pbBg;
        if (usedFrac > 0.0f) {
            Color barColor = (usedFrac > 0.8f) ? ClrRed : ClrOrange;
            GraphicsPath* pbFg = GetRoundRectPath(RectF(pbX, pbY, pbW * usedFrac, pbH), 5);
            SolidBrush pbFgBrush(barColor); g.FillPath(&pbFgBrush, pbFg); delete pbFg;
        }
        wchar_t usedBuf[40];
        swprintf_s(usedBuf, L"Used: %s / %ls", FormatCountdown(elapsed).c_str(), dailyLimits[dailyLimitIdx].c_str());
        g.DrawString(usedBuf, -1, &fSmall, RectF(pbX, pbY + 14.0f, pbW, 18.0f), &fL, &bGray);
    }

    RectF dailyBtnRect(rx + 18.0f, row2Y + 210.0f, cardW - 36.0f, 38.0f);
    GraphicsPath* dlbp = GetRoundRectPath(dailyBtnRect, 5);
    SolidBrush dlBrush(isDailyLimitEnabled ? (hovDailyLimBtn ? Color(255, 200, 50, 50) : ClrRed) : (hovDailyLimBtn ? Color(255, 200, 100, 20) : ClrOrange));
    g.FillPath(&dlBrush, dlbp); delete dlbp;
    g.DrawString(isDailyLimitEnabled ? L"\xE71A  Disable Daily Limit" : L"\xE787  Enable Daily Limit", -1, &fBold, dailyBtnRect, &fC, &bWhite);

    // ============================================================
    // ROW 3 LEFT: APP BLOCKER
    // ============================================================
    DrawCard(g, lx, row3Y, cardW, cardH, ClrPurple);
    SolidBrush bPurple(ClrPurple);
    g.DrawString(L"\xE71D", -1, &fIconBig, RectF(lx + 18.0f, row3Y + 18.0f, 36.0f, 36.0f), &fL, &bPurple);
    g.DrawString(L"App Blocker", -1, &fCardTitle, RectF(lx + 58.0f, row3Y + 18.0f, cardW - 150.0f, 30.0f), &fL, &bDark);
    // Toggle switch
    float togX = lx + cardW - 70.0f, togY = row3Y + 24.0f;
    GraphicsPath* togTrack = GetRoundRectPath(RectF(togX, togY, 50.0f, 22.0f), 11);
    SolidBrush togBg(isAppBlockerEnabled ? ClrPurple : ClrBorder);
    g.FillPath(&togBg, togTrack); delete togTrack;
    float thumbX = isAppBlockerEnabled ? (togX + 30.0f) : (togX + 2.0f);
    GraphicsPath* togThumb = GetRoundRectPath(RectF(thumbX, togY + 2.0f, 18.0f, 18.0f), 9);
    SolidBrush togWhite(ClrWhite); g.FillPath(&togWhite, togThumb); delete togThumb;

    g.DrawString(L"Kill distraction apps automatically when blocking is active.", -1, &fSmall, RectF(lx + 18.0f, row3Y + 56.0f, cardW - 36.0f, 34.0f), &fL, &bGray);

    // App list (first 4)
    float appItemY = row3Y + 96.0f;
    for (int i = 0; i < min(4, (int)blockedApps.size()); ++i) {
        bool hov = (hovAppItem == i);
        SolidBrush iBg(hov ? ClrPurpleLight : ClrBg);
        GraphicsPath* ip = GetRoundRectPath(RectF(lx + 18.0f, appItemY, cardW - 36.0f, 30.0f), 4);
        g.FillPath(&iBg, ip); delete ip;
        SolidBrush iText(ClrDark);
        g.DrawString(L"\xE71D", -1, new Font(&ffIcons, 12, FontStyleRegular, UnitPixel), RectF(lx + 24.0f, appItemY, 20.0f, 30.0f), &fL, &bPurple);
        g.DrawString(blockedApps[i].c_str(), -1, &fSmall, RectF(lx + 48.0f, appItemY, cardW - 100.0f, 30.0f), &fL, &iText);
        appItemY += 34.0f;
    }
    if (blockedApps.size() > 4) {
        wchar_t moreBuf[20];
        swprintf_s(moreBuf, L"+%zu more apps", blockedApps.size() - 4);
        g.DrawString(moreBuf, -1, &fSmall, RectF(lx + 18.0f, appItemY, cardW - 36.0f, 22.0f), &fL, &bGray);
    }

    // ============================================================
    // ROW 3 RIGHT: BREAK REMINDER
    // ============================================================
    DrawCard(g, rx, row3Y, cardW, cardH, ClrTeal);
    g.DrawString(L"\xE91B", -1, &fIconBig, RectF(rx + 18.0f, row3Y + 18.0f, 36.0f, 36.0f), &fL, &bTeal);
    g.DrawString(L"Break Reminder", -1, &fCardTitle, RectF(rx + 58.0f, row3Y + 18.0f, cardW - 150.0f, 30.0f), &fL, &bDark);
    // Toggle switch
    float bTogX = rx + cardW - 70.0f, bTogY = row3Y + 24.0f;
    GraphicsPath* bTogTrack = GetRoundRectPath(RectF(bTogX, bTogY, 50.0f, 22.0f), 11);
    SolidBrush bTogBg(isBreakReminderEnabled ? ClrTeal : ClrBorder);
    g.FillPath(&bTogBg, bTogTrack); delete bTogTrack;
    float bThumbX = isBreakReminderEnabled ? (bTogX + 30.0f) : (bTogX + 2.0f);
    GraphicsPath* bTogThumb = GetRoundRectPath(RectF(bThumbX, bTogY + 2.0f, 18.0f, 18.0f), 9);
    g.FillPath(&togWhite, bTogThumb); delete bTogThumb;

    g.DrawString(L"Get notified to take eye/stretch breaks at regular intervals.", -1, &fSmall, RectF(rx + 18.0f, row3Y + 56.0f, cardW - 36.0f, 34.0f), &fL, &bGray);

    g.DrawString(L"Remind every:", -1, &fBold, RectF(rx + 18.0f, row3Y + 98.0f, 100.0f, 20.0f), &fL, &bDark);
    RectF breakIntDropRect(rx + 18.0f, row3Y + 122.0f, cardW - 36.0f, 34.0f);
    DrawDropdown(g, ff, breakIntDropRect, breakIntervals[breakIntervalIdx], isBreakIntDropOpen, hovBreakIntDrop, !isBreakReminderEnabled);

    // Next reminder countdown
    if (isBreakReminderEnabled && lastBreakTick > 0) {
        int intervals[] = { 20*60, 30*60, 45*60, 60*60 };
        DWORD elapsed = (GetTickCount() - lastBreakTick) / 1000;
        DWORD intvl = intervals[breakIntervalIdx];
        DWORD nextIn = (elapsed < (DWORD)intvl) ? (intvl - elapsed) : 0;
        wchar_t nextBuf[50];
        swprintf_s(nextBuf, L"\xE916  Next break in: %ls", FormatCountdown(nextIn).c_str());
        SolidBrush bTealD(ClrTeal);
        g.DrawString(nextBuf, -1, &fSmall, RectF(rx + 18.0f, row3Y + 170.0f, cardW - 36.0f, 24.0f), &fL, &bTealD);
    }

    g.DrawString(L"\xE7F4  20-20-20 Rule: Every 20 min, look at something 20ft away for 20s.", -1, &fSmall, RectF(rx + 18.0f, row3Y + 200.0f, cardW - 36.0f, 34.0f), &fL, &bGray);

    // ============================================================
    // Z-INDEX: Draw all open dropdowns on top
    // ============================================================
    if (isFastingDropOpen && !isFastingActive)
        DrawDropList(g, ff, fDropRect, fastingModes, hovFastingOpt);
    if (isPowerActionDropOpen)
        DrawDropList(g, ff, pActDropRect, powerActions, hovPowerActionOpt);
    if (isPowerTimerDropOpen)
        DrawDropList(g, ff, pTimDropRect, powerTimers, hovPowerTimerOpt);
    if (isFocusDurDropOpen && !isFocusSessionActive)
        DrawDropList(g, ff, focDurDropRect, focusDurations, hovFocusDurOpt);
    if (isDailyLimDropOpen && !isDailyLimitEnabled)
        DrawDropList(g, ff, dailyLimDropRect, dailyLimits, hovDailyLimOpt);
    if (isBreakIntDropOpen && !isBreakReminderEnabled)
        DrawDropList(g, ff, breakIntDropRect, breakIntervals, hovBreakIntOpt);
}

// ──────────────────────────────────────────────
// MOUSE MOVE
// ──────────────────────────────────────────────
void ProcessDeviceBlockMouseMove(float x, float y) {
    float bx = s_cx + 30.0f, by = s_cy + 30.0f, bw = s_cw - 60.0f;
    float cardW = (bw - 20.0f) / 2.0f;
    float cardY = by + 65.0f;
    float lx = bx, rx = bx + cardW + 20.0f;
    float row2Y = cardY + 290.0f, row3Y = row2Y + 290.0f;
    float halfDropW = (cardW - 52.0f) / 2.0f;

    // Reset all hover
    hovFastingDrop = hovFastingBtn = hovUnlockBtn = false;
    hovFastingOpt = -1;
    hovPowerActionDrop = hovPowerTimerDrop = hovPowerBtn = false;
    hovPowerActionOpt = hovPowerTimerOpt = -1;
    hovFocusDurDrop = hovFocusBtn = false; hovFocusDurOpt = -1;
    hovDailyLimDrop = hovDailyLimBtn = false; hovDailyLimOpt = -1;
    hovAppBlockerToggle = hovAddAppBtn = false; hovAppItem = -1;
    hovBreakReminderToggle = hovBreakIntDrop = false; hovBreakIntOpt = -1;

    // ── Open dropdown list hit-testing first (Z-priority) ──
    auto testDropList = [&](RectF dropRect, const vector<wstring>& items, int& hovIdx) -> bool {
        float listY = dropRect.Y + dropRect.Height + 2.0f;
        RectF listRect(dropRect.X, listY, dropRect.Width, items.size() * 36.0f);
        if (listRect.Contains(x, y)) {
            float itemY = listY;
            for (size_t i = 0; i < items.size(); ++i) {
                if (RectF(listRect.X, itemY, listRect.Width, 36.0f).Contains(x, y)) { hovIdx = (int)i; return true; }
                itemY += 36.0f;
            }
            return true; // still inside list, consume event
        }
        return false;
    };

    RectF fDropRect(lx + 18.0f, cardY + 122.0f, cardW - 36.0f, 34.0f);
    RectF pActDropRect(rx + 18.0f, cardY + 122.0f, halfDropW, 34.0f);
    RectF pTimDropRect(rx + 28.0f + halfDropW, cardY + 122.0f, halfDropW, 34.0f);
    RectF focDurDropRect(lx + 18.0f, row2Y + 122.0f, cardW - 36.0f, 34.0f);
    RectF dailyLimDropRect(rx + 18.0f, row2Y + 122.0f, cardW - 36.0f, 34.0f);
    RectF breakIntDropRect(rx + 18.0f, row3Y + 122.0f, cardW - 36.0f, 34.0f);

    if (isFastingDropOpen    && !isFastingActive        && testDropList(fDropRect,        fastingModes,   hovFastingOpt))   return;
    if (isPowerActionDropOpen                            && testDropList(pActDropRect,     powerActions,   hovPowerActionOpt)) return;
    if (isPowerTimerDropOpen                             && testDropList(pTimDropRect,     powerTimers,    hovPowerTimerOpt))  return;
    if (isFocusDurDropOpen   && !isFocusSessionActive   && testDropList(focDurDropRect,   focusDurations, hovFocusDurOpt))   return;
    if (isDailyLimDropOpen   && !isDailyLimitEnabled    && testDropList(dailyLimDropRect, dailyLimits,    hovDailyLimOpt))   return;
    if (isBreakIntDropOpen   && !isBreakReminderEnabled && testDropList(breakIntDropRect, breakIntervals, hovBreakIntOpt))   return;

    // ── Base hovers ──
    // Card 1 – Fasting
    if (!isFastingActive) {
        hovFastingDrop = fDropRect.Contains(x, y);
    } else {
        float unlW = (cardW - 46.0f) / 2.0f - 5.0f;
        hovUnlockBtn = RectF(lx + 18.0f, cardY + 198.0f, unlW, 36.0f).Contains(x, y);
        hovFastingBtn = RectF(lx + 18.0f + unlW + 10.0f, cardY + 198.0f, unlW, 36.0f).Contains(x, y);
    }
    if (!isFastingActive) hovFastingBtn = RectF(lx + 18.0f, cardY + 210.0f, cardW - 36.0f, 38.0f).Contains(x, y);

    // Card 2 – Power
    hovPowerActionDrop = pActDropRect.Contains(x, y);
    hovPowerTimerDrop  = pTimDropRect.Contains(x, y);
    hovPowerBtn = RectF(rx + 18.0f, cardY + 210.0f, cardW - 36.0f, 38.0f).Contains(x, y);

    // Card 3 – Focus Session
    if (!isFocusSessionActive) hovFocusDurDrop = focDurDropRect.Contains(x, y);
    RectF focBtnRect = isFocusSessionActive ? RectF(lx + 18.0f, row2Y + 210.0f, cardW - 100.0f, 38.0f) : RectF(lx + 18.0f, row2Y + 210.0f, cardW - 36.0f, 38.0f);
    hovFocusBtn = focBtnRect.Contains(x, y);

    // Card 4 – Daily Limit
    if (!isDailyLimitEnabled) hovDailyLimDrop = dailyLimDropRect.Contains(x, y);
    hovDailyLimBtn = RectF(rx + 18.0f, row2Y + 210.0f, cardW - 36.0f, 38.0f).Contains(x, y);

    // Card 5 – App Blocker toggle
    hovAppBlockerToggle = RectF(lx + cardW - 70.0f, row3Y + 24.0f, 50.0f, 22.0f).Contains(x, y);
    float appItemY = row3Y + 96.0f;
    for (int i = 0; i < min(4, (int)blockedApps.size()); ++i) {
        if (RectF(lx + 18.0f, appItemY, cardW - 36.0f, 30.0f).Contains(x, y)) { hovAppItem = i; break; }
        appItemY += 34.0f;
    }

    // Card 6 – Break Reminder toggle + dropdown
    hovBreakReminderToggle = RectF(rx + cardW - 70.0f, row3Y + 24.0f, 50.0f, 22.0f).Contains(x, y);
    if (!isBreakReminderEnabled) hovBreakIntDrop = breakIntDropRect.Contains(x, y);
}

// ──────────────────────────────────────────────
// MOUSE CLICK
// ──────────────────────────────────────────────
void ProcessDeviceBlockMouseClick(float x, float y) {
    if (!g_isPremiumUser) { g_showUpgradePopup = true; return; }

    float bx = s_cx + 30.0f, by = s_cy + 30.0f, bw = s_cw - 60.0f;
    float cardW = (bw - 20.0f) / 2.0f;
    float cardY = by + 65.0f;
    float lx = bx, rx = bx + cardW + 20.0f;
    float row2Y = cardY + 290.0f, row3Y = row2Y + 290.0f;
    float halfDropW = (cardW - 52.0f) / 2.0f;

    RectF fDropRect(lx + 18.0f, cardY + 122.0f, cardW - 36.0f, 34.0f);
    RectF pActDropRect(rx + 18.0f, cardY + 122.0f, halfDropW, 34.0f);
    RectF pTimDropRect(rx + 28.0f + halfDropW, cardY + 122.0f, halfDropW, 34.0f);
    RectF focDurDropRect(lx + 18.0f, row2Y + 122.0f, cardW - 36.0f, 34.0f);
    RectF dailyLimDropRect(rx + 18.0f, row2Y + 122.0f, cardW - 36.0f, 34.0f);
    RectF breakIntDropRect(rx + 18.0f, row3Y + 122.0f, cardW - 36.0f, 34.0f);

    // ── 1. Consume open dropdown selections ──
    auto consumeDrop = [&](bool& isOpen, RectF dropRect, const vector<wstring>& items, int& selIdx, int hovIdx) -> bool {
        if (!isOpen) return false;
        if (hovIdx != -1) selIdx = hovIdx;
        isOpen = false; return true;
    };
    if (consumeDrop(isFastingDropOpen,    fDropRect,        fastingModes,   fastingModeIdx,   hovFastingOpt))   return;
    if (consumeDrop(isPowerActionDropOpen, pActDropRect,    powerActions,   powerActionIdx,   hovPowerActionOpt)) return;
    if (consumeDrop(isPowerTimerDropOpen,  pTimDropRect,    powerTimers,    powerTimerIdx,    hovPowerTimerOpt))  return;
    if (consumeDrop(isFocusDurDropOpen,   focDurDropRect,  focusDurations, focusDurationIdx, hovFocusDurOpt))   return;
    if (consumeDrop(isDailyLimDropOpen,   dailyLimDropRect, dailyLimits,   dailyLimitIdx,    hovDailyLimOpt))   return;
    if (consumeDrop(isBreakIntDropOpen,   breakIntDropRect, breakIntervals, breakIntervalIdx, hovBreakIntOpt))   return;

    // ── 2. Open dropdowns ──
    if (hovFastingDrop   && !isFastingActive)       { isFastingDropOpen    = true; return; }
    if (hovPowerActionDrop)                          { isPowerActionDropOpen = true; return; }
    if (hovPowerTimerDrop)                           { isPowerTimerDropOpen  = true; return; }
    if (hovFocusDurDrop  && !isFocusSessionActive)  { isFocusDurDropOpen   = true; return; }
    if (hovDailyLimDrop  && !isDailyLimitEnabled)   { isDailyLimDropOpen   = true; return; }
    if (hovBreakIntDrop  && !isBreakReminderEnabled){ isBreakIntDropOpen   = true; return; }

    // ── 3. CARD 1: Internet Fasting buttons ──
    if (hovFastingBtn) {
        if (isFastingActive) {
            // Force stop (no PIN check here — that's hovUnlockBtn)
            isFastingActive = false;
            system("netsh interface set interface \"Wi-Fi\" enabled");
            system("netsh interface set interface \"Ethernet\" enabled");
            MessageBoxA(NULL, "Internet Connection Restored.\nStay focused!", "Fasting Stopped", MB_OK | MB_ICONINFORMATION);
        } else {
            isFastingActive = true;
            fastingStartTick = GetTickCount();
            // Disable internet (requires admin)
            system("netsh interface set interface \"Wi-Fi\" disabled");
            system("netsh interface set interface \"Ethernet\" disabled");
            MessageBoxA(NULL, "Internet Fasting Started!\nAll network connections have been disabled.", "Focus Mode", MB_OK | MB_ICONWARNING);
        }
        return;
    }

    if (hovUnlockBtn && isFastingActive) {
        // Simple 4-digit PIN dialog (placeholder — in production: custom GDI+ PIN keypad)
        char pinBuf[8] = {};
        // For now use InputBox-style: real implementation would use custom GDI+ numpad overlay
        if (wcslen(emergencyPin) == 0) {
            // No PIN set: just stop
            isFastingActive = false;
            system("netsh interface set interface \"Wi-Fi\" enabled");
            system("netsh interface set interface \"Ethernet\" enabled");
        } else {
            MessageBoxA(NULL, "Enter your 4-digit emergency PIN to stop fasting.\n(Implement custom GDI+ PIN dialog here)", "PIN Required", MB_OK | MB_ICONQUESTION);
        }
        return;
    }

    // ── 4. CARD 2: Power Management ──
    if (hovPowerBtn) {
        if (powerActionIdx == 0) {
            // Lock PC
            if (powerTimerIdx == 0) LockWorkStation();
            else if (powerTimerIdx == 1) { system("shutdown /l /t 900"); }
            else { system("shutdown /l /t 3600"); }
        } else if (powerActionIdx == 1) {
            // Sleep
            SetSuspendState(FALSE, FALSE, FALSE);
        } else if (powerActionIdx == 2) {
            // Shutdown
            const char* cmds[] = { "shutdown /s /t 0", "shutdown /s /t 900", "shutdown /s /t 3600" };
            system(cmds[powerTimerIdx]);
        }
        return;
    }

    // Family Link override
    if (g_parentLockAllTabs) { LockWorkStation(); return; }

    // ── 5. CARD 3: Focus Session ──
    if (hovFocusBtn) {
        if (isFocusSessionActive) {
            isFocusSessionActive = false;
            if (focusDisabledInternet) {
                system("netsh interface set interface \"Wi-Fi\" enabled");
                system("netsh interface set interface \"Ethernet\" enabled");
                focusDisabledInternet = false;
            }
            MessageBoxA(NULL, "Focus session ended. Good work!\nInternet has been restored.", "Session Complete", MB_OK | MB_ICONINFORMATION);
        } else {
            isFocusSessionActive = true;
            focusStartTick = GetTickCount();
            focusDisabledInternet = true;
            system("netsh interface set interface \"Wi-Fi\" disabled");
            system("netsh interface set interface \"Ethernet\" disabled");
            int mins[] = { 25, 45, 60, 90 };
            char buf[80];
            sprintf_s(buf, "Focus Session started for %d minutes.\nInternet has been disabled for deep work.", mins[focusDurationIdx]);
            MessageBoxA(NULL, buf, "Focus Session Active", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    // ── 6. CARD 4: Daily Usage Limit ──
    if (hovDailyLimBtn) {
        if (isDailyLimitEnabled) {
            isDailyLimitEnabled = false;
            // Accumulate used seconds
            if (dailySessionStart > 0) {
                dailyUsedSeconds += (GetTickCount() - dailySessionStart) / 1000;
                dailySessionStart = 0;
            }
            system("netsh interface set interface \"Wi-Fi\" enabled");
            system("netsh interface set interface \"Ethernet\" enabled");
            MessageBoxA(NULL, "Daily usage limit disabled.", "Limit Off", MB_OK | MB_ICONINFORMATION);
        } else {
            isDailyLimitEnabled = true;
            dailySessionStart = GetTickCount();
            MessageBoxA(NULL, "Daily usage limit enabled.\nInternet will auto-disconnect when your quota runs out.", "Limit Active", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    // ── 7. CARD 5: App Blocker toggle ──
    if (hovAppBlockerToggle) {
        isAppBlockerEnabled = !isAppBlockerEnabled;
        if (isAppBlockerEnabled) {
            for (auto& app : blockedApps) KillProcessByName(app);
            MessageBoxA(NULL, "App Blocker enabled.\nDistraction apps have been terminated.", "App Blocker", MB_OK | MB_ICONWARNING);
        } else {
            MessageBoxA(NULL, "App Blocker disabled.", "App Blocker", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    // ── 8. CARD 6: Break Reminder toggle ──
    if (hovBreakReminderToggle) {
        isBreakReminderEnabled = !isBreakReminderEnabled;
        if (isBreakReminderEnabled) {
            lastBreakTick = GetTickCount();
            int mins[] = { 20, 30, 45, 60 };
            char buf[80];
            sprintf_s(buf, "Break reminders enabled.\nYou will be reminded every %d minutes.", mins[breakIntervalIdx]);
            MessageBoxA(NULL, buf, "Break Reminder", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }
}

// ──────────────────────────────────────────────
// TIMER TICK — call this every second from WM_TIMER
// ──────────────────────────────────────────────
void ProcessDeviceBlockTick() {
    // Check Focus Session expiry
    if (isFocusSessionActive) {
        int durations[] = { 25*60, 45*60, 60*60, 90*60 };
        DWORD elapsed = (GetTickCount() - focusStartTick) / 1000;
        if (elapsed >= (DWORD)durations[focusDurationIdx]) {
            isFocusSessionActive = false;
            if (focusDisabledInternet) {
                system("netsh interface set interface \"Wi-Fi\" enabled");
                system("netsh interface set interface \"Ethernet\" enabled");
                focusDisabledInternet = false;
            }
            MessageBoxA(NULL, "Focus session complete!\nGreat work! Internet has been restored.\nTime for a break.", "Session Done", MB_OK | MB_ICONINFORMATION);
        }
    }

    // Check Internet Fasting expiry (timed modes)
    if (isFastingActive && fastingModeIdx > 0) {
        DWORD totalSec = (fastingModeIdx == 1) ? 3600 : 7200;
        DWORD elapsed = (GetTickCount() - fastingStartTick) / 1000;
        if (elapsed >= totalSec) {
            isFastingActive = false;
            system("netsh interface set interface \"Wi-Fi\" enabled");
            system("netsh interface set interface \"Ethernet\" enabled");
            MessageBoxA(NULL, "Internet Fasting period ended.\nNetwork connections restored.", "Fasting Complete", MB_OK | MB_ICONINFORMATION);
        }
    }

    // Check Daily Usage Limit
    if (isDailyLimitEnabled && dailySessionStart > 0) {
        int limSeconds[] = { 3600, 7200, 10800, 14400, 21600 };
        DWORD totalUsed = dailyUsedSeconds + (GetTickCount() - dailySessionStart) / 1000;
        if (totalUsed >= (DWORD)limSeconds[dailyLimitIdx]) {
            isDailyLimitEnabled = false;
            dailySessionStart = 0;
            system("netsh interface set interface \"Wi-Fi\" disabled");
            system("netsh interface set interface \"Ethernet\" disabled");
            MessageBoxA(NULL, "Daily internet quota reached!\nInternet has been disabled until midnight.", "Quota Exhausted", MB_OK | MB_ICONWARNING);
        }
        // Kill app blocker apps if enabled
        if (isAppBlockerEnabled) {
            for (auto& app : blockedApps) KillProcessByName(app);
        }
    }

    // Check Break Reminder
    if (isBreakReminderEnabled && lastBreakTick > 0) {
        int intervals[] = { 20*60, 30*60, 45*60, 60*60 };
        DWORD elapsed = (GetTickCount() - lastBreakTick) / 1000;
        if (elapsed >= (DWORD)intervals[breakIntervalIdx]) {
            lastBreakTick = GetTickCount();
            MessageBoxA(NULL,
                "Time for a break!\n\n20-20-20 Rule:\nLook at something 20 feet away for 20 seconds.\nStretch your neck and shoulders.",
                "Break Reminder", MB_OK | MB_ICONINFORMATION);
        }
    }
}

// ─── Aliases ───────────────────────────────────────────────────────────────
void ProcessDeviceBlockKeyPress(wchar_t /*c*/)                 { /* reserved */ }
void ProcessDeviceBlockKeyDown(WPARAM /*key*/)                 { /* reserved */ }
void ProcessDeviceBlockMouseWheel(float x, float y, int /*d*/) { ProcessDeviceBlockMouseMove(x, y); }
