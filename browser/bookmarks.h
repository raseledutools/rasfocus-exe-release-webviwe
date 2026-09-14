#pragma once
// bookmarks.h — RasBrowser Bookmark Manager (Full Implementation)

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// DATA STRUCTURE
// ─────────────────────────────────────────────────────────────────────────────
struct BookmarkItem {
    std::wstring title;
    std::wstring url;
};

extern std::vector<BookmarkItem> g_bookmarks;
extern bool g_bookmarkPanelOpen;
extern int  g_bookmarkHoverIdx;
extern int  g_bookmarkBarHoverIdx;

// ─────────────────────────────────────────────────────────────────────────────
// CORE FUNCTIONS
// ─────────────────────────────────────────────────────────────────────────────
void LoadBookmarks();
void SaveBookmarks();
void AddBookmark(const std::wstring& title, const std::wstring& url);
void RemoveBookmark(int index);
bool IsBookmarked(const std::wstring& url);
void ToggleBookmark(const std::wstring& url, const std::wstring& title);

// ─────────────────────────────────────────────────────────────────────────────
// UI FUNCTIONS
// ─────────────────────────────────────────────────────────────────────────────

// Right-side bookmark panel আঁকো (Chrome bookmark sidebar style)
void DrawBookmarkPanel(
    Gdiplus::Graphics& g,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY,
    int& outHoverIdx
);

// Toolbar এর নিচে bookmark bar আঁকো (g_bookmarks থেকে dynamic)
void DrawBookmarkBar(
    Gdiplus::Graphics& g,
    int windowWidth,
    int bmkY, int bmkH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY,
    int& outHoverIdx
);

// Address bar এ bookmark star icon আঁকো
void DrawBookmarkStar(
    Gdiplus::Graphics& g,
    int starX, int starY, int starW, int starH,
    bool isBookmarked, bool isHover,
    bool isDarkMode, int dpi
);

// Side panel click handle
bool HandleBookmarkPanelClick(
    int mouseX, int mouseY,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    int dpi,
    std::wstring& outUrl,
    int& outRemoveIdx
);

// Bookmark bar click — returns clicked URL or ""
std::wstring HandleBookmarkBarClick(
    int mouseX, int mouseY,
    int windowWidth,
    int bmkY, int bmkH,
    int dpi
);