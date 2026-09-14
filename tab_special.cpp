// tab_special.cpp
// Special Tab — Windows File Explorer–style shell with
//   left sidebar (Quick Access, This PC, Drives, Google Drive)
//   top sub-tab bar (File Manager Plus | Professional Diary | RasGram Desktop)
//   content area delegates to existing sub-tab renderers

#include "tab_special.h"
#include "tab_gemini.h"        // Diary Tab
#include "tab_rasgram.h"       // RasGram Desktop Sub-Tab
#include "tab_file_manager.h"  // File Manager Plus Sub-Tab
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <time.h>
#include <fstream>

using namespace Gdiplus;
using namespace std;

// ============================================================
// STATE
// ============================================================
int sf_activeSubTab = 0; // 0 = File Manager Plus, 1 = Diary, 2 = Utilities

// Motivational popup state (kept from original)
static bool sf_chkMotivation      = false;
static int  sf_langSel            = 0; // 0 = English, 1 = Bangla
static bool motivationThreadRunning = false;
static wstring currentMotiveQuote = L"";

// Sidebar hover / selection
static int  sf_hovSideItem  = -1;  // quick-access row
static int  sf_hovDriveItem = -1;  // drive row
static bool sf_hovGDrive    = false;

// Sub-tab hover
static bool sf_hovTabFM      = false;
static bool sf_hovTabDiary   = false;
static bool sf_hovTabRasGram = false;

// Layout cache (set each draw, used by mouse handlers)
static float g_cx = 0, g_cy = 0, g_cw = 0, g_ch = 0;
static float g_sideW   = 220.0f;  // sidebar width
static float g_headerH =  52.0f;  // sub-tab bar height

// Sidebar item rects cache (for hit testing)
struct SideRect { float x, y, w, h; };
static vector<SideRect> g_quickRects;   // quick-access items
static vector<SideRect> g_driveRects;   // drive items
static SideRect          g_gdriveRect;  // google drive item

// Motivational quotes
static vector<wstring> quotesEng = {
    L"\"Don't watch the clock; do what it does. Keep going.\" - Sam Levenson",
    L"\"The future depends on what you do today.\" - Mahatma Gandhi",
    L"\"Focus on your goal. Don't look in any direction but ahead.\"",
    L"\"Time is what we want most, but what we use worst.\" - William Penn",
    L"\"Push yourself, because no one else is going to do it for you.\""
};
static vector<wstring> quotesBen = {
    L"\"ঘড়ির দিকে তাকিও না; ঘড়ি যা করে তা করো। চলতে থাকো।\"",
    L"\"তোমার ভবিষ্যৎ নির্ভর করে তুমি আজ কী করছো তার ওপর।\"",
    L"\"শুধু লক্ষ্যের দিকে ফোকাস করো। অন্য কোথাও তাকানোর সময় নেই।\"",
    L"\"সময়ই আমাদের সবচেয়ে বেশি দরকার, অথচ এটাকেই আমরা সবচেয়ে বাজেভাবে ব্যবহার করি।\"",
    L"\"নিজেকে নিজে পুশ করো, কারণ অন্য কেউ তোমার হয়ে এটা করে দেবে না।\""
};

// ============================================================
// EXTERNALS
// ============================================================
extern bool IsRunAsAdmin();
extern string GetSecretDir();
extern HWND hParentWnd;

// Diary
extern void ShowGeminiControls(bool show);
extern void DrawGeminiTab(Graphics& g, float cx, float cy, float cw, float ch);
extern void ResizeGeminiControls(int cx, int cy, int cw, int ch);
extern void ProcessGeminiMouseMove(float x, float y);
extern void ProcessGeminiMouseClick(float x, float y);

// Utilities (removed — replaced by RasGram Desktop)
// RasGram Desktop
extern void ShowRasGramControls(bool show);
extern void DrawRasGramTab(Graphics& g, float cx, float cy, float cw, float ch);
extern void ProcessRasGramMouseMove(float x, float y);
extern void ProcessRasGramMouseClick(float x, float y);
extern void ProcessRasGramMouseWheel(int delta);

