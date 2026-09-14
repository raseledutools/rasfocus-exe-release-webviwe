// keyboard_shortcuts.cpp — RasBrowser Keyboard Shortcuts
// এই file টা mini_browser.cpp এ WM_KEYDOWN এর মধ্যে integrate করতে হবে
// অথবা আলাদা file হিসেবে রাখতে পারো

// ─────────────────────────────────────────────────────────────────────────────
// HOW TO INTEGRATE: mini_browser.cpp এর BrowserWndProc() এ
// WM_KEYDOWN case এর মধ্যে নিচের code যোগ করো
// ─────────────────────────────────────────────────────────────────────────────

/*

// এই includes গুলো mini_browser.cpp এর top এ যোগ করো:
#include "bookmarks.h"
#include "settings.h"
#include "find_in_page.h"
#include "context_menu.h"
#include "history_panel.h"
#include "downloads_panel.h"

// ─────────────────────────────────────────────────────────────────────────────
// BrowserWndProc() এর switch(msg) এ এই cases যোগ করো:
// ─────────────────────────────────────────────────────────────────────────────

case WM_KEYDOWN: {
    if (!g_windows.count(hWnd)) break;
    auto& wd = g_windows[hWnd];
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

    if (ctrl) {
        switch (wParam) {
            case 'T':
                // Ctrl+T — New Tab
                AddTab(hWnd, L"LOCAL_NTP");
                return 0;

            case 'W':
                // Ctrl+W — Close current tab
                if (wd.tabs.size() > 1)
                    CloseTab(hWnd, wd.activeTab);
                else
                    DestroyWindow(hWnd);
                return 0;

            case 'N':
                // Ctrl+N — New Window
                LaunchMiniBrowser(L"LOCAL_NTP", L"New Window");
                return 0;

            case 'H':
                // Ctrl+H — History
                g_historyPanelOpen   = !g_historyPanelOpen;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                g_settingsPanelOpen  = false;
                if (g_historyPanelOpen) LoadHistory();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;

            case 'J':
                // Ctrl+J — Downloads
                g_downloadsPanelOpen = !g_downloadsPanelOpen;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;

            case 'D':
                // Ctrl+D — Bookmark current page
                if (auto* tab = wd.active()) {
                    ToggleBookmark(tab->url, tab->title);
                    InvalidateRect(hWnd, NULL, FALSE); // star icon update
                }
                return 0;

            case 'B':
                // Ctrl+B — Bookmarks panel
                g_bookmarkPanelOpen  = !g_bookmarkPanelOpen;
                g_historyPanelOpen   = false;
                g_downloadsPanelOpen = false;
                if (g_bookmarkPanelOpen) LoadBookmarks();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;

            case 'F':
                // Ctrl+F — Find in page
                if (g_findBarOpen) CloseFindBar();
                else OpenFindBar();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;

            case 'R':
                // Ctrl+R — Reload
                if (auto* tab = wd.active())
                    if (tab->webview) tab->webview->Reload();
                return 0;

            case VK_TAB:
                // Ctrl+Tab — Next tab
                if (!wd.tabs.empty()) {
                    int next = (wd.activeTab + 1) % (int)wd.tabs.size();
                    SwitchToTab(hWnd, next);
                }
                return 0;

            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8':
                // Ctrl+1~8 — Switch to tab
                {
                    int idx = (int)(wParam - '1');
                    if (idx < (int)wd.tabs.size()) SwitchToTab(hWnd, idx);
                }
                return 0;

            case '9':
                // Ctrl+9 — Switch to last tab
                SwitchToTab(hWnd, (int)wd.tabs.size() - 1);
                return 0;
        }
    }

    // Escape — সব panel বন্ধ করো
    if (wParam == VK_ESCAPE) {
        bool changed = false;
        if (g_findBarOpen)       { CloseFindBar();        changed = true; }
        if (g_contextMenuOpen)   { CloseContextMenu();    changed = true; }
        if (g_bookmarkPanelOpen) { g_bookmarkPanelOpen  = false; changed = true; }
        if (g_historyPanelOpen)  { g_historyPanelOpen   = false; changed = true; }
        if (g_downloadsPanelOpen){ g_downloadsPanelOpen = false; changed = true; }
        if (wd.isMenuOpen)       { wd.isMenuOpen        = false; changed = true; }
        if (changed) { InvalidateRect(hWnd, NULL, FALSE); return 0; }
    }

    // Find bar এ typing
    if (g_findBarOpen && wParam != VK_ESCAPE) {
        // Find bar keyboard handle এখানে করতে পারো
        // অথবা HWND Edit control ব্যবহার করো
        InvalidateRect(hWnd, NULL, FALSE);
    }

    // F5 — Reload
    if (wParam == VK_F5) {
        if (auto* tab = wd.active())
            if (tab->webview) tab->webview->Reload();
        return 0;
    }

    // F11 — Fullscreen toggle (আগে থেকেই আছে, যদি না থাকে এখানে add করো)
    if (wParam == VK_F11) {
        // FullscreenToggle(hWnd); // existing function call
    }

    break;
}

// ─────────────────────────────────────────────────────────────────────────────
// WM_CHAR case যোগ করো (find bar typing এর জন্য)
// ─────────────────────────────────────────────────────────────────────────────

case WM_CHAR: {
    if (g_findBarOpen) {
        wchar_t ch = (wchar_t)wParam;
        if (ch == L'\b') {           // Backspace
            FindBarBackspace();
        } else if (ch == L'\r') {    // Enter — next match
            if (auto* tab = g_windows[hWnd].active())
                ExecuteFind(tab->webview.Get(), true);
        } else if (ch >= L' ') {     // Printable character
            FindBarAddChar(ch);
            if (auto* tab = g_windows[hWnd].active())
                ExecuteFind(tab->webview.Get(), true);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    break;
}

// ─────────────────────────────────────────────────────────────────────────────
// WM_RBUTTONDOWN — Right-click context menu
// ─────────────────────────────────────────────────────────────────────────────

case WM_RBUTTONDOWN: {
    if (!g_windows.count(hWnd)) break;
    auto& wd = g_windows[hWnd];
    int x = GET_X_LPARAM(lParam);
    int y = GET_Y_LPARAM(lParam);

    // WebView area তে right-click হলেই দেখাও
    RECT wvr = GetWebViewRect(hWnd);
    if (x >= wvr.left && x <= wvr.right && y >= wvr.top && y <= wvr.bottom) {
        OpenContextMenu(x, y, false, false, false, L"");
        InvalidateRect(hWnd, NULL, FALSE);
    }
    break;
}

// ─────────────────────────────────────────────────────────────────────────────
// WM_MOUSEWHEEL — History panel scroll
// ─────────────────────────────────────────────────────────────────────────────

case WM_MOUSEWHEEL: {
    if (g_historyPanelOpen) {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        HandleHistoryScroll(delta);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    break;
}

// ─────────────────────────────────────────────────────────────────────────────
// WM_LBUTTONDOWN এ এই panel clicks যোগ করো (existing case এর ভেতরে):
// ─────────────────────────────────────────────────────────────────────────────

// Context menu click
if (g_contextMenuOpen) {
    std::wstring action = HandleContextMenuClick(x, y, dpi);
    InvalidateRect(hWnd, NULL, FALSE);
    if (action == L"back"    && wd.active() && wd.active()->webview) wd.active()->webview->GoBack();
    if (action == L"forward" && wd.active() && wd.active()->webview) wd.active()->webview->GoForward();
    if (action == L"reload"  && wd.active() && wd.active()->webview) wd.active()->webview->Reload();
    if (action == L"open_new_tab") AddTab(hWnd, L"LOCAL_NTP");
    // ... বাকি actions handle করো
    return 0;
}

// Bookmark panel click
if (g_bookmarkPanelOpen) {
    std::wstring url; int removeIdx;
    if (HandleBookmarkPanelClick(x, y, W, cr.bottom, TitleBarH(dpi), ToolbarH(dpi), dpi, url, removeIdx)) {
        if (!url.empty()) NavigateTo(hWnd, url);
        if (removeIdx >= 0) RemoveBookmark(removeIdx);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
}

// History panel click
if (g_historyPanelOpen) {
    std::wstring url = HandleHistoryPanelClick(x, y, W, cr.bottom, TitleBarH(dpi), ToolbarH(dpi), dpi);
    if (!url.empty()) NavigateTo(hWnd, url);
    InvalidateRect(hWnd, NULL, FALSE);
    return 0;
}

// Downloads panel click
if (g_downloadsPanelOpen) {
    std::wstring action = HandleDownloadsPanelClick(x, y, W, cr.bottom, TitleBarH(dpi), ToolbarH(dpi), dpi);
    if (action.substr(0, 10) == L"open_file:")
        ShellExecuteW(NULL, L"open", action.substr(10).c_str(), NULL, NULL, SW_SHOWNORMAL);
    InvalidateRect(hWnd, NULL, FALSE);
    return 0;
}

// Find bar close click
if (g_findBarOpen) {
    if (HandleFindBarClick(x, y, W, cr.bottom, dpi))
        InvalidateRect(hWnd, NULL, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3-dot MENU এর MessageBox গুলো replace করো:
// ─────────────────────────────────────────────────────────────────────────────

// আগের code (DELETE করো):
// else if (clickIdx == 3) MessageBoxW(hWnd, L"History...", ...);
// else if (clickIdx == 4) MessageBoxW(hWnd, L"Downloads...", ...);
// else if (clickIdx == 5) MessageBoxW(hWnd, L"Bookmarks...", ...);
// else if (clickIdx == 6) MessageBoxW(hWnd, L"Extensions...", ...);

// নতুন code (এটা দিয়ে replace করো):
// else if (clickIdx == 3) { g_historyPanelOpen = true; LoadHistory(); InvalidateRect(hWnd, NULL, FALSE); }
// else if (clickIdx == 4) { g_downloadsPanelOpen = true; InvalidateRect(hWnd, NULL, FALSE); }
// else if (clickIdx == 5) { g_bookmarkPanelOpen = true; LoadBookmarks(); InvalidateRect(hWnd, NULL, FALSE); }
// else if (clickIdx == 6) { AddTab(hWnd, L"edge://extensions/"); } // WebView2 extensions

// Profile button:
// if (wd.hProfile) { g_settingsPanelOpen = true; ... }
// Extensions button:
// if (wd.hExt) { AddTab(hWnd, L"edge://extensions/"); }

// ─────────────────────────────────────────────────────────────────────────────
// DrawBrowserContent() এ panel drawing যোগ করো (WM_PAINT এর ভেতরে):
// ─────────────────────────────────────────────────────────────────────────────

// সব existing drawing এর পরে এগুলো add করো:
// DrawBookmarkPanel(g, W, H, TitleBarH(dpi), ToolbarH(dpi), wd.isDarkMode, dpi, wd.bookmarkHoverIdx);
// DrawHistoryPanel(g, W, H, TitleBarH(dpi), ToolbarH(dpi), wd.isDarkMode, dpi, mouseX, mouseY);
// DrawDownloadsPanel(g, W, H, TitleBarH(dpi), ToolbarH(dpi), wd.isDarkMode, dpi, mouseX, mouseY);
// DrawFindBar(g, W, H, wd.isDarkMode, dpi);
// DrawContextMenu(g, W, H, wd.isDarkMode, dpi, mouseX, mouseY);

*/

// এই file টা শুধু reference guide — এটা directly compile হবে না
// mini_browser.cpp এ manually integrate করো
