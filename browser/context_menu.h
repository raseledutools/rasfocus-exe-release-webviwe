#pragma once
// context_menu.h — RasBrowser Right-Click Context Menu
// TODO: implement DrawContextMenu and HandleContextMenuClick

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

struct ContextMenuItem {
    std::wstring icon;
    std::wstring label;
    bool isSeparator = false;
};

extern bool g_contextMenuOpen;
extern int  g_contextMenuX;
extern int  g_contextMenuY;
extern int  g_contextMenuHoverIdx;

// Right-click এ context menu খোলো
void OpenContextMenu(int x, int y, bool hasSelection, bool isOnImage, bool isOnLink, const std::wstring& linkUrl);

void CloseContextMenu();

// GDI+ দিয়ে context menu আঁকো
void DrawContextMenu(
    Gdiplus::Graphics& g,
    int windowWidth,
    int windowHeight,
    bool isDarkMode,
    int dpi,
    int mouseX, int mouseY
);

// Click handle করো
// Returns action string: "back", "forward", "reload", "save_image",
//                        "copy_link", "open_new_tab", "inspect", ""
std::wstring HandleContextMenuClick(int mouseX, int mouseY, int dpi);