// ============================================================
// HELPERS
// ============================================================
static void FillRoundRect(Graphics& g, Brush* br, Pen* pen,
                          float x, float y, float w, float h, float r = 6.0f) {
    GraphicsPath path;
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,   y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,   y+h-r*2,   r*2, r*2,   0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
    if (br)  g.FillPath(br,  &path);
    if (pen) g.DrawPath(pen, &path);
}

// ============================================================
// MOTIVATIONAL POPUP (kept from original)
// ============================================================
LRESULT CALLBACK MotivationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT r; GetClientRect(hwnd, &r);
        GraphicsPath path; int d = 20;
        path.AddArc(0,0,d,d,180,90); path.AddArc(r.right-d,0,d,d,270,90);
        path.AddArc(r.right-d,r.bottom-d,d,d,0,90); path.AddArc(0,r.bottom-d,d,d,90,90);
        path.CloseFigure();
        SolidBrush bg(Color(250,30,41,59));  g.FillPath(&bg, &path);
        Pen border(Color(255,245,158,11),2.0f); g.DrawPath(&border,&path);
        FontFamily ff(sf_langSel==1 ? L"Vrinda" : L"Segoe UI");
        Font fQ(&ff,22,FontStyleBold,UnitPixel);
        SolidBrush wBr(Color(255,255,255,255));
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(currentMotiveQuote.c_str(),-1,&fQ,RectF(20,20,(float)r.right-40,(float)r.bottom-40),&fmt,&wBr);
        EndPaint(hwnd,&ps); return 0;
    }
    if (msg==WM_TIMER) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}
static void ShowMotivationalPopup() {
    thread([](){
        srand((unsigned)time(0));
        if (sf_langSel==0) currentMotiveQuote = quotesEng[rand()%quotesEng.size()];
        else               currentMotiveQuote = quotesBen[rand()%quotesBen.size()];
        static bool reg=false;
        if (!reg) {
            WNDCLASSW wc={0}; wc.lpfnWndProc=MotivationWndProc; wc.hInstance=GetModuleHandle(NULL);
            wc.lpszClassName=L"RasMotivClass"; RegisterClassW(&wc); reg=true;
        }
        int w=550,h=120,x=(GetSystemMetrics(SM_CXSCREEN)-w)/2;
        HWND hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,
            L"RasMotivClass",L"",WS_POPUP,x,50,w,h,NULL,NULL,NULL,NULL);
        SetLayeredWindowAttributes(hwnd,0,245,LWA_ALPHA);
        ShowWindow(hwnd,SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetTimer(hwnd,1,6000,NULL);
        MSG msg; while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
        DestroyWindow(hwnd);
    }).detach();
}
static void MotivationBackgroundThread() {
    while(true) {
        Sleep(1000);
        if (sf_chkMotivation) {
            static DWORD last=GetTickCount();
            DWORD now=GetTickCount();
            if (now-last>=900000){last=now;ShowMotivationalPopup();}
        }
    }
}

