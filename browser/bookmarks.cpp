// bookmarks.cpp — RasBrowser Bookmark Manager (Full Implementation)
#define _CRT_SECURE_NO_WARNINGS
#include "bookmarks.h"
#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace Gdiplus;

// ─────────────────────────────────────────────────────────────────────────────
// GLOBALS
// ─────────────────────────────────────────────────────────────────────────────
std::vector<BookmarkItem> g_bookmarks;
bool g_bookmarkPanelOpen   = false;
int  g_bookmarkHoverIdx    = -1;
int  g_bookmarkBarHoverIdx = -1;

// ─────────────────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static int S(int val, int dpi) { return MulDiv(val, dpi, 96); }

static std::wstring GetBookmarkFilePath() {
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path);
    std::wstring dir = std::wstring(path) + L"\\RasBrowserData";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\bookmarks.txt";
}

// URL থেকে domain/short name বের করো (bookmark bar label এর জন্য)
static std::wstring ShortTitle(const std::wstring& title, const std::wstring& url) {
    if (!title.empty() && title.size() <= 20) return title;
    // url থেকে domain বের করো
    std::wstring u = url;
    size_t p = u.find(L"://");
    if (p != std::wstring::npos) u = u.substr(p + 3);
    size_t s = u.find(L'/');
    if (s != std::wstring::npos) u = u.substr(0, s);
    if (u.substr(0, 4) == L"www.") u = u.substr(4);
    // Max 16 chars
    if (u.size() > 16) u = u.substr(0, 14) + L"..";
    return u;
}

// ─────────────────────────────────────────────────────────────────────────────
// LOAD / SAVE
// ─────────────────────────────────────────────────────────────────────────────
void LoadBookmarks() {
    g_bookmarks.clear();
    std::wifstream in(GetBookmarkFilePath());
    if (!in.is_open()) {
        // Default bookmarks
        g_bookmarks.push_back({ L"YouTube",   L"https://www.youtube.com"   });
        g_bookmarks.push_back({ L"Facebook",  L"https://www.facebook.com"  });
        g_bookmarks.push_back({ L"GitHub",    L"https://github.com"        });
        g_bookmarks.push_back({ L"Wikipedia", L"https://www.wikipedia.org" });
        g_bookmarks.push_back({ L"ChatGPT",   L"https://chatgpt.com"       });
        SaveBookmarks();
        return;
    }
    std::wstring line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == L'#') continue;
        size_t pipe = line.find(L'|');
        if (pipe == std::wstring::npos) continue;
        BookmarkItem item;
        item.title = line.substr(0, pipe);
        item.url   = line.substr(pipe + 1);
        if (!item.url.empty()) g_bookmarks.push_back(item);
    }
}

void SaveBookmarks() {
    std::wofstream out(GetBookmarkFilePath(), std::ios::trunc);
    if (!out.is_open()) return;
    out << L"# RasBrowser Bookmarks\n";
    for (const auto& b : g_bookmarks)
        out << b.title << L"|" << b.url << L"\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// CORE
// ─────────────────────────────────────────────────────────────────────────────
bool IsBookmarked(const std::wstring& url) {
    for (const auto& b : g_bookmarks)
        if (b.url == url) return true;
    return false;
}

void AddBookmark(const std::wstring& title, const std::wstring& url) {
    if (url.empty() || url == L"LOCAL_NTP" || url == L"about:blank") return;
    if (IsBookmarked(url)) return;
    g_bookmarks.insert(g_bookmarks.begin(), { title.empty() ? url : title, url });
    SaveBookmarks();
}

void RemoveBookmark(int index) {
    if (index < 0 || index >= (int)g_bookmarks.size()) return;
    g_bookmarks.erase(g_bookmarks.begin() + index);
    SaveBookmarks();
}

void ToggleBookmark(const std::wstring& url, const std::wstring& title) {
    for (int i = 0; i < (int)g_bookmarks.size(); i++) {
        if (g_bookmarks[i].url == url) {
            RemoveBookmark(i);
            return;
        }
    }
    AddBookmark(title, url);
}

// ─────────────────────────────────────────────────────────────────────────────
// DRAW: BOOKMARK STAR (address bar এ)
// ─────────────────────────────────────────────────────────────────────────────
void DrawBookmarkStar(
    Graphics& g,
    int starX, int starY, int starW, int starH,
    bool isBookmarked_, bool isHover,
    bool isDarkMode, int dpi
) {
    // Segoe MDL2 star icons: E8D9 = empty star, E735 = filled star
    FontFamily ff(L"Segoe MDL2 Assets");
    Font fStar(&ff, (float)S(14, dpi), FontStyleRegular, UnitPixel);
    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);

    Color starColor;
    if (isBookmarked_)
        starColor = Color(255, 250, 196, 48); // gold
    else if (isHover)
        starColor = isDarkMode ? Color(255, 200, 200, 200) : Color(255, 80, 80, 80);
    else
        starColor = isDarkMode ? Color(255, 140, 140, 140) : Color(255, 150, 150, 150);

    SolidBrush br(starColor);
    const wchar_t* icon = isBookmarked_ ? L"\uE735" : L"\uE8D9";
    g.DrawString(icon, -1, &fStar,
        RectF((float)starX, (float)starY, (float)starW, (float)starH),
        &sfC, &br);
}

