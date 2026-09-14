#pragma once
// find_in_page.h — RasBrowser Find-in-Page Bar (Ctrl+F)
// TODO: implement draw and interaction functions

#include <windows.h>
#include <gdiplus.h>
#include <string>

extern bool         g_findBarOpen;
extern std::wstring g_findQuery;
extern int          g_findMatchCount;
extern int          g_findCurrentMatch;

// Find bar খোলা/বন্ধ করো
void OpenFindBar();
void CloseFindBar();

// Find bar এ character যোগ করো (keyboard input)
void FindBarAddChar(wchar_t ch);
void FindBarBackspace();

// WebView2 দিয়ে page এ search করো
// webview pointer পাঠাও — ICoreWebView2* cast করে ব্যবহার করো
void ExecuteFind(void* webview, bool forward = true);

// Find bar আঁকো (window এর নিচে, Chrome style)
void DrawFindBar(
    Gdiplus::Graphics& g,
    int windowWidth,
    int windowHeight,
    bool isDarkMode,
    int dpi
);

// Find bar click handle করো
// Returns true যদি close বাটনে click হয়
bool HandleFindBarClick(int mouseX, int mouseY, int windowWidth, int windowHeight, int dpi);
