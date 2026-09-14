
// tab_dashboard.cpp

#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)

#include "tab_dashboard.h"
#include "browser/mini_browser.h"
#include "tab_blocks.h"
#include "tab_adult.h"
#include "tab_schedule_blocks.h"
#include <string>
#include <vector>
#include <ctime>
#include <tlhelp32.h>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;
extern void LaunchFoxitStylePdfReader(std::wstring pdfPath);

static bool showKillPrompt = false;
static wstring killInput = L"";
static int hoverNumBtn = -1;
static bool dash_hovKillBtn = false;
static float d_cX = 0.0f, d_cY = 0.0f, d_cW = 0.0f, d_cH = 0.0f;

struct ActionCard {
    wstring title;
    wstring subtitle;
    RectF bounds;
    bool isHovered;
};
static ActionCard s_actionCards[4];
static bool s_actionCardsInit = false;

static int selectedDashTab = 0;
static int hoveredDashTab = -1;
static RectF s_tabRects[5];
static float s_gridScrollY = 0.0f;   // right panel scroll offset
static float s_gridMaxScroll = 0.0f; // maximum scroll (set during draw)
static RectF  s_gridClipRect;         // right panel clip area (set during draw)

struct DashBtn {
    wstring title;
    wstring subtext;
    wstring icon;
    RectF bounds;
    bool isHovered;
};

struct DashSec {
    wstring title;
    vector<DashBtn> btns;
};

static vector<DashSec> s_sections;
static bool s_init = false;

