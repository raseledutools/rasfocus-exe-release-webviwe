#ifndef MINI_BROWSER_H
#define MINI_BROWSER_H

#include <string>
#include <windows.h>
#include <wrl/client.h>
#include "WebView2.h"

// Shared WebView2 environment — defined in browser/mini_browser.cpp.
// Other modules (tab_rasgram, etc.) reference this via extern to reuse
// the already-running browser process instead of spawning a second one.
// MINI_BROWSER_IMPL is defined only in mini_browser.cpp's own translation unit.
#ifndef MINI_BROWSER_IMPL
extern Microsoft::WRL::ComPtr<ICoreWebView2Environment> g_sharedEnv;
#endif

// Open a full mini-browser window
void LaunchMiniBrowser(std::wstring url, std::wstring title);

// Embedded preview WebView (used by File Manager Plus)
void CreateEmbeddedPreviewWebView(HWND parentHwnd, RECT bounds, const std::wstring& url);
void UpdateEmbeddedPreviewBounds(RECT newBounds);
void DestroyEmbeddedPreview();

#endif