// ─────────────────────────────────────────────────────────────────────────────
// DRAW: BOOKMARK BAR (toolbar এর নিচে)
// ─────────────────────────────────────────────────────────────────────────────
void DrawBookmarkBar(
    Graphics& g,
    int windowWidth,
    int bmkY, int bmkH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY,
    int& outHoverIdx
) {
    outHoverIdx = -1;
    if (g_bookmarks.empty()) return;

    FontFamily ff(L"Segoe UI");
    Font fSmall(&ff, (float)S(12, dpi), FontStyleRegular, UnitPixel);
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcon, (float)S(11, dpi), FontStyleRegular, UnitPixel);

    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);

    Color cText    = isDarkMode ? Color(255, 210, 210, 210) : Color(255, 50, 50, 50);
    Color cHover   = isDarkMode ? Color(255, 55, 55, 60)    : Color(255, 225, 227, 232);
    Color cIcon    = isDarkMode ? Color(255, 130, 130, 130)  : Color(255, 120, 120, 120);

    SolidBrush brText(cText);
    SolidBrush brHover(cHover);
    SolidBrush brIcon(cIcon);

    int curX = S(8, dpi);
    int itemPadX = S(10, dpi);
    int iconW    = S(14, dpi);
    int gap      = S(4, dpi);

    for (int i = 0; i < (int)g_bookmarks.size(); i++) {
        std::wstring label = ShortTitle(g_bookmarks[i].title, g_bookmarks[i].url);

        // Text width estimate (~7px per char at 12px)
        int labelW = (int)(label.size() * S(7, dpi));
        int itemW  = iconW + gap + labelW + itemPadX * 2;
        if (curX + itemW > windowWidth - S(80, dpi)) break; // overflow

        bool hover = (mouseX >= curX && mouseX < curX + itemW &&
                      mouseY >= bmkY && mouseY < bmkY + bmkH);
        if (hover) outHoverIdx = i;

        // Hover background
        if (hover) {
            g.FillRectangle(&brHover,
                (float)curX, (float)(bmkY + S(3, dpi)),
                (float)itemW, (float)(bmkH - S(6, dpi)));
        }

        // Favicon icon (generic globe: E909)
        g.DrawString(L"\uE909", -1, &fIcon,
            RectF((float)(curX + itemPadX), (float)bmkY,
                  (float)iconW, (float)bmkH),
            &sfC, &brIcon);

        // Label
        g.DrawString(label.c_str(), -1, &fSmall,
            RectF((float)(curX + itemPadX + iconW + gap), (float)bmkY,
                  (float)(labelW + itemPadX), (float)bmkH),
            &sfL, &brText);

        curX += itemW + S(2, dpi);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DRAW: BOOKMARK SIDE PANEL
// ─────────────────────────────────────────────────────────────────────────────
void DrawBookmarkPanel(
    Graphics& g,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY,
    int& outHoverIdx
) {
    if (!g_bookmarkPanelOpen) return;
    outHoverIdx = -1;

    int panelW = S(340, dpi);
    int panelX = windowWidth - panelW;
    int panelY = titleBarH + toolbarH;
    int panelH = windowHeight - panelY;

    // ── Background ──
    Color cBg      = isDarkMode ? Color(255, 32, 33, 38)   : Color(255, 255, 255, 255);
    Color cHeader  = isDarkMode ? Color(255, 38, 39, 46)   : Color(255, 246, 248, 250);
    Color cBorder  = isDarkMode ? Color(255, 60, 61, 70)   : Color(255, 218, 220, 224);
    Color cItem    = isDarkMode ? Color(255, 45, 46, 54)   : Color(255, 242, 244, 247);
    Color cText    = isDarkMode ? Color(255, 225, 225, 230) : Color(255, 30, 30, 30);
    Color cDim     = isDarkMode ? Color(255, 130, 130, 140) : Color(255, 120, 120, 130);
    Color cAccent  = Color(255, 12, 168, 176); // teal

    SolidBrush brBg(cBg);
    SolidBrush brHeader(cHeader);
    SolidBrush brItem(cItem);
    SolidBrush brText(cText);
    SolidBrush brDim(cDim);
    SolidBrush brAccent(cAccent);
    Pen penBorder(cBorder, 1.0f);

    // Panel background + left border
    g.FillRectangle(&brBg, panelX, panelY, panelW, panelH);
    g.DrawLine(&penBorder, panelX, panelY, panelX, panelY + panelH);

    // ── Header ──
    int headerH = S(52, dpi);
    g.FillRectangle(&brHeader, panelX, panelY, panelW, headerH);
    g.DrawLine(&penBorder, panelX, panelY + headerH, panelX + panelW, panelY + headerH);

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, (float)S(15, dpi), FontStyleBold,    UnitPixel);
    Font fNorm (&ff, (float)S(13, dpi), FontStyleRegular, UnitPixel);
    Font fSmall(&ff, (float)S(11, dpi), FontStyleRegular, UnitPixel);
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcon, (float)S(13, dpi), FontStyleRegular, UnitPixel);

    StringFormat sfL, sfC, sfR;
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfR.SetAlignment(StringAlignmentFar);    sfR.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(StringFormatFlagsNoWrap);
    sfL.SetTrimming(StringTrimmingEllipsisCharacter);

    // Title: Bookmarks
    g.DrawString(L"\uE8A4", -1, &fIcon,
        RectF((float)(panelX + S(14, dpi)), (float)panelY,
              (float)S(24, dpi), (float)headerH),
        &sfC, &brAccent);
    g.DrawString(L"Bookmarks", -1, &fTitle,
        RectF((float)(panelX + S(42, dpi)), (float)panelY,
              (float)(panelW - S(90, dpi)), (float)headerH),
        &sfL, &brText);

    // Close (X) button
    int closeX = panelX + panelW - S(36, dpi);
    bool closeHover = (mouseX >= closeX && mouseX < panelX + panelW &&
                       mouseY >= panelY && mouseY < panelY + headerH);
    if (closeHover) {
        SolidBrush brClose(isDarkMode ? Color(60,255,80,80) : Color(40,220,50,50));
        g.FillRectangle(&brClose, (float)closeX, (float)panelY, (float)S(36,dpi), (float)headerH);
    }
    SolidBrush brCloseIcon(closeHover ? Color(255,220,50,50) : cDim);
    g.DrawString(L"\uE8BB", -1, &fIcon,
        RectF((float)closeX, (float)panelY, (float)S(36,dpi), (float)headerH),
        &sfC, &brCloseIcon);

    // ── Bookmark count ──
    std::wstring countStr = std::to_wstring(g_bookmarks.size()) + L" bookmarks";
    g.DrawString(countStr.c_str(), -1, &fSmall,
        RectF((float)(panelX + S(14,dpi)), (float)(panelY + headerH + S(10,dpi)),
              (float)(panelW - S(28,dpi)), (float)S(20,dpi)),
        &sfL, &brDim);

    // ── Item list ──
    int listY  = panelY + headerH + S(34, dpi);
    int itemH  = S(56, dpi);
    int iconSz = S(32, dpi);

    if (g_bookmarks.empty()) {
        g.DrawString(L"No bookmarks yet.\nPress Ctrl+D to add the current page.",
            -1, &fNorm,
            RectF((float)(panelX + S(20,dpi)), (float)(listY + S(20,dpi)),
                  (float)(panelW - S(40,dpi)), (float)S(60,dpi)),
            &sfL, &brDim);
        return;
    }

    for (int i = 0; i < (int)g_bookmarks.size(); i++) {
        int iy = listY + i * itemH;
        if (iy + itemH > windowHeight - S(4, dpi)) break; // clip

        bool hover = (mouseX >= panelX && mouseX < panelX + panelW &&
                      mouseY >= iy     && mouseY < iy + itemH);
        if (hover) outHoverIdx = i;

        // Hover background
        if (hover) {
            Color cHov = isDarkMode ? Color(255, 50, 52, 62) : Color(255, 235, 238, 244);
            SolidBrush brHov(cHov);
            g.FillRectangle(&brHov, (float)panelX, (float)iy, (float)panelW, (float)itemH);
        }

        // Bottom divider
        Pen penDiv(isDarkMode ? Color(255,45,46,56) : Color(255,232,234,238), 1.0f);
        g.DrawLine(&penDiv, panelX + S(14,dpi), iy + itemH - 1, panelX + panelW - S(14,dpi), iy + itemH - 1);

        // Favicon background circle
        Color cFavBg = isDarkMode ? Color(255, 55, 56, 68) : Color(255, 228, 232, 240);
        SolidBrush brFavBg(cFavBg);
        int fx = panelX + S(14, dpi);
        int fy = iy + (itemH - iconSz) / 2;
        g.FillEllipse(&brFavBg, (float)fx, (float)fy, (float)iconSz, (float)iconSz);

        // First letter of title as icon
        std::wstring letter = g_bookmarks[i].title.empty() ? L"?" :
                              g_bookmarks[i].title.substr(0, 1);
        Font fLetter(&ff, (float)S(14, dpi), FontStyleBold, UnitPixel);
        g.DrawString(letter.c_str(), -1, &fLetter,
            RectF((float)fx, (float)fy, (float)iconSz, (float)iconSz),
            &sfC, &brAccent);

        // Title
        int textX = panelX + S(14, dpi) + iconSz + S(10, dpi);
        int textW = panelW - S(14, dpi) - iconSz - S(10, dpi) - S(36, dpi);
        g.DrawString(g_bookmarks[i].title.c_str(), -1, &fNorm,
            RectF((float)textX, (float)iy, (float)textW, (float)(itemH / 2 + S(3,dpi))),
            &sfL, &brText);

        // URL (dimmed)
        g.DrawString(g_bookmarks[i].url.c_str(), -1, &fSmall,
            RectF((float)textX, (float)(iy + itemH / 2 - S(3,dpi)),
                  (float)textW, (float)(itemH / 2)),
            &sfL, &brDim);

        // Delete (X) button — only on hover
        if (hover) {
            int delX = panelX + panelW - S(32, dpi);
            int delY = iy + (itemH - S(24,dpi)) / 2;
            bool delHov = (mouseX >= delX && mouseX < delX + S(24,dpi) &&
                           mouseY >= delY && mouseY < delY + S(24,dpi));
            Color cDel = delHov ? Color(255, 220, 50, 50) : cDim;
            SolidBrush brDel(cDel);
            g.DrawString(L"\uE711", -1, &fIcon,
                RectF((float)delX, (float)iy, (float)S(32,dpi), (float)itemH),
                &sfC, &brDel);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CLICK HANDLERS
// ─────────────────────────────────────────────────────────────────────────────
bool HandleBookmarkPanelClick(
    int mouseX, int mouseY,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    int dpi,
    std::wstring& outUrl,
    int& outRemoveIdx
) {
    outUrl = L"";
    outRemoveIdx = -1;
    if (!g_bookmarkPanelOpen) return false;

    int panelW  = S(340, dpi);
    int panelX  = windowWidth - panelW;
    int panelY  = titleBarH + toolbarH;
    int headerH = S(52, dpi);

    // Close button (X in header)
    int closeX = panelX + panelW - S(36, dpi);
    if (mouseX >= closeX && mouseX < panelX + panelW &&
        mouseY >= panelY  && mouseY < panelY + headerH) {
        g_bookmarkPanelOpen = false;
        return true;
    }

    // Not in panel area
    if (mouseX < panelX) {
        g_bookmarkPanelOpen = false;
        return false;
    }

    // Item list
    int listY = panelY + headerH + S(34, dpi);
    int itemH = S(56, dpi);

    for (int i = 0; i < (int)g_bookmarks.size(); i++) {
        int iy = listY + i * itemH;
        if (iy + itemH > windowHeight) break;

        if (mouseY >= iy && mouseY < iy + itemH) {
            // Delete button area
            int delX = panelX + panelW - S(32, dpi);
            if (mouseX >= delX) {
                outRemoveIdx = i;
                return true;
            }
            // Navigate
            outUrl = g_bookmarks[i].url;
            return true;
        }
    }
    return true; // click consumed (inside panel)
}

std::wstring HandleBookmarkBarClick(
    int mouseX, int mouseY,
    int windowWidth,
    int bmkY, int bmkH,
    int dpi
) {
    if (mouseY < bmkY || mouseY >= bmkY + bmkH) return L"";

    int curX = S(8, dpi);
    int itemPadX = S(10, dpi);
    int iconW    = S(14, dpi);
    int gap      = S(4, dpi);

    for (int i = 0; i < (int)g_bookmarks.size(); i++) {
        std::wstring label = g_bookmarks[i].title;
        if (label.size() > 16) label = label.substr(0, 14) + L"..";
        int labelW = (int)(label.size() * S(7, dpi));
        int itemW  = iconW + gap + labelW + itemPadX * 2;
        if (curX + itemW > windowWidth - S(80, dpi)) break;

        if (mouseX >= curX && mouseX < curX + itemW)
            return g_bookmarks[i].url;

        curX += itemW + S(2, dpi);
    }
    return L"";
}