// ============================================================
// DRAW SIDEBAR  —  Windows Explorer left panel style
// ============================================================
static void DrawSidebar(Graphics& g,
                        float sx, float sy, float sw, float sh,
                        const FontFamily& ff, const FontFamily& ffIc)
{
    g_quickRects.clear();
    g_driveRects.clear();

    // Fonts
    Font fSm (&ff,   12, FontStyleRegular, UnitPixel);
    Font fTiny(&ff,  10, FontStyleBold,    UnitPixel);
    Font fIcSm(&ffIc,14, FontStyleRegular, UnitPixel);

    // Brushes / pens
    SolidBrush bSideBg (Color(255, 243, 243, 243));   // Win11-ish sidebar grey
    SolidBrush bDark   (Color(255,  40,  40,  40));
    SolidBrush bGray   (Color(255, 130, 130, 130));
    SolidBrush bLabel  (Color(255, 110, 110, 110));
    SolidBrush bTeal   (Color(255,   0, 150, 160));
    SolidBrush bActBg  (Color(255, 209, 238, 241));  // selected row tint
    SolidBrush bHovBg  (Color(255, 228, 228, 228));  // hovered row

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    // Sidebar background
    g.FillRectangle(&bSideBg, sx, sy, sw, sh);
    // Right border
    Pen pSideBrd(Color(255, 218, 220, 224), 1.0f);
    g.DrawLine(&pSideBrd, sx+sw, sy, sx+sw, sy+sh);

    float rowH = 32.0f;
    float curY = sy + 8.0f;
    float padX = 12.0f;

    // --- Quick Access section ---
    // Section header
    g.DrawString(L"Quick access", -1, &fTiny,
                 RectF(sx+padX, curY, sw-padX*2, 18.0f), &fmtL, &bLabel);
    curY += 22.0f;

    // Resolve shell paths
    wchar_t desktopPath[MAX_PATH]={}, dlPath[MAX_PATH]={};
    wchar_t docPath[MAX_PATH]={},    picPath[MAX_PATH]={};
    wchar_t musicPath[MAX_PATH]={},  vidPath[MAX_PATH]={};
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
    SHGetFolderPathW(NULL, CSIDL_PERSONAL,          NULL, 0, docPath);
    SHGetFolderPathW(NULL, CSIDL_MYPICTURES,        NULL, 0, picPath);
    SHGetFolderPathW(NULL, CSIDL_MYMUSIC,           NULL, 0, musicPath);
    SHGetFolderPathW(NULL, CSIDL_MYVIDEO,           NULL, 0, vidPath);
    PWSTR dlRaw=NULL;
    SHGetKnownFolderPath(FOLDERID_Downloads,0,NULL,&dlRaw);
    if (dlRaw){wcscpy_s(dlPath,dlRaw);CoTaskMemFree(dlRaw);}

    struct QItem { const wchar_t* icon; const wchar_t* label; const wchar_t* path; };
    QItem qa[] = {
        { L"\xE8B7", L"Desktop",    desktopPath },
        { L"\xEC0A", L"Downloads",  dlPath      },
        { L"\xE8A5", L"Documents",  docPath     },
        { L"\xEB9F", L"Pictures",   picPath     },
        { L"\xEC4F", L"Music",      musicPath   },
        { L"\xE8B2", L"Videos",     vidPath     },
    };

    for (int i=0; i<6; i++) {
        float ry = curY;
        bool hov = (sf_hovSideItem == i);

        if (hov) {
            SolidBrush bH(Color(255, 228, 228, 228));
            FillRoundRect(g, &bH, nullptr, sx+4, ry, sw-8, rowH, 4.0f);
        }
        // Active-tab indicator (teal pill on left edge)
        // (for this sidebar we highlight when file manager is active)
        if (sf_activeSubTab == 0) {
            // no path tracking here — just style
        }

        g.DrawString(qa[i].icon,  -1, &fIcSm, RectF(sx+padX,         ry, 20.0f,  rowH), &fmtL, &bGray);
        g.DrawString(qa[i].label, -1, &fSm,   RectF(sx+padX+24.0f,   ry, sw-padX*2-24, rowH), &fmtL, &bDark);

        g_quickRects.push_back({sx, ry, sw, rowH});
        curY += rowH;
    }

    // --- This PC separator + label ---
    curY += 6.0f;
    Pen pSep(Color(200, 200, 200, 200), 1.0f);
    g.DrawLine(&pSep, sx+padX, curY, sx+sw-padX, curY);
    curY += 6.0f;
    g.DrawString(L"This PC", -1, &fTiny,
                 RectF(sx+padX, curY, sw-padX*2, 18.0f), &fmtL, &bLabel);
    curY += 22.0f;

    // --- Drives (dynamic) ---
    wchar_t driveStrings[512]={};
    GetLogicalDriveStringsW(511, driveStrings);
    vector<wstring> drives;
    for (wchar_t* p=driveStrings; *p; p+=wcslen(p)+1) {
        UINT t=GetDriveTypeW(p);
        if (t==DRIVE_FIXED||t==DRIVE_REMOVABLE||t==DRIVE_REMOTE||t==DRIVE_RAMDISK)
            drives.push_back(p);
    }

    for (int di=0; di<(int)drives.size(); di++) {
        float ry = curY;
        bool hov = (sf_hovDriveItem == di);

        if (hov) {
            FillRoundRect(g, &bHovBg, nullptr, sx+4, ry, sw-8, rowH, 4.0f);
        }

        wstring lbl = drives[di];
        if (!lbl.empty() && lbl.back()==L'\\') lbl.pop_back(); // "C:"

        UINT dtype = GetDriveTypeW(drives[di].c_str());
        const wchar_t* dIcon = L"\xE7D2"; // HDD
        if (dtype==DRIVE_REMOVABLE) dIcon = L"\xE88E"; // USB
        if (dtype==DRIVE_REMOTE)    dIcon = L"\xE753"; // Network

        // Drive label + free space bar
        g.DrawString(dIcon,       -1, &fIcSm, RectF(sx+padX,        ry, 20.0f, rowH), &fmtL, &bGray);
        g.DrawString(lbl.c_str(),-1, &fSm,   RectF(sx+padX+24.0f, ry, sw-padX*2-24, rowH), &fmtL, &bDark);

        // Mini drive usage bar (only for fixed drives)
        if (dtype == DRIVE_FIXED) {
            ULARGE_INTEGER freeBytesAvail={}, totalBytes={}, totalFreeBytes={};
            if (GetDiskFreeSpaceExW(drives[di].c_str(), &freeBytesAvail, &totalBytes, &totalFreeBytes)
                && totalBytes.QuadPart > 0)
            {
                float used = 1.0f - (float)totalFreeBytes.QuadPart / (float)totalBytes.QuadPart;
                float barX = sx+padX+24.0f, barY = ry+rowH-8.0f;
                float barW = sw-padX*2-30.0f, barH = 4.0f;
                // Track
                SolidBrush bTrack(Color(255, 210, 210, 210));
                g.FillRectangle(&bTrack, barX, barY, barW, barH);
                // Fill (blue if < 80%, orange if < 90%, red if >= 90%)
                Color fillCol = used < 0.80f ? Color(255, 66, 133, 244) :
                                used < 0.90f ? Color(255, 245, 158, 11)  :
                                               Color(255, 220, 60, 60);
                SolidBrush bFill(fillCol);
                g.FillRectangle(&bFill, barX, barY, barW * used, barH);
            }
        }

        g_driveRects.push_back({sx, ry, sw, rowH});
        curY += rowH;
    }

    // --- Google Drive separator + entry ---
    curY += 6.0f;
    g.DrawLine(&pSep, sx+padX, curY, sx+sw-padX, curY);
    curY += 6.0f;

    // Google Drive colored-dot icon (G colour marks)
    float gdY = curY;
    bool gdHov = sf_hovGDrive;
    if (gdHov) {
        FillRoundRect(g, &bHovBg, nullptr, sx+4, gdY, sw-8, rowH, 4.0f);
    }

    // Draw Google Drive tri-colour icon manually
    float dotX = sx + padX + 2.0f;
    float dotCY = gdY + rowH/2.0f;
    float r2 = 5.0f;
    // Triangle shape: three coloured circles arranged as Google Drive logo hint
    SolidBrush bGBlue (Color(255,  66, 133, 244));
    SolidBrush bGGreen(Color(255,  52, 168,  83));
    SolidBrush bGYellow(Color(255, 251, 188,   5));
    // Small triangle of dots
    g.FillEllipse(&bGBlue,   dotX,        dotCY - r2*1.1f, r2*1.5f, r2*1.5f);
    g.FillEllipse(&bGGreen,  dotX+r2*0.8f,dotCY + r2*0.2f, r2*1.5f, r2*1.5f);
    g.FillEllipse(&bGYellow, dotX-r2*0.1f,dotCY + r2*0.2f, r2*1.5f, r2*1.5f);

    g.DrawString(L"Google Drive", -1, &fSm,
                 RectF(sx+padX+24.0f, gdY, sw-padX*2-24, rowH), &fmtL, &bDark);
    g_gdriveRect = {sx, gdY, sw, rowH};
    curY += rowH;
}

