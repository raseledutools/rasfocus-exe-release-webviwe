# RasBrowser — Project Structure & Integration Guide

## ফাইল তালিকা

### পুরনো ফাইল (আগে থেকে আছে)
| ফাইল | কাজ |
|------|-----|
| `mini_browser.cpp` | মূল browser window, tab system, GDI+ UI drawing |
| `mini_browser.h` | LaunchMiniBrowser() declaration |
| `advanced_feature.cpp` | Download tracking, history save, AI inject script |
| `advanced_feature.h` | DownloadInfo struct, SetupAdvancedFeatures() |
| `feature_browser.cpp` | DrawFeatureBrowser() — feature list UI |
| `feature_browser.h` | DrawFeatureBrowser() declaration |
| `html_tools.h` | HTML utility functions |

### নতুন ফাইল (তুমি implement করবে)
| ফাইল | কাজ |
|------|-----|
| `bookmarks.h/.cpp` | Bookmark add/remove/show/save |
| `settings.h/.cpp` | Browser settings (search engine, homepage, etc.) |
| `find_in_page.h/.cpp` | Ctrl+F find bar |
| `context_menu.h/.cpp` | Right-click context menu |
| `history_panel.h/.cpp` | History panel UI (Ctrl+H) |
| `downloads_panel.h/.cpp` | Downloads panel UI (Ctrl+J) |
| `rasfocus_config.h/.cpp` | RasFocus AI filter settings |
| `resource.rc` | App icon, version info |
| `CMakeLists.txt` | Build system |
| `keyboard_shortcuts.cpp` | Integration guide (compile করবে না, reference শুধু) |

---

## Integration Steps

### Step 1: mini_browser.cpp এ includes যোগ করো
```cpp
#include "bookmarks.h"
#include "settings.h"
#include "find_in_page.h"
#include "context_menu.h"
#include "history_panel.h"
#include "downloads_panel.h"
```

### Step 2: keyboard_shortcuts.cpp দেখো
`keyboard_shortcuts.cpp` ফাইলটা একটা guide — এটা compile হবে না।
এটা দেখে `mini_browser.cpp` এ WM_KEYDOWN, WM_CHAR, WM_RBUTTONDOWN integrate করো।

### Step 3: MessageBox গুলো replace করো
`mini_browser.cpp` এর লাইন ১৬১৯–১৬৫২ এ MessageBox গুলো সরিয়ে
actual panel open করার code দাও।

### Step 4: DrawBrowserContent() এ panel drawing যোগ করো
সব existing drawing এর পরে নতুন panel draw calls যোগ করো।

### Step 5: LoadBookmarks() এবং LoadSettings() call করো
`WinMain()` বা browser init এ এগুলো call করো।

---

## Build করার নিয়ম

1. **Visual Studio 2022** install করো
2. **WebView2 SDK** download করো:
   https://www.nuget.org/packages/Microsoft.Web.WebView2
3. `CMakeLists.txt` এ `WEBVIEW2_SDK_PATH` ঠিক করো
4. Build:
   ```
   mkdir build && cd build
   cmake .. -DWEBVIEW2_SDK_PATH="C:\WebView2SDK"
   cmake --build . --config Release
   ```

---

## Data Files (Runtime এ তৈরি হবে)

| ফাইল | Location |
|------|----------|
| `bookmarks.txt` | `%LOCALAPPDATA%\RasBrowserData\` |
| `settings.ini` | `%LOCALAPPDATA%\RasBrowserData\` |
| `rasbrowser_history.txt` | `%LOCALAPPDATA%\RasBrowserData\` |
| `rasfocus_ai_data.txt` | exe এর পাশে |

---

## Keyboard Shortcuts (implement করার পরে)

| Shortcut | Action |
|----------|--------|
| Ctrl+T | New Tab |
| Ctrl+W | Close Tab |
| Ctrl+N | New Window |
| Ctrl+H | History Panel |
| Ctrl+J | Downloads Panel |
| Ctrl+D | Bookmark current page |
| Ctrl+B | Bookmarks Panel |
| Ctrl+F | Find in Page |
| Ctrl+R / F5 | Reload |
| Ctrl+Tab | Next Tab |
| Ctrl+1~9 | Switch Tab |
| Escape | Close all panels |
| F11 | Fullscreen |
