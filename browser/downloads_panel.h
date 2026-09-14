#pragma once
// downloads_panel.h — RasBrowser Downloads Panel (Ctrl+J)
// advanced_feature.h এ DownloadInfo আছে — এখানে panel UI
// TODO: implement DrawDownloadsPanel, HandleDownloadsPanelClick

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include "advanced_feature.h" // DownloadInfo, g_downloads

extern bool g_downloadsPanelOpen;
extern int  g_downloadsHoverIdx;

// GDI+ দিয়ে downloads panel আঁকো (Chrome downloads page style)
void DrawDownloadsPanel(
    Gdiplus::Graphics& g,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY
);

// Click handle করো
// Returns: "open_file:<path>", "open_folder:<path>", "cancel", "clear_all", ""
std::wstring HandleDownloadsPanelClick(
    int mouseX, int mouseY,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    int dpi
);

// Download list clear করো (completed/failed গুলো)
void ClearCompletedDownloads();
