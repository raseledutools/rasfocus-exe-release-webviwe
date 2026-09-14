// settings.cpp — RasBrowser Settings Manager
// TODO: এখানে সব function implement করো

#include "settings.h"
#include <shlobj.h>
#include <fstream>
#include <gdiplus.h>

using namespace Gdiplus;

BrowserSettings g_settings;
bool g_settingsPanelOpen = false;

static std::wstring GetSettingsFilePath() {
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path);
    return std::wstring(path) + L"\\RasBrowserData\\settings.ini";
}

void LoadSettings() {
    // TODO: GetSettingsFilePath() থেকে পড়ো
    // Format (প্রতি লাইন): key=value
    // Example:
    //   homepage=LOCAL_NTP
    //   darkMode=0
    //   fontSize=16
    //   downloadPath=C:\Users\...\Downloads
}

void SaveSettings() {
    // TODO: g_settings এর সব field GetSettingsFilePath() তে লেখো
    // Format: key=value
}

std::wstring GetSettingsPageHTML(bool isDarkMode) {
    // TODO: একটা সুন্দর HTML settings page return করো
    // Chrome settings এর মতো — sections: Appearance, Search Engine, Downloads, Privacy
    // JavaScript থেকে C++ এ communicate করতে WebView2 PostWebMessage ব্যবহার করো

    std::wstring bg   = isDarkMode ? L"#1e1e24" : L"#f8fafc";
    std::wstring text = isDarkMode ? L"#ffffff"  : L"#323232";

    return L"<!DOCTYPE html><html><head><meta charset='utf-8'>"
           L"<title>RasBrowser Settings</title>"
           L"<style>body{margin:0;padding:40px;background:" + bg + L";color:" + text + L";"
           L"font-family:'Segoe UI',sans-serif;}"
           L"h1{font-size:28px;margin-bottom:30px;}"
           L"</style></head><body>"
           L"<h1>&#x2699; Settings</h1>"
           L"<!-- TODO: settings sections এখানে add করো -->"
           L"<p>Settings page — implement করো settings.cpp এ।</p>"
           L"</body></html>";
}

void DrawSettingsPanel(Graphics& g, int windowWidth, int windowHeight, bool isDarkMode, int dpi) {
    // TODO: optional GDI+ overlay — বা শুধু GetSettingsPageHTML() ব্যবহার করো
}
