// context_menu.cpp — RasBrowser Right-Click Context Menu
// TODO: এখানে সব function implement করো

#include "context_menu.h"
#include <gdiplus.h>

using namespace Gdiplus;

bool g_contextMenuOpen     = false;
int  g_contextMenuX        = 0;
int  g_contextMenuY        = 0;
int  g_contextMenuHoverIdx = -1;

// Active menu items (OpenContextMenu এ set হবে)
static std::vector<ContextMenuItem> s_menuItems;
static std::wstring s_activeLinkUrl = L"";

void OpenContextMenu(int x, int y, bool hasSelection, bool isOnImage, bool isOnLink, const std::wstring& linkUrl) {
    g_contextMenuOpen = true;
    g_contextMenuX    = x;
    g_contextMenuY    = y;
    s_activeLinkUrl   = linkUrl;
    g_contextMenuHoverIdx = -1;

    s_menuItems.clear();

    // সবসময় থাকবে
    s_menuItems.push_back({ L"\uE72B", L"Back" });
    s_menuItems.push_back({ L"\uE72A", L"Forward" });
    s_menuItems.push_back({ L"\uE72C", L"Reload" });
    s_menuItems.push_back({ L"", L"", true }); // separator

    // Link এ right-click হলে
    if (isOnLink) {
        s_menuItems.push_back({ L"\uE8A7", L"Open link in new tab" });
        s_menuItems.push_back({ L"\uE8C8", L"Copy link address" });
        s_menuItems.push_back({ L"", L"", true });
    }

    // Image এ right-click হলে
    if (isOnImage) {
        s_menuItems.push_back({ L"\uEB9F", L"Save image as..." });
        s_menuItems.push_back({ L"\uE8C8", L"Copy image" });
        s_menuItems.push_back({ L"", L"", true });
    }

    // Text select থাকলে
    if (hasSelection) {
        s_menuItems.push_back({ L"\uE8C8", L"Copy" });
        s_menuItems.push_back({ L"\uE721", L"Search Google for selection" });
        s_menuItems.push_back({ L"", L"", true });
    }

    // সবসময় নিচে
    s_menuItems.push_back({ L"\uE896", L"Save page as..." });
    s_menuItems.push_back({ L"\uE8AD", L"Print..." });
    s_menuItems.push_back({ L"", L"", true });
    s_menuItems.push_back({ L"\uEB91", L"Inspect element" });
}

void CloseContextMenu() {
    g_contextMenuOpen     = false;
    g_contextMenuHoverIdx = -1;
    s_menuItems.clear();
}

void DrawContextMenu(Graphics& g, int windowWidth, int windowHeight, bool isDarkMode, int dpi, int mouseX, int mouseY) {
    if (!g_contextMenuOpen || s_menuItems.empty()) return;

    // TODO: GDI+ দিয়ে mini_browser.cpp এর 3-dot menu এর মতো আঁকো
    // Width: ~220px, Item height: 32px, Separator: 8px
    // g_contextMenuX, g_contextMenuY থেকে শুরু করো
    // Window edge এ গেলে flip করো (উপরে বা বামে)

    float menuW   = 220.0f * dpi / 96.0f;
    float itemH   = 32.0f  * dpi / 96.0f;
    float sepH    = 8.0f   * dpi / 96.0f;
    float padding = 6.0f   * dpi / 96.0f;

    // Total height calculate
    float totalH = padding * 2;
    for (const auto& item : s_menuItems)
        totalH += item.isSeparator ? sepH : itemH;

    // Position (window edge check)
    float mX = (float)g_contextMenuX;
    float mY = (float)g_contextMenuY;
    if (mX + menuW > windowWidth  - 10) mX = windowWidth  - menuW - 10;
    if (mY + totalH > windowHeight - 10) mY = windowHeight - totalH - 10;

    // Background
    Color bgColor = isDarkMode ? Color(255, 41, 42, 45) : Color(255, 255, 255, 255);
    SolidBrush bgBrush(bgColor);
    Pen borderPen(isDarkMode ? Color(255,80,80,80) : Color(255,200,200,200), 1.0f);

    g.FillRectangle(&bgBrush, mX, mY, menuW, totalH);
    g.DrawRectangle(&borderPen, mX, mY, menuW, totalH);

    // TODO: প্রতিটা item আঁকো (icon + label, hover highlight)
    // mini_browser.cpp এর DrawBrowserContent() এর menu drawing দেখো reference হিসেবে
}

std::wstring HandleContextMenuClick(int mouseX, int mouseY, int dpi) {
    if (!g_contextMenuOpen) return L"";

    // TODO: কোন item এ click হয়েছে calculate করো, action return করো
    // Actions: "back", "forward", "reload", "open_new_tab",
    //          "copy_link", "save_image", "copy", "print", "inspect"

    CloseContextMenu();
    return L"";
}