static void KillDebugAppsNow() {
    static const wchar_t* kDebugApps[] = {
        L"taskmgr.exe", L"resmon.exe", L"perfmon.exe",
        L"procexp.exe", L"procexp64.exe", L"procmon.exe",
        L"processhacker.exe", L"wireshark.exe", L"fiddler.exe"
    };

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            bool matched = false;
            for (auto* app : kDebugApps) {
                if (_wcsicmp(pe.szExeFile, app) == 0) { matched = true; break; }
            }
            if (matched) {
                HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (ph) { TerminateProcess(ph, 1); CloseHandle(ph); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void InitDashboardData() {
    if (s_init) return;

    DashSec sec1 = { L"Quick Blocks" };
    sec1.btns.push_back({ L"Rest Button", L"Take a break", L"\xE7E8", RectF(), false });
    sec1.btns.push_back({ L"Internet Block", L"Disable all traffic", L"\xEB55", RectF(), false });
    sec1.btns.push_back({ L"Uninstall Block", L"Secure settings", L"\xE25B", RectF(), false });
    sec1.btns.push_back({ L"Debug Kill Apps", L"Close task/debug tools", L"\xE7BA", RectF(), false });
    sec1.btns.push_back({ L"Ads Block", L"Remove distractions", L"\xE711", RectF(), false });
    sec1.btns.push_back({ L"Adult Block", L"Filter content", L"\xE72E", RectF(), false });
    sec1.btns.push_back({ L"YT Shorts Block", L"Block short videos", L"\xE8D6", RectF(), false });
    sec1.btns.push_back({ L"FB Reels Block", L"Block social reels", L"\xE8D6", RectF(), false });
    s_sections.push_back(sec1);

    DashSec sec2 = { L"Web & Cloud Workspace" };
    sec2.btns.push_back({ L"RasBrowser", L"Secure surfing", L"\xE774", RectF(), false });
    sec2.btns.push_back({ L"Gemini", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"ChatGPT", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"DeepSeek", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Grok", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Perplexity", L"AI Search", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"MATLAB", L"Math workspace", L"\xE9A1", RectF(), false });
    sec2.btns.push_back({ L"YouTube", L"Video platform", L"\xE714", RectF(), false });
    sec2.btns.push_back({ L"Facebook", L"Social network", L"\xE774", RectF(), false });
    sec2.btns.push_back({ L"Google Colab", L"Code notebook", L"\xE753", RectF(), false });
    sec2.btns.push_back({ L"OneDrive", L"Cloud storage", L"\xE8AD", RectF(), false });
    sec2.btns.push_back({ L"Gmail", L"Email client", L"\xE715", RectF(), false });
    sec2.btns.push_back({ L"Google Docs", L"Word processor", L"\xE8A5", RectF(), false });
    sec2.btns.push_back({ L"Google Slides", L"Presentations", L"\xE8B3", RectF(), false });
    sec2.btns.push_back({ L"Google Sheets", L"Spreadsheets", L"\xE82D", RectF(), false });
    s_sections.push_back(sec2);

    DashSec sec3 = { L"Pro Tools & Viewers" };
    sec3.btns.push_back({ L"PDF Reader", L"View documents", L"\xEA90", RectF(), false });
    sec3.btns.push_back({ L"Photo Viewer", L"View images", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Docs Viewer", L"Read text files", L"\xE8A5", RectF(), false });
    sec3.btns.push_back({ L"PDF Merge", L"Combine PDFs", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF Split", L"Extract pages", L"\xE8B6", RectF(), false });
    sec3.btns.push_back({ L"Image to PDF", L"Convert pictures", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF to Image", L"Extract pictures", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Compress PDF", L"Reduce file size", L"\xE7B8", RectF(), false });
    sec3.btns.push_back({ L"Job Photo", L"Resize 300x300", L"\xE7C5", RectF(), false });
    sec3.btns.push_back({ L"Job Signature", L"Resize signature", L"\xE73A", RectF(), false });
    sec3.btns.push_back({ L"Age Calculator", L"Calculate exact age", L"\xE787", RectF(), false });
    sec3.btns.push_back({ L"Graphic Calc", L"Plot graphs", L"\xE1D0", RectF(), false });
    sec3.btns.push_back({ L"Scientific Calc", L"Advanced math", L"\xE1D0", RectF(), false });
    s_sections.push_back(sec3);

    DashSec sec4 = { L"Personal & Notes" };
    sec4.btns.push_back({ L"Personal Diary", L"Private journal", L"\xE82D", RectF(), false });
    sec4.btns.push_back({ L"Instant Note", L"Quick jot down", L"\xE70B", RectF(), false });
    s_sections.push_back(sec4);

    DashSec sec5 = { L"Student Corner" };
    sec5.btns.push_back({ L"Study Materials", L"Vault & resources", L"\xE838", RectF(), false });
    sec5.btns.push_back({ L"CGPA Calc", L"Grade calculator", L"\xE1D0", RectF(), false });
    sec5.btns.push_back({ L"Exam Routine", L"Schedule tracker", L"\xE787", RectF(), false });
    s_sections.push_back(sec5);

    s_init = true;

    if (!s_actionCardsInit) {
        s_actionCards[0] = { L"Create Blocking Profile", L"Set up Schedule, Simple & Emergency blocks", RectF(), false };
        s_actionCards[1] = { L"Advanced Adult Block",    L"Block adult sites & filter content",          RectF(), false };
        s_actionCards[2] = { L"Start Deep Study",        L"Launch a Pomodoro focus session now",         RectF(), false };
        s_actionCards[3] = { L"Allow Only Websites & Apps", L"Whitelist specific sites and applications",RectF(), false };
        s_actionCardsInit = true;
    }
}

static void AddRoundedRectPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    if (d > w) d = w; if (d > h) d = h;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static wstring GetGreeting() {
    time_t t = time(0);
    struct tm* now = localtime(&t);
    if (now->tm_hour < 12) return L"Good Morning, Rasel";
    if (now->tm_hour < 18) return L"Good Afternoon, Rasel";
    return L"Good Evening, Rasel";
}

void DrawDashboardTab(Graphics& g, float cx, float cy, float cw, float ch) {
    d_cX = cx; d_cY = cy; d_cW = cw; d_cH = ch;
    InitDashboardData();

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 20, FontStyleBold, UnitPixel);
    Font fSub(&ff, 11, FontStyleRegular, UnitPixel);
    Font fTabTxt(&ff, 12, FontStyleBold, UnitPixel);
    Font fBtnTitle(&ff, 12, FontStyleBold, UnitPixel);
    Font fBtnSub(&ff, 10, FontStyleRegular, UnitPixel);
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIconBig(&ffIc, 18, FontStyleRegular, UnitPixel);

    Color clrPageBg(255, 255, 255, 255);
    Color clrSideBg(255, 248, 250, 252);
    Color clrDivider(255, 226, 232, 240);
    Color clrCardBg(255, 248, 250, 252);
    Color clrCardHovBg(255, 240, 244, 248);
    Color clrIconBg(255, 237, 242, 247);
    Color clrIconHovBg(255, 224, 246, 243);
    Color clrTabSelBg(255, 209, 245, 240);
    Color clrTabHovBg(255, 237, 242, 247);

    Color clrTextPrimary(255, 30, 41, 59);
    Color clrTextSecondary(255, 71, 85, 105);
    Color clrTextMuted(255, 148, 163, 184);
    Color clrTeal(255, 8, 145, 130);

    SolidBrush brTeal(clrTeal);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtTL; fmtTL.SetAlignment(StringAlignmentNear); fmtTL.SetLineAlignment(StringAlignmentNear);

    float leftColWidth = 260.0f;
    float rightColX = cx + leftColWidth;
    float rightColWidth = cw - leftColWidth;

    g.FillRectangle(&SolidBrush(clrSideBg), cx, cy, leftColWidth, ch);
    g.FillRectangle(&SolidBrush(clrPageBg), rightColX, cy, rightColWidth, ch);

    Pen penDivider(clrDivider, 1.0f);
    g.DrawLine(&penDivider, rightColX, cy, rightColX, cy + ch);

    float marginX = cx + 16.0f;
    float currentY = cy + 20.0f;
    float usableLeftW = leftColWidth - 32.0f;

    wstring greeting = GetGreeting();
    g.DrawString(greeting.c_str(), -1, &fH1, RectF(marginX, currentY, usableLeftW, 28.0f), &fmtL, &SolidBrush(clrTextPrimary));
    currentY += 28.0f;
    g.DrawString(L"Manage workflow & boost productivity", -1, &fSub, RectF(marginX, currentY, usableLeftW, 18.0f), &fmtTL, &SolidBrush(Color(255, 148, 163, 184)));
    currentY += 24.0f;

    // Action Cards
    float acH = 62.0f; float acGapY = 8.0f;
    Color acThemes[4][2] = {{Color(255, 13, 140, 122), Color(255, 11, 191, 168)}, {Color(255, 107, 47, 212), Color(255, 145, 85, 245)}, {Color(255, 196, 122, 10), Color(255, 240, 162, 32)}, {Color(255, 26, 85, 200), Color(255, 61, 130, 245)}};
    wstring acIcons[4] = { L"\xE72E", L"\xE8D7", L"\xE728", L"\xE774" };

    for (int i = 0; i < 4; i++) {
        s_actionCards[i].bounds = RectF(marginX, currentY, usableLeftW, acH);
        GraphicsPath acPath; AddRoundedRectPath(acPath, marginX, currentY, usableLeftW, acH, 10.0f);
        g.FillPath(&SolidBrush(s_actionCards[i].isHovered ? acThemes[i][1] : acThemes[i][0]), &acPath);
        g.DrawString(acIcons[i].c_str(), -1, &fIconBig, RectF(marginX + 10.0f, currentY + 15.0f, 32.0f, 32.0f), &fmtC, &SolidBrush(Color(255, 255, 255, 255)));
        g.DrawString(s_actionCards[i].title.c_str(), -1, &fBtnTitle, RectF(marginX + 50.0f, currentY + 12.0f, usableLeftW - 60.0f, 18.0f), &fmtTL, &SolidBrush(Color(255, 255, 255, 255)));
        g.DrawString(s_actionCards[i].subtitle.c_str(), -1, &fBtnSub, RectF(marginX + 50.0f, currentY + 32.0f, usableLeftW - 60.0f, 16.0f), &fmtTL, &SolidBrush(Color(200, 255, 255, 255)));
        currentY += acH + acGapY;
    }

    currentY += 10.0f;
    g.DrawString(L"QUICK ACCESS", -1, &Font(&ff, 9, FontStyleBold, UnitPixel), RectF(marginX + 8.0f, currentY, usableLeftW, 14.0f), &fmtL, &SolidBrush(Color(255, 58, 64, 96)));
    currentY += 18.0f;

    wstring subNames[] = { L"Quick Blocks", L"Web & Cloud", L"Pro Tools", L"Personal Notes", L"Student Corner" };
    float tabH = 36.0f;
    for (int i = 0; i < 5; i++) {
        s_tabRects[i] = RectF(marginX, currentY, usableLeftW, tabH);
        if (selectedDashTab == i) {
            GraphicsPath tPath; AddRoundedRectPath(tPath, marginX, currentY, usableLeftW, tabH, 8.0f);
            g.FillPath(&SolidBrush(clrTabSelBg), &tPath);
            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, RectF(marginX + 18.0f, currentY, usableLeftW - 18.0f, tabH), &fmtL, &brTeal);
        } else {
            if (hoveredDashTab == i) {
                GraphicsPath tPath; AddRoundedRectPath(tPath, marginX, currentY, usableLeftW, tabH, 8.0f);
                g.FillPath(&SolidBrush(clrTabHovBg), &tPath);
            }
            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, RectF(marginX + 18.0f, currentY, usableLeftW - 18.0f, tabH), &fmtL, &SolidBrush(hoveredDashTab == i ? Color(255, 51, 65, 85) : Color(255, 100, 116, 139)));
        }
        currentY += tabH + 4.0f;
    }

    // ─── HIGH-END PRO UPGRADE CARD ──────────────────────────────────────
    float promoH = 105.0f; float promoY = cy + ch - promoH - 16.0f;
    RectF promoRect(marginX, promoY, usableLeftW, promoH);
    GraphicsPath promoPath; AddRoundedRectPath(promoPath, promoRect.X, promoRect.Y, promoRect.Width, promoRect.Height, 14.0f);
    LinearGradientBrush promoBr(promoRect, Color(255, 15, 23, 42), Color(255, 30, 41, 59), LinearGradientModeForwardDiagonal);
    g.FillPath(&promoBr, &promoPath);
    g.DrawPath(&Pen(Color(40, 255, 255, 255), 1.0f), &promoPath);
    g.DrawString(L"\xEB67", -1, &Font(&ffIc, 16, FontStyleRegular, UnitPixel), RectF(marginX + 14.0f, promoY + 16.0f, 20.0f, 20.0f), &fmtL, &SolidBrush(Color(255, 250, 204, 21)));
    g.DrawString(L"PRO FEATURES ACTIVE", -1, &Font(&ff, 12, FontStyleBold, UnitPixel), RectF(marginX + 40.0f, promoY + 16.0f, usableLeftW - 50.0f, 18.0f), &fmtL, &SolidBrush(Color(255, 255, 255, 255)));
    g.DrawString(L"Unlock Advanced Analytics & AI", -1, &Font(&ff, 10, FontStyleRegular, UnitPixel), RectF(marginX + 14.0f, promoY + 38.0f, usableLeftW - 28.0f, 16.0f), &fmtL, &SolidBrush(Color(200, 255, 255, 255)));

    float pillW = 100.0f; float pillH = 26.0f; RectF pillRect(marginX + 14.0f, promoY + 65.0f, pillW, pillH);
    GraphicsPath pillPath; AddRoundedRectPath(pillPath, pillRect.X, pillRect.Y, pillRect.Width, pillRect.Height, 13.0f);
    g.FillPath(&SolidBrush(Color(255, 250, 204, 21)), &pillPath);
    g.DrawString(L"Manage Pro", -1, &Font(&ff, 10, FontStyleBold, UnitPixel), pillRect, &fmtC, &SolidBrush(Color(255, 15, 23, 42)));

    // ─── RIGHT PANEL GRID (clipped + scrollable) ──────────────
    float gridX = rightColX + 22.0f; float gridY = cy + 20.0f; float gridW = rightColWidth - 44.0f;
    // Title outside clip — always visible
    g.DrawString(s_sections[selectedDashTab].title.c_str(), -1, &fH1, RectF(gridX, gridY, gridW, 28.0f), &fmtL, &SolidBrush(clrTextPrimary));
    float gridContentY = gridY + 50.0f;

    // Set clip region for scrollable area
    s_gridClipRect = RectF(rightColX, gridContentY, rightColWidth, ch - (gridContentY - cy));
    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(s_gridClipRect);

    int cols = 3; float gap = 10.0f; float bW = (gridW - (gap * (cols - 1))) / cols; float bH = 68.0f;
    float curX = gridX; float curY = gridContentY - s_gridScrollY; int cCount = 0;
    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (cCount >= cols) { cCount = 0; curX = gridX; curY += bH + gap; }
        btn.bounds = RectF(curX, curY, bW, bH);
        GraphicsPath bP; AddRoundedRectPath(bP, curX, curY, bW, bH, 10.0f);
        g.FillPath(&SolidBrush(btn.isHovered ? clrCardHovBg : clrCardBg), &bP);
        g.DrawPath(&Pen(btn.isHovered ? clrTeal : clrDivider, 1.0f), &bP);
        g.DrawString(btn.icon.c_str(), -1, &fIconBig, RectF(curX + 12.0f, curY + 17.0f, 34.0f, 34.0f), &fmtC, &SolidBrush(btn.isHovered ? clrTeal : Color(255, 80, 100, 150)));
        g.DrawString(btn.title.c_str(), -1, &fBtnTitle, RectF(curX + 56.0f, curY + 16.0f, bW - 64.0f, 18.0f), &fmtTL, &SolidBrush(clrTextSecondary));
        g.DrawString(btn.subtext.c_str(), -1, &fBtnSub, RectF(curX + 56.0f, curY + 36.0f, bW - 64.0f, 16.0f), &fmtTL, &SolidBrush(btn.isHovered ? clrTeal : clrTextMuted));
        curX += bW + gap; cCount++;
    }

    // Compute max scroll
    { int totalRows = ((int)s_sections[selectedDashTab].btns.size() + cols - 1) / cols;
      float totalGridH = totalRows * (bH + gap);
      float visH = s_gridClipRect.Height - 10.0f;
      s_gridMaxScroll = (totalGridH > visH) ? totalGridH - visH : 0.0f;
      if (s_gridScrollY > s_gridMaxScroll) s_gridScrollY = s_gridMaxScroll;
      // Scroll indicator
      if (s_gridMaxScroll > 0.0f) {
          float trackH = s_gridClipRect.Height - 8.0f;
          float thumbH = max(30.0f, trackH * (visH / totalGridH));
          float thumbY = s_gridClipRect.Y + 4.0f + (s_gridScrollY / s_gridMaxScroll) * (trackH - thumbH);
          float trackX = rightColX + rightColWidth - 6.0f;
          g.FillRectangle(&SolidBrush(Color(30, 0, 0, 0)), trackX, s_gridClipRect.Y + 4.0f, 3.0f, trackH);
          g.FillRectangle(&SolidBrush(Color(140, 8, 145, 130)), trackX, thumbY, 3.0f, thumbH);
      }
    }

    g.SetClip(&oldClip); // restore
}

void ProcessDashboardMouseMove(float x, float y) {
    bool redraw = false;
    for (int i = 0; i < 4; i++) { bool h = s_actionCards[i].bounds.Contains(x, y); if(h != s_actionCards[i].isHovered) { s_actionCards[i].isHovered = h; redraw = true; } }
    int oldH = hoveredDashTab; hoveredDashTab = -1;
    for (int i = 0; i < 5; i++) { if (s_tabRects[i].Contains(x, y)) { hoveredDashTab = i; redraw = true; break; } }
    if(oldH != hoveredDashTab) redraw = true;
    for (auto& btn : s_sections[selectedDashTab].btns) {
        bool inClip = s_gridClipRect.Contains(x, y);
        bool h = inClip && btn.bounds.Contains(x, y);
        if (h != btn.isHovered) { btn.isHovered = h; redraw = true; }
    }
    if (redraw && hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}

void ProcessDashboardMouseClick(float x, float y, int& selectedTab) {
    for (int i = 0; i < 4; i++) {
        if (s_actionCards[i].bounds.Contains(x, y)) {
            if (i == 0) { SetBlockSubTabWithAction(1, 1); selectedTab = 1; }
            else if (i == 1) { SetBlockSubTabWithAction(2, 0); selectedTab = 1; }
            else if (i == 2) { selectedTab = 2; }
            else if (i == 3) { SetBlockSubTabWithAction(0, 2); selectedTab = 1; }
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return;
        }
    }
    for (int i = 0; i < 5; i++) { if (s_tabRects[i].Contains(x, y)) { if (selectedDashTab != i) s_gridScrollY = 0.0f; selectedDashTab = i; if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; } }
    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (!s_gridClipRect.Contains(x, y)) break;
        if (!btn.bounds.Contains(x, y)) continue;
        // Quick Blocks (sec 0)
        if      (btn.title == L"Rest Button")      { SetBlockSubTabWithAction(0, 0); selectedTab = 1; }
        else if (btn.title == L"Internet Block")   { SetBlockSubTabWithAction(0, 0); selectedTab = 1; }
        else if (btn.title == L"Uninstall Block")  { SetBlockSubTabWithAction(0, 0); selectedTab = 1; }
        else if (btn.title == L"Debug Kill Apps")  { KillDebugAppsNow(); MessageBoxW(NULL, L"Debug/monitor apps বন্ধ করা হয়েছে।", L"RasFocus", MB_OK | MB_ICONINFORMATION | MB_TOPMOST); }
        else if (btn.title == L"Ads Block")        { SetBlockSubTabWithAction(1, 0); selectedTab = 1; }
        else if (btn.title == L"Adult Block")      { SetBlockSubTab(2);              selectedTab = 1; }
        else if (btn.title == L"YT Shorts Block")  { SetBlockSubTabWithAction(1, 0); selectedTab = 1; }
        else if (btn.title == L"FB Reels Block")   { SetBlockSubTabWithAction(1, 0); selectedTab = 1; }
        // Web & Cloud (sec 1)
        else if (btn.title == L"RasBrowser")    LaunchMiniBrowser(L"RAS_BROWSER", L"RasBrowser");
        else if (btn.title == L"Gemini")         LaunchMiniBrowser(L"https://gemini.google.com", L"Gemini");
        else if (btn.title == L"ChatGPT")        LaunchMiniBrowser(L"https://chatgpt.com", L"ChatGPT");
        else if (btn.title == L"DeepSeek")       LaunchMiniBrowser(L"https://chat.deepseek.com", L"DeepSeek");
        else if (btn.title == L"Grok")           LaunchMiniBrowser(L"https://grok.com", L"Grok");
        else if (btn.title == L"Perplexity")     LaunchMiniBrowser(L"https://www.perplexity.ai", L"Perplexity");
        else if (btn.title == L"MATLAB")         LaunchMiniBrowser(L"https://matlab.mathworks.com", L"MATLAB");
        else if (btn.title == L"YouTube")        LaunchMiniBrowser(L"https://www.youtube.com", L"YouTube");
        else if (btn.title == L"Facebook")       LaunchMiniBrowser(L"https://www.facebook.com", L"Facebook");
        else if (btn.title == L"Google Colab")   LaunchMiniBrowser(L"https://colab.research.google.com", L"Google Colab");
        else if (btn.title == L"OneDrive")       LaunchMiniBrowser(L"https://onedrive.live.com", L"OneDrive");
        else if (btn.title == L"Gmail")          LaunchMiniBrowser(L"https://mail.google.com", L"Gmail");
        else if (btn.title == L"Google Docs")    LaunchMiniBrowser(L"https://docs.google.com", L"Google Docs");
        else if (btn.title == L"Google Slides")  LaunchMiniBrowser(L"https://slides.google.com", L"Google Slides");
        else if (btn.title == L"Google Sheets")  LaunchMiniBrowser(L"https://sheets.google.com", L"Google Sheets");
        // Pro Tools (sec 2)
        else if (btn.title == L"PDF Reader")     LaunchFoxitStylePdfReader(L"");
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }
}

void ProcessDashboardMouseWheel(int delta) {
    float step = 60.0f;
    if (delta > 0) s_gridScrollY -= step;
    else           s_gridScrollY += step;
    if (s_gridScrollY < 0.0f)             s_gridScrollY = 0.0f;
    if (s_gridScrollY > s_gridMaxScroll)  s_gridScrollY = s_gridMaxScroll;
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}