// ============================================================
// DRAW SUB-TAB HEADER BAR
// ============================================================
static void DrawSubTabBar(Graphics& g,
                          float tx, float ty, float tw, float th,
                          const FontFamily& ff, const FontFamily& ffIc)
{
    Font fBold(&ff, 13, FontStyleBold,    UnitPixel);
    Font fReg (&ff, 13, FontStyleRegular, UnitPixel);
    Font fIcSm(&ffIc,14, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255,255,255,255));
    SolidBrush bTeal (Color(255,  0,150,160));
    SolidBrush bGray (Color(255,130,130,130));
    SolidBrush bDiaryBlue(Color(255,35,137,215));
    SolidBrush bRasGramGreen(Color(255,0,168,132));  // RasGram teal-green

    Pen pBrd(Color(255,218,225,232),1.0f);
    Pen pTeal(Color(255,0,150,160),2.5f);
    Pen pDiaryBlue(Color(255,35,137,215),2.5f);
    Pen pRasGramGreen(Color(255,0,168,132),2.5f);

    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);

    // Background
    g.FillRectangle(&bWhite, tx, ty, tw, th);
    g.DrawLine(&pBrd, tx, ty+th, tx+tw, ty+th);

    struct TabDef {
        const wchar_t* icon;
        const wchar_t* label;
        int            idx;
        bool           hov;
        SolidBrush*    activeColor;
        Pen*           activePen;
    };
    TabDef tabs[] = {
        { L"\xEC50", L"File Manager Plus",   0, sf_hovTabFM,      &bTeal,           &pTeal          },
        { L"\xE7BC", L"Professional Diary",  1, sf_hovTabDiary,   &bDiaryBlue,      &pDiaryBlue     },
        { L"\xE8BD", L"RasGram Desktop",     2, sf_hovTabRasGram, &bRasGramGreen,   &pRasGramGreen  },
    };

    float tabW = tw / 3.0f;
    for (int i = 0; i < 3; i++) {
        float tbx = tx + i * tabW;
        bool active = (sf_activeSubTab == tabs[i].idx);

        if (active) {
            SolidBrush bActBg(Color(30, 0, 150, 160));
            g.FillRectangle(&bActBg, tbx, ty, tabW, th);
            // Bottom accent line
            g.DrawLine(tabs[i].activePen, tbx+4, ty+th-2, tbx+tabW-4, ty+th-2);
            // Icon + label
            g.DrawString(tabs[i].icon,  -1, &fIcSm, RectF(tbx+12, ty, 22, th), &fmtL, tabs[i].activeColor);
            g.DrawString(tabs[i].label, -1, &fBold,  RectF(tbx+36, ty, tabW-40, th), &fmtL, tabs[i].activeColor);
        } else {
            if (tabs[i].hov) {
                SolidBrush bH(Color(255,245,245,245));
                g.FillRectangle(&bH, tbx, ty, tabW, th);
            }
            g.DrawString(tabs[i].icon,  -1, &fIcSm, RectF(tbx+12, ty, 22, th), &fmtL, &bGray);
            g.DrawString(tabs[i].label, -1, &fReg,  RectF(tbx+36, ty, tabW-40, th), &fmtL, &bGray);
        }

        // Vertical divider between tabs
        if (i < 2) {
            Pen pDiv(Color(255,225,228,232),1.0f);
            g.DrawLine(&pDiv, tbx+tabW, ty+8, tbx+tabW, ty+th-8);
        }
    }
}

