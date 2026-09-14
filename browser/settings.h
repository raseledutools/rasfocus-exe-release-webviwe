#pragma once
// settings.h — RasBrowser Settings Manager
// TODO: implement DrawSettingsPage, LoadSettings, SaveSettings

#include <windows.h>
#include <gdiplus.h>
#include <string>

struct BrowserSettings {
    std::wstring homepage        = L"LOCAL_NTP";
    std::wstring defaultSearch   = L"https://www.google.com/search?q=";
    bool         darkMode        = false;
    bool         showBookmarkBar = true;
    bool         blockAds        = false;
    int          fontSize        = 16;
    std::wstring downloadPath    = L""; // empty = default Downloads folder
};

extern BrowserSettings g_settings;
extern bool g_settingsPanelOpen;

void LoadSettings();
void SaveSettings();

// Settings পেজের HTML string return করে (WebView2 তে NavigateToString দিয়ে দেখাবে)
std::wstring GetSettingsPageHTML(bool isDarkMode);

// GDI+ দিয়ে settings overlay panel আঁকো (optional — HTML version prefer করো)
void DrawSettingsPanel(
    Gdiplus::Graphics& g,
    int windowWidth, int windowHeight,
    bool isDarkMode, int dpi
);
