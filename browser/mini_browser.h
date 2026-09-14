#ifndef MINI_BROWSER_H
#define MINI_BROWSER_H

#include <string>
#include <windows.h>

// ড্যাশবোর্ড বা অন্য যেকোনো ফাইল থেকে এই ফাংশনটি কল করা যাবে
void LaunchMiniBrowser(std::wstring url, std::wstring title);

// File Manager embedded preview panel — WebView2 controller for PDF/video/audio
// Renders PDF/video/audio inline in the file manager preview panel.
void CreateEmbeddedPreviewWebView(HWND parentHwnd, RECT bounds, const std::wstring& url);
void UpdateEmbeddedPreviewBounds(RECT newBounds);
void DestroyEmbeddedPreview();

#endif