// ============================================================
// MAIN DRAW
// ============================================================
void DrawSpecialFeatureTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_cx=cx; g_cy=cy; g_cw=cw; g_ch=ch;

    if (!motivationThreadRunning) {
        thread t(MotivationBackgroundThread); t.detach();
        motivationThreadRunning = true;
    }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff  (L"Segoe UI");
    FontFamily ffIc(L"Segoe MDL2 Assets");

    // ---- Outer background ----
    SolidBrush bBg(Color(255,248,250,252));
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // ---- Sub-tab header (full width, at top) ----
    DrawSubTabBar(g, cx, cy, cw, g_headerH, ff, ffIc);

    float bodyY = cy + g_headerH;
    float bodyH = ch - g_headerH;

    // ---- Sidebar: only for File Manager tab ----
    float contentX, contentW;
    if (sf_activeSubTab == 0) {
        DrawSidebar(g, cx, bodyY, g_sideW, bodyH, ff, ffIc);
        contentX = cx + g_sideW;
        contentW = cw - g_sideW;
    } else {
        // Non-file-manager tabs: full width, no sidebar
        contentX = cx;
        contentW = cw;
    }

    if (sf_activeSubTab == 0) {
        // Hide ALL overlay controls first so WebView2 / Win32 edits
        // from other sub-tabs don't paint over the File Manager GDI content.
        ShowGeminiControls(false);
        ShowRasGramControls(false);   // hides WebView2 if it exists
        DrawFileManagerTab(g, contentX, bodyY, contentW, bodyH);
    }
    else if (sf_activeSubTab == 1) {
        ShowRasGramControls(false);
        DrawGeminiTab(g, contentX, bodyY, contentW, bodyH);
        ResizeGeminiControls((int)contentX, (int)bodyY, (int)contentW, (int)bodyH);
        ShowGeminiControls(true);
    }
    else if (sf_activeSubTab == 2) {
        ShowGeminiControls(false);
        // DrawRasGramTab creates/positions the WebView2 and calls InitRasGramDesktop.
        // ShowRasGramControls(true) makes the controller visible AFTER it is positioned.
        DrawRasGramTab(g, contentX, bodyY, contentW, bodyH);
        ShowRasGramControls(true);
    }
}

