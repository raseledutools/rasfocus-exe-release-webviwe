#pragma once
// history_panel.h — RasBrowser History Panel (Ctrl+H)
// TODO: implement DrawHistoryPanel, HandleHistoryPanelClick

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

struct HistoryItem {
    std::wstring timestamp; // "[2025-01-01 10:00:00]"
    std::wstring title;
    std::wstring url;
};

extern std::vector<HistoryItem> g_history;
extern bool g_historyPanelOpen;
extern int  g_historyHoverIdx;
extern int  g_historyScrollOffset; // scroll position (item index)

// history file থেকে load করো
void LoadHistory();

// History panel clear করো
void ClearHistory();

// GDI+ দিয়ে full-width history panel আঁকো
void DrawHistoryPanel(
    Gdiplus::Graphics& g,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY
);

// Click handle করো
// Returns clicked URL (empty = no navigation needed)
std::wstring HandleHistoryPanelClick(
    int mouseX, int mouseY,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    int dpi
);

// Scroll handle করো (mouse wheel)
void HandleHistoryScroll(int delta);