// ============================================================
// MOUSE MOVE
// ============================================================
void ProcessSpecialFeatureMouseMove(float x, float y) {
    // ---- Sub-tab bar hover ----
    bool old_hFM      = sf_hovTabFM;
    bool old_hDiary   = sf_hovTabDiary;
    bool old_hRasGram = sf_hovTabRasGram;

    float tabW = g_cw / 3.0f;
    sf_hovTabFM      = (y >= g_cy && y <= g_cy+g_headerH && x >= g_cx          && x < g_cx+tabW);
    sf_hovTabDiary   = (y >= g_cy && y <= g_cy+g_headerH && x >= g_cx+tabW     && x < g_cx+tabW*2);
    sf_hovTabRasGram = (y >= g_cy && y <= g_cy+g_headerH && x >= g_cx+tabW*2   && x < g_cx+g_cw);

    // ---- Sidebar hover (only for File Manager tab) ----
    int old_hSide  = sf_hovSideItem;
    int old_hDrive = sf_hovDriveItem;
    bool old_hGD   = sf_hovGDrive;

    sf_hovSideItem  = -1;
    sf_hovDriveItem = -1;
    sf_hovGDrive    = false;

    if (sf_activeSubTab == 0) {
        for (int i=0; i<(int)g_quickRects.size(); i++) {
            auto& r = g_quickRects[i];
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) { sf_hovSideItem=i; break; }
        }
        if (sf_hovSideItem < 0) {
            for (int i=0; i<(int)g_driveRects.size(); i++) {
                auto& r = g_driveRects[i];
                if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) { sf_hovDriveItem=i; break; }
            }
        }
        if (sf_hovSideItem<0 && sf_hovDriveItem<0) {
            auto& r = g_gdriveRect;
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) sf_hovGDrive=true;
        }
    }

    // ---- Delegate to active sub-tab ----
    float bodyY    = g_cy + g_headerH;
    float contentX = (sf_activeSubTab == 0) ? (g_cx + g_sideW) : g_cx;
    float contentW = (sf_activeSubTab == 0) ? (g_cw - g_sideW) : g_cw;

    if (sf_activeSubTab == 0)
        ProcessFileManagerMouseMove(x, y);
    else if (sf_activeSubTab == 1)
        ProcessGeminiMouseMove(x, y);
    else if (sf_activeSubTab == 2)
        ProcessRasGramMouseMove(x, y);

    bool changed = (old_hFM      != sf_hovTabFM      ||
                    old_hDiary   != sf_hovTabDiary    ||
                    old_hRasGram != sf_hovTabRasGram  ||
                    old_hSide    != sf_hovSideItem    ||
                    old_hDrive   != sf_hovDriveItem   ||
                    old_hGD      != sf_hovGDrive);
    if (changed && hParentWnd)
        InvalidateRect(hParentWnd, NULL, TRUE);
}

// ============================================================
// MOUSE CLICK
// ============================================================
// Helper: immediately hide all overlay controls for sub-tabs we are
// switching AWAY from, so WebView2 / Win32 windows don't bleed through
// on the very next WM_PAINT before DrawSpecialFeatureTab runs.
static void HideInactiveOverlays(int nextSubTab) {
    if (nextSubTab != 2) ShowRasGramControls(false);
    if (nextSubTab != 1) ShowGeminiControls(false);
}

void ProcessSpecialFeatureMouseClick(float x, float y) {
    // ---- Sub-tab bar clicks ----
    if (y >= g_cy && y <= g_cy + g_headerH) {
        float tabW = g_cw / 3.0f;
        int next = sf_activeSubTab;
        if      (x >= g_cx          && x < g_cx+tabW)   next = 0;
        else if (x >= g_cx+tabW     && x < g_cx+tabW*2) next = 1;
        else if (x >= g_cx+tabW*2   && x < g_cx+g_cw)   next = 2;
        if (next != sf_activeSubTab) {
            HideInactiveOverlays(next);
            sf_activeSubTab = next;
        }
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    float bodyY    = g_cy + g_headerH;
    float contentX = (sf_activeSubTab == 0) ? (g_cx + g_sideW) : g_cx;

    // ---- Sidebar quick-access clicks (only for File Manager tab) ----
    if (sf_activeSubTab == 0 && x >= g_cx && x < g_cx + g_sideW) {
        // Quick access items
        for (int i=0; i<(int)g_quickRects.size(); i++) {
            auto& r = g_quickRects[i];
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                // Switch to File Manager tab and navigate
                if (sf_activeSubTab != 0) HideInactiveOverlays(0);
                sf_activeSubTab = 0;
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }
        // Drive items — switch to file manager
        for (int i=0; i<(int)g_driveRects.size(); i++) {
            auto& r = g_driveRects[i];
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                if (sf_activeSubTab != 0) HideInactiveOverlays(0);
                sf_activeSubTab = 0;
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }
        // Google Drive
        {
            auto& r = g_gdriveRect;
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                if (sf_activeSubTab != 0) HideInactiveOverlays(0);
                sf_activeSubTab = 0; // Switch to File Manager (Drive tab inside)
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }
        return; // click was in sidebar but missed all items
    }

    // ---- Content area clicks — guard below header ----
    if (y <= bodyY) return;

    if (sf_activeSubTab == 0)
        ProcessFileManagerMouseClick(x, y, hParentWnd);
    else if (sf_activeSubTab == 1)
        ProcessGeminiMouseClick(x, y);
    else if (sf_activeSubTab == 2)
        ProcessRasGramMouseClick(x, y);
}
