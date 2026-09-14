// mini_browser.cpp — RasBrowser | Premium UI, Smart Omnibox, Dynamic Bookmarks
#define MINI_BROWSER_IMPL   // tells mini_browser.h not to extern g_sharedEnv here
// REFACTORED: Per-Monitor v2 DPI, Chrome bezier tabs, double-buffering,
//             Smart Google Search, Dark Mode Toggle, App Branding.
// ADDED: Google Login Bypass, Pure Popup Mode, AI Filter Integration, Chrome 3-Dot Menu.
// FIXED: mY undeclared identifier, C4267 size_t conversions, C4244 type conversions,
//        C4996 deprecated functions, wchar_t->char string construction warnings.

#define _CRT_SECURE_NO_WARNINGS
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#define GDIPVER      0x0110

#include "mini_browser.h"
#include "html_tools.h"
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "bookmarks.h"
#include "settings.h"
#include "find_in_page.h"
#include "context_menu.h"
#include "history_panel.h"
#include "downloads_panel.h"
#include "extensions.h"
#include "advanced_feature.h"   // SetupAdvancedFeatures, SaveToHistory, g_downloads
#include "feature_browser.h"    // DrawFeatureBrowser
#include "browser_profiles.h"    // Chrome-style profile manager

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <wrl.h>
#include <shlobj.h>   
#include <shlwapi.h>  
#include <wininet.h>  // HINTERNET, InternetOpenA, InternetOpenUrlA
#include <process.h>  // _beginthread, _endthread

#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <functional>
#include <cassert>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Microsoft::WRL;
using namespace Gdiplus;

// ─────────────────────────────────────────────────────────────────────────────
// EXTERNAL GLOBALS
// ─────────────────────────────────────────────────────────────────────────────
extern bool  g_isPureViewerMode;
extern float g_scaleFactor;

// ─────────────────────────────────────────────────────────────────────────────
// YOUTUBE MODE GLOBALS  (3-dot menu toggle)
// g_youtubeDesktopMode = false → mobile (m.youtube.com, mobile UA) — DEFAULT
// g_youtubeDesktopMode = true  → desktop (www.youtube.com, desktop UA, no redirect)
// g_youtubeAdBlockEnabled      → network layer ad/tracker block toggle
// ─────────────────────────────────────────────────────────────────────────────
bool g_youtubeDesktopMode    = false; // false = mobile mode (default)
bool g_youtubeAdBlockEnabled = true;  // true  = ads blocked (default)
bool g_profilePanelOpen      = false; // Profile floating dropdown
bool g_forceSiteDark         = false; // true  = all websites force-dark (CSS invert)

// ── Profile Photo Bitmap (toolbar avatar) ─────────────────────────────────
static Gdiplus::Bitmap* g_profilePhotoBmp = nullptr; // decoded photo, nullptr = use letter
static std::wstring      g_profilePhotoLoadedUrl;     // which URL is currently loaded

// Google profile photo URL থেকে Bitmap download করে load করা
static void LoadProfilePhotoAsync(const std::wstring& url) {
    if (url.empty() || url == g_profilePhotoLoadedUrl) return;
    g_profilePhotoLoadedUrl = url; // duplicate fetch এড়াতে আগেই set করো

    struct Ctx { std::wstring url; };
    auto* ctx = new Ctx{ url };

    _beginthread([](void* arg) {
        auto* ctx = static_cast<Ctx*>(arg);
        std::string urlA(ctx->url.begin(), ctx->url.end());
        delete ctx;

        // WinINet দিয়ে image bytes download করো
        HINTERNET hNet = InternetOpenA("RasBrowser/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hNet) { _endthread(); return; }
        HINTERNET hUrl = InternetOpenUrlA(hNet, urlA.c_str(), NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
        if (!hUrl) { InternetCloseHandle(hNet); _endthread(); return; }

        std::vector<BYTE> data;
        char buf[8192]; DWORD rd = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &rd) && rd > 0)
            data.insert(data.end(), buf, buf + rd);
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hNet);

        if (data.empty()) { _endthread(); return; }

        // bytes থেকে IStream তৈরি করে Gdiplus::Bitmap load করো
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, data.size());
        if (!hMem) { _endthread(); return; }
        memcpy(GlobalLock(hMem), data.data(), data.size());
        GlobalUnlock(hMem);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) {
            GlobalFree(hMem); _endthread(); return;
        }

        auto* bmp = Gdiplus::Bitmap::FromStream(stream);
        stream->Release();

        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            delete g_profilePhotoBmp;
            g_profilePhotoBmp = bmp;
            // toolbar repaint
            HWND hw = FindWindowA("RasBrowserCore", nullptr);
            if (!hw) hw = FindWindowExA(NULL, NULL, "RasBrowserCore", NULL);
            if (hw) InvalidateRect(hw, NULL, FALSE);
        } else {
            delete bmp;
        }
        _endthread();
    }, 0, ctx);
}

// ── Profile Panel extended state ──────────────────────────────────────────
// Panel has 3 sub-views: MAIN, ACCOUNTS, ADD_PROFILE
enum class ProfilePanelView { MAIN, ACCOUNTS, ADD_PROFILE };
static ProfilePanelView g_profileView   = ProfilePanelView::MAIN;
static int  g_profilePanelHover         = -1;  // hovered action item
static int  g_profileAccountHover       = -1;  // hovered account row
static int  g_profileCardHover          = -1;  // hovered profile card (profile switcher)
static bool g_profileAddNameActive      = false; // typing new profile name
static std::wstring g_profileAddName    = L"";   // new profile name being typed

// ─────────────────────────────────────────────────────────────────────────────
// RESOURCE IDs
// ─────────────────────────────────────────────────────────────────────────────
#define IDI_APP_ICON    101
#define IDC_ADDRESS_BAR 1005

// ─────────────────────────────────────────────────────────────────────────────
// 1. DYNAMIC LOCAL NTP (APP THEMED NEW TAB PAGE)
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetLocalNTP_HTML(bool isDark) {
    // ── Chrome-style New Tab Page (Material You) ──────────────────────────────
    std::wstring bg        = isDark ? L"#202124" : L"#ffffff";
    std::wstring text      = isDark ? L"#e8eaed" : L"#202124";
    std::wstring subText   = isDark ? L"#9aa0a6" : L"#5f6368";
    std::wstring cardBg    = isDark ? L"#292a2d" : L"#f8f9fa";
    std::wstring cardHover = isDark ? L"#3c4043" : L"#f1f3f4";
    std::wstring border    = isDark ? L"#5f6368" : L"#dadce0";
    std::wstring inputBg   = isDark ? L"#303134" : L"#f1f3f4";
    std::wstring inputFocusBg = isDark ? L"#303134" : L"#ffffff";
    std::wstring shadow    = isDark ? L"0 2px 6px rgba(0,0,0,0.5)" : L"0 2px 6px rgba(60,64,67,0.16)";
    std::wstring shadowHov = isDark ? L"0 4px 12px rgba(0,0,0,0.6)" : L"0 4px 12px rgba(60,64,67,0.24)";

    return L"<!DOCTYPE html><html><head><meta charset='utf-8'>"
    L"<title>New Tab</title>"
    L"<link rel='preconnect' href='https://fonts.googleapis.com'>"
    L"<style>"
    L"*{box-sizing:border-box;margin:0;padding:0}"
    L"body{min-height:100vh;background:" + bg + L";color:" + text + L";"
    L"font-family:'Google Sans','Segoe UI',Roboto,Arial,sans-serif;"
    L"display:flex;flex-direction:column;align-items:center;"
    L"padding-top:clamp(60px,10vh,120px);}"
    // ── Google top-right bar (Gmail, Images, Apps, Profile) ──
    L".g-topbar{position:fixed;top:0;right:0;display:flex;align-items:center;"
    L"gap:2px;padding:8px 12px;z-index:999}"
    L".g-topbar a{font-size:13px;color:" + subText + L";text-decoration:none;"
    L"padding:6px 8px;border-radius:4px;transition:background .15s}"
    L".g-topbar a:hover{background:" + cardHover + L";color:" + text + L"}"
    L".g-apps-btn{width:36px;height:36px;border-radius:50%;border:none;"
    L"background:transparent;cursor:pointer;display:flex;align-items:center;"
    L"justify-content:center;transition:background .15s;padding:0}"
    L".g-apps-btn:hover{background:" + cardHover + L"}"
    L".g-apps-btn svg{width:20px;height:20px;fill:" + subText + L"}"
    L".g-avatar{width:32px;height:32px;border-radius:50%;"
    L"background:linear-gradient(135deg,#8b5cf6,#6d28d9);"
    L"display:flex;align-items:center;justify-content:center;"
    L"font-size:14px;font-weight:600;color:#fff;cursor:pointer;"
    L"margin-left:4px;border:2px solid transparent;transition:box-shadow .15s}"
    L".g-avatar:hover{box-shadow:0 0 0 3px " + border + L"}"
    // Apps dropdown
    L".g-apps-panel{display:none;position:fixed;top:52px;right:8px;"
    L"background:" + (isDark ? L"#292a2d" : L"#fff") + L";"
    L"border-radius:24px;padding:16px;width:340px;"
    L"box-shadow:0 8px 24px rgba(0,0,0," + (isDark ? L"0.5" : L"0.15") + L");"
    L"z-index:1000}"
    L".g-apps-panel.open{display:block}"
    L".g-apps-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:4px}"
    L".g-app-item{display:flex;flex-direction:column;align-items:center;"
    L"gap:6px;padding:12px 8px;border-radius:12px;text-decoration:none;"
    L"color:" + text + L";font-size:12px;font-weight:500;transition:background .1s}"
    L".g-app-item:hover{background:" + cardHover + L"}"
    L".g-app-icon{width:40px;height:40px;border-radius:50%;"
    L"display:flex;align-items:center;justify-content:center}"
    L".g-app-icon img{width:40px;height:40px;border-radius:8px}"
    // ── Google logo (SVG inline, exact Google colors) ──
    L".g-logo{margin-bottom:28px;line-height:1}"
    L".g-logo svg{height:92px;width:272px}"
    // ── Search box (pill, exact Chrome style) ──
    L".search-wrap{width:100%;max-width:584px;position:relative;margin-bottom:36px}"
    L".search-box{"
    L"width:100%;height:44px;padding:0 46px 0 52px;"
    L"font-size:16px;font-family:inherit;"
    L"border:1px solid " + border + L";"
    L"border-radius:24px;"
    L"background:" + inputBg + L";"
    L"color:" + text + L";"
    L"outline:none;"
    L"box-shadow:" + shadow + L";"
    L"transition:background .15s,box-shadow .15s,border-color .15s}"
    L".search-box:focus{"
    L"background:" + inputFocusBg + L";"
    L"border-color:transparent;"
    L"box-shadow:" + shadowHov + L"}"
    // search icon
    L".ico-search{position:absolute;left:16px;top:50%;transform:translateY(-50%);"
    L"width:20px;height:20px;fill:#9aa0a6;pointer-events:none}"
    // mic + camera icons
    L".ico-right{position:absolute;right:12px;top:50%;transform:translateY(-50%);"
    L"display:flex;gap:6px;align-items:center}"
    L".ico-right svg{width:20px;height:20px;fill:#9aa0a6;cursor:pointer;opacity:.7}"
    L".ico-right svg:hover{opacity:1}"
    // ── I'm Feeling Lucky ──
    L".search-btns{display:flex;gap:12px;justify-content:center;margin-bottom:44px}"
    L".search-btn{"
    L"padding:0 16px;height:36px;"
    L"border:1px solid " + border + L";"
    L"border-radius:4px;"
    L"background:" + cardBg + L";"
    L"color:" + text + L";"
    L"font-size:14px;cursor:pointer;"
    L"transition:background .1s,box-shadow .1s}"
    L".search-btn:hover{background:" + cardHover + L";box-shadow:" + shadow + L";border-color:" + border + L"}"
    // ── Shortcuts grid ──
    L".shortcuts{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;"
    L"width:100%;max-width:640px}"
    L".shortcut{"
    L"display:flex;flex-direction:column;align-items:center;"
    L"width:96px;height:90px;border-radius:16px;"
    L"text-decoration:none;color:" + text + L";"
    L"font-size:12.5px;font-weight:500;"
    L"cursor:pointer;transition:background .1s}"
    L".shortcut:hover{background:" + cardHover + L"}"
    L".shortcut-icon{"
    L"width:52px;height:52px;border-radius:50%;"
    L"background:" + cardBg + L";"
    L"display:flex;align-items:center;justify-content:center;"
    L"margin-bottom:8px;overflow:hidden;"
    L"box-shadow:" + shadow + L"}"
    L".shortcut-icon img{width:100%;height:100%;object-fit:contain;border-radius:0;display:block}"
    L".shortcut-icon span{font-size:22px}"
    L".shortcut-label{text-align:center;max-width:88px;"
    L"overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    // ── Customise button ──
    L".customise-btn{"
    L"margin-top:20px;display:flex;align-items:center;gap:8px;"
    L"padding:8px 16px;border-radius:20px;"
    L"border:1px solid " + border + L";"
    L"background:transparent;color:" + subText + L";"
    L"font-size:13px;cursor:pointer;"
    L"transition:background .1s}"
    L".customise-btn:hover{background:" + cardHover + L"}"
    L"</style></head><body>"
    // ── Google top-right bar ──
    L"<div class='g-topbar'>"
    L"<a href='https://mail.google.com' title='Gmail'>Gmail</a>"
    L"<a href='https://images.google.com' title='Google Images'>Images</a>"
    L"<button class='g-apps-btn' onclick='toggleApps()' title='Google apps'>"
    L"<svg viewBox='0 0 24 24'><path d='M6 8c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6 0c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6 0c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zM6 14c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6 0c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6 0c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zM6 20c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6 0c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6 0c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2z'/></svg>"
    L"</button>"
    L"<div class='g-avatar' title='Google Account'>R</div>"
    L"</div>"
    // Apps panel
    L"<div class='g-apps-panel' id='appsPanel'>"
    L"<div class='g-apps-grid'>"
    L"<a class='g-app-item' href='https://mail.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=mail.google.com&sz=64'></div>Gmail</a>"
    L"<a class='g-app-item' href='https://drive.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=drive.google.com&sz=64'></div>Drive</a>"
    L"<a class='g-app-item' href='https://calendar.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=calendar.google.com&sz=64'></div>Calendar</a>"
    L"<a class='g-app-item' href='https://translate.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=translate.google.com&sz=64'></div>Translate</a>"
    L"<a class='g-app-item' href='https://photos.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=photos.google.com&sz=64'></div>Photos</a>"
    L"<a class='g-app-item' href='https://docs.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=docs.google.com&sz=64'></div>Docs</a>"
    L"<a class='g-app-item' href='https://maps.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=maps.google.com&sz=64'></div>Maps</a>"
    L"<a class='g-app-item' href='https://meet.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=meet.google.com&sz=64'></div>Meet</a>"
    L"<a class='g-app-item' href='https://gemini.google.com'><div class='g-app-icon'><img src='https://www.google.com/s2/favicons?domain=gemini.google.com&sz=64'></div>Gemini</a>"
    L"</div></div>"
    // Google logo SVG
    L"<div class='g-logo'>"
    L"<svg viewBox='0 0 272 92' xmlns='http://www.w3.org/2000/svg'>"
    L"<path d='M115.75 47.18c0 12.77-9.99 22.18-22.25 22.18s-22.25-9.41-22.25-22.18C71.25 34.32 81.24 25 93.5 25s22.25 9.32 22.25 22.18zm-9.74 0c0-7.98-5.79-13.44-12.51-13.44S80.99 39.2 80.99 47.18c0 7.9 5.79 13.44 12.51 13.44s12.51-5.55 12.51-13.44z' fill='#EA4335'/>"
    L"<path d='M163.75 47.18c0 12.77-9.99 22.18-22.25 22.18s-22.25-9.41-22.25-22.18c0-12.85 9.99-22.18 22.25-22.18s22.25 9.32 22.25 22.18zm-9.74 0c0-7.98-5.79-13.44-12.51-13.44s-12.51 5.46-12.51 13.44c0 7.9 5.79 13.44 12.51 13.44s12.51-5.55 12.51-13.44z' fill='#FBBC05'/>"
    L"<path d='M209.75 26.34v39.82c0 16.38-9.66 23.07-21.08 23.07-10.75 0-17.22-7.19-19.67-13.07l8.48-3.53c1.51 3.61 5.21 7.87 11.17 7.87 7.31 0 11.84-4.51 11.84-13v-3.19h-.34c-2.18 2.69-6.38 5.04-11.68 5.04-11.09 0-21.25-9.66-21.25-22.09 0-12.52 10.16-22.26 21.25-22.26 5.29 0 9.49 2.35 11.68 4.96h.34v-3.61h9.26zm-8.56 20.92c0-7.81-5.21-13.52-11.84-13.52-6.72 0-12.35 5.71-12.35 13.52 0 7.73 5.63 13.36 12.35 13.36 6.63 0 11.84-5.63 11.84-13.36z' fill='#4285F4'/>"
    L"<path d='M225 3v65h-9.5V3h9.5z' fill='#34A853'/>"
    L"<path d='M262.02 54.48l7.56 5.04c-2.44 3.61-8.32 9.83-18.48 9.83-12.6 0-22.01-9.74-22.01-22.18 0-13.19 9.49-22.18 20.92-22.18 11.51 0 17.14 9.16 18.98 14.11l1.01 2.52-29.65 12.28c2.27 4.45 5.8 6.72 10.75 6.72 4.96 0 8.4-2.44 10.92-6.14zm-23.27-7.98l19.82-8.23c-1.09-2.77-4.37-4.70-8.23-4.70-4.95 0-11.84 4.37-11.59 12.93z' fill='#EA4335'/>"
    L"<path d='M35.29 41.41V32H67c.31 1.64.47 3.58.47 5.68 0 7.06-1.93 15.79-8.15 22.01-6.05 6.3-13.78 9.66-24.02 9.66C16.32 69.35.36 53.89.36 34.91.36 15.93 16.32.47 35.3.47c10.5 0 17.98 4.12 23.6 9.49l-6.64 6.64c-4.03-3.78-9.49-6.72-16.97-6.72-13.86 0-24.7 11.17-24.7 25.03 0 13.86 10.84 25.03 24.7 25.03 8.99 0 14.11-3.61 17.39-6.89 2.66-2.66 4.41-6.46 5.1-11.65H35.29z' fill='#4285F4'/>"
    L"</svg></div>"
    // Search form
    L"<div class='search-wrap'>"
    L"<svg class='ico-search' viewBox='0 0 24 24'><path d='M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z'/></svg>"
    L"<input type='text' id='q' class='search-box' placeholder='Search Google or type a URL' autocomplete='off' autofocus>"
    L"<div class='ico-right'>"
    L"<svg viewBox='0 0 24 24'><path d='M12 14c1.66 0 3-1.34 3-3V5c0-1.66-1.34-3-3-3S9 3.34 9 5v6c0 1.66 1.34 3 3 3zm-1-9c0-.55.45-1 1-1s1 .45 1 1v6c0 .55-.45 1-1 1s-1-.45-1-1V5zm6 6c0 2.76-2.24 5-5 5s-5-2.24-5-5H5c0 3.53 2.61 6.43 6 6.92V21h2v-3.08c3.39-.49 6-3.39 6-6.92h-2z'/></svg>"
    L"<svg viewBox='0 0 24 24'><path d='M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z'/></svg>"
    L"</div></div>"
    L"<div class='search-btns'>"
    L"<button class='search-btn' onclick='doSearch()'>Google Search</button>"
    L"<button class='search-btn' onclick='location.href=\"https://www.google.com/doodles\"'>I&#x2019;m Feeling Lucky</button>"
    L"</div>"
    // Shortcuts
    L"<div class='shortcuts'>"
    L"<a class='shortcut' href='https://www.youtube.com'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=youtube.com&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"&#127909;\"'></div><span class='shortcut-label'>YouTube</span></a>"
    L"<a class='shortcut' href='https://www.google.com'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=google.com&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"&#127758;\"'></div><span class='shortcut-label'>Google</span></a>"
    L"<a class='shortcut' href='https://www.facebook.com'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=facebook.com&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"f\"'></div><span class='shortcut-label'>Facebook</span></a>"
    L"<a class='shortcut' href='https://chatgpt.com'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=chatgpt.com&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"&#129302;\"'></div><span class='shortcut-label'>ChatGPT</span></a>"
    L"<a class='shortcut' href='https://github.com'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=github.com&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"&lt;/&gt;\"'></div><span class='shortcut-label'>GitHub</span></a>"
    L"<a class='shortcut' href='https://www.wikipedia.org'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=wikipedia.org&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"W\"'></div><span class='shortcut-label'>Wikipedia</span></a>"
    L"<a class='shortcut' href='https://gemini.google.com'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=gemini.google.com&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"&#10024;\"'></div><span class='shortcut-label'>Gemini</span></a>"
    L"<a class='shortcut' href='https://translate.google.com'><div class='shortcut-icon'><img src='https://www.google.com/s2/favicons?domain=translate.google.com&sz=64' onerror='this.style.display=\"none\";this.parentNode.innerHTML=\"&#127760;\"'></div><span class='shortcut-label'>Translate</span></a>"
    L"</div>"
    L"<script>"
    L"function doSearch(){"
    L"var q=document.getElementById('q').value.trim();"
    L"if(!q)return;"
    L"if(q.includes(' ')||!q.includes('.')){location.href='https://www.google.com/search?q='+encodeURIComponent(q);}"
    L"else{location.href=q.startsWith('http')?q:'https://'+q;}"
    L"}"
    L"document.getElementById('q').addEventListener('keydown',function(e){if(e.key==='Enter')doSearch();});"
    L"function toggleApps(){"
    L"var p=document.getElementById('appsPanel');"
    L"p.classList.toggle('open');"
    L"}"
    L"document.addEventListener('click',function(e){"
    L"var panel=document.getElementById('appsPanel');"
    L"if(!e.target.closest('.g-apps-btn')&&!e.target.closest('.g-apps-panel')){"
    L"panel.classList.remove('open');}"
    L"});"
    L"</script>"
    L"</body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. LOCAL HISTORY PAGE (Chrome-style rasbrowser://history)
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetLocalHistory_HTML(bool isDark) {
    LoadHistory(); // সর্বশেষ data লোড করো
    std::wstring bg      = isDark ? L"#202124" : L"#f8f9fa";
    std::wstring surface = isDark ? L"#292a2d" : L"#ffffff";
    std::wstring text    = isDark ? L"#e8eaed" : L"#202124";
    std::wstring sub     = isDark ? L"#9aa0a6" : L"#5f6368";
    std::wstring accent  = L"#00969f"; // RasBrowser teal
    std::wstring border  = isDark ? L"#3c4043" : L"#e0e0e0";
    std::wstring hover   = isDark ? L"#3c4043" : L"#f1f3f4";
    std::wstring danger  = L"#ea4335";

    // History items → JSON array for JS
    std::wstring itemsJson = L"[";
    for (size_t i = 0; i < g_history.size(); i++) {
        auto& h = g_history[i];
        // Escape quotes in strings
        auto esc = [](std::wstring s) {
            std::wstring r; r.reserve(s.size());
            for (auto c : s) {
                if (c == L'"')  r += L"\\\"";
                else if (c == L'\\') r += L"\\\\";
                else if (c == L'\n' || c == L'\r') r += L' ';
                else r += c;
            }
            return r;
        };
        if (i > 0) itemsJson += L",";
        itemsJson += L"{\"ts\":\"" + esc(h.timestamp) + L"\","
                     L"\"title\":\"" + esc(h.title.empty() ? h.url : h.title) + L"\","
                     L"\"url\":\"" + esc(h.url) + L"\"}";
    }
    itemsJson += L"]";

    std::wstring html =
        L"<!DOCTYPE html><html><head><meta charset='utf-8'>"
        L"<title>History</title>"
        L"<style>"
        L"*{box-sizing:border-box;margin:0;padding:0;font-family:'Segoe UI',Arial,sans-serif}"
        L"body{background:" + bg + L";color:" + text + L";min-height:100vh}"
        L".header{background:" + surface + L";border-bottom:1px solid " + border + L";"
        L"padding:20px 32px;position:sticky;top:0;z-index:100;display:flex;align-items:center;gap:16px}"
        L".header h1{font-size:22px;font-weight:600;color:" + text + L"}"
        L".header .icon{font-size:24px;color:" + accent + L"}"
        L".clear-btn{margin-left:auto;padding:8px 18px;background:transparent;border:1.5px solid " + danger + L";"
        L"color:" + danger + L";border-radius:6px;cursor:pointer;font-size:13px;font-weight:500}"
        L".clear-btn:hover{background:" + danger + L";color:#fff}"
        L".search-bar{padding:16px 32px;background:" + bg + L"}"
        L".search-bar input{width:100%;max-width:600px;padding:10px 16px;"
        L"border:1.5px solid " + border + L";border-radius:8px;font-size:14px;"
        L"background:" + surface + L";color:" + text + L";outline:none}"
        L".search-bar input:focus{border-color:" + accent + L"}"
        L".content{max-width:800px;margin:0 auto;padding:8px 32px 32px}"
        L".day-group{margin-bottom:8px}"
        L".day-label{font-size:12px;font-weight:600;color:" + sub + L";padding:12px 0 4px;text-transform:uppercase;letter-spacing:.5px}"
        L".item{display:flex;align-items:center;gap:12px;padding:10px 12px;border-radius:8px;cursor:pointer;transition:background .12s}"
        L".item:hover{background:" + hover + L"}"
        L".favicon{width:16px;height:16px;border-radius:3px;flex-shrink:0}"
        L".item-title{font-size:14px;color:" + accent + L";white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex:1}"
        L".item-title:hover{text-decoration:underline}"
        L".item-url{font-size:12px;color:" + sub + L";white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:240px}"
        L".item-time{font-size:12px;color:" + sub + L";flex-shrink:0;margin-left:auto}"
        L".del-btn{width:24px;height:24px;border:none;background:transparent;cursor:pointer;"
        L"color:" + sub + L";font-size:14px;border-radius:50%;display:flex;align-items:center;justify-content:center;flex-shrink:0}"
        L".del-btn:hover{background:" + danger + L";color:#fff}"
        L".empty{text-align:center;padding:60px 20px;color:" + sub + L"}"
        L".empty .big-icon{font-size:48px;margin-bottom:12px}"
        L"</style></head><body>"
        L"<div class='header'>"
        L"  <span class='icon'>📋</span>"
        L"  <h1>History</h1>"
        L"  <button class='clear-btn' onclick='clearAll()'>Clear all history</button>"
        L"</div>"
        L"<div class='search-bar'><input id='q' placeholder='Search history...' oninput='render()'></div>"
        L"<div class='content' id='list'></div>"
        L"<script>"
        L"const items=" + itemsJson + L";"
        L"const groups={};"
        L"items.forEach(h=>{"
        L"  const d=h.ts.replace(/\\[/,'').replace(/\\].*/,'').split(' ')[0]||'Unknown';"
        L"  if(!groups[d])groups[d]=[];"
        L"  groups[d].push(h);"
        L"});"
        L"function render(){"
        L"  const q=(document.getElementById('q').value||'').toLowerCase();"
        L"  const el=document.getElementById('list');"
        L"  const keys=Object.keys(groups).sort((a,b)=>b.localeCompare(a));"
        L"  let html='';"
        L"  let total=0;"
        L"  keys.forEach(day=>{"
        L"    const its=groups[day].filter(h=>!q||h.title.toLowerCase().includes(q)||h.url.toLowerCase().includes(q));"
        L"    if(!its.length)return;"
        L"    total+=its.length;"
        L"    html+=\"<div class='day-group'><div class='day-label'>\"+day+\"</div>\";"
        L"    its.forEach((h,i)=>{"
        L"      const time=h.ts.replace(/.*\\s/,'').replace(/\\]/,'');"
        L"      const fav='https://www.google.com/s2/favicons?sz=16&domain='+encodeURIComponent(h.url);"
        L"      html+=\"<div class='item' id='item_\"+h.url.replace(/[^a-z0-9]/gi,'_')+'_'+i+\"'\"+"
        L"            \" onclick=\\\"location.href='\"+h.url.replace(/'/g,\"&#39;\")+\"'\\\">"
        L"            <img class='favicon' src='\"+fav+\"' onerror=\\\"this.style.display='none'\\\">"
        L"            <div style='flex:1;overflow:hidden'>"
        L"              <div class='item-title'>\"+h.title+\"</div>"
        L"              <div class='item-url'>\"+h.url+\"</div>"
        L"            </div>"
        L"            <span class='item-time'>\"+time+\"</span>"
        L"            <button class='del-btn' title='Remove' onclick='event.stopPropagation();delItem(this)'>✕</button>"
        L"            </div>\";"
        L"    });"
        L"    html+=\"</div>\";"
        L"  });"
        L"  if(!total)html=\"<div class='empty'><div class='big-icon'>🕐</div><p>No history found.</p></div>\";"
        L"  el.innerHTML=html;"
        L"}"
        L"function clearAll(){"
        L"  if(confirm('Clear all browsing history?')){"
        L"    Object.keys(groups).forEach(k=>delete groups[k]);"
        L"    render();"
        L"    window.chrome&&window.chrome.webview&&window.chrome.webview.postMessage('CLEAR_HISTORY');"
        L"  }"
        L"}"
        L"function delItem(btn){"
        L"  const row=btn.closest('.item');"
        L"  const url=row.querySelector('.item-url').textContent;"
        L"  row.remove();"
        L"}"
        L"render();"
        L"</script></body></html>";
    return html;
}

// ─────────────────────────────────────────────────────────────────────────────
// 2b. LOCAL DOWNLOADS PAGE (Chrome-style rasbrowser://downloads)
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetLocalDownloads_HTML(bool isDark) {
    std::wstring bg      = isDark ? L"#202124" : L"#f8f9fa";
    std::wstring surface = isDark ? L"#292a2d" : L"#ffffff";
    std::wstring text    = isDark ? L"#e8eaed" : L"#202124";
    std::wstring sub     = isDark ? L"#9aa0a6" : L"#5f6368";
    std::wstring accent  = L"#00969f";
    std::wstring border  = isDark ? L"#3c4043" : L"#e0e0e0";
    std::wstring hover   = isDark ? L"#3c4043" : L"#f1f3f4";
    std::wstring green   = L"#34a853";
    std::wstring danger  = L"#ea4335";
    std::wstring orange  = L"#fbbc04";

    // Downloads → JSON
    std::wstring itemsJson = L"[";
    for (size_t i = 0; i < g_downloads.size(); i++) {
        auto& d = g_downloads[i];
        auto esc = [](std::wstring s) {
            std::wstring r;
            for (auto c : s) {
                if (c == L'"')  r += L"\\\"";
                else if (c == L'\\') r += L"\\\\";
                else if (c == L'\n' || c == L'\r') r += L' ';
                else r += c;
            }
            return r;
        };
        auto fmtSize = [](INT64 bytes) -> std::wstring {
            if (bytes <= 0) return L"Unknown size";
            wchar_t buf[32];
            if (bytes < 1024) { swprintf_s(buf, L"%lld B", bytes); }
            else if (bytes < 1024*1024) { swprintf_s(buf, L"%.1f KB", bytes/1024.0); }
            else if (bytes < 1024*1024*1024) { swprintf_s(buf, L"%.1f MB", bytes/(1024.0*1024.0)); }
            else { swprintf_s(buf, L"%.1f GB", bytes/(1024.0*1024.0*1024.0)); }
            return buf;
        };
        int pct = (d.totalBytes > 0) ? (int)(d.receivedBytes*100/d.totalBytes) : (d.isCompleted?100:0);
        std::wstring state = d.isCompleted ? L"done" : d.isInterrupted ? L"fail" : L"progress";
        if (i > 0) itemsJson += L",";
        itemsJson += L"{\"name\":\"" + esc(d.fileName) + L"\","
                     L"\"path\":\"" + esc(d.fullPath) + L"\","
                     L"\"size\":\"" + fmtSize(d.totalBytes) + L"\","
                     L"\"recv\":\"" + fmtSize(d.receivedBytes) + L"\","
                     L"\"pct\":" + std::to_wstring(pct) + L","
                     L"\"state\":\"" + state + L"\"}";
    }
    itemsJson += L"]";

    std::wstring html =
        L"<!DOCTYPE html><html><head><meta charset='utf-8'>"
        L"<title>Downloads</title>"
        L"<style>"
        L"*{box-sizing:border-box;margin:0;padding:0;font-family:'Segoe UI',Arial,sans-serif}"
        L"body{background:" + bg + L";color:" + text + L";min-height:100vh}"
        L".header{background:" + surface + L";border-bottom:1px solid " + border + L";"
        L"padding:20px 32px;position:sticky;top:0;z-index:100;display:flex;align-items:center;gap:16px}"
        L".header h1{font-size:22px;font-weight:600;color:" + text + L"}"
        L".header .icon{font-size:24px}"
        L".clear-btn{margin-left:auto;padding:8px 18px;background:transparent;border:1.5px solid " + sub + L";"
        L"color:" + sub + L";border-radius:6px;cursor:pointer;font-size:13px;font-weight:500}"
        L".clear-btn:hover{border-color:" + danger + L";color:" + danger + L"}"
        L".content{max-width:800px;margin:0 auto;padding:24px 32px}"
        L".item{background:" + surface + L";border:1px solid " + border + L";border-radius:10px;"
        L"padding:16px 20px;margin-bottom:12px;display:flex;gap:14px;align-items:flex-start}"
        L".file-icon{font-size:28px;flex-shrink:0;margin-top:2px}"
        L".info{flex:1;overflow:hidden}"
        L".fname{font-size:15px;font-weight:500;color:" + accent + L";cursor:pointer;"
        L"white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        L".fname:hover{text-decoration:underline}"
        L".meta{font-size:12px;color:" + sub + L";margin-top:3px}"
        L".bar-wrap{height:4px;background:" + border + L";border-radius:2px;margin-top:8px}"
        L".bar{height:4px;border-radius:2px;background:" + accent + L";transition:width .3s}"
        L".status-done{color:" + green + L";font-size:12px;font-weight:500}"
        L".status-fail{color:" + danger + L";font-size:12px;font-weight:500}"
        L".status-prog{color:" + orange + L";font-size:12px;font-weight:500}"
        L".actions{display:flex;gap:8px;margin-top:8px}"
        L".act-btn{padding:5px 12px;border:1.5px solid " + border + L";border-radius:5px;"
        L"cursor:pointer;font-size:12px;background:transparent;color:" + text + L"}"
        L".act-btn:hover{border-color:" + accent + L";color:" + accent + L"}"
        L".empty{text-align:center;padding:80px 20px;color:" + sub + L"}"
        L".empty .big-icon{font-size:56px;margin-bottom:14px}"
        L"</style></head><body>"
        L"<div class='header'>"
        L"  <span class='icon'>⬇️</span>"
        L"  <h1>Downloads</h1>"
        L"  <button class='clear-btn' onclick='clearDone()'>Clear completed</button>"
        L"</div>"
        L"<div class='content' id='list'></div>"
        L"<script>"
        L"const items=" + itemsJson + L";"
        L"function getIcon(name){"
        L"  const ext=(name.split('.').pop()||'').toLowerCase();"
        L"  const m={'pdf':'📄','zip':'🗜️','rar':'🗜️','7z':'🗜️','exe':'⚙️','msi':'⚙️',"
        L"           'mp4':'🎬','mkv':'🎬','avi':'🎬','mp3':'🎵','wav':'🎵','flac':'🎵',"
        L"           'jpg':'🖼️','jpeg':'🖼️','png':'🖼️','gif':'🖼️','webp':'🖼️',"
        L"           'doc':'📝','docx':'📝','xls':'📊','xlsx':'📊','ppt':'📑','pptx':'📑'};"
        L"  return m[ext]||'📦';"
        L"}"
        L"function render(){"
        L"  const el=document.getElementById('list');"
        L"  if(!items.length){"
        L"    el.innerHTML=\"<div class='empty'><div class='big-icon'>⬇️</div><p>No downloads yet.</p></div>\";"
        L"    return;"
        L"  }"
        L"  el.innerHTML=items.map((d,i)=>{"
        L"    const pct=d.pct||0;"
        L"    const prog=d.state==='progress'?\"<div class='bar-wrap'><div class='bar' style='width:\"+pct+\"%'></div></div>\":'';"
        L"    const status=d.state==='done'?\"<span class='status-done'>✓ Done</span>\":"
        L"                 d.state==='fail'?\"<span class='status-fail'>✕ Failed</span>\":"
        L"                 \"<span class='status-prog'>⬇ \"+pct+\"%</span>\";"
        L"    const acts=d.state==='done'"
        L"      ?\"<button class='act-btn' onclick='openFile(\"+i+\")'>Open file</button>\""
        L"        +\"<button class='act-btn' onclick='openFolder(\"+i+\")'>Show in folder</button>\""
        L"      :'';"
        L"    return \"<div class='item'><div class='file-icon'>\"+getIcon(d.name)+\"</div>\""
        L"          +\"<div class='info'><div class='fname' onclick='openFile(\"+i+\")' title='\"+d.path+\"'>\"+d.name+\"</div>\""
        L"          +\"<div class='meta'>\"+d.size+\" &bull; \"+status+\"</div>\""
        L"          +prog"
        L"          +\"<div class='actions'>\"+acts+\"</div></div></div>\";"
        L"  }).join('');"
        L"}"
        L"function openFile(i){"
        L"  window.chrome&&window.chrome.webview&&window.chrome.webview.postMessage('OPEN_FILE:'+items[i].path);"
        L"}"
        L"function openFolder(i){"
        L"  window.chrome&&window.chrome.webview&&window.chrome.webview.postMessage('OPEN_FOLDER:'+items[i].path);"
        L"}"
        L"function clearDone(){"
        L"  for(let i=items.length-1;i>=0;i--){if(items[i].state==='done')items.splice(i,1);}"
        L"  render();"
        L"}"
        L"render();"
        L"</script></body></html>";
    return html;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. DYNAMIC BLOCKED PAGE (MOTIVATIONAL QUOTES)
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetBlocked_HTML(bool isDark) {
    // ── Chrome-style error page + RasFocus motivation overlay ───────────────
    std::wstring bg      = isDark ? L"#202124" : L"#ffffff";
    std::wstring text    = isDark ? L"#e8eaed" : L"#202124";
    std::wstring subText = isDark ? L"#9aa0a6" : L"#5f6368";
    std::wstring cardBg  = isDark ? L"#292a2d" : L"#f8f9fa";
    std::wstring border  = isDark ? L"#5f6368" : L"#dadce0";
    std::wstring blue    = L"#1a73e8"; // Google Blue
    std::wstring red     = L"#d93025"; // Google Red

    return L"<!DOCTYPE html><html><head><meta charset='utf-8'>"
    L"<title>Blocked — RasFocus</title>"
    L"<style>"
    L"*{box-sizing:border-box;margin:0;padding:0}"
    L"body{min-height:100vh;display:flex;align-items:center;justify-content:center;"
    L"background:" + bg + L";color:" + text + L";"
    L"font-family:'Google Sans','Segoe UI',Roboto,Arial,sans-serif;padding:24px}"
    L".card{max-width:440px;width:100%;text-align:center}"
    // Shield icon (SVG, Google red)
    L".shield{width:72px;height:72px;margin:0 auto 20px;display:block}"
    L"h1{font-size:24px;font-weight:400;color:" + text + L";margin-bottom:10px}"
    L".url{font-size:14px;color:" + subText + L";margin-bottom:24px;word-break:break-all}"
    // Motivation card (teal accent, like Chrome's info box)
    L".mot{background:" + cardBg + L";border:1px solid " + border + L";"
    L"border-radius:8px;padding:20px 22px;margin-bottom:24px;text-align:left}"
    L".mot-label{font-size:11px;font-weight:600;letter-spacing:.8px;text-transform:uppercase;"
    L"color:" + blue + L";margin-bottom:10px}"
    L".mot-quote{font-size:15px;line-height:1.6;color:" + text + L";font-weight:500}"
    L".mot-attr{font-size:12px;color:" + subText + L";margin-top:8px}"
    // Buttons row (Chrome-style outlined + filled)
    L".btns{display:flex;gap:10px;justify-content:center}"
    L".btn{padding:0 22px;height:36px;border-radius:4px;font-size:14px;cursor:pointer;"
    L"font-family:inherit;transition:background .1s,box-shadow .1s}"
    L".btn-back{background:transparent;border:1px solid " + border + L";color:" + blue + L"}"
    L".btn-back:hover{background:rgba(26,115,232,.06)}"
    L".btn-report{background:" + blue + L";border:none;color:#fff}"
    L".btn-report:hover{background:#1557b0;box-shadow:0 1px 3px rgba(0,0,0,.3)}"
    L"</style></head><body><div class='card'>"
    // SVG Shield
    L"<svg class='shield' viewBox='0 0 72 72' fill='none' xmlns='http://www.w3.org/2000/svg'>"
    L"<path d='M36 6L12 16v18c0 16.6 10.3 32.1 24 36 13.7-3.9 24-19.4 24-36V16L36 6z' fill='#fce8e6'/>"
    L"<path d='M36 6L12 16v18c0 16.6 10.3 32.1 24 36 13.7-3.9 24-19.4 24-36V16L36 6z' stroke='#d93025' stroke-width='2' fill='none'/>"
    L"<text x='36' y='44' font-size='26' font-weight='700' fill='#d93025' text-anchor='middle' font-family='Arial'>!</text>"
    L"</svg>"
    L"<h1>Access blocked by RasFocus</h1>"
    L"<p class='url'>This site has been restricted to protect your focus and wellbeing.</p>"
    L"<div class='mot'>"
    L"<div class='mot-label'>✨ Motivation</div>"
    L"<div class='mot-quote' id='quote'></div>"
    L"<div class='mot-attr' id='attr'></div>"
    L"</div>"
    L"<div class='btns'>"
    L"<button class='btn btn-back' onclick='history.back()'>← Go back</button>"
    L"<button class='btn btn-report' onclick='window.close()'>Close tab</button>"
    L"</div>"
    L"</div>"
    L"<script>"
    L"const quotes=["
    L"{q:'\u099A\u09B0\u09BF\u09A4\u09CD\u09B0\u09B9\u09C0\u09A8\u09A4\u09BE\u09B0 \u099A\u09C7\u09AF\u09BC\u09C7 \u09AC\u09A1\u09BC \u09A6\u09BE\u09B0\u09BF\u09A6\u09CD\u09B0 \u09A8\u09C7\u0987\u0964',a:'\u2014 \u09B9\u09AF\u09B0\u09A4 \u0986\u09B2\u09C0 (\u09B0\u09BE\u0983)'},"
    L"{q:'Discipline is choosing between what you want now and what you want most.',a:'\u2014 Abraham Lincoln'},"
    L"{q:'Small disciplines repeated with consistency lead to great achievements.',a:'\u2014 John C. Maxwell'},"
    L"{q:'\u0995\u09CD\u09B7\u09A3\u09BF\u0995\u09C7\u09B0 \u0986\u09A8\u09A8\u09CD\u09A6\u09C7\u09B0 \u099C\u09A8\u09CD\u09AF \u09AD\u09AC\u09BF\u09B7\u09CD\u09AF\u09CE \u09A8\u09B7\u09CD\u099F \u0995\u09B0\u09CB \u09A8\u09BE\u0964',a:'\u2014 \u09B8\u0982\u0997\u09C3\u09B9\u09C0\u09A4'}"
    L"];"
    L"var p=quotes[Math.floor(Math.random()*quotes.length)];"
    L"document.getElementById('quote').textContent=p.q;"
    L"document.getElementById('attr').textContent=p.a;"
    L"</script></body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 🟢 AI IN-APP BLOCKING INJECTION SCRIPT GENERATOR
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetAiInjectScript(const std::wstring& currentUrl) {
    std::wifstream in(L"rasfocus_ai_data.txt");
    if (!in.is_open()) return L"";

    bool isAiEngineActive = false, cbAiImageBlur = false, cbFemaleDetectWeb = false, cbFemaleDetectVideo = false;
    int aiSensitivityIdx = 0;
    bool ytHideHome = false, ytHideShorts = false, ytHideComments = false, ytHideRecVideos = false;
    bool ytHideThumbnails = false, ytBlurThumbnails = false, ytHideSubs = false, ytHideExplore = false;
    bool ytHideTopBar = false, ytDisableEndCards = false, ytBlackWhiteMode = false, ytDisableAutoplay = false;
    bool ttHideExplore = false, ttHideLive = false, ttHideComments = false, ttHideSearch = false, ttBlackWhiteMode = false;
    bool igHideStories = false, igHideReels = false, igHideExplore = false, igHideComments = false;
    bool igHideSuggested = false, igBlackWhiteMode = false;

    in >> isAiEngineActive >> cbAiImageBlur >> cbFemaleDetectWeb >> cbFemaleDetectVideo >> aiSensitivityIdx;
    in >> ytHideHome >> ytHideShorts >> ytHideComments >> ytHideRecVideos >> ytHideThumbnails
       >> ytBlurThumbnails >> ytHideSubs >> ytHideExplore >> ytHideTopBar >> ytDisableEndCards
       >> ytBlackWhiteMode >> ytDisableAutoplay;
    in >> ttHideExplore >> ttHideLive >> ttHideComments >> ttHideSearch >> ttBlackWhiteMode;
    in >> igHideStories >> igHideReels >> igHideExplore >> igHideComments >> igHideSuggested >> igBlackWhiteMode;
    in.close();

    std::wstring css = L"";

    if (currentUrl.find(L"youtube.com") != std::wstring::npos) {
        if (ytHideHome)        css += L"ytd-browse[page-subtype='home'] { display: none !important; } ";
        if (ytHideShorts)      css += L"ytd-reel-shelf-renderer, ytd-rich-shelf-renderer[is-shorts], a[title='Shorts'], ytd-mini-guide-entry-renderer[aria-label='Shorts'] { display: none !important; } ";
        if (ytHideComments)    css += L"ytd-comments { display: none !important; } ";
        if (ytHideRecVideos)   css += L"ytd-watch-next-secondary-results-renderer { display: none !important; } ";
        if (ytHideThumbnails)  css += L"ytd-thumbnail { display: none !important; } ";
        if (ytBlurThumbnails)  css += L"ytd-thumbnail img { filter: blur(15px) !important; } ";
        if (ytHideSubs)        css += L"a[title='Subscriptions'], ytd-mini-guide-entry-renderer[aria-label='Subscriptions'] { display: none !important; } ";
        if (ytHideExplore)     css += L"ytd-guide-section-renderer:nth-child(3) { display: none !important; } ";
        if (ytHideTopBar)      css += L"ytd-masthead #masthead-container #logo-icon-container, ytd-masthead #start, ytd-masthead #end, ytd-masthead #buttons, ytd-masthead #masthead-container ytd-topbar-logo-renderer { display: none !important; } ytd-masthead #center { visibility: visible !important; display: flex !important; } #page-manager { margin-top: 0 !important; } ";
        if (ytDisableEndCards) css += L".ytp-ce-element { display: none !important; } ";
        if (ytBlackWhiteMode)  css += L"html { filter: grayscale(100%) !important; } ";
    } 
    else if (currentUrl.find(L"tiktok.com") != std::wstring::npos) {
        if (ttHideExplore)  css += L"[data-e2e='nav-explore'] { display: none !important; } ";
        if (ttHideLive)     css += L"[data-e2e='nav-live'] { display: none !important; } ";
        if (ttHideComments) css += L".comment-container, [data-e2e='comment-icon'] { display: none !important; } ";
        if (ttBlackWhiteMode) css += L"html { filter: grayscale(100%) !important; } ";
    } 
    else if (currentUrl.find(L"instagram.com") != std::wstring::npos) {
        if (igHideReels)    css += L"a[href*='/reels/'] { display: none !important; } ";
        if (igHideExplore)  css += L"a[href*='/explore/'] { display: none !important; } ";
        if (igBlackWhiteMode) css += L"html { filter: grayscale(100%) !important; } ";
    }

    if (css.empty()) return L"";

    std::wstring js = L"let style = document.createElement('style'); style.innerHTML = \"" + css + L"\"; document.head.appendChild(style);";
    return js;
}

// ─────────────────────────────────────────────────────────────────────────────
// DPI HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static inline int S(int px, UINT dpi) { return MulDiv(px, (int)dpi, 96); }
static inline float Sf(float px, UINT dpi) { return px * (float)dpi / 96.0f; }

static UINT GetWndDpi(HWND hWnd) {
    UINT dpi = GetDpiForWindow(hWnd);
    return dpi ? dpi : 96;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAYOUT CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────
// ── Chrome-accurate layout constants ─────────────────────────────────────────
static const int D_TITLEBAR_H  = 38;  // Chrome tab strip height (38px @96dpi)
static const int D_TOOLBAR_H   = 40;  // Chrome omnibox bar height
static const int D_BOOKMARK_H  = 32;  // Bookmark bar (shown on NTP only)

static const int D_TAB_W_MAX   = 240; // Chrome max tab width
static const int D_TAB_W_MIN   = 60;  // Chrome min tab width (collapsed)
static const int D_TAB_PAD     = 8;
static const int D_WIN_BTN_W   = 46;
static const int D_LOGO_W      = 128; // tighter branding area
static const int D_NEW_TAB_BTN = 28;

// ─────────────────────────────────────────────────────────────────────────────
// PER-TAB DATA
// ─────────────────────────────────────────────────────────────────────────────
struct TabData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    std::wstring title   = L"New Tab";
    std::wstring url     = L"LOCAL_NTP"; 
    bool         loading = false;
    bool         canBack = false;
    bool         canFwd  = false;
    // Favicon: GDI+ Bitmap (fetched via JavaScript inject)
    std::shared_ptr<Bitmap> favicon;
    // Loading spinner frame counter (0-7, incremented via timer)
    int          loadingFrame = 0;
    // Dark mode script ID — AddScriptToExecuteOnDocumentCreated এর ID,
    // toggle এর সময় remove + re-register করার জন্য
    std::wstring darkScriptId;
};

// ─────────────────────────────────────────────────────────────────────────────
// PER-WINDOW DATA
// ─────────────────────────────────────────────────────────────────────────────
struct BrowserWindowData {
    std::vector<TabData> tabs;
    int                  activeTab    = 0;
    bool                 isFullScreen = false;
    bool                 isDarkMode   = true; 
    WINDOWPLACEMENT      wpPrev       = { sizeof(WINDOWPLACEMENT) };
    HWND                 hAddressBar  = NULL;
    HFONT                hAddrFont    = NULL;

    bool hMin = false, hMax = false, hClose = false;
    bool hPin = false, hDark = false, hFocus = false;
    bool isPinned = false;
    bool isFocusMode = false; // header+tab লুকানো "native app" mode
    bool hBack = false, hFwd = false, hRel = false;
    
    // Right Icons
    bool hProfile = false, hExt = false, hMenu = false; 
    
    // 🟢 Menu State Tracking
    bool isMenuOpen    = false;
    int  hoverMenuIdx  = -1;

    int  hoverTabIndex = -1;
    bool hNewTab       = false;

    TabData* active() {
        if (activeTab >= 0 && activeTab < (int)tabs.size()) return &tabs[activeTab];
        return nullptr;
    }
};

static std::map<HWND, BrowserWindowData> g_windows;
ComPtr<ICoreWebView2Environment>  g_sharedEnv;

// ─────────────────────────────────────────────────────────────────────────────
// FIX: Helper to compute menu Y position consistently across all handlers
// This was the root cause of the 'mY undeclared identifier' errors at
// mini_browser.cpp(1461) and mini_browser.cpp(1477).
// ─────────────────────────────────────────────────────────────────────────────
static float GetMenuY(HWND hWnd, UINT dpi) {
    return (float)(S(D_TITLEBAR_H, dpi) + S(D_TOOLBAR_H, dpi) - S(5, dpi));
}

// Dynamic Total Header Height Calculation
static int NavTotalH(HWND hWnd) {
    UINT dpi = GetWndDpi(hWnd);
    if (g_isPureViewerMode) return S(D_TITLEBAR_H, dpi); 

    int h = S(D_TITLEBAR_H + D_TOOLBAR_H, dpi);
    if (g_windows.count(hWnd)) {
        auto* tab = g_windows[hWnd].active();
        if (tab && (tab->url == L"LOCAL_NTP" || tab->url == L"about:blank"))
            h += S(D_BOOKMARK_H, dpi); // bookmark bar শুধু home page এ দেখাবে
    }
    return h;
}

static int TitleBarH(UINT dpi) { return S(D_TITLEBAR_H, dpi); }
static int ToolbarH (UINT dpi) { return S(D_TOOLBAR_H,  dpi); }
static int WinBtnW  (UINT dpi) { return S(D_WIN_BTN_W,  dpi); }
static int LogoW    (UINT dpi) { return S(D_LOGO_W,     dpi); } 

// ─────────────────────────────────────────────────────────────────────────────
// URL ENCODER
// ─────────────────────────────────────────────────────────────────────────────
static std::string utf8_encode(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

static std::wstring UrlEncode(const std::wstring& text) {
    std::string utf8 = utf8_encode(text);
    std::wstringstream escaped;
    for (unsigned char c : utf8) {
        // Only plain ASCII letters/digits are safe to pass through unescaped.
        // NOTE: previously this used isalnum(c) directly, which is undefined
        // behavior in the MSVC CRT for byte values >= 0x80 (i.e. any non-ASCII
        // UTF-8 continuation byte). That made searches containing non-English
        // characters (Bangla, accented Latin, etc.) fail or behave randomly
        // depending on locale/build config — this is the "some searches
        // don't work" bug. Doing our own ASCII-only range check avoids
        // calling isalnum() with anything outside [0,127] entirely.
        bool isAsciiAlnum = (c >= '0' && c <= '9') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= 'a' && c <= 'z');
        if (isAsciiAlnum || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << (wchar_t)c;
        } else if (c == ' ') {
            escaped << L"+";
        } else {
            wchar_t buf[10];
            swprintf(buf, 10, L"%%%02X", c);
            escaped << buf;
        }
    }
    return escaped.str();
}

static void CreateDesktopShortcut() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    wchar_t desktopPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
        std::wstring linkPath = std::wstring(desktopPath) + L"\\RasBrowser.lnk";
        if (GetFileAttributesW(linkPath.c_str()) != INVALID_FILE_ATTRIBUTES) return;

        CoInitialize(NULL);
        IShellLinkW* psl;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl))) {
            psl->SetPath(exePath);
            psl->SetArguments(L"-minibrowser");
            psl->SetIconLocation(exePath, 0); 
            IPersistFile* ppf;
            if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf))) {
                ppf->Save(linkPath.c_str(), TRUE);
                ppf->Release();
            }
            psl->Release();
        }
        CoUninitialize();
    }
}

static void RegisterAppForDefaultBrowser() {}

// ═════════════════════════════════════════════════════════════════════════════
// RASFOCUS FULL CONTENT BLOCKER  (AdBlocker.kt → C++/WebView2 port)
//
// Layer 1 : NavigationStarting  → adult URL / keyword block  (main frame)
// Layer 2 : WebResourceRequested → ad / tracker sub-resource block
// Layer 3 : NavigationCompleted  → YouTube ad-prune JS + content scanner JS
//
// Mirrors AdBlocker.kt 1-to-1:
//   isAdultHost()       → IsAdultHost()
//   isAdOrTrackerUrl()  → IsAdOrTrackerUrl()
//   shouldBlock()       → called in add_WebResourceRequested handler
//   injectContentScanner() / YouTubeAdPruner.getJsInjectScript()
//                       → GetContentScannerScript() / GetYouTubeAdPrunerScript()
// ═════════════════════════════════════════════════════════════════════════════

// ─── Adult URL Keywords (= AdBlocker.kt ADULT_URL_KEYWORDS) ──────────────────
static const std::vector<std::wstring>& AdultKeywords() {
    static const std::vector<std::wstring> kBadWords = {
        L"porn", L"xxx", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34",
        L"milf", L"blowjob", L"boobs", L"pussy", L"escort", L"bdsm",
        L"fetish", L"erotica", L"dildo", L"webcam", L"camgirls",
        L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers",
        L"onlyfans", L"playboy", L"chaturbate", L"stripchat", L"eporner"
        // "sex","cock","dick","tits" → whole-word match নিচে ContainsBadWord()-এ
    };
    return kBadWords;
}

// ─── Adult Domains (= AdBlocker.kt ADULT_DOMAINS) ────────────────────────────
static const std::vector<std::wstring>& AdultDomains() {
    static const std::vector<std::wstring> kList = {
        L"pornhub.com", L"xvideos.com", L"xnxx.com", L"xhamster.com",
        L"brazzers.com", L"onlyfans.com", L"chaturbate.com", L"stripchat.com",
        L"eporner.com", L"redtube.com", L"youporn.com", L"spankbang.com",
        L"tnaflix.com", L"motherless.com", L"rule34.xxx", L"e-hentai.org",
        L"nhentai.net", L"hentaihaven.xxx", L"hentai2read.com", L"fakku.net",
        L"literotica.com", L"sexstories.com", L"adultfriendfinder.com",
        L"ashleymadison.com", L"bongacams.com", L"livejasmin.com",
        L"myfreecams.com", L"cam4.com", L"camsoda.com", L"flirt4free.com"
    };
    return kList;
}

// ─── Adult TLDs (= AdBlocker.kt ADULT_TLDS) ──────────────────────────────────
static const std::vector<std::wstring>& AdultTlds() {
    static const std::vector<std::wstring> kList = {
        L".xxx", L".adult", L".porn", L".sex"
    };
    return kList;
}

// ─── Ad Network Domains (= AdBlocker.kt AD_DOMAINS) ─────────────────────────
static const std::vector<std::wstring>& AdDomains() {
    static const std::vector<std::wstring> kList = {
        L"doubleclick.net", L"googlesyndication.com", L"adservice.google.com",
        L"googleadservices.com", L"pagead2.googlesyndication.com",
        L"tpc.googlesyndication.com", L"securepubads.g.doubleclick.net",
        L"stats.g.doubleclick.net", L"cm.g.doubleclick.net",
        L"ad.doubleclick.net", L"googleads.g.doubleclick.net",
        L"imasdk.googleapis.com", L"static.doubleclick.net",
        L"www.googleadservices.com", L"amazon-adsystem.com",
        L"adsystem.amazon.com", L"fls-na.amazon.com",
        L"an.facebook.com", L"connect.facebook.net",
        L"adnxs.com", L"ib.adnxs.com", L"secure.adnxs.com", L"acdn.adnxs.com",
        L"rubiconproject.com", L"pixel.rubiconproject.com",
        L"pubmatic.com", L"ads.pubmatic.com", L"simage2.pubmatic.com",
        L"openx.net", L"criteo.com", L"criteo.net", L"adsrvr.org",
        L"advertising.com", L"appnexus.com", L"bidswitch.net",
        L"casalemedia.com", L"indexexchange.com", L"lijit.com",
        L"sovrn.com", L"yieldmo.com", L"media.net",
        L"mathtag.com", L"pixel.mathtag.com", L"adsafeprotected.com",
        L"eyeota.net", L"moatads.com", L"pixel.moatads.com",
        L"taboola.com", L"cdn.taboola.com", L"trc.taboola.com",
        L"outbrain.com", L"revcontent.com", L"mgid.com", L"zergnet.com",
        L"adblade.com", L"ads.twitter.com", L"static.ads-twitter.com",
        L"analytics.twitter.com", L"bat.bing.com",
        L"hotjar.com", L"mouseflow.com", L"fullstory.com", L"logrocket.com",
        L"scorecardresearch.com", L"quantserve.com", L"semasio.net",
        L"exelate.com", L"bluekai.com", L"demdex.net", L"turn.com",
        L"agkn.com", L"segment.io", L"banner.siteimprove.com"
    };
    return kList;
}

// ─── Tracker Domains (= AdBlocker.kt TRACKER_DOMAINS) ───────────────────────
static const std::vector<std::wstring>& TrackerDomains() {
    static const std::vector<std::wstring> kList = {
        L"google-analytics.com", L"googletagmanager.com", L"googletagservices.com",
        L"analytics.google.com", L"ssl.google-analytics.com",
        L"www.google-analytics.com", L"stats.wp.com", L"pixel.wp.com",
        L"bat.bing.com", L"analytics.twitter.com", L"t.co",
        L"connect.facebook.net", L"graph.facebook.com",
        L"analytics.yahoo.com", L"beacon.yahoo.com",
        L"clicks.beap.bc.yahoo.com", L"piwik.org", L"matomo.org",
        L"statcounter.com", L"clicktale.net", L"clicktale.com",
        L"crazyegg.com", L"trackjs.com", L"raygun.io", L"bugsnag.com",
        L"newrelic.com", L"nr-data.net",
        L"amplitude.com", L"api.amplitude.com", L"cdn.amplitude.com",
        L"mixpanel.com", L"cdn4.mxpnl.com",
        L"segment.com", L"cdn.segment.com", L"api.segment.io",
        L"cdn.heapanalytics.com", L"heapanalytics.com",
        L"rollbar.com", L"sentry.io", L"ingest.sentry.io",
        L"browser.sentry-cdn.com", L"intercom.io", L"widget.intercom.io",
        L"nexus.ensighten.com"
    };
    return kList;
}

// ─── Shared host-extract helper ───────────────────────────────────────────────
// URL থেকে normalized lowercase host বের করে (www. ছাড়া)।
// AdBlocker.kt এর ExtractHost() + isAdultHost() এর host normalization এর মতো।
static std::wstring ExtractHost(const std::wstring& text) {
    std::wstring s = text;
    size_t schemePos = s.find(L"://");
    if (schemePos != std::wstring::npos) s = s.substr(schemePos + 3);
    size_t cut = s.find_first_of(L"/?#");
    if (cut != std::wstring::npos) s = s.substr(0, cut);
    // strip port
    size_t portPos = s.find(L':');
    if (portPos != std::wstring::npos) s = s.substr(0, portPos);
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    if (s.rfind(L"www.", 0) == 0) s = s.substr(4);
    return s;
}

// ─── Suffix domain match (= AdBlocker.kt HostMatchesDomain) ──────────────────
static bool HostMatchesDomain(const std::wstring& host, const std::wstring& domain) {
    if (host == domain) return true;
    if (host.size() > domain.size()) {
        size_t off = host.size() - domain.size();
        return host[off - 1] == L'.' && host.substr(off) == domain;
    }
    return false;
}

// ─── isAdultHost() port ───────────────────────────────────────────────────────
static bool IsAdultHost(const std::wstring& host) {
    // TLD চেক (AdBlocker.kt ADULT_TLDS)
    for (const auto& tld : AdultTlds())
        if (host.size() >= tld.size() &&
            host.compare(host.size() - tld.size(), tld.size(), tld) == 0)
            return true;
    // Domain চেক
    for (const auto& domain : AdultDomains())
        if (HostMatchesDomain(host, domain)) return true;
    return false;
}

// ─── Ad / Tracker host check (= AdBlocker.kt shouldBlock inner logic) ────────
static bool IsAdHost(const std::wstring& host) {
    for (const auto& d : AdDomains())
        if (HostMatchesDomain(host, d)) return true;
    return false;
}

static bool IsTrackerHost(const std::wstring& host) {
    for (const auto& d : TrackerDomains())
        // exact host বা proper subdomain — substring নয় (AdBlocker.kt FIX comment দ্রষ্টব্য)
        if (host == d || HostMatchesDomain(host, d)) return true;
    return false;
}

// ─── Whole-word keyword check ─────────────────────────────────────────────────
static bool ContainsBadWord(const std::wstring& lowerText, const std::wstring& kw) {
    size_t pos = 0;
    while ((pos = lowerText.find(kw, pos)) != std::wstring::npos) {
        bool leftOk  = (pos == 0) || !iswalnum(lowerText[pos - 1]);
        size_t end   = pos + kw.size();
        bool rightOk = (end >= lowerText.size()) || !iswalnum(lowerText[end]);
        if (leftOk && rightOk) return true;
        pos = end;
    }
    return false;
}

// ─── IsBlockedContent — NavigationStarting (main frame adult block) ───────────
// AdBlocker.kt: shouldBlockNavigation() + shouldBlock() main-frame adult path
bool IsBlockedContent(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    // keyword check
    for (const auto& kw : AdultKeywords())
        if (ContainsBadWord(lower, kw)) return true;

    // domain / TLD check
    if (IsAdultHost(ExtractHost(text))) return true;

    return false;
}

// ─── IsAdOrTrackerUrl — WebResourceRequested (sub-resource block) ─────────────
// AdBlocker.kt: shouldBlock() → AD_DOMAINS + TRACKER_DOMAINS path
static bool IsAdOrTrackerUrl(const std::wstring& url) {
    std::wstring host = ExtractHost(url);
    if (host.empty()) return false;
    if (IsAdHost(host))     return true;
    if (IsTrackerHost(host)) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// 🟢 M.YOUTUBE SYSTEM — desktop www.youtube.com কে জোর করে mobile
// m.youtube.com এ পাঠানো হয় + mobile UA বসানো হয়, কারণ YouTube-এর
// anti-adblock/SSAI detection desktop web player-এ অনেক বেশি aggressive।
// ─────────────────────────────────────────────────────────────────────────────
static const wchar_t* kMobileUA =
    L"Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/137.0.0.0 Mobile Safari/537.36";

static const wchar_t* kDesktopUA =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    L"AppleWebKit/537.36 (KHTML, like Gecko) "
    L"Chrome/137.0.0.0 Safari/537.36";

// youtube.com পরিবারের host কিনা (video CDN / thumbnail CDN সহ) — এগুলোর
// request-এও mobile UA পাঠানো দরকার, নাহলে Google পক্ষে UA/host mismatch ধরা পড়ে।
static bool IsYouTubeFamilyHost(const std::wstring& host) {
    static const std::vector<std::wstring> kHosts = {
        L"youtube.com", L"googlevideo.com", L"ytimg.com", L"ggpht.com"
    };
    for (const auto& d : kHosts)
        if (HostMatchesDomain(host, d)) return true;
    return false;
}

// শুধু main site (www.youtube.com / youtube.com) — m.youtube.com বা
// music.youtube.com বাদ, redirect loop এড়াতে।
// g_youtubeDesktopMode = true হলে redirect বন্ধ — desktop UI দেখাবে।
static bool NeedsMobileYouTubeRedirect(const std::wstring& host) {
    if (g_youtubeDesktopMode) return false; // Desktop mode: no m.youtube redirect
    return host == L"youtube.com" || host == L"www.youtube.com";
}

// "https://www.youtube.com/watch?v=xyz" → "https://m.youtube.com/watch?v=xyz"
// host অংশটুকু বাদ দিয়ে বাকিটা (scheme + path + query) অবিকৃত রাখা হয়।
static std::wstring RewriteToMobileYouTube(const std::wstring& url) {
    size_t schemeEnd = url.find(L"://");
    if (schemeEnd == std::wstring::npos) return url;
    size_t hostStart = schemeEnd + 3;
    size_t hostEnd = url.find_first_of(L"/?#", hostStart);
    if (hostEnd == std::wstring::npos) hostEnd = url.size();
    std::wstring rest = url.substr(hostEnd); // path + query + fragment (থাকলে)
    return url.substr(0, hostStart) + L"m.youtube.com" + rest;
}

// ─────────────────────────────────────────────────────────────────────────────
// YOUTUBE AD PRUNER JS  (= AdBlocker.kt YouTubeAdPruner.getJsInjectScript())
//
// uBlock Origin json-prune approach:
//   fetch() + XHR intercept → /youtubei/v1/player response এর adPlacements,
//   playerAds, adSlots ইত্যাদি field সরিয়ে দাও।
//   MutationObserver + setInterval → skip button auto-click (fallback)।
// ─────────────────────────────────────────────────────────────────────────────
static std::wstring GetYouTubeAdPrunerScript() {
    return
    L"(function(){"
    L"if(window.__rasAdPrunerInstalled)return;"
    L"window.__rasAdPrunerInstalled=true;"
    L"if(window.__rasAdBlockEnabled===false)return;"
    // ad fields — uBlock Origin json-prune এর exact list
    L"var AF=['adPlacements','playerAds','adSlots','adBreakHeartbeatParams',"
    L"'auxiliaryUi','adMessagingConfig','adVideoId'];"
    L"function prune(json){"
    L"  try{var o=JSON.parse(json);rm(o);return JSON.stringify(o);}catch(e){return json;}"
    L"}"
    L"function rm(o){"
    L"  if(!o||typeof o!=='object')return;"
    L"  AF.forEach(function(f){delete o[f];});"
    L"  if(o.playerResponse)AF.forEach(function(f){delete o.playerResponse[f];});"
    L"  Object.keys(o).forEach(function(k){"
    L"    var v=o[k];"
    L"    if(Array.isArray(v))v.forEach(function(i){rm(i);});"
    L"    else if(v&&typeof v==='object')rm(v);"
    L"  });"
    L"}"
    L"function isYT(url){"
    L"  return url&&(url.includes('/youtubei/v1/player')||"
    L"    url.includes('/youtubei/v1/next')||url.includes('/youtubei/v1/browse'));"
    L"}"
    // fetch() intercept
    L"var oF=window.fetch;"
    L"window.fetch=function(input,init){"
    L"  var url=typeof input==='string'?input:(input&&input.url)||'';"
    L"  return oF.call(this,input,init).then(function(r){"
    L"    if(!isYT(url))return r;"
    L"    return r.clone().text().then(function(t){"
    L"      return new Response(prune(t),{status:r.status,statusText:r.statusText,headers:r.headers});"
    L"    });"
    L"  });"
    L"};"
    // XHR intercept
    L"var oO=XMLHttpRequest.prototype.open;"
    L"XMLHttpRequest.prototype.open=function(m,url){"
    L"  this._rasUrl=url;return oO.apply(this,arguments);"
    L"};"
    L"var oS=XMLHttpRequest.prototype.send;"
    L"XMLHttpRequest.prototype.send=function(){"
    L"  if(isYT(this._rasUrl)){"
    L"    var x=this;"
    L"    var d=Object.getOwnPropertyDescriptor(XMLHttpRequest.prototype,'responseText');"
    L"    Object.defineProperty(x,'responseText',{"
    L"      get:function(){var t=d?d.get.call(x):'';return(x.readyState===4&&isYT(x._rasUrl))?prune(t):t;},"
    L"      configurable:true"
    L"    });"
    L"  }"
    L"  return oS.apply(this,arguments);"
    L"};"
    // Skip ad button + video fast-forward fallback
    L"function skipAds(){"
    L"  var s=document.querySelector('.ytp-skip-ad-button,.ytp-ad-skip-button,.ytp-ad-skip-button-modern');"
    L"  if(s){s.click();return;}"
    L"  var v=document.querySelector('video');"
    L"  var a=document.querySelector('.ad-showing,.ad-interrupting');"
    L"  if(v&&a&&!v.paused)v.currentTime=v.duration||9999;"
    L"}"
    L"var obs=new MutationObserver(function(){skipAds();});"
    L"if(document.body)obs.observe(document.body,{childList:true,subtree:true});"
    L"setInterval(skipAds,500);"
    L"console.log('[RasBrowser] YT ad pruner installed');"
    L"})();";
}

// ─────────────────────────────────────────────────────────────────────────────
// CONTENT SCANNER JS  (= AdBlocker.kt injectContentScanner())
//
// DOM text, image alt/src, meta rating scan করে adult content detect করলে
// full-screen block overlay দেখায়।
// AdBlocker.kt এর exact JS logic এর C++ wstring version।
// ─────────────────────────────────────────────────────────────────────────────
static std::wstring GetContentScannerScript() {
    // keyword array — AdultKeywords() থেকে JS array বানাও
    std::wstring kwArray = L"[";
    bool first = true;
    for (const auto& kw : AdultKeywords()) {
        if (!first) kwArray += L",";
        kwArray += L"'" + kw + L"'";
        first = false;
    }
    kwArray += L"]";

    return
    L"(function(){"
    L"var badWords=" + kwArray + L";"
    L"var QUOTES=["
    L"['\\u09A4\\u09CB\\u09AE\\u09BE\\u09B0 \\u09B8\\u09AE\\u09AF\\u09BC \\u09A4\\u09CB\\u09AE\\u09BE\\u09B0 \\u09B8\\u09AC\\u099A\\u09C7\\u09AF\\u09BC\\u09C7 \\u09AC\\u09DC \\u09B8\\u09AE\\u09CD\\u09AA\\u09A6\\u0964','Your time is your greatest asset.'],"
    L"['\\u09AB\\u09CB\\u0995\\u09BE\\u09B8 \\u09AE\\u09BE\\u09A8\\u09C7\\u0987 \\u09B8\\u09CD\\u09AC\\u09BE\\u09A7\\u09C0\\u09A8\\u09A4\\u09BE\\u0964','Focus is freedom.'],"
    L"['\\u09A4\\u09C1\\u09AE\\u09BF \\u09AF\\u09BE \\u09AC\\u09BE\\u09B0\\u09AC\\u09BE\\u09B0 \\u0995\\u09B0\\u09CB, \\u09A4\\u09C1\\u09AE\\u09BF \\u09A4\\u09BE\\u0987 \\u09B9\\u09AF\\u09BC\\u09C7 \\u0993\\u09A0\\u09CB\\u0964','You become what you repeatedly do.']"
    L"];"
    L"function execBlock(){"
    L"  window.stop();"
    L"  if(window.__rasBlockOverlayShown)return;"
    L"  window.__rasBlockOverlayShown=true;"
    L"  var pick=QUOTES[Math.floor(Math.random()*QUOTES.length)];"
    L"  var ov=document.createElement('div');"
    L"  ov.id='ras-block-overlay';"
    L"  ov.style.cssText='position:fixed;top:0;left:0;width:100%;height:100%;z-index:2147483647;"
    L"background:linear-gradient(160deg,#0f2027 0%,#203a43 45%,#2c5364 100%);"
    L"display:flex;align-items:center;justify-content:center;padding:24px;';"
    L"  ov.innerHTML="
    L"'<div style=\"width:100%;max-width:380px;text-align:center;\">'"
    L"+'<div style=\"font-size:58px;margin-bottom:14px;\">\\uD83D\\uDEE1\\uFE0F</div>'"
    L"+'<div style=\"color:#fff;font-size:21px;font-weight:700;margin-bottom:6px;\">RasFocus Safe Mode</div>'"
    L"+'<div style=\"color:rgba(255,255,255,0.55);font-size:12px;letter-spacing:1.5px;text-transform:uppercase;margin-bottom:22px;\">Content Blocked</div>'"
    L"+'<div style=\"background:rgba(255,255,255,0.08);border:1px solid rgba(255,255,255,0.12);border-radius:18px;padding:22px 20px;margin-bottom:22px;\">'"
    L"+'<div style=\"color:#7EE8C7;font-size:11px;font-weight:600;letter-spacing:1px;text-transform:uppercase;margin-bottom:10px;\">\\u2728 Motivation</div>'"
    L"+'<div style=\"color:#fff;font-size:16px;line-height:1.55;font-weight:600;margin-bottom:6px;\">'+pick[0]+'</div>'"
    L"+'<div style=\"color:rgba(255,255,255,0.6);font-size:12.5px;line-height:1.5;font-style:italic;\">'+pick[1]+'</div>'"
    L"+'</div>'"
    L"+'<button onclick=\"history.back()\" style=\"width:100%;padding:15px;border:none;border-radius:14px;"
    L"background:linear-gradient(135deg,#43e97b,#38f9d7);color:#0f2027;font-size:15px;font-weight:700;cursor:pointer;\">"
    L"\\u2190 Go Back</button>'"
    L"+'</div>';"
    L"  document.documentElement.appendChild(ov);"
    L"  if(document.body)document.body.style.overflow='hidden';"
    L"}"
    L"function check(){"
    L"  var metaR=document.querySelector('meta[name=\"rating\" i]');"
    L"  var metaRTA=document.querySelector('meta[name=\"RATING\" i]');"
    L"  if((metaR&&metaR.content.toLowerCase()==='adult')||"
    L"     (metaRTA&&metaRTA.content.includes('RTA-5042'))){execBlock();return;}"
    L"  var txt=(document.title+' '+(document.body?document.body.innerText.substring(0,5000):'')).toLowerCase();"
    L"  if(badWords.some(function(w){var r=new RegExp('\\\\b'+w+'\\\\b');return r.test(txt);})){execBlock();return;}"
    L"  var imgs=document.getElementsByTagName('img');"
    L"  var max=Math.min(imgs.length,100);"
    L"  for(var i=0;i<max;i++){"
    L"    var src=(imgs[i].src||'').toLowerCase();"
    L"    var alt=(imgs[i].alt||'').toLowerCase();"
    L"    if(badWords.some(function(w){return src.includes(w)||alt.includes(w);})){execBlock();return;}"
    L"  }"
    L"}"
    L"check();"
    L"if(!window.__rasScannerObs){"
    L"  window.__rasScannerObs=true;"
    L"  var ob=new MutationObserver(function(){check();});"
    L"  if(document.body)ob.observe(document.body,{childList:true,subtree:true});"
    L"}"
    L"})();";
}

// ─────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static int CalcTabWidth(int windowW, int tabCount, UINT dpi) {
    int winBtnArea = WinBtnW(dpi) * 6; 
    int available  = windowW - winBtnArea - LogoW(dpi) - S(D_NEW_TAB_BTN + 16, dpi);
    int w = (tabCount > 0) ? available / tabCount : S(D_TAB_W_MAX, dpi);
    return max(S(D_TAB_W_MIN, dpi), min(S(D_TAB_W_MAX, dpi), w));
}

static RECT GetTabRect(int windowW, int index, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = LogoW(dpi) + index * tw;
    RECT r = { x, S(8, dpi), x + tw, TitleBarH(dpi) };
    return r;
}

static RECT GetNewTabBtnRect(int windowW, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = LogoW(dpi) + tabCount * tw + S(4, dpi);
    int cy = S(8, dpi) + (TitleBarH(dpi) - S(8, dpi)) / 2;
    int sz = S(22, dpi);
    RECT r = { x, cy - sz/2, x + sz, cy + sz/2 };
    return r;
}

static RECT GetWebViewRect(HWND hWnd) {
    RECT b; GetClientRect(hWnd, &b);
    // Focus mode: WebView সম্পূর্ণ window নেয়, কোনো header নেই
    if (g_windows.count(hWnd) && g_windows[hWnd].isFocusMode) {
        return b; // top = 0, full window
    }
    b.top += NavTotalH(hWnd);
    // Menu open থাকলে right side এ 340px খালি রাখো — WebView2 GDI+ এর উপরে থাকে
    // তাই menu area তে WebView shrink করলেই menu দেখা যাবে
    if (g_windows.count(hWnd) && g_windows[hWnd].isMenuOpen) {
        UINT dpi = GetWndDpi(hWnd);
        int menuAreaW = S(340, dpi); // menuW(320) + margin(10) + extra(10)
        b.right -= menuAreaW;
        if (b.right < b.left + S(100, dpi)) b.right = b.left + S(100, dpi);
    }
    // Extension panel open থাকলেও right side shrink করো (panel width 320 + margin)
    if (g_extensionPanelOpen) {
        UINT dpi = GetWndDpi(hWnd);
        int extAreaW = S(340, dpi); // panelW(320) + margin(4) + extra(16)
        b.right -= extAreaW;
        if (b.right < b.left + S(100, dpi)) b.right = b.left + S(100, dpi);
    }
    // Profile panel open থাকলে right side shrink করো — WebView2 GDI+ এর উপরে থাকে,
    // তাই panel area তে WebView shrink না করলে profile dropdown নিচে চাপা পড়ে
    if (g_profilePanelOpen) {
        UINT dpi = GetWndDpi(hWnd);
        int profileAreaW = S(320, dpi); // pw(300) + margin(8) + extra(12)
        b.right -= profileAreaW;
        if (b.right < b.left + S(100, dpi)) b.right = b.left + S(100, dpi);
    }
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR POSITIONING & CURSOR FIX
// ─────────────────────────────────────────────────────────────────────────────
static void RepositionAddressBar(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (!wd.hAddressBar) return;

    if (g_isPureViewerMode || wd.isFullScreen || wd.isFocusMode) {
        ShowWindow(wd.hAddressBar, SW_HIDE);
        return;
    }

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int W = cr.right;

    int navBtnArea    = S(8 + 36*3 + 8, dpi);
    int rightIconArea = S(36*3 + 8,     dpi);
    int addrH         = S(32,           dpi); // Chrome omnibox 32px
    int toolY         = TitleBarH(dpi);
    int addrY         = toolY + (ToolbarH(dpi) - addrH) / 2;
    int addrX         = navBtnArea;
    int addrW         = W - navBtnArea - rightIconArea - S(8, dpi);

    // Edit control sits inside the pill, offset for lock icon + Gemini chip
    int leftDecorW  = S(32, dpi);  // lock icon width
    int rightDecorW = S(90, dpi);  // Gemini chip + padding

    int editH = S(20, dpi);
    int editY = addrY + (addrH - editH) / 2;

    ShowWindow(wd.hAddressBar, SW_SHOW);
    SetWindowPos(wd.hAddressBar, NULL,
        addrX + leftDecorW, editY, addrW - leftDecorW - rightDecorW, editH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (wd.hAddrFont) DeleteObject(wd.hAddrFont);
    wd.hAddrFont = CreateFontW(
        S(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessage(wd.hAddressBar, WM_SETFONT, (WPARAM)wd.hAddrFont, TRUE);
}

// Forward declarations
static void ToggleFocusMode(HWND hWnd);

// ─────────────────────────────────────────────────────────────────────────────
// FULLSCREEN TOGGLE
// ─────────────────────────────────────────────────────────────────────────────
void ToggleFullScreen(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    DWORD style = GetWindowLong(hWnd, GWL_STYLE);

    if (!wd.isFullScreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hWnd, &wd.wpPrev) &&
            GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
        {
            SetWindowLong(hWnd, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME));
            SetWindowPos(hWnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right  - mi.rcMonitor.left,
                (mi.rcMonitor.bottom - mi.rcMonitor.top) - 2, 
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            wd.isFullScreen = true;
        }
    } else {
        SetWindowLong(hWnd, GWL_STYLE, style | WS_CAPTION | WS_THICKFRAME);
        SetWindowPlacement(hWnd, &wd.wpPrev);
        SetWindowPos(hWnd, NULL, 0,0,0,0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        wd.isFullScreen = false;
    }

    RepositionAddressBar(hWnd);
    RECT wvr = GetWebViewRect(hWnd);
    if (wd.isFullScreen) { wvr.top = 0; }
    for (auto& tab : wd.tabs)
        if (tab.controller) tab.controller->put_Bounds(wvr);
    InvalidateRect(hWnd, NULL, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// ACCELERATOR KEY HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class AcceleratorHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
    HWND  m_hWnd;
    ULONG m_ref = 1;
public:
    AcceleratorHandler(HWND h) : m_hWnd(h) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler))
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
        if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            UINT vk; args->get_VirtualKey(&vk);
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

            // F11 — Fullscreen
            if (vk == VK_F11) { ToggleFullScreen(m_hWnd); args->put_Handled(TRUE); return S_OK; }

            // F5 / Ctrl+R — Reload (no forward-decl needed)
            if ((vk == VK_F5) || (ctrl && vk == 'R')) {
                if (g_windows.count(m_hWnd)) {
                    auto* tab = g_windows[m_hWnd].active();
                    if (tab && tab->webview) tab->webview->Reload();
                }
                args->put_Handled(TRUE); return S_OK;
            }

            // Escape — close panels / focus mode
            if (vk == VK_ESCAPE && g_windows.count(m_hWnd)) {
                auto& wdEsc = g_windows[m_hWnd];
                if (wdEsc.isFocusMode)   { ToggleFocusMode(m_hWnd);   args->put_Handled(TRUE); return S_OK; }
                if (wdEsc.isFullScreen)  { ToggleFullScreen(m_hWnd);  args->put_Handled(TRUE); return S_OK; }
                bool any = g_bookmarkPanelOpen || g_historyPanelOpen ||
                           g_downloadsPanelOpen || g_findBarOpen ||
                           g_contextMenuOpen   || g_extensionPanelOpen;
                if (any) {
                    g_bookmarkPanelOpen = g_historyPanelOpen = g_downloadsPanelOpen = false;
                    g_extensionPanelOpen = g_findBarOpen = g_contextMenuOpen = false;
                    InvalidateRect(m_hWnd, NULL, FALSE);
                    args->put_Handled(TRUE); return S_OK;
                }
            }

            if (ctrl) {
                // Ctrl+D — Bookmark (no AddTab/SwitchToTab needed)
                if (vk == 'D' && g_windows.count(m_hWnd)) {
                    auto* tab = g_windows[m_hWnd].active();
                    if (tab) { ToggleBookmark(tab->url, tab->title); InvalidateRect(m_hWnd, NULL, FALSE); }
                    args->put_Handled(TRUE); return S_OK;
                }
                // Ctrl+B — Bookmarks panel (no AddTab needed)
                if (vk == 'B') {
                    g_bookmarkPanelOpen  = !g_bookmarkPanelOpen;
                    g_historyPanelOpen   = false;
                    g_downloadsPanelOpen = false;
                    g_extensionPanelOpen = false;
                    if (g_bookmarkPanelOpen) LoadBookmarks();
                    InvalidateRect(m_hWnd, NULL, FALSE);
                    args->put_Handled(TRUE); return S_OK;
                }
                // Ctrl+F — Find in page (no AddTab needed)
                if (vk == 'F') {
                    if (g_findBarOpen) CloseFindBar(); else OpenFindBar();
                    InvalidateRect(m_hWnd, NULL, FALSE);
                    args->put_Handled(TRUE); return S_OK;
                }
                // Ctrl+T/W/N/H/J/E/Tab/1-9 — PostMessage to BrowserWndProc
                // (AddTab/CloseTab/SwitchToTab are forward-declared later in this file)
                if (vk == 'T' || vk == 'W' || vk == 'N' || vk == 'H' ||
                    vk == 'J' || vk == 'E' || vk == VK_TAB ||
                    (vk >= '1' && vk <= '9'))
                {
                    PostMessage(m_hWnd, WM_KEYDOWN, (WPARAM)vk, 0);
                    args->put_Handled(TRUE); return S_OK;
                }
            }
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR SUBCLASS 
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK AddrBarProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hWnd);
        if (!g_windows.count(hParent)) return 0;
        auto& wd = g_windows[hParent];
        auto* tab = wd.active();
        if (!tab || !tab->webview) return 0;

        wchar_t buf[2048]; GetWindowTextW(hWnd, buf, 2048);
        std::wstring input = buf;
        
        input.erase(0, input.find_first_not_of(L" \t"));
        input.erase(input.find_last_not_of(L" \t") + 1);

        if (input.empty()) return 0; 
        
        if (IsBlockedContent(input)) { 
            SetWindowTextW(hWnd, L"blocked by rasfocus"); 
            tab->url = L"blocked by rasfocus";
            tab->webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
            return 0; 
        }

        std::wstring url;
        if (input.find(L" ") != std::wstring::npos) {
            url = L"https://www.google.com/search?q=" + UrlEncode(input);
        } else if (input.find(L"http://") == 0 || input.find(L"https://") == 0) {
            url = input;
        } else if (input.find(L".") != std::wstring::npos) {
            url = L"https://" + input;
        } else {
            url = L"https://www.google.com/search?q=" + UrlEncode(input);
        }

        tab->webview->Navigate(url.c_str());
        return 0;
    }
    if (msg == WM_NCPAINT) return 0;
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// DOUBLE-BUFFERED PAINT HELPER
// ─────────────────────────────────────────────────────────────────────────────
static void DoubleBufferedPaint(HWND hWnd, HDC hdcReal, std::function<void(HDC, int, int)> drawFn) {
    RECT cr; GetClientRect(hWnd, &cr);
    int W = cr.right, H = cr.bottom;
    if (W <= 0 || H <= 0) return;

    HDC     hdcMem  = CreateCompatibleDC(hdcReal);
    HBITMAP hBmp    = CreateCompatibleBitmap(hdcReal, W, H);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    drawFn(hdcMem, W, H);
    BitBlt(hdcReal, 0, 0, W, H, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI+ HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    if (r <= 0.f) { path.AddRectangle(RectF(x,y,w,h)); return; }
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,   y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,   y+h-r*2,   r*2, r*2,   0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
}

static void BuildChromeTabPath(GraphicsPath& path, float x, float y, float w, float h, float cornerR) {
    float bl = x, br = x + w, top = y, bot = y + h;
    float notchW = cornerR * 1.6f;
    path.StartFigure();
    path.AddLine(bl, bot, bl + notchW, bot);
    path.AddBezier(bl + notchW, bot, bl + notchW * 0.5f, bot, bl + cornerR * 0.25f, bot - cornerR, bl + cornerR, top + cornerR);
    path.AddArc(bl + cornerR, top, cornerR * 2, cornerR * 2, 180, 90);
    path.AddLine(bl + cornerR * 3, top, br - cornerR * 3, top);
    path.AddArc(br - cornerR * 3, top, cornerR * 2, cornerR * 2, 270, 90);
    path.AddBezier(br - cornerR, top + cornerR, br - cornerR * 0.25f, bot - cornerR, br - notchW * 0.5f, bot, br - notchW, bot);
    path.AddLine(br - notchW, bot, br, bot);
    path.CloseFigure();
}

// ─────────────────────────────────────────────────────────────────────────────
// MENU ITEM TYPES SHARED TABLE
// Used consistently in DrawBrowserContent, WM_MOUSEMOVE, WM_LBUTTONDOWN
// ─────────────────────────────────────────────────────────────────────────────
// Menu item types: 2=profile header, 1=separator, 0=regular item
// Order matches menuItems vector in DrawBrowserContent:
// profile, sep, new-tab, new-win, sep, history, downloads, bookmarks, extensions,
// sep, yt-mode, yt-ads, sep, gemini, settings, exit
static const int kMenuTypes[] = { 2, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0 }; // 16 items total
static const int kMenuTypeCount = 16;

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE PANEL — Chrome-style Multi-Profile + Google Account Manager
//
//  MAIN view:
//    [ Avatar  Name  Email ]
//    [ Manage Google Account ]  → opens myaccount.google.com
//    [ Add Google Account   ]  → navigates to accounts.google.com/AddSession
//    ─ separator ─
//    [ + Add Profile        ]  → MAIN → ADD_PROFILE sub-view
//    [ All profiles → ]        → MAIN → ACCOUNTS sub-view (profile switcher)
//
//  ACCOUNTS view (profile list):
//    Each profile card: avatar + name + email
//    [ + Add new profile ]
//    [ ← Back ]
//
//  ADD_PROFILE view:
//    Text input for name
//    [ Create ] button
//    [ ← Back ]
// ─────────────────────────────────────────────────────────────────────────────

// ── Geometry constants ────────────────────────────────────────────────────
static const int kProfPanelW   = 300; // panel width (logical px)
static const int kProfHeaderH  = 110; // header area (avatar+name+email) height
static const int kProfItemH    =  44; // each menu item row height
static const int kProfCardH    =  60; // profile switcher card height

// ── GDI+ color helper ─────────────────────────────────────────────────────
static Gdiplus::Color CRtoGP(COLORREF cr, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

// ── Draw a filled rounded rectangle ──────────────────────────────────────
static void FillRoundRect(Gdiplus::Graphics& g, Gdiplus::Brush& br,
                           float x, float y, float w, float h, float r)
{
    using namespace Gdiplus;
    GraphicsPath path;
    path.AddArc(x,       y,       r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2, y,       r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2, y+h-r*2, r*2, r*2,   0, 90);
    path.AddArc(x,       y+h-r*2, r*2, r*2,  90, 90);
    path.CloseFigure();
    g.FillPath(&br, &path);
}

// ── Draw avatar circle with letter ────────────────────────────────────────
static void DrawAvatarCircle(Gdiplus::Graphics& g, float x, float y, float r,
                              COLORREF bgColor, wchar_t letter, float fontSize,
                              bool isDark)
{
    using namespace Gdiplus;
    (void)isDark;
    SolidBrush avBr(CRtoGP(bgColor));
    g.FillEllipse(&avBr, x, y, r, r);

    Font fLetter(L"Segoe UI", fontSize, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush wBr(Color(255,255,255,255));
    wchar_t str[2] = { letter, 0 };
    g.DrawString(str, -1, &fLetter, RectF(x, y, r, r), &sf, &wBr);
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE PANEL — MAIN VIEW
// ─────────────────────────────────────────────────────────────────────────────
static void DrawProfilePanel_Main(Gdiplus::Graphics& g, int W, int dpi,
                                   bool isDark, int mouseX, int mouseY)
{
    using namespace Gdiplus;
    auto S = [&](int v){ return v * dpi / 96; };

    BrowserProfile* prof = GetActiveProfile();
    int nAccounts = prof ? (int)prof->accounts.size() : 0;

    // ── Panel geometry — Chrome-style: header + accounts list + actions + footer ──
    int pw     = S(kProfPanelW);
    int panelX = W - pw - S(8);
    int panelY = S(36 + 36);   // titleBar + toolbar

    // Dynamic height: header(big avatar) + accounts + sep + 3 actions + sep + 2 footer
    int headerH   = S(130);                           // big avatar + name + email
    int acctRowH  = S(52);                            // each signed-in account row
    int acctsH    = nAccounts * acctRowH;
    int sepH      = S(1) + S(8);
    int actionH   = S(kProfItemH) * 3;               // Manage / Add account / Sign out
    int footerH   = S(kProfItemH) * 2 + S(8);        // Add profile + All profiles
    int ph        = headerH + acctsH + sepH + actionH + sepH + footerH;

    // ── Drop shadow ──
    for (int i = 3; i >= 1; i--) {
        SolidBrush sh(Color((BYTE)(12*i), 0,0,0));
        g.FillRectangle(&sh, (float)(panelX+i), (float)(panelY+i), (float)pw, (float)ph);
    }

    // ── Background + rounded top corners ──
    Color bgCol  = isDark ? Color(255,32,33,36)   : Color(255,255,255,255);
    Color sepCol = isDark ? Color(255,60,61,65)   : Color(255,218,220,224);
    Color txtCol = isDark ? Color(255,232,234,237): Color(255,32,33,36);
    Color dimCol = isDark ? Color(255,154,160,166): Color(255,95,99,104);
    Color hovCol = isDark ? Color(40,255,255,255) : Color(25,0,0,0);
    Color accCol = Color(255,26,115,232);   // Google blue
    Color grnCol = Color(255,52,168,83);    // Google green (active indicator)

    SolidBrush bgBr(bgCol);
    g.FillRectangle(&bgBr, (float)panelX, (float)panelY, (float)pw, (float)ph);
    Pen bPen(sepCol, 1.0f);
    g.DrawRectangle(&bPen, (float)panelX, (float)panelY, (float)(pw-1), (float)(ph-1));

    Font fIco  (L"Segoe MDL2 Assets",  (REAL)S(14), FontStyleRegular, UnitPixel);
    Font fName (L"Segoe UI Semibold",   (REAL)S(14), FontStyleBold,    UnitPixel);
    Font fSub  (L"Segoe UI",            (REAL)S(11), FontStyleRegular, UnitPixel);
    Font fItem (L"Segoe UI",            (REAL)S(13), FontStyleRegular, UnitPixel);
    SolidBrush txtBr(txtCol), dimBr(dimCol), accBr(accCol), grnBr(grnCol);
    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentNear);
    StringFormat sfCM; sfCM.SetAlignment(StringAlignmentCenter); sfCM.SetLineAlignment(StringAlignmentCenter);
    StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
    Pen sepPen(sepCol, 1.0f);

    // ── HEADER: large avatar + active account name + email ──
    wchar_t letter   = prof ? prof->avatarLetter : L'R';
    COLORREF avColor = prof ? prof->avatarColor  : RGB(26,115,232);
    std::wstring dispName  = prof ? prof->displayLabel() : L"Guest";
    std::wstring dispEmail = prof ? prof->primaryEmail()  : L"";

    int avR = S(50);
    int avX = panelX + (pw - avR) / 2;
    int avY = panelY + S(18);
    DrawAvatarCircle(g, (float)avX, (float)avY, (float)avR, avColor, letter, (float)S(23), isDark);

    // Small "Google" badge on avatar if signed in
    if (!dispEmail.empty()) {
        int gR = S(10);
        SolidBrush gBg(Color(255,255,255,255));
        g.FillEllipse(&gBg, (float)(avX+avR-gR*2+2), (float)(avY+avR-gR*2+2), (float)(gR*2-4), (float)(gR*2-4));
        SolidBrush gDot(grnCol);
        g.FillEllipse(&gDot, (float)(avX+avR-gR+2), (float)(avY+avR-gR+2), (float)(gR*2-4), (float)(gR*2-4));
    }

    int nameY  = avY + avR + S(8);
    int emailY = nameY + S(20);
    g.DrawString(dispName.c_str(), -1, &fName,
        RectF((float)panelX, (float)nameY, (float)pw, (float)S(22)), &sfC, &txtBr);
    g.DrawString(dispEmail.empty() ? L"Not signed in" : dispEmail.c_str(),
        -1, &fSub, RectF((float)panelX, (float)emailY, (float)pw, (float)S(18)), &sfC, &dimBr);

    // ── "Manage your Google Account" pill button ──
    int manageY = emailY + S(20);
    int pillW = pw - S(40), pillH = S(26);
    int pillX = panelX + S(20);
    FillRoundRect(g, bgBr, (float)pillX, (float)manageY, (float)pillW, (float)pillH, (float)S(13));
    bool hovManage = (mouseX >= pillX && mouseX < pillX+pillW &&
                      mouseY >= manageY && mouseY < manageY+pillH);
    Color pillBrd = hovManage ? accCol : sepCol;
    Pen pillPen(pillBrd, 1.0f);
    g.DrawRectangle(&pillPen, (float)pillX, (float)manageY, (float)(pillW-1), (float)(pillH-1));
    Font fPill(L"Segoe UI", (REAL)S(11), FontStyleRegular, UnitPixel);
    g.DrawString(L"Manage your Google Account", -1, &fPill,
        RectF((float)pillX, (float)manageY, (float)pillW, (float)pillH), &sfCM,
        hovManage ? &accBr : &txtBr);
    if (hovManage) g_profilePanelHover = 0;

    // ── SEPARATOR ──
    int curY = panelY + headerH;
    g.DrawLine(&sepPen, (float)(panelX+S(8)), (float)curY,
                         (float)(panelX+pw-S(8)), (float)curY);
    curY += S(4);

    // ── SIGNED-IN ACCOUNTS LIST (Chrome-style rows) ──
    g_profileAccountHover = -1;
    for (int ai = 0; ai < nAccounts; ai++) {
        auto& acc = prof->accounts[ai];
        bool isActive = (ai == prof->activeAccount);
        bool hovA = (mouseX >= panelX && mouseX < panelX+pw &&
                     mouseY >= curY   && mouseY < curY+acctRowH);
        if (hovA) {
            g_profileAccountHover = ai;
            SolidBrush hBr(hovCol);
            g.FillRectangle(&hBr, (float)panelX, (float)curY, (float)pw, (float)acctRowH);
        }

        // Mini avatar for each account
        int maR = S(32);
        int maX = panelX + S(12);
        int maY = curY + (acctRowH - maR) / 2;
        COLORREF maColor = ProfileAvatarColor(ai + 1);
        wchar_t maLetter = acc.email.empty() ? L'?' : towupper(acc.email[0]);
        DrawAvatarCircle(g, (float)maX, (float)maY, (float)maR, maColor, maLetter, (float)S(15), isDark);

        // Email + display name
        int txX = maX + maR + S(10);
        int txW = pw - (txX - panelX) - S(36);
        Font fAcctN(L"Segoe UI", (REAL)S(12), FontStyleBold,    UnitPixel);
        Font fAcctE(L"Segoe UI", (REAL)S(10), FontStyleRegular, UnitPixel);
        g.DrawString(acc.displayName.empty() ? acc.email.c_str() : acc.displayName.c_str(),
            -1, &fAcctN, RectF((float)txX, (float)(curY+S(8)), (float)txW, (float)S(18)), &sfL, &txtBr);
        g.DrawString(acc.email.c_str(), -1, &fAcctE,
            RectF((float)txX, (float)(curY+S(27)), (float)txW, (float)S(16)), &sfL, &dimBr);

        // Active checkmark (Google blue tick)
        if (isActive) {
            SolidBrush chkBr(accCol);
            g.DrawString(L"\uE73E", -1, &fIco,
                RectF((float)(panelX+pw-S(30)), (float)curY,
                      (float)S(22), (float)acctRowH), &sfL, &chkBr);
        }
        curY += acctRowH;
    }

    // ── SEPARATOR before actions ──
    g.DrawLine(&sepPen, (float)(panelX+S(8)), (float)curY,
                         (float)(panelX+pw-S(8)), (float)curY);
    int actionStartY = curY + S(4);

    // ── ACTION ITEMS: Add account / Sign out ──
    struct ActionItem { const wchar_t* ico; const wchar_t* label; int id; bool danger; };
    static const ActionItem kActions[] = {
        { L"\uE8D4", L"Add Google Account",       20, false },
        { L"\uE894", L"Sign out of all accounts", 21, true  },
    };
    static const int kActionCount = 2;

    g_profilePanelHover = hovManage ? 0 : -1;
    for (int i = 0; i < kActionCount; i++) {
        int iy = actionStartY + i * S(kProfItemH);
        bool hov = (mouseX >= panelX && mouseX < panelX+pw &&
                    mouseY >= iy     && mouseY < iy+S(kProfItemH));
        if (hov) {
            g_profilePanelHover = kActions[i].id;
            SolidBrush hBr(hovCol);
            g.FillRectangle(&hBr, (float)panelX, (float)iy, (float)pw, (float)S(kProfItemH));
        }
        Color icoC = kActions[i].danger ? Color(255,220,50,50) : dimCol;
        SolidBrush icoBr(icoC), lblBr(kActions[i].danger ? Color(255,220,50,50) : txtCol);
        g.DrawString(kActions[i].ico, -1, &fIco,
            RectF((float)(panelX+S(16)), (float)iy, (float)S(24), (float)S(kProfItemH)), &sfL, &icoBr);
        g.DrawString(kActions[i].label, -1, &fItem,
            RectF((float)(panelX+S(48)), (float)iy, (float)(pw-S(56)), (float)S(kProfItemH)), &sfL, &lblBr);
    }

    // ── SEPARATOR before footer ──
    int sep2Y = actionStartY + kActionCount * S(kProfItemH) + S(4);
    g.DrawLine(&sepPen, (float)(panelX+S(8)), (float)sep2Y,
                         (float)(panelX+pw-S(8)), (float)sep2Y);

    // ── FOOTER: Add new profile + All profiles ──
    int addProfY = sep2Y + S(6);
    bool hovAdd = (mouseX >= panelX && mouseX < panelX+pw &&
                   mouseY >= addProfY && mouseY < addProfY + S(kProfItemH));
    if (hovAdd) {
        g_profilePanelHover = 10;
        SolidBrush hBr(hovCol);
        g.FillRectangle(&hBr, (float)panelX, (float)addProfY, (float)pw, (float)S(kProfItemH));
    }
    g.DrawString(L"\uE8FA", -1, &fIco,
        RectF((float)(panelX+S(16)), (float)addProfY, (float)S(24), (float)S(kProfItemH)), &sfL, &accBr);
    g.DrawString(L"Add new profile", -1, &fItem,
        RectF((float)(panelX+S(48)), (float)addProfY, (float)(pw-S(56)), (float)S(kProfItemH)), &sfL, &accBr);

    int allProfY = addProfY + S(kProfItemH);
    bool hovAll = (mouseX >= panelX && mouseX < panelX+pw &&
                   mouseY >= allProfY && mouseY < allProfY + S(kProfItemH));
    if (hovAll) {
        g_profilePanelHover = 11;
        SolidBrush hBr(hovCol);
        g.FillRectangle(&hBr, (float)panelX, (float)allProfY, (float)pw, (float)S(kProfItemH));
    }
    SolidBrush dimBr2(dimCol);
    std::wstring profLabel = L"All profiles  (" + std::to_wstring((int)g_profiles.size()) + L")  \u203A";
    g.DrawString(L"\uE716", -1, &fIco,
        RectF((float)(panelX+S(16)), (float)allProfY, (float)S(24), (float)S(kProfItemH)), &sfL, &dimBr2);
    g.DrawString(profLabel.c_str(), -1, &fItem,
        RectF((float)(panelX+S(48)), (float)allProfY, (float)(pw-S(56)), (float)S(kProfItemH)), &sfL, &txtBr);
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE PANEL — ACCOUNTS/SWITCHER VIEW
// ─────────────────────────────────────────────────────────────────────────────
static void DrawProfilePanel_Accounts(Gdiplus::Graphics& g, int W, int dpi,
                                       bool isDark, int mouseX, int mouseY)
{
    using namespace Gdiplus;
    auto S = [&](int v){ return v * dpi / 96; };

    int pw     = S(kProfPanelW);
    int panelX = W - pw - S(8);
    int panelY = S(36 + 36);

    int nProf = (int)g_profiles.size();
    int backH = S(kProfItemH);
    int ph    = S(44) + nProf * S(kProfCardH) + S(8) + S(kProfItemH) + S(4) + backH + S(8);

    // Shadow + bg + border
    for (int i = 3; i >= 1; i--) {
        SolidBrush sh(Color((BYTE)(12*i), 0,0,0));
        g.FillRectangle(&sh, (float)(panelX+i), (float)(panelY+i), (float)pw, (float)ph);
    }
    Color bgCol  = isDark ? Color(255,32,33,36)   : Color(255,255,255,255);
    Color sepCol = isDark ? Color(255,60,61,65)   : Color(255,218,220,224);
    Color txtCol = isDark ? Color(255,232,234,237): Color(255,32,33,36);
    Color dimCol = isDark ? Color(255,154,160,166): Color(255,95,99,104);
    Color hovCol = isDark ? Color(30,255,255,255) : Color(20,0,0,0);
    Color accCol = Color(255,26,115,232);
    Color chkCol = Color(255,26,115,232);

    SolidBrush bgBr(bgCol);
    g.FillRectangle(&bgBr, (float)panelX, (float)panelY, (float)pw, (float)ph);
    Pen bPen(sepCol, 1.0f);
    g.DrawRectangle(&bPen, (float)panelX, (float)panelY, (float)(pw-1), (float)(ph-1));

    // Title
    Font fTitle(L"Segoe UI Semibold", (REAL)S(13), FontStyleBold, UnitPixel);
    Font fIco  (L"Segoe MDL2 Assets", (REAL)S(13), FontStyleRegular, UnitPixel);
    Font fName (L"Segoe UI",          (REAL)S(13), FontStyleRegular, UnitPixel);
    Font fSub  (L"Segoe UI",          (REAL)S(11), FontStyleRegular, UnitPixel);
    StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush txtBr(txtCol), dimBr(dimCol), accBr(accCol);

    int titleY = panelY + S(12);
    g.DrawString(L"Profiles", -1, &fTitle,
        RectF((float)(panelX+S(16)), (float)titleY, (float)(pw-S(32)), (float)S(24)), &sfL, &txtBr);

    // Profile cards
    g_profileCardHover = -1;
    int cardStartY = titleY + S(28);

    for (int i = 0; i < nProf; i++) {
        auto& p   = g_profiles[i];
        int   cy  = cardStartY + i * S(kProfCardH);
        bool  hov = (mouseX >= panelX && mouseX < panelX+pw &&
                     mouseY >= cy     && mouseY < cy+S(kProfCardH));
        bool  active = (i == g_activeProfileIdx);

        if (hov) {
            g_profileCardHover = i;
            SolidBrush hBr(hovCol);
            g.FillRectangle(&hBr, (float)panelX, (float)cy, (float)pw, (float)S(kProfCardH));
        }
        if (active) {
            // active profile subtle highlight
            Color actHigh = isDark ? Color(18,26,115,232) : Color(15,26,115,232);
            SolidBrush aBr(actHigh);
            g.FillRectangle(&aBr, (float)panelX, (float)cy, (float)pw, (float)S(kProfCardH));
        }

        // Avatar
        int avR = S(36);
        int avX = panelX + S(12);
        int avY = cy + (S(kProfCardH) - avR) / 2;
        DrawAvatarCircle(g, (float)avX, (float)avY, (float)avR,
                         p.avatarColor, p.avatarLetter, (float)S(17), isDark);

        // Name + email
        int textX = avX + avR + S(10);
        int textW = pw - (textX - panelX) - S(32);
        g.DrawString(p.name.c_str(), -1, &fName,
            RectF((float)textX, (float)cy+S(10), (float)textW, (float)S(20)), &sfL, &txtBr);
        std::wstring sub = p.primaryEmail().empty() ? L"No account" : p.primaryEmail();
        g.DrawString(sub.c_str(), -1, &fSub,
            RectF((float)textX, (float)(cy+S(30)), (float)textW, (float)S(18)), &sfL, &dimBr);

        // Checkmark for active
        if (active) {
            SolidBrush chkBr(chkCol);
            g.DrawString(L"\uE73E", -1, &fIco,
                RectF((float)(panelX+pw-S(32)), (float)cy,
                      (float)S(24), (float)S(kProfCardH)), &sfL, &chkBr);
        }
    }

    // "Add profile" row
    Pen sepPen(sepCol, 1.0f);
    int sepY2 = cardStartY + nProf * S(kProfCardH) + S(4);
    g.DrawLine(&sepPen, (float)(panelX+S(8)), (float)sepY2,
                        (float)(panelX+pw-S(8)), (float)sepY2);

    int addY = sepY2 + S(4);
    bool hovAddNew = (mouseX >= panelX && mouseX < panelX+pw &&
                      mouseY >= addY   && mouseY < addY+S(kProfItemH));
    if (hovAddNew) {
        g_profileCardHover = 100; // sentinel: "add new"
        SolidBrush hBr(hovCol);
        g.FillRectangle(&hBr, (float)panelX, (float)addY, (float)pw, (float)S(kProfItemH));
    }
    Font fItem(L"Segoe UI", (REAL)S(13), FontStyleRegular, UnitPixel);
    g.DrawString(L"\uE8FA", -1, &fIco,
        RectF((float)(panelX+S(16)), (float)addY, (float)S(24), (float)S(kProfItemH)), &sfL, &accBr);
    g.DrawString(L"Add new profile", -1, &fItem,
        RectF((float)(panelX+S(48)), (float)addY, (float)(pw-S(56)), (float)S(kProfItemH)), &sfL, &accBr);

    // "← Back" row
    int backY = addY + S(kProfItemH);
    bool hovBack = (mouseX >= panelX && mouseX < panelX+pw &&
                    mouseY >= backY  && mouseY < backY+S(kProfItemH));
    if (hovBack) {
        g_profileCardHover = 101; // sentinel: back
        SolidBrush hBr(hovCol);
        g.FillRectangle(&hBr, (float)panelX, (float)backY, (float)pw, (float)S(kProfItemH));
    }
    g.DrawString(L"\uE76B", -1, &fIco,
        RectF((float)(panelX+S(16)), (float)backY, (float)S(24), (float)S(kProfItemH)), &sfL, &dimBr);
    g.DrawString(L"Back", -1, &fItem,
        RectF((float)(panelX+S(48)), (float)backY, (float)(pw-S(56)), (float)S(kProfItemH)), &sfL, &dimBr);
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE PANEL — ADD PROFILE VIEW
// ─────────────────────────────────────────────────────────────────────────────
static void DrawProfilePanel_AddProfile(Gdiplus::Graphics& g, int W, int dpi,
                                         bool isDark, int mouseX, int mouseY)
{
    using namespace Gdiplus;
    auto S = [&](int v){ return v * dpi / 96; };

    int pw     = S(kProfPanelW);
    int panelX = W - pw - S(8);
    int panelY = S(36 + 36);
    int ph     = S(220);

    // Shadow + bg + border
    for (int i = 3; i >= 1; i--) {
        SolidBrush sh(Color((BYTE)(12*i), 0,0,0));
        g.FillRectangle(&sh, (float)(panelX+i), (float)(panelY+i), (float)pw, (float)ph);
    }
    Color bgCol  = isDark ? Color(255,32,33,36)   : Color(255,255,255,255);
    Color sepCol = isDark ? Color(255,60,61,65)   : Color(255,218,220,224);
    Color txtCol = isDark ? Color(255,232,234,237): Color(255,32,33,36);
    Color dimCol = isDark ? Color(255,154,160,166): Color(255,95,99,104);
    Color hovCol = isDark ? Color(30,255,255,255) : Color(20,0,0,0);
    Color accCol = Color(255,26,115,232);
    Color inpBg  = isDark ? Color(255,48,49,52)   : Color(255,241,243,244);

    SolidBrush bgBr(bgCol);
    g.FillRectangle(&bgBr, (float)panelX, (float)panelY, (float)pw, (float)ph);
    Pen bPen(sepCol, 1.0f);
    g.DrawRectangle(&bPen, (float)panelX, (float)panelY, (float)(pw-1), (float)(ph-1));

    Font fTitle(L"Segoe UI Semibold", (REAL)S(13), FontStyleBold,    UnitPixel);
    Font fItem (L"Segoe UI",          (REAL)S(12), FontStyleRegular, UnitPixel);
    Font fPlh  (L"Segoe UI",          (REAL)S(12), FontStyleItalic,  UnitPixel);
    Font fIco  (L"Segoe MDL2 Assets", (REAL)S(13), FontStyleRegular, UnitPixel);
    StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush txtBr(txtCol), dimBr(dimCol), accBr(accCol);

    // Title
    int titleY = panelY + S(16);
    g.DrawString(L"Add new profile", -1, &fTitle,
        RectF((float)(panelX+S(16)), (float)titleY, (float)(pw-S(32)), (float)S(24)), &sfL, &txtBr);

    // Preview avatar (uses first letter of typed name)
    wchar_t prevLetter = g_profileAddName.empty() ? L'?' : towupper(g_profileAddName[0]);
    int nextId = g_profiles.empty() ? 1 : g_profiles.back().id + 1;
    COLORREF prevColor = ProfileAvatarColor(nextId);
    int avR = S(44);
    int avX = panelX + (pw - avR) / 2;
    int avY = titleY + S(32);
    DrawAvatarCircle(g, (float)avX, (float)avY, (float)avR, prevColor, prevLetter, (float)S(20), isDark);

    // Name input box
    int inpY = avY + avR + S(12);
    int inpH = S(36);
    SolidBrush inpBr(inpBg);
    g.FillRectangle(&inpBr, (float)(panelX+S(12)), (float)inpY, (float)(pw-S(24)), (float)inpH);
    Pen inpPen(g_profileAddNameActive ? accCol : sepCol, g_profileAddNameActive ? 2.0f : 1.0f);
    g.DrawRectangle(&inpPen, (float)(panelX+S(12)), (float)inpY, (float)(pw-S(24)-1), (float)(inpH-1));

    // Input text or placeholder
    if (g_profileAddName.empty()) {
        g.DrawString(L"Profile name", -1, &fPlh,
            RectF((float)(panelX+S(20)), (float)inpY, (float)(pw-S(32)), (float)inpH), &sfL, &dimBr);
    } else {
        std::wstring displayed = g_profileAddName;
        if (g_profileAddNameActive) displayed += L"|"; // cursor
        g.DrawString(displayed.c_str(), -1, &fItem,
            RectF((float)(panelX+S(20)), (float)inpY, (float)(pw-S(32)), (float)inpH), &sfL, &txtBr);
    }

    // "Create" button
    int btnY   = inpY + inpH + S(12);
    int btnW   = pw - S(24);
    int btnH   = S(36);
    bool hovCreate = (mouseX >= panelX+S(12) && mouseX < panelX+S(12)+btnW &&
                      mouseY >= btnY && mouseY < btnY+btnH);
    bool canCreate = !g_profileAddName.empty();
    Color btnBg = canCreate ? (hovCreate ? Color(255,20,100,210) : accCol) : Color(255,160,160,160);
    SolidBrush btnBr(btnBg);
    FillRoundRect(g, btnBr, (float)(panelX+S(12)), (float)btnY, (float)btnW, (float)btnH, (float)S(6));
    SolidBrush wBr(Color(255,255,255,255));
    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Create", -1, &fTitle,
        RectF((float)(panelX+S(12)), (float)btnY, (float)btnW, (float)btnH), &sfC, &wBr);
    if (hovCreate && canCreate) g_profileCardHover = 200; // sentinel: create

    // "← Back" row
    int backY = btnY + btnH + S(4);
    bool hovBack = (mouseX >= panelX && mouseX < panelX+pw &&
                    mouseY >= backY  && mouseY < backY+S(kProfItemH));
    if (hovBack) {
        g_profileCardHover = 101;
        SolidBrush hBr(hovCol);
        g.FillRectangle(&hBr, (float)panelX, (float)backY, (float)pw, (float)S(kProfItemH));
    }
    g.DrawString(L"\uE76B", -1, &fIco,
        RectF((float)(panelX+S(16)), (float)backY, (float)S(24), (float)S(kProfItemH)), &sfL, &dimBr);
    g.DrawString(L"Back", -1, &fItem,
        RectF((float)(panelX+S(48)), (float)backY, (float)(pw-S(56)), (float)S(kProfItemH)), &sfL, &dimBr);
}

// ─────────────────────────────────────────────────────────────────────────────
// DISPATCH: draws the correct sub-view
// ─────────────────────────────────────────────────────────────────────────────
static void DrawProfilePanel(Gdiplus::Graphics& g, int W, int dpi,
                              bool isDark, int mouseX, int mouseY)
{
    // Reset hover sentinels
    g_profilePanelHover  = -1;
    g_profileAccountHover= -1;
    g_profileCardHover   = -1;

    switch (g_profileView) {
    case ProfilePanelView::MAIN:
        DrawProfilePanel_Main(g, W, dpi, isDark, mouseX, mouseY);
        break;
    case ProfilePanelView::ACCOUNTS:
        DrawProfilePanel_Accounts(g, W, dpi, isDark, mouseX, mouseY);
        break;
    case ProfilePanelView::ADD_PROFILE:
        DrawProfilePanel_AddProfile(g, W, dpi, isDark, mouseX, mouseY);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE PANEL TOTAL HEIGHT  (for click-outside detection)
// ─────────────────────────────────────────────────────────────────────────────
static int GetProfilePanelHeight(int dpi) {
    auto S = [&](int v){ return v * dpi / 96; };
    switch (g_profileView) {
    case ProfilePanelView::ACCOUNTS:
        return S(44) + (int)g_profiles.size()*S(kProfCardH)
               + S(8) + S(kProfItemH) + S(4) + S(kProfItemH) + S(8);
    case ProfilePanelView::ADD_PROFILE:
        return S(220);
    default: { // MAIN — dynamic: header + accounts + sep + 2 actions + sep + 2 footer
        BrowserProfile* p = GetActiveProfile();
        int nAccounts = p ? (int)p->accounts.size() : 0;
        int headerH  = S(130);
        int acctsH   = nAccounts * S(52);
        int sepH     = S(1) + S(8);
        int actionH  = S(kProfItemH) * 2;
        int footerH  = S(kProfItemH) * 2 + S(8);
        return headerH + acctsH + sepH + actionH + sepH + footerH;
    }
    }
}

// helper kept for any code that still calls it (click handler below replaces it)
static void GetProfilePanelItemRect(int W, int dpi, int /*idx*/,
                                     int& panelX, int& itemStartY, int& itemH)
{
    auto S = [&](int v){ return v * dpi / 96; };
    int pw = S(kProfPanelW);
    panelX     = W - pw - S(8);
    itemStartY = S(36+36) + S(kProfHeaderH) + S(8) + S(1)+S(12);
    itemH      = S(kProfItemH);
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN DRAW FUNCTION 
// ─────────────────────────────────────────────────────────────────────────────
static void DrawBrowserContent(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;
    // Focus mode: header ও tab bar draw করার দরকার নেই
    if (wd.isFocusMode) return;

    UINT dpi = GetWndDpi(hWnd);
    RECT cr;  GetClientRect(hWnd, &cr);
    int W = cr.right;

    int titleH  = TitleBarH(dpi);
    int toolH   = ToolbarH(dpi);
    int navH    = NavTotalH(hWnd);
    int winBtnW = WinBtnW(dpi);

    // ── Chrome Material You exact colors ────────────────────────────────────────
    // Dark:  Chrome dark theme (#202124 frame, #292a2d toolbar, #303134 omnibox)
    // Light: Chrome default    (#dee1e6 frame, #ffffff toolbar, #f1f3f4 omnibox)
    Color cBgFrame   = wd.isDarkMode ? Color(255, 32,  33,  36)  : Color(255, 218, 225, 230);
    Color cBgTool    = wd.isDarkMode ? Color(255, 41,  42,  45)  : Color(255, 255, 255, 255);
    Color cTxtPrim   = wd.isDarkMode ? Color(255, 232, 234, 237) : Color(255, 32,  33,  36);
    Color cTxtDim    = wd.isDarkMode ? Color(255, 154, 160, 166) : Color(255, 95,  99,  104);
    Color cTabActive = wd.isDarkMode ? Color(255, 41,  42,  45)  : Color(255, 255, 255, 255);
    Color cTabHover  = wd.isDarkMode ? Color(255, 54,  55,  59)  : Color(255, 232, 234, 237);
    Color cAddrBg    = wd.isDarkMode ? Color(255, 48,  49,  52)  : Color(255, 241, 243, 244);
    Color cAddrBord  = wd.isDarkMode ? Color(255, 95,  99,  104) : Color(255, 218, 220, 224);
    Color cDivLine   = wd.isDarkMode ? Color(255, 54,  55,  59)  : Color(255, 218, 220, 224);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    // AntiAlias (not ClearTypeGridFit) — ClearType requires RGB sub-pixel stripe
    // geometry that symbol/PUA fonts like Segoe MDL2 Assets don't have, causing
    // all icon DrawString calls to render as blank rectangles.
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    SolidBrush bFrame(cBgFrame);
    g.FillRectangle(&bFrame, 0, 0, W, titleH);
    
    if (!g_isPureViewerMode) {
        SolidBrush bTool(cBgTool);
        g.FillRectangle(&bTool, 0, titleH, W, toolH);

        Pen sepPen(cDivLine, 1.0f);
        if (!(wd.active() && (wd.active()->url == L"LOCAL_NTP" || wd.active()->url == L"about:blank"))) {
            g.DrawLine(&sepPen, 0, navH - 1, W, navH - 1);
        }

        // ── Chrome-style Loading Bar ──
        if (wd.active() && wd.active()->loading) {
            static ULONGLONG loadStart = 0;
            ULONGLONG now = GetTickCount64();
            if (loadStart == 0 || (now - loadStart) > 4000) loadStart = now;

            // elapsed 0→4000ms → progress 0→95%
            float elapsed = (float)(now - loadStart);
            float progress = elapsed / 4000.0f;
            if (progress > 0.95f) progress = 0.95f;

            // ── Chrome-style 3px loading bar (Google Blue #4285F4) ────────────────
            int barY = navH - 3;
            int barW = (int)(W * progress);

            // Chrome blue solid bar
            SolidBrush loadBrush(Color(255, 66, 133, 244)); // #4285F4
            g.FillRectangle(&loadBrush, 0, barY, barW, 3);

            // Bright shimmer at leading edge (Chrome-style)
            if (barW > 12) {
                LinearGradientBrush shimmerBrush(
                    PointF((float)(barW - 40), (float)barY),
                    PointF((float)barW, (float)barY),
                    Color(0, 255, 255, 255),
                    Color(180, 255, 255, 255));
                g.FillRectangle(&shimmerBrush, barW - 40, barY, 40, 3);
            }

            // Repaint চালিয়ে যাও animation এর জন্য
            // spinner frame advance করো সব loading tab এর জন্য
            for (auto& t : wd.tabs) {
                if (t.loading) t.loadingFrame = (t.loadingFrame + 1) % 8;
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }

    FontFamily ffSeg(L"Segoe UI");
    // MDL2 fallback guard: if Segoe MDL2 Assets is not available (older Windows),
    // fall back to Segoe UI Symbol which covers the same PUA codepoints.
    FontFamily ffMDL_primary(L"Segoe MDL2 Assets");
    FontFamily ffMDL_fallback(L"Segoe UI Symbol");
    FontFamily& ffMDL = (ffMDL_primary.GetLastStatus() == Ok) ? ffMDL_primary : ffMDL_fallback;

    Font fSmall  (&ffSeg, Sf(12.f, dpi), FontStyleRegular, UnitPixel);
    Font fSmallBd(&ffSeg, Sf(12.f, dpi), FontStyleBold,    UnitPixel);
    Font fBrand  (&ffSeg, Sf(16.f, dpi), FontStyleBold,    UnitPixel);
    Font fIcon   (&ffMDL, Sf(16.f, dpi), FontStyleRegular, UnitPixel);  // 14→16px: better legibility
    Font fIconSm (&ffMDL, Sf(13.f, dpi), FontStyleRegular, UnitPixel);  // 11→13px: better legibility

    StringFormat sfC, sfL, sfR;
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfR.SetAlignment(StringAlignmentFar);    sfR.SetLineAlignment(StringAlignmentCenter);

    SolidBrush brPrim(cTxtPrim);
    SolidBrush brDim (cTxtDim);

    // ── Chrome-style app icon + name (compact, left of tab strip) ──────────────
    {
        int iconX = S(10, dpi);
        // Icon circle (teal fill, white "R" like Chrome's colored circle)
        float icSz = Sf(20.f, dpi);
        float icY  = ((float)titleH - icSz) * 0.5f;
        GraphicsPath iconCircle;
        AddRoundRect(iconCircle, (float)iconX, icY, icSz, icSz, icSz * 0.5f);
        LinearGradientBrush iconGrad(
            PointF((float)iconX, icY),
            PointF((float)iconX + icSz, icY + icSz),
            Color(255, 12, 168, 176), Color(255, 0, 92, 204));
        g.FillPath(&iconGrad, &iconCircle);
        Font fIconLetter(&ffSeg, Sf(11.f, dpi), FontStyleBold, UnitPixel);
        SolidBrush brWhite(Color(255, 255, 255, 255));
        g.DrawString(L"R", -1, &fIconLetter,
            RectF((float)iconX, icY, icSz, icSz), &sfC, &brWhite);

        // App name next to icon
        SolidBrush brTeal(Color(255, 12, 168, 176));
        SolidBrush brName(wd.isDarkMode ? Color(255,232,234,237) : Color(255,32,33,36));
        float nameX = (float)iconX + icSz + Sf(5.f, dpi);
        Font fBrandSm(&ffSeg, Sf(13.f, dpi), FontStyleBold, UnitPixel);
        RectF rRas(nameX, 0.f, Sf(28.f, dpi), (float)titleH);
        g.DrawString(L"Ras", -1, &fBrandSm, rRas, &sfL, &brTeal);
        RectF rBrow(nameX + Sf(26.f, dpi), 0.f, Sf(60.f, dpi), (float)titleH);
        g.DrawString(L"Browser", -1, &fBrandSm, rBrow, &sfL, &brName);
    }

    // Window controls
    {
        int bx = W - winBtnW * 6; 
        auto DrawWinBtn = [&](int x, bool hover, bool isClose, const wchar_t* ico) {
            if (hover) {
                SolidBrush hb(isClose ? Color(255, 232, 17, 35) : (wd.isDarkMode ? Color(50, 255,255,255) : Color(20, 0,0,0)));
                g.FillRectangle(&hb, x, 0, winBtnW, titleH);
            }
            SolidBrush txtClr(isClose && hover ? Color(255,255,255,255) : cTxtPrim);
            g.DrawString(ico, -1, &fIconSm, RectF((float)x, 0.f, (float)winBtnW, (float)titleH), &sfC, &txtClr);
        };

        // ── Chrome order: [Focus][Pin][Dark][─][□][✕] ────────────────────────────
        DrawWinBtn(bx,               wd.hFocus, false, wd.isFocusMode ? L"\uE7B8" : L"\uE7C8");
        DrawWinBtn(bx + winBtnW,     wd.hPin,   false, wd.isPinned ? L"\uE840" : L"\uE718");
        DrawWinBtn(bx + winBtnW * 2, wd.hDark,  false, wd.isDarkMode ? L"\uE708" : L"\uE706");
        DrawWinBtn(bx + winBtnW * 3, wd.hMin,   false, L"\uE921"); // Minimize (─)
        DrawWinBtn(bx + winBtnW * 4, wd.hMax,   false, IsZoomed(hWnd) ? L"\uE923" : L"\uE922"); // Restore/Max
        DrawWinBtn(bx + winBtnW * 5, wd.hClose, true,  L"\uE8BB"); // Close (✕)
    }

    if (!g_isPureViewerMode) {
        // ── Chrome-style Tab Strip ───────────────────────────────────────────────
        {
            int tc = (int)wd.tabs.size();
            // Chrome uses 8px corner radius on tabs
            float cornerR = Sf(8.f, dpi);
            // Vertical separator between inactive tabs
            Pen tabSepPen(wd.isDarkMode ? Color(80, 255,255,255) : Color(60, 0,0,0), 1.0f);

            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc, dpi);
                float tx = (float)tr.left, ty = (float)tr.top;
                float tw = (float)(tr.right - tr.left), th = (float)(tr.bottom - tr.top);
                bool isActive = (i == wd.activeTab);
                bool isHover  = (i == wd.hoverTabIndex);

                // ── Tab background ────────────────────────────────────────────
                if (isActive) {
                    // Active tab: same color as toolbar (merges visually)
                    GraphicsPath tabPath;
                    BuildChromeTabPath(tabPath, tx, ty, tw, th, cornerR);
                    SolidBrush bActive(cTabActive);
                    g.FillPath(&bActive, &tabPath);
                    // Active tab bottom border removal: draw toolbar-colored line
                    SolidBrush bMerge(cBgTool);
                    g.FillRectangle(&bMerge, tx + cornerR, (float)(titleH - 1), tw - cornerR*2, 2.f);
                } else if (isHover) {
                    GraphicsPath tabPath;
                    BuildChromeTabPath(tabPath, tx, ty, tw, th, cornerR);
                    SolidBrush bHov(cTabHover);
                    g.FillPath(&bHov, &tabPath);
                }

                // Separator line between adjacent inactive tabs
                bool showSep = !isActive && i + 1 < tc && (i + 1) != wd.activeTab && !isHover;
                if (showSep) {
                    float sepX = tx + tw - 1;
                    float sepY1 = ty + th * 0.2f;
                    float sepY2 = ty + th * 0.8f;
                    g.DrawLine(&tabSepPen, sepX, sepY1, sepX, sepY2);
                }

                // ── Favicon / spinner ─────────────────────────────────────────
                float iconSz = Sf(16.f, dpi);
                float iconX  = tx + Sf((float)D_TAB_PAD + 2, dpi);
                float iconY  = ty + (th - iconSz) * 0.5f;

                const auto& tab = wd.tabs[i];

                if (tab.loading) {
                    // Chrome-style circular spinner (8 dots)
                    int frame = tab.loadingFrame % 8;
                    float cx2 = iconX + iconSz * 0.5f;
                    float cy2 = iconY + iconSz * 0.5f;
                    float r   = iconSz * 0.40f;
                    for (int d = 0; d < 8; d++) {
                        float angle = (float)d * 3.14159f / 4.0f - 3.14159f * 0.5f;
                        float dx = cx2 + r * cosf(angle) - 1.5f;
                        float dy = cy2 + r * sinf(angle) - 1.5f;
                        int dist = (d - frame + 8) % 8;
                        int alpha = 220 - dist * 24;
                        if (alpha < 30) alpha = 30;
                        // Chrome spinner is blue (#4285F4)
                        SolidBrush dotBrush(Color(alpha, 66, 133, 244));
                        g.FillEllipse(&dotBrush, dx, dy, 3.f, 3.f);
                    }
                } else if (tab.favicon && tab.url != L"LOCAL_NTP" && tab.url != L"about:blank"
                           && tab.url.find(L"blocked by rasfocus") == std::wstring::npos) {
                    // Real favicon
                    g.DrawImage(tab.favicon.get(),
                        RectF(iconX, iconY, iconSz, iconSz),
                        0.f, 0.f,
                        (float)tab.favicon->GetWidth(),
                        (float)tab.favicon->GetHeight(),
                        UnitPixel);
                } else if (tab.url == L"LOCAL_NTP" || tab.url == L"about:blank") {
                    // NTP: RasBrowser logo icon (teal circle with R)
                    GraphicsPath fvCircle;
                    AddRoundRect(fvCircle, iconX, iconY, iconSz, iconSz, iconSz * 0.5f);
                    LinearGradientBrush fvGrad(
                        PointF(iconX, iconY), PointF(iconX + iconSz, iconY + iconSz),
                        Color(255, 12, 168, 176), Color(255, 0, 92, 204));
                    g.FillPath(&fvGrad, &fvCircle);
                    Font fFavLetter(&ffSeg, Sf(9.f, dpi), FontStyleBold, UnitPixel);
                    SolidBrush fvTxt(Color(255,255,255,255));
                    g.DrawString(L"R", -1, &fFavLetter,
                        RectF(iconX, iconY, iconSz, iconSz), &sfC, &fvTxt);
                } else {
                    // Generic fallback: grey globe
                    SolidBrush fvBrush(wd.isDarkMode ? Color(180,95,99,104) : Color(180,95,99,104));
                    Font fGlobe(&ffMDL, Sf(13.f, dpi), FontStyleRegular, UnitPixel);
                    g.DrawString(L"\uE774", -1, &fGlobe,
                        RectF(iconX, iconY, iconSz, iconSz), &sfC, &fvBrush);
                }

                // ── Tab title ─────────────────────────────────────────────────
                SolidBrush tBrush(isActive ? cTxtPrim : cTxtDim);
                float titleX2 = iconX + iconSz + Sf(6.f, dpi);
                float closeW  = Sf(20.f, dpi);
                float titleW2 = tw - (titleX2 - tx) - closeW - Sf(4.f, dpi);

                if (titleW2 > 20) {
                    std::wstring displayTitle = tab.title;
                    if (displayTitle.empty() || tab.url == L"LOCAL_NTP" || tab.url == L"about:blank")
                        displayTitle = L"New Tab";
                    if (tab.url == L"LOCAL_HISTORY")
                        displayTitle = L"History";
                    if (tab.url == L"LOCAL_DOWNLOADS")
                        displayTitle = L"Downloads";
                    if (tab.url.find(L"blocked by rasfocus") != std::wstring::npos)
                        displayTitle = L"Page Blocked";

                    StringFormat sfTab;
                    sfTab.SetAlignment(StringAlignmentNear);
                    sfTab.SetLineAlignment(StringAlignmentCenter);
                    sfTab.SetTrimming(StringTrimmingEllipsisCharacter);
                    sfTab.SetFormatFlags(StringFormatFlagsNoWrap);
                    g.DrawString(displayTitle.c_str(), -1, &fSmall,
                        RectF(titleX2, ty, titleW2, th), &sfTab, &tBrush);
                }

                // ── Close button (shown on active tab always, on hover too) ──
                if (isActive || isHover) {
                    float cSz = Sf(16.f, dpi);
                    float cBtnX = tx + tw - cSz - Sf(6.f, dpi);
                    float cBtnY = ty + (th - cSz) * 0.5f;
                    // Hover circle behind X
                    if (isHover) {
                        SolidBrush hClose(wd.isDarkMode ? Color(40,255,255,255) : Color(30,0,0,0));
                        g.FillEllipse(&hClose, cBtnX - 2, cBtnY - 2, cSz + 4, cSz + 4);
                    }
                    Font fClose(&ffMDL, Sf(10.f, dpi), FontStyleRegular, UnitPixel);
                    g.DrawString(L"\uE711", -1, &fClose,
                        RectF(cBtnX, cBtnY, cSz, cSz), &sfC, &brDim);
                }
            }

            // ── New Tab button (+ circle, Chrome style) ──────────────────────
            RECT ntr = GetNewTabBtnRect(W, tc, dpi);
            float ntX = (float)ntr.left, ntY = (float)ntr.top;
            float ntSz = (float)(ntr.right - ntr.left);
            if (wd.hNewTab) {
                SolidBrush hbNT(wd.isDarkMode ? Color(40,255,255,255) : Color(25,0,0,0));
                g.FillEllipse(&hbNT, ntX, ntY, ntSz, ntSz);
            }
            Font fNewTab(&ffMDL, Sf(12.f, dpi), FontStyleRegular, UnitPixel);
            g.DrawString(L"\uE710", -1, &fNewTab,
                RectF(ntX, ntY, ntSz, ntSz), &sfC, &brDim);
        }

        // Toolbar
        {
            int toolY   = titleH;
            int curX    = S(8, dpi);
            int btnSz   = S(32, dpi);
            int btnStep = S(36, dpi);
            float btnHf = (float)toolH;

            auto DrawNavBtn = [&](bool hover, bool enabled, const wchar_t* ico, int& x) {
                if (hover && enabled) {
                    SolidBrush hb(wd.isDarkMode ? Color(50,255,255,255) : Color(20,0,0,0));
                    g.FillEllipse(&hb, (float)(x+S(2,dpi)), (float)(toolY+S(4,dpi)),
                                  (float)S(28,dpi), (float)S(28,dpi));
                }
                SolidBrush ic(enabled ? cTxtPrim : cDivLine);
                g.DrawString(ico, -1, &fIcon,
                    RectF((float)x, (float)toolY, (float)btnSz, btnHf), &sfC, &ic);
                x += btnStep;
            };

            auto* atab = wd.active();
            bool canBack = atab && atab->canBack;
            bool canFwd  = atab && atab->canFwd;

            DrawNavBtn(wd.hBack, canBack, L"\xE72B", curX);
            DrawNavBtn(wd.hFwd,  canFwd,  L"\xE72A", curX);
            DrawNavBtn(wd.hRel,  true,    L"\xE72C", curX);

            {
                // ── Chrome-style Omnibox ─────────────────────────────────────────────
                int addrX  = curX + S(4, dpi);
                int rightIX = W - S(36*3 + 8, dpi);
                int addrW  = rightIX - addrX - S(8, dpi);
                int addrH  = S(32, dpi);  // Chrome omnibox is 32px tall
                int addrY  = toolY + (toolH - addrH) / 2;

                // Omnibox background pill (Chrome: 16px radius = full pill)
                SolidBrush addrBg(cAddrBg);
                GraphicsPath pill;
                AddRoundRect(pill, (float)addrX, (float)addrY, (float)addrW, (float)addrH, Sf(16.f, dpi));
                g.FillPath(&addrBg, &pill);

                // Hover/focus border (Chrome shows 1px border on hover)
                // Always draw a subtle border for definition
                Pen addrPen(cAddrBord, 1.0f);
                g.DrawPath(&addrPen, &pill);

                // ── Lock icon (HTTPS security badge) ─────────────────────────
                auto* atab2 = wd.active();
                bool isSecure = atab2 && (
                    atab2->url.find(L"https://") == 0 ||
                    atab2->url == L"LOCAL_NTP" ||
                    atab2->url == L"about:blank");
                bool isNTP = atab2 && (atab2->url == L"LOCAL_NTP" || atab2->url == L"about:blank");

                Font fLock(&ffMDL, Sf(12.f, dpi), FontStyleRegular, UnitPixel);
                if (isNTP) {
                    // NTP: show search icon inside omnibox
                    SolidBrush lockBr(Color(255, 95, 99, 104));
                    g.DrawString(L"\uE721", -1, &fLock,
                        RectF((float)addrX + Sf(10.f,dpi), (float)addrY, Sf(20.f,dpi), (float)addrH),
                        &sfC, &lockBr);
                } else if (isSecure) {
                    // HTTPS: teal lock
                    SolidBrush lockBr(Color(255, 26, 115, 232)); // Google blue lock
                    g.DrawString(L"\uE72E", -1, &fLock,
                        RectF((float)addrX + Sf(10.f,dpi), (float)addrY, Sf(20.f,dpi), (float)addrH),
                        &sfC, &lockBr);
                } else {
                    // HTTP: warning icon
                    SolidBrush lockBr(Color(255, 234, 67, 53)); // Google red
                    g.DrawString(L"\uE7BA", -1, &fLock,
                        RectF((float)addrX + Sf(10.f,dpi), (float)addrY, Sf(20.f,dpi), (float)addrH),
                        &sfC, &lockBr);
                }

                // ── Gemini chip (right side of omnibox, Chrome-style AI button) ──
                float chipW = Sf(76.f, dpi);
                float chipH = Sf(22.f, dpi);
                float chipX = (float)addrX + (float)addrW - chipW - Sf(6.f, dpi);
                float chipY = (float)addrY + ((float)addrH - chipH) * 0.5f;

                GraphicsPath chipPath;
                AddRoundRect(chipPath, chipX, chipY, chipW, chipH, chipH * 0.5f);
                // Gemini gradient: Google blue → purple
                LinearGradientBrush chipBg(
                    PointF(chipX, chipY), PointF(chipX + chipW, chipY),
                    Color(255, 26, 115, 232), Color(255, 103, 58, 183));
                g.FillPath(&chipBg, &chipPath);

                Font fChip(&ffSeg, Sf(10.f, dpi), FontStyleBold, UnitPixel);
                SolidBrush chipTxt(Color(255, 255, 255, 255));
                g.DrawString(L"'28 Gemini", -1, &fChip,
                    RectF(chipX, chipY, chipW, chipH), &sfC, &chipTxt);
            }

            // ── Chrome-style right toolbar icons ─────────────────────────────────
            // Chrome: [Extensions puzzle] [Profile avatar] [⋮ Menu]
            int rx = W - S(36*3 + 8, dpi);
            auto DrawRightBtn = [&](bool hover, const wchar_t* ico, int x, bool accent=false) {
                if (hover) {
                    SolidBrush hb(wd.isDarkMode ? Color(40,255,255,255) : Color(20,0,0,0));
                    g.FillEllipse(&hb, (float)(x+S(2,dpi)), (float)(toolY+S(4,dpi)),
                                  (float)S(28,dpi), (float)S(28,dpi));
                }
                if (accent) {
                    SolidBrush acBr(Color(255, 66, 133, 244)); // Google blue
                    g.DrawString(ico, -1, &fIcon,
                        RectF((float)x, (float)toolY, (float)btnSz, btnHf), &sfC, &acBr);
                } else {
                    g.DrawString(ico, -1, &fIcon,
                        RectF((float)x, (float)toolY, (float)btnSz, btnHf), &sfC, &brPrim);
                }
            };
            DrawRightBtn(wd.hExt,  L"\uE9D2", rx); rx += btnStep; // Extensions

            // ── Profile avatar button ─────────────────────────────────────
            // Google photo URL আছে কিনা দেখো — থাকলে async load শুরু করো
            {
                std::wstring photoUrl;
                if (auto* prof = GetActiveProfile()) {
                    if (prof->activeAccount >= 0 && prof->activeAccount < (int)prof->accounts.size())
                        photoUrl = prof->accounts[prof->activeAccount].photoUrl;
                }
                if (!photoUrl.empty() && photoUrl != g_profilePhotoLoadedUrl)
                    LoadProfilePhotoAsync(photoUrl);
            }

            // hover ring
            if (wd.hProfile) {
                SolidBrush hb(wd.isDarkMode ? Color(40,255,255,255) : Color(20,0,0,0));
                g.FillEllipse(&hb, (float)(rx+S(2,dpi)), (float)(toolY+S(4,dpi)),
                              (float)S(28,dpi), (float)S(28,dpi));
            }

            float avX = (float)(rx + S(6, dpi));
            float avY = (float)(toolY + S(7, dpi));
            float avD = (float)S(22, dpi); // diameter

            if (g_profilePhotoBmp) {
                // Round clip → draw photo
                Gdiplus::GraphicsPath clipPath;
                clipPath.AddEllipse(avX, avY, avD, avD);
                Gdiplus::Region clipRgn(&clipPath);
                g.SetClip(&clipRgn);
                g.DrawImage(g_profilePhotoBmp, avX, avY, avD, avD);
                g.ResetClip();
            } else {
                // Letter avatar (Google blue circle + white initial)
                SolidBrush avBg(Color(255, 66, 133, 244));
                g.FillEllipse(&avBg, avX, avY, avD, avD);
                FontFamily ffAv(L"Segoe UI");
                Font fAv(&ffAv, avD * 0.55f, FontStyleBold, UnitPixel);
                StringFormat sfAv; sfAv.SetAlignment(StringAlignmentCenter);
                sfAv.SetLineAlignment(StringAlignmentCenter);
                SolidBrush wBr(Color(255,255,255,255));
                // Letter: active profile এর প্রথম অক্ষর
                wchar_t letter = L'R';
                if (auto* prof = GetActiveProfile())
                    letter = prof->avatarLetter;
                wchar_t lStr[2] = { letter, 0 };
                g.DrawString(lStr, 1, &fAv, RectF(avX, avY, avD, avD), &sfAv, &wBr);
            }
            rx += btnStep; // Profile done

            DrawRightBtn(wd.hMenu, L"\uE712", rx); // ⋮ Menu
        }

        // Bookmark Bar — শুধু home page (NTP) এ দেখাবে
        if (wd.active() && (wd.active()->url == L"LOCAL_NTP" || wd.active()->url == L"about:blank"))
        {
            int bmkY = titleH + toolH;
            int bmkH = S(D_BOOKMARK_H, dpi);
            
            SolidBrush bmkBg(cBgTool); 
            g.FillRectangle(&bmkBg, 0, bmkY, W, bmkH);

            Pen sepPen(cDivLine, 1.0f);
            g.DrawLine(&sepPen, 0, bmkY + bmkH - 1, W, bmkY + bmkH - 1);

            // ── Real bookmark bar — g_bookmarks থেকে dynamic ──────────────────
            POINT cur; GetCursorPos(&cur); ScreenToClient(hWnd, &cur);
            DrawBookmarkBar(g, W, bmkY, bmkH, wd.isDarkMode, (int)dpi,
                            cur.x, cur.y, g_bookmarkBarHoverIdx);
        }
    } // End of !g_isPureViewerMode

    // ── 🟢 3-DOT MENU OVERLAY (CHROME STYLE) ──────────────────
    if (wd.isMenuOpen && !g_isPureViewerMode) {
        float menuW = (float)S(320, dpi);
        int   itemH = S(34, dpi);
        float mX    = (float)W - menuW - (float)S(10, dpi);
        float mY    = GetMenuY(hWnd, dpi);   // ← FIX: use shared helper

        struct MenuItem { int type; const wchar_t* icon; const wchar_t* text; const wchar_t* shortcut; };
        // YouTube mode labels — dynamic based on current state
        const wchar_t* ytModeIcon  = g_youtubeDesktopMode ? L"\uE8AF" : L"\uE702";
        const wchar_t* ytModeLabel = g_youtubeDesktopMode ? L"YouTube: Desktop ●" : L"YouTube: Mobile ○";
        const wchar_t* ytAdIcon    = g_youtubeAdBlockEnabled ? L"\uEA18" : L"\uEA1A";
        const wchar_t* ytAdLabel   = g_youtubeAdBlockEnabled ? L"YT Ads: Blocked ✔" : L"YT Ads: Allowed ✖";
        const wchar_t* darkSiteIcon  = g_forceSiteDark ? L"\uE708" : L"\uE793";
        const wchar_t* darkSiteLabel = g_forceSiteDark ? L"Dark Theme: On ●" : L"Dark Theme: Off ○";

        // ── Profile header: name + email from active profile ──
        std::wstring menuProfileName  = L"Rasel Mia";
        std::wstring menuProfileSub   = L"Signed in";
        if (auto* prof = GetActiveProfile()) {
            if (!prof->primaryEmail().empty()) {
                menuProfileName = prof->displayLabel();
                menuProfileSub  = prof->primaryEmail();
            }
        }

        std::vector<MenuItem> menuItems = {
            { 2, L"\xE77B", menuProfileName.c_str(),    menuProfileSub.c_str() },
            { 1, L"",        L"",                      L""           },
            { 0, L"\xE710", L"New tab",               L"Ctrl+T"     },
            { 0, L"\xE727", L"New window",            L"Ctrl+N"     },
            { 1, L"",        L"",                      L""           },
            { 0, L"\xE81C", L"History",               L"Ctrl+H"     },
            { 0, L"\xE896", L"Downloads",             L"Ctrl+J"     },
            { 0, L"\xE8A4", L"Bookmarks and lists",   L""           },
            { 0, L"\xE9D2", L"Extensions",            L""           },
            { 1, L"",        L"",                      L""           },
            { 0, darkSiteIcon, darkSiteLabel,           L""           },
            { 0, ytModeIcon, ytModeLabel,               L""           },
            { 0, ytAdIcon,   ytAdLabel,                 L""           },
            { 1, L"",        L"",                      L""           },
            { 0, L"\x2728", L"Open Gemini AI",        L""           },
            { 0, L"\xE713", L"Settings",              L""           },
            { 0, L"\xE7E8", L"Exit",                  L"Alt+F4"     }
        };

        float totalH = (float)S(10, dpi); 
        for (const auto& mi : menuItems) {
            if      (mi.type == 2) totalH += (float)S(54, dpi);
            else if (mi.type == 1) totalH += (float)S(11, dpi);
            else                   totalH += (float)itemH;
        }
        totalH += (float)S(10, dpi);

        // ── Chrome-style menu panel (Material You card) ─────────────────────────
        GraphicsPath mPath;
        AddRoundRect(mPath, mX, mY, menuW, totalH, Sf(8.f, dpi));

        // Multi-layer shadow (Chrome-accurate elevation)
        for (int sh = 6; sh >= 1; sh--) {
            int alpha = 8 + sh * 4;
            SolidBrush shadowBr(Color(alpha, 0, 0, 0));
            g.FillRectangle(&shadowBr, mX + sh, mY + sh*2, menuW + sh, totalH + sh);
        }

        // Menu background
        SolidBrush mBg(wd.isDarkMode ? Color(255, 41, 42, 45) : Color(255, 255, 255, 255));
        g.FillPath(&mBg, &mPath);
        // Subtle border (Chrome light mode has very light border)
        Pen mBorder(wd.isDarkMode ? Color(255, 60, 61, 65) : Color(255, 218, 220, 224), 1.0f);
        g.DrawPath(&mBorder, &mPath);

        // Chrome menu colors
        SolidBrush hHover(wd.isDarkMode ? Color(255, 60, 61, 65) : Color(255, 232, 240, 254)); // blue tint on hover
        SolidBrush txtBr (wd.isDarkMode ? Color(255, 232, 234, 237) : Color(255, 32, 33, 36));
        SolidBrush dimBr (wd.isDarkMode ? Color(255, 154, 160, 166) : Color(255, 95, 99, 104));
        SolidBrush accentBr(Color(255, 26, 115, 232)); // Google Blue #1a73e8
        
        float currY  = mY + (float)S(10, dpi);
        int itemIndex = 0;

        for (const auto& mi : menuItems) {
            if (mi.type == 2) {
                if (itemIndex == wd.hoverMenuIdx)
                    g.FillRectangle(&hHover, mX + 2, currY, menuW - 4, (float)S(54, dpi));
                g.FillEllipse(&accentBr, mX + (float)S(15, dpi), currY + (float)S(10, dpi),
                              (float)S(34, dpi), (float)S(34, dpi));
                SolidBrush wBr(Color(255,255,255,255));
                g.DrawString(mi.icon, -1, &fIcon,
                    RectF(mX + (float)S(15,dpi), currY + (float)S(10,dpi),
                          (float)S(34,dpi), (float)S(34,dpi)), &sfC, &wBr);
                g.DrawString(mi.text, -1, &fSmallBd,
                    RectF(mX + (float)S(65,dpi), currY, menuW - (float)S(75,dpi), (float)S(30,dpi)),
                    &sfL, &txtBr);
                g.DrawString(mi.shortcut, -1, &fSmall,
                    RectF(mX + (float)S(65,dpi), currY + (float)S(22,dpi),
                          menuW - (float)S(75,dpi), (float)S(20,dpi)), &sfL, &dimBr);
                currY += (float)S(54, dpi);
                itemIndex++;
            } 
            else if (mi.type == 1) {
                currY += (float)S(5, dpi);
                g.DrawLine(&mBorder, mX, currY, mX + menuW, currY);
                currY += (float)S(6, dpi);
            } 
            else {
                if (itemIndex == wd.hoverMenuIdx)
                    g.FillRectangle(&hHover, mX + 2, currY, menuW - 4, (float)itemH);
                g.DrawString(mi.icon, -1, &fIconSm,
                    RectF(mX + (float)S(15,dpi), currY, (float)S(25,dpi), (float)itemH), &sfL, &txtBr);
                g.DrawString(mi.text, -1, &fSmall,
                    RectF(mX + (float)S(55,dpi), currY, menuW - (float)S(100,dpi), (float)itemH),
                    &sfL, &txtBr);
                g.DrawString(mi.shortcut, -1, &fSmall,
                    RectF(mX, currY, menuW - (float)S(20,dpi), (float)itemH), &sfR, &dimBr);
                currY += (float)itemH;
                itemIndex++;
            }
        }
    }

    // ── Panel Overlays (menu এর পরে আঁকো যাতে সব কিছুর উপরে থাকে) ──
    RECT cr2; GetClientRect(hWnd, &cr2);
    int W2 = cr2.right, H2 = cr2.bottom;
    POINT mousePt; GetCursorPos(&mousePt); ScreenToClient(hWnd, &mousePt);

    if (g_bookmarkPanelOpen)
        DrawBookmarkPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                          wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y,
                          g_bookmarkHoverIdx);

    if (g_historyPanelOpen)
        DrawHistoryPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                         wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);

    if (g_downloadsPanelOpen)
        DrawDownloadsPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                           wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);

    if (g_extensionPanelOpen)
        DrawExtensionPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                           wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);

    if (g_profilePanelOpen)
        DrawProfilePanel(g, W2, (int)dpi, wd.isDarkMode, mousePt.x, mousePt.y);

    if (g_findBarOpen)
        DrawFindBar(g, W2, H2, wd.isDarkMode, (int)dpi);

    if (g_contextMenuOpen)
        DrawContextMenu(g, W2, H2, wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    if (g_windows[hWnd].isFullScreen) return;

    DoubleBufferedPaint(hWnd, hdc, [&](HDC memDC, int W, int H) {
        bool dark = g_windows[hWnd].isDarkMode;
        // Chrome frame: dark=#202124, light=#dee1e6
        HBRUSH hbg = CreateSolidBrush(dark ? RGB(32,33,36) : RGB(218,225,230));
        RECT fr = { 0, 0, W, H };
        FillRect(memDC, &fr, hbg);
        DeleteObject(hbg);
        DrawBrowserContent(hWnd, memDC);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND, int);
static void AddTab(HWND, std::wstring);
static void CloseTab(HWND, int);
static void CreateWebViewForTab(HWND, int);

// ─────────────────────────────────────────────────────────────────────────────
// FOCUS MODE TOGGLE  (header + tab bar লুকায়, native app feel)
// ─────────────────────────────────────────────────────────────────────────────
static void ToggleFocusMode(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    wd.isFocusMode = !wd.isFocusMode;

    // Address bar hide/show
    ShowWindow(wd.hAddressBar, wd.isFocusMode ? SW_HIDE : SW_SHOW);

    // WebView কে সব জায়গা দাও যদি focus mode চালু থাকে
    RECT wvr = GetWebViewRect(hWnd);
    for (auto& tab : wd.tabs)
        if (tab.controller) tab.controller->put_Bounds(wvr);

    InvalidateRect(hWnd, NULL, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (idx < 0 || idx >= (int)wd.tabs.size()) return;

    if (wd.activeTab != idx && wd.activeTab < (int)wd.tabs.size())
        if (wd.tabs[wd.activeTab].controller) {
            // Move off-screen instead of put_IsVisible(FALSE) — hiding triggers
            // document.visibilityState='hidden' which causes YouTube to pause playback.
            RECT offscreen = {-10000, -10000, -9000, -9000};
            wd.tabs[wd.activeTab].controller->put_Bounds(offscreen);
        }

    wd.activeTab = idx;
    auto& tab = wd.tabs[idx];

    if (tab.controller) {
        tab.controller->put_IsVisible(TRUE);
        RECT wvr = GetWebViewRect(hWnd);
        tab.controller->put_Bounds(wvr);
    } else {
        CreateWebViewForTab(hWnd, idx);
    }

    if (wd.hAddressBar) {
        if (tab.url == L"LOCAL_NTP" || tab.url == L"LOCAL_HISTORY" ||
            tab.url == L"LOCAL_DOWNLOADS" || tab.url == L"about:blank" ||
            tab.url.find(L"blocked by rasfocus") != std::wstring::npos) 
            SetWindowTextW(wd.hAddressBar, L"");
        else 
            SetWindowTextW(wd.hAddressBar, tab.url.c_str());
    }

    RepositionAddressBar(hWnd);
    InvalidateRect(hWnd, NULL, TRUE); 
}

static void CloseTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (wd.tabs.empty()) return;

    auto& tab = wd.tabs[idx];
    if (tab.controller) {
        tab.controller->put_IsVisible(FALSE);
        tab.controller->Close();
    }
    wd.tabs.erase(wd.tabs.begin() + idx);

    if (wd.tabs.empty()) { DestroyWindow(hWnd); return; }

    wd.activeTab = min(wd.activeTab, (int)wd.tabs.size() - 1);
    SwitchToTab(hWnd, wd.activeTab);
}

static void AddTab(HWND hWnd, std::wstring url) {
    auto& wd = g_windows[hWnd];
    TabData tab; tab.url = url; tab.title = L"New Tab";
    wd.tabs.push_back(tab);
    int newIdx = (int)wd.tabs.size() - 1;
    SwitchToTab(hWnd, newIdx);
    CreateWebViewForTab(hWnd, newIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 CONTROLLER COMPLETION HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class TabControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    HWND         m_hWnd;
    int          m_tabIdx;
    std::wstring m_startUrl;
    ULONG        m_ref = 1;

public:
    TabControllerHandler(HWND h, int idx, std::wstring url)
        : m_hWnd(h), m_tabIdx(idx), m_startUrl(std::move(url)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { *ppv = this; return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (FAILED(hr) || !ctl) return S_OK;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd = g_windows[m_hWnd];
        if (m_tabIdx >= (int)wd.tabs.size()) return S_OK;

        auto& tab = wd.tabs[m_tabIdx];
        tab.controller = ctl;
        ctl->get_CoreWebView2(&tab.webview);

        // ── Advanced Features: Download Manager, Custom Context Menu, Privacy Shield ──
        SetupAdvancedFeatures(m_hWnd, tab.webview);

        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = wd.isDarkMode
                ? COREWEBVIEW2_COLOR{255, 30, 30, 30}
                : COREWEBVIEW2_COLOR{255, 255, 255, 255};
            ctl2->put_DefaultBackgroundColor(bg);
        }

        ICoreWebView2Settings* settings = nullptr;
        if (SUCCEEDED(tab.webview->get_Settings(&settings)) && settings) {
            settings->put_IsScriptEnabled(TRUE);
            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            settings->put_IsWebMessageEnabled(TRUE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_IsStatusBarEnabled(TRUE);

            ComPtr<ICoreWebView2Settings2> s2;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s2)))) {
                // Latest Chrome UA — ChatGPT/OpenAI older UA কে suspicious মনে করে।
                // YouTube family hosts-এর জন্য mobile UA নিচে WebResourceRequested
                // এ per-request override করা হয় (m.youtube system)।
                s2->put_UserAgent(kDesktopUA);
            }
        }

        // ── Layer 2 (AdBlocker.kt shouldBlock()/AD_DOMAINS+TRACKER_DOMAINS) ──
        // সব sub-resource request filter করো, তারপর WebResourceRequested এ
        // ad/tracker host হলে empty response দিয়ে block করে দাও।
        tab.webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        tab.webview->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [](ICoreWebView2* /*sender*/, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                ComPtr<ICoreWebView2WebResourceRequest> req;
                args->get_Request(&req);
                if (!req) return S_OK;
                LPWSTR uri = nullptr;
                req->get_Uri(&uri);
                if (uri) {
                    std::wstring urlStr(uri);
                    CoTaskMemFree(uri);

                    // 🟢 M.YOUTUBE SYSTEM — youtube.com family-র প্রতিটা
                    // sub-resource request-এ mobile UA override করো, যাতে
                    // main-frame redirect (m.youtube.com) + সব asset request
                    // consistent mobile client হিসেবে দেখা যায়।
                    // Desktop mode এ UA override বন্ধ — desktop YouTube দেখাবে।
                    std::wstring reqHost = ExtractHost(urlStr);
                    if (!g_youtubeDesktopMode && !reqHost.empty() && IsYouTubeFamilyHost(reqHost)) {
                        ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                        if (SUCCEEDED(req->get_Headers(&headers)) && headers) {
                            headers->SetHeader(L"User-Agent", kMobileUA);
                        }
                    }

                    if (g_youtubeAdBlockEnabled && IsAdOrTrackerUrl(urlStr) && g_sharedEnv) {
                        ComPtr<IStream> emptyStream;
                        emptyStream.Attach(SHCreateMemStream(nullptr, 0));
                        ComPtr<ICoreWebView2WebResourceResponse> response;
                        if (SUCCEEDED(g_sharedEnv->CreateWebResourceResponse(
                                emptyStream.Get(), 204, L"No Content", L"", &response))) {
                            args->put_Response(response.Get());
                        }
                    }
                }
                return S_OK;
            }).Get(), nullptr);

        // ── Full Anti-Bot Detection Bypass (Google + ChatGPT + OpenAI compatible) ──
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            // ── 1. webdriver flag — most important ──
            L"(() => {"
            L"  try { Object.defineProperty(navigator, 'webdriver', { get: () => undefined, configurable: true }); } catch(e){}"

            // ── 2. chrome object — ChatGPT checks window.chrome deeply ──
            L"  if (!window.chrome) {"
            L"    window.chrome = {"
            L"      app: { isInstalled: false },"
            L"      csi: function(){ return {}; },"
            L"      loadTimes: function(){ return {}; },"
            L"      runtime: {},"
            L"      webstore: {}"
            L"    };"
            L"  }"

            // ── 3. permissions API — ChatGPT uses this for microphone/notifications ──
            L"  const origQuery = window.navigator.permissions && window.navigator.permissions.query ? window.navigator.permissions.query.bind(window.navigator.permissions) : null;"
            L"  if (origQuery) {"
            L"    Object.defineProperty(window.navigator.permissions, 'query', {"
            L"      value: (p) => p.name === 'notifications' ? Promise.resolve({state: Notification.permission}) : origQuery(p),"
            L"      configurable: true"
            L"    });"
            L"  }"

            // ── 4. plugins — empty plugins = automation flag ──
            L"  try { Object.defineProperty(navigator, 'plugins', { get: () => { const a = [1,2,3,4,5]; a.item = i => a[i]; a.namedItem = () => null; a.refresh = () => {}; return a; }, configurable: true }); } catch(e){}"

            // ── 5. languages ──
            L"  try { Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en'], configurable: true }); } catch(e){}"

            // ── 6. platform ──
            L"  try { Object.defineProperty(navigator, 'platform', { get: () => 'Win32', configurable: true }); } catch(e){}"

            // ── 7. hardwareConcurrency — 0 = bot flag ──
            L"  try { Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => 8, configurable: true }); } catch(e){}"

            // ── 8. deviceMemory — ChatGPT checks this ──
            L"  try { Object.defineProperty(navigator, 'deviceMemory', { get: () => 8, configurable: true }); } catch(e){}"

            // ── 9. connection — WebView2 lacks this, ChatGPT checks it ──
            L"  try { if (!navigator.connection) { Object.defineProperty(navigator, 'connection', { get: () => ({ effectiveType: '4g', rtt: 50, downlink: 10, saveData: false }), configurable: true }); } } catch(e){}"

            // ── 10. vendor — should be 'Google Inc.' for Chrome ──
            L"  try { Object.defineProperty(navigator, 'vendor', { get: () => 'Google Inc.', configurable: true }); } catch(e){}"

            // ── 11. maxTouchPoints — 0 on desktop is OK but some checks look for it ──
            L"  try { Object.defineProperty(navigator, 'maxTouchPoints', { get: () => 1, configurable: true }); } catch(e){}"

            // ── 12. WebGL fingerprint — some anti-bot checks this ──
            L"  try {"
            L"    const origGetParam = WebGLRenderingContext.prototype.getParameter;"
            L"    WebGLRenderingContext.prototype.getParameter = function(p) {"
            L"      if (p === 37445) return 'Intel Inc.';"
            L"      if (p === 37446) return 'Intel Iris OpenGL Engine';"
            L"      return origGetParam.call(this, p);"
            L"    };"
            L"  } catch(e){}"

            // ── 13. Remove WebView2-specific properties from window ──
            L"  try { delete window.chrome.webview; } catch(e){}"

            // ── 14. Notification permission — ChatGPT checks this on load ──
            L"  try { if (Notification && Notification.permission === 'default') {} } catch(e){}"

            // ── 15. window.open — Google OAuth uses this for popup flow ──
            L"  const _origOpen = window.open;"
            L"  window.open = function(url, target, features) {"
            L"    if (url && (url.includes('accounts.google.com') || url.includes('google.com/o/oauth'))) {"
            L"      return _origOpen.call(this, url, '_blank', features);"
            L"    }"
            L"    return _origOpen.call(this, url, target, features);"
            L"  };"

            // ── 16. localStorage / sessionStorage — কিছু auth flow এ দরকার ──
            L"  try { window.localStorage.setItem('__test__', '1'); window.localStorage.removeItem('__test__'); } catch(e){}"

            // ── 17. Credential Management API — Google One-tap এ দরকার ──
            L"  if (!navigator.credentials) {"
            L"    Object.defineProperty(navigator, 'credentials', {"
            L"      get: () => ({ get: () => Promise.resolve(null), store: () => Promise.resolve(), create: () => Promise.resolve(null) }),"
            L"      configurable: true"
            L"    });"
            L"  }"

            L"})();",
            nullptr);

        // ── Layer 3a (AdBlocker.kt YouTubeAdPruner.getJsInjectScript()) ──
        // প্রতি navigation এ document তৈরি হওয়ার আগেই fetch/XHR patch করে দাও,
        // যাতে YouTube নিজের script চালানোর আগেই ad fields prune হয়ে যায়।
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            GetYouTubeAdPrunerScript().c_str(), nullptr);

        // ── Claude.ai / Anthropic — Cloudflare Turnstile early bypass ──
        // DocumentCreated এ inject করলে Cloudflare-এর challenge script চালানোর
        // আগেই navigator.webdriver এবং অন্য bot markers সরানো যায়।
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            L"(() => {"
            L"  try { Object.defineProperty(navigator, 'webdriver', { get: () => undefined, configurable: true }); } catch(e){}"
            L"  if (window.location.hostname.includes('claude.ai') || window.location.hostname.includes('anthropic.com')) {"
            L"    if (!window.chrome) window.chrome = { app:{isInstalled:false}, csi:()=>({}), loadTimes:()=>({}), runtime:{}, webstore:{} };"
            L"    try { delete window.chrome.webview; } catch(e){}"
            L"    try { delete window.__WebView2__; } catch(e){}"
            L"    try { Object.defineProperty(navigator, 'vendor', { get: () => 'Google Inc.', configurable: true }); } catch(e){}"
            L"    try { Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => 8, configurable: true }); } catch(e){}"
            L"    try { Object.defineProperty(navigator, 'deviceMemory', { get: () => 8, configurable: true }); } catch(e){}"
            L"    try { if (!navigator.connection) Object.defineProperty(navigator, 'connection', { get: () => ({effectiveType:'4g',rtt:50,downlink:10,saveData:false}), configurable: true }); } catch(e){}"
            L"    try { Object.defineProperty(navigator, 'plugins', { get: () => { const a=[1,2,3,4,5]; a.item=i=>a[i]; a.namedItem=()=>null; a.refresh=()=>{}; return a; }, configurable:true }); } catch(e){}"
            L"  }"
            L"})();",
            nullptr);

        // ── RasExtensions — inject enabled extensions on every document ──
        {
            std::wstring extScript = GetExtensionInjectScript();
            if (!extScript.empty())
                tab.webview->AddScriptToExecuteOnDocumentCreated(extScript.c_str(), nullptr);
        }

        // ── Force Dark Mode — নতুন page load এ সব website dark করো ──
        // Header dark button: সব site এ CSS invert, কোনো exception নেই।
        // Script ID capture করা হচ্ছে যাতে toggle এ remove + re-register করা যায়।
        {
            int tabIdx = m_tabIdx; // lambda capture এর জন্য
            HWND capturedHwnd = m_hWnd;
            const std::wstring darkCss =
                L"(() => {"
                L"  if (document.getElementById('ras-force-dark')) return;"
                L"  const s = document.createElement('style');"
                L"  s.id = 'ras-force-dark';"
                L"  s.textContent = 'html { filter: invert(90%) hue-rotate(180deg) !important; }'"
                L"    + 'img, video, iframe, canvas, picture, svg image, [style*=\"background-image\"] '"
                L"    + '{ filter: invert(100%) hue-rotate(180deg) !important; }';"
                L"  (document.head || document.documentElement).appendChild(s);"
                L"})();";
            if (wd.isDarkMode) {
                tab.webview->AddScriptToExecuteOnDocumentCreated(
                    darkCss.c_str(),
                    Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                        [capturedHwnd, tabIdx](HRESULT hr, LPCWSTR id) -> HRESULT {
                            if (SUCCEEDED(hr) && id && g_windows.count(capturedHwnd)) {
                                auto& w = g_windows[capturedHwnd];
                                if (tabIdx >= 0 && tabIdx < (int)w.tabs.size())
                                    w.tabs[tabIdx].darkScriptId = id;
                            }
                            return S_OK;
                        }).Get());
            }
        }

        // ── g_forceSiteDark — menu "Dark Theme" toggle ──
        if (g_forceSiteDark) {
            tab.webview->AddScriptToExecuteOnDocumentCreated(
                L"(() => {"
                L"  if (document.getElementById('ras-force-dark')) return;"
                L"  const s = document.createElement('style');"
                L"  s.id = 'ras-force-dark';"
                L"  s.textContent = 'html{filter:invert(90%) hue-rotate(180deg)!important}'"
                L"    + 'img,video,iframe,canvas,picture,svg image,[style*=\"background-image\"]'"
                L"    + '{filter:invert(100%) hue-rotate(180deg)!important}';"
                L"  (document.head||document.documentElement).appendChild(s);"
                L"})()",
                nullptr);
        }

        tab.webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr; args->get_Uri(&uri);
                if (uri) {
                    std::wstring urlStr(uri);

                    // 🟢 M.YOUTUBE SYSTEM — desktop youtube.com/www.youtube.com
                    // ধরা পড়লে সাথে সাথে cancel করে m.youtube.com এ পাঠাও।
                    std::wstring navHost = ExtractHost(urlStr);
                    if (NeedsMobileYouTubeRedirect(navHost)) {
                        args->put_Cancel(TRUE);
                        std::wstring mobileUrl = RewriteToMobileYouTube(urlStr);
                        if (g_windows.count(m_hWnd)) {
                            auto& w = g_windows[m_hWnd];
                            if (m_tabIdx >= 0 && m_tabIdx < (int)w.tabs.size() &&
                                w.tabs[m_tabIdx].webview) {
                                w.tabs[m_tabIdx].webview->Navigate(mobileUrl.c_str());
                            }
                        }
                        CoTaskMemFree(uri);
                        return S_OK;
                    }

                    // ── ChatGPT/OpenAI এর জন্য extra headers inject করো ──
                    bool isChatGPT = (urlStr.find(L"chatgpt.com") != std::wstring::npos ||
                                      urlStr.find(L"openai.com")  != std::wstring::npos);
                    if (isChatGPT) {
                        // Sec-Fetch headers যোগ করো — real browser এর মতো
                        ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                        if (SUCCEEDED(args->get_RequestHeaders(&headers)) && headers) {
                            headers->SetHeader(L"Sec-Fetch-Mode",    L"navigate");
                            headers->SetHeader(L"Sec-Fetch-Site",    L"none");
                            headers->SetHeader(L"Sec-Fetch-User",    L"?1");
                            headers->SetHeader(L"Sec-Fetch-Dest",    L"document");
                            headers->SetHeader(L"Accept-Language",   L"en-US,en;q=0.9");
                            headers->SetHeader(L"Upgrade-Insecure-Requests", L"1");
                        }
                    }

                    // ── Claude.ai / Anthropic — Cloudflare bypass ──
                    bool isClaude = (urlStr.find(L"claude.ai")    != std::wstring::npos ||
                                     urlStr.find(L"anthropic.com") != std::wstring::npos);
                    if (isClaude) {
                        ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                        if (SUCCEEDED(args->get_RequestHeaders(&headers)) && headers) {
                            headers->SetHeader(L"Sec-Fetch-Mode",    L"navigate");
                            headers->SetHeader(L"Sec-Fetch-Site",    L"none");
                            headers->SetHeader(L"Sec-Fetch-User",    L"?1");
                            headers->SetHeader(L"Sec-Fetch-Dest",    L"document");
                            headers->SetHeader(L"Accept-Language",   L"en-US,en;q=0.9");
                            headers->SetHeader(L"Upgrade-Insecure-Requests", L"1");
                            headers->SetHeader(L"Accept",            L"text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8");
                        }
                    }

                    if (IsBlockedContent(urlStr)) {
                        args->put_Cancel(TRUE);
                        if (g_windows.count(m_hWnd)) {
                            auto& w = g_windows[m_hWnd];
                            if (w.hAddressBar) SetWindowTextW(w.hAddressBar, L"blocked by rasfocus");
                            if (m_tabIdx >= 0 && m_tabIdx < (int)w.tabs.size()) {
                                w.tabs[m_tabIdx].url = L"blocked by rasfocus";
                                w.tabs[m_tabIdx].loading = false;
                                w.tabs[m_tabIdx].webview->NavigateToString(
                                    GetBlocked_HTML(w.isDarkMode).c_str());
                            }
                        }
                    } else {
                        // ── Loading শুরু হলে loading = true ──
                        if (g_windows.count(m_hWnd)) {
                            auto& w = g_windows[m_hWnd];
                            if (m_tabIdx >= 0 && m_tabIdx < (int)w.tabs.size()) {
                                w.tabs[m_tabIdx].loading = true;
                                w.tabs[m_tabIdx].favicon = nullptr; // পুরনো favicon সরিয়ে দাও
                                w.tabs[m_tabIdx].loadingFrame = 0;
                                InvalidateRect(m_hWnd, NULL, FALSE);
                            }
                        }
                    }
                    CoTaskMemFree(uri);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                // Google OAuth, "Sign in with Google" এবং যেকোনো popup window handle করো
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                std::wstring popupUrl = uri ? uri : L"";
                if (uri) CoTaskMemFree(uri);

                BOOL isUserInitiated = FALSE;
                args->get_IsUserInitiated(&isUserInitiated);

                // Google OAuth / SSO popup — accounts.google.com বা auth endpoint
                bool isGoogleAuth = (
                    popupUrl.find(L"accounts.google.com") != std::wstring::npos ||
                    popupUrl.find(L"google.com/o/oauth")  != std::wstring::npos ||
                    popupUrl.find(L"auth.openai.com")     != std::wstring::npos ||
                    popupUrl.find(L"login.microsoftonline")!= std::wstring::npos ||
                    popupUrl.find(L"appleid.apple.com")   != std::wstring::npos ||
                    popupUrl.find(L"github.com/login")    != std::wstring::npos ||
                    popupUrl.find(L"facebook.com/login")  != std::wstring::npos
                );

                if (isGoogleAuth || isUserInitiated) {
                    // নতুন tab এ খোলো
                    AddTab(m_hWnd, popupUrl);
                    args->put_Handled(TRUE);
                } else if (!popupUrl.empty()) {
                    // অন্য popup — নতুন tab এ খোলো
                    AddTab(m_hWnd, popupUrl);
                    args->put_Handled(TRUE);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                LPWSTR docTitle = nullptr;
                sender->get_DocumentTitle(&docTitle);
                if (docTitle) {
                    w.tabs[m_tabIdx].title = docTitle;
                    CoTaskMemFree(docTitle);
                    InvalidateRect(m_hWnd, NULL, FALSE);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx != w.activeTab) return S_OK;
                LPWSTR src = nullptr; sender->get_Source(&src);
                if (src) {
                    std::wstring urlStr(src);
                    w.tabs[m_tabIdx].url = urlStr;
                    
                    // ── History Auto-save ──
                    SaveToHistory(urlStr, w.tabs[m_tabIdx].title);

                    if (w.hAddressBar) {
                        if (urlStr == L"LOCAL_NTP" || urlStr == L"LOCAL_HISTORY" ||
                            urlStr == L"LOCAL_DOWNLOADS" || urlStr == L"about:blank" ||
                            urlStr.find(L"blocked by rasfocus") != std::wstring::npos) {
                            SetWindowTextW(w.hAddressBar, L"");
                        } else {
                            SetWindowTextW(w.hAddressBar, src);
                        }
                    }
                    CoTaskMemFree(src);
                }
                
                if (m_tabIdx == w.activeTab) {
                    RECT wvr = GetWebViewRect(m_hWnd);
                    w.tabs[m_tabIdx].controller->put_Bounds(wvr);
                    InvalidateRect(m_hWnd, NULL, TRUE);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;

                // ── Loading শেষ ──
                w.tabs[m_tabIdx].loading = false;

                // ── Favicon fetch via JS ──
                // favicon কে base64 data URL হিসেবে নিয়ে আসি
                {
                    int captureIdx = m_tabIdx;
                    HWND captureWnd = m_hWnd;
                    sender->ExecuteScript(
                        L"(() => {"
                        L"  const link = document.querySelector(\"link[rel*='icon']\");"
                        L"  const href = link ? link.href : (location.origin + '/favicon.ico');"
                        L"  return new Promise(resolve => {"
                        L"    const img = new Image();"
                        L"    img.crossOrigin = 'anonymous';"
                        L"    img.onload = () => {"
                        L"      const c = document.createElement('canvas');"
                        L"      c.width = c.height = 32;"
                        L"      const ctx = c.getContext('2d');"
                        L"      ctx.drawImage(img, 0, 0, 32, 32);"
                        L"      resolve(c.toDataURL('image/png'));"
                        L"    };"
                        L"    img.onerror = () => resolve('');"
                        L"    img.src = href;"
                        L"  });"
                        L"})()",
                        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                        [captureIdx, captureWnd](HRESULT, LPCWSTR resultJson) -> HRESULT {
                            if (!resultJson || !g_windows.count(captureWnd)) return S_OK;
                            auto& w2 = g_windows[captureWnd];
                            if (captureIdx >= (int)w2.tabs.size()) return S_OK;

                            // resultJson = "\"data:image/png;base64,AAAA...\""
                            std::wstring s(resultJson);
                            // strip leading/trailing quotes
                            if (s.size() >= 2 && s.front() == L'"') {
                                s = s.substr(1, s.size() - 2);
                                // unescape \\/ etc.
                                std::wstring clean;
                                for (size_t k = 0; k < s.size(); k++) {
                                    if (s[k] == L'\\' && k + 1 < s.size()) { clean += s[k+1]; k++; }
                                    else clean += s[k];
                                }
                                s = clean;
                            }
                            // data:image/png;base64, prefix কাটো
                            const std::wstring prefix = L"data:image/png;base64,";
                            if (s.find(prefix) != 0) return S_OK;
                            std::wstring b64w = s.substr(prefix.size());

                            // wstring → narrow string
                            std::string b64(b64w.begin(), b64w.end());

                            // Base64 decode
                            auto decodeB64 = [](const std::string& in) -> std::vector<BYTE> {
                                static const std::string tbl =
                                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                                std::vector<BYTE> out;
                                int val = 0, valb = -8;
                                for (unsigned char c : in) {
                                    if (c == '=') break;
                                    int pos = (int)tbl.find(c);
                                    if (pos == (int)std::string::npos) continue;
                                    val = (val << 6) + pos;
                                    valb += 6;
                                    if (valb >= 0) {
                                        out.push_back((BYTE)((val >> valb) & 0xFF));
                                        valb -= 8;
                                    }
                                }
                                return out;
                            };
                            std::vector<BYTE> pngBytes = decodeB64(b64);
                            if (pngBytes.empty()) return S_OK;

                            // PNG bytes → GDI+ Bitmap (IStream দিয়ে)
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, pngBytes.size());
                            if (!hMem) return S_OK;
                            void* pMem = GlobalLock(hMem);
                            if (!pMem) { GlobalFree(hMem); return S_OK; }
                            memcpy(pMem, pngBytes.data(), pngBytes.size());
                            GlobalUnlock(hMem);

                            IStream* pStream = nullptr;
                            if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream)) && pStream) {
                                auto bmp = std::make_shared<Bitmap>(pStream);
                                pStream->Release();
                                if (bmp && bmp->GetLastStatus() == Ok) {
                                    w2.tabs[captureIdx].favicon = bmp;
                                    InvalidateRect(captureWnd, NULL, FALSE);
                                }
                            }
                            return S_OK;
                        }).Get()
                    );
                }

                InvalidateRect(m_hWnd, NULL, FALSE);

                // ── ChatGPT / OpenAI post-load bot-check bypass ──
                {
                    const std::wstring& curUrl = w.tabs[m_tabIdx].url;
                    bool isChatGPT = (curUrl.find(L"chatgpt.com") != std::wstring::npos ||
                                      curUrl.find(L"openai.com")  != std::wstring::npos);
                    if (isChatGPT) {
                        sender->ExecuteScript(
                            L"(() => {"
                            // webdriver আবার remove করো (some SPAs re-check on route change)
                            L"  try { Object.defineProperty(navigator, 'webdriver', { get: () => undefined, configurable: true }); } catch(e){}"
                            // chrome object reinforce করো
                            L"  if (!window.chrome || !window.chrome.runtime) {"
                            L"    window.chrome = { app:{isInstalled:false}, csi:()=>({}), loadTimes:()=>({}), runtime:{}, webstore:{} };"
                            L"  }"
                            // vendor
                            L"  try { Object.defineProperty(navigator, 'vendor', { get: () => 'Google Inc.', configurable: true }); } catch(e){}"
                            // hardwareConcurrency
                            L"  try { Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => 8, configurable: true }); } catch(e){}"
                            // deviceMemory
                            L"  try { Object.defineProperty(navigator, 'deviceMemory', { get: () => 8, configurable: true }); } catch(e){}"
                            // connection
                            L"  try { if (!navigator.connection) Object.defineProperty(navigator, 'connection', { get: () => ({effectiveType:'4g',rtt:50,downlink:10,saveData:false}), configurable: true }); } catch(e){}"
                            // WebView2 specific flag remove
                            L"  try { delete window.__WebView2__; } catch(e){}"
                            L"  try { delete window.chrome.webview; } catch(e){}"
                            L"})();",
                            nullptr);
                    }
                }

                // ── Claude.ai / Anthropic post-load Cloudflare bypass ──
                {
                    const std::wstring& curUrl = w.tabs[m_tabIdx].url;
                    bool isClaude = (curUrl.find(L"claude.ai")     != std::wstring::npos ||
                                     curUrl.find(L"anthropic.com") != std::wstring::npos);
                    if (isClaude) {
                        sender->ExecuteScript(
                            L"(() => {"
                            // webdriver flag সরাও — Cloudflare এটা detect করে
                            L"  try { Object.defineProperty(navigator, 'webdriver', { get: () => undefined, configurable: true }); } catch(e){}"
                            // chrome object — Cloudflare window.chrome চেক করে
                            L"  if (!window.chrome || !window.chrome.runtime) {"
                            L"    window.chrome = { app:{isInstalled:false}, csi:()=>({}), loadTimes:()=>({}), runtime:{onConnect:()=>{},connect:()=>({})}, webstore:{} };"
                            L"  }"
                            // WebView2 specific markers সরাও
                            L"  try { delete window.__WebView2__; } catch(e){}"
                            L"  try { delete window.chrome.webview; } catch(e){}"
                            // vendor
                            L"  try { Object.defineProperty(navigator, 'vendor', { get: () => 'Google Inc.', configurable: true }); } catch(e){}"
                            // hardwareConcurrency — 0 হলে bot মনে হয়
                            L"  try { Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => 8, configurable: true }); } catch(e){}"
                            // deviceMemory
                            L"  try { Object.defineProperty(navigator, 'deviceMemory', { get: () => 8, configurable: true }); } catch(e){}"
                            // connection
                            L"  try { if (!navigator.connection) Object.defineProperty(navigator, 'connection', { get: () => ({effectiveType:'4g',rtt:50,downlink:10,saveData:false}), configurable: true }); } catch(e){}"
                            // plugins — empty হলে bot সন্দেহ হয়
                            L"  try { Object.defineProperty(navigator, 'plugins', { get: () => { const a=[1,2,3,4,5]; a.item=i=>a[i]; a.namedItem=()=>null; a.refresh=()=>{}; return a; }, configurable:true }); } catch(e){}"
                            // Cloudflare Turnstile এর জন্য setTimeout override — real browser simulation
                            L"  try { const _orig = window.setTimeout; window.setTimeout = function(fn, delay, ...args) { return _orig.call(this, fn, Math.max(delay||0, 1), ...args); }; } catch(e){}"
                            L"})();",
                            nullptr);
                    }
                }

                // ── Google Account Email/Name + Photo extract ──
                // accounts.google.com বা myaccount.google.com এ গেলে
                // signed-in user এর email, name, photo URL JS দিয়ে বের করো।
                {
                    const std::wstring& curUrl = w.tabs[m_tabIdx].url;
                    bool isGoogleAcct = (curUrl.find(L"accounts.google.com") != std::wstring::npos ||
                                         curUrl.find(L"myaccount.google.com") != std::wstring::npos ||
                                         curUrl.find(L"mail.google.com") != std::wstring::npos ||
                                         curUrl.find(L"google.com") != std::wstring::npos);
                    // ── Extract email + display name ──
                    if (isGoogleAcct) {
                        HWND captureWnd = m_hWnd;
                        sender->ExecuteScript(
                            L"(() => {"
                            // Try multiple Google DOM sources for email
                            L"  let email = '';"
                            L"  let name  = '';"
                            // a[href*='SignOutOptions'] data-email
                            L"  const eEl = document.querySelector('[data-email]');"
                            L"  if (eEl) email = eEl.getAttribute('data-email') || '';"
                            // aria-label on profile img
                            L"  if (!email) {"
                            L"    const imgEl = document.querySelector('img[aria-label*=\"@\"]');"
                            L"    if (imgEl) {"
                            L"      const lbl = imgEl.getAttribute('aria-label') || '';"
                            L"      const m = lbl.match(/[\\w.+-]+@[\\w-]+\\.[\\w.]+/);"
                            L"      if (m) email = m[0];"
                            L"    }"
                            L"  }"
                            // header profile button aria-label
                            L"  if (!email) {"
                            L"    const hdr = document.querySelector('[aria-label*=\"Google Account\"]');"
                            L"    if (hdr) {"
                            L"      const lbl = hdr.getAttribute('aria-label') || '';"
                            L"      const m = lbl.match(/[\\w.+-]+@[\\w-]+\\.[\\w.]+/);"
                            L"      if (m) email = m[0];"
                            L"      const nm = lbl.match(/^([^(]+)/);"
                            L"      if (nm) name = nm[1].trim();"
                            L"    }"
                            L"  }"
                            // myaccount page h1/h2 for name
                            L"  if (!name) {"
                            L"    const h = document.querySelector('h1,h2,[data-name]');"
                            L"    if (h) name = h.innerText || h.getAttribute('data-name') || '';"
                            L"    name = name.trim();"
                            L"  }"
                            L"  return JSON.stringify({email, name});"
                            L"})()",
                            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                            [captureWnd](HRESULT, LPCWSTR resultJson) -> HRESULT {
                                if (!resultJson) return S_OK;
                                std::wstring s(resultJson);
                                // Parse {"email":"...","name":"..."}
                                auto extractField = [&](const std::wstring& key) -> std::wstring {
                                    std::wstring search = L"\"" + key + L"\":\"";
                                    auto pos = s.find(search);
                                    if (pos == std::wstring::npos) return L"";
                                    pos += search.size();
                                    auto end = s.find(L'"', pos);
                                    if (end == std::wstring::npos) return L"";
                                    return s.substr(pos, end - pos);
                                };
                                std::wstring email = extractField(L"email");
                                std::wstring name  = extractField(L"name");
                                if (!email.empty()) {
                                    // AddGoogleAccount deduplicates automatically
                                    AddGoogleAccount(email, name);
                                    InvalidateRect(captureWnd, NULL, FALSE);
                                }
                                return S_OK;
                            }).Get());
                    }
                    if (isGoogleAcct) {
                        HWND captureWnd = m_hWnd;
                        sender->ExecuteScript(
                            L"(() => {"
                            // Google page এ signed-in user এর avatar img খোঁজো
                            L"  const img = document.querySelector("
                            L"    'img[src*=\"googleusercontent.com\"],"
                            L"     img[data-src*=\"googleusercontent.com\"],"
                            L"     [style*=\"googleusercontent.com\"]');"
                            L"  if (img) {"
                            L"    const src = img.src || img.getAttribute('data-src') || '';"
                            L"    if (src.includes('googleusercontent.com')) return src;"
                            L"  }"
                            // meta tag থেকেও চেষ্টা করো
                            L"  const og = document.querySelector('meta[property=\"og:image\"]');"
                            L"  if (og && og.content && og.content.includes('googleusercontent.com')) return og.content;"
                            L"  return '';"
                            L"})()",
                            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                            [captureWnd](HRESULT, LPCWSTR resultJson) -> HRESULT {
                                if (!resultJson) return S_OK;
                                std::wstring s(resultJson);
                                // strip JSON string quotes
                                if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
                                    s = s.substr(1, s.size() - 2);
                                if (s.empty() || s == L"null") return S_OK;
                                // =s96-c size suffix → বড় করো
                                auto pos = s.find(L"=s");
                                if (pos != std::wstring::npos) {
                                    auto end = s.find(L'-', pos+2);
                                    if (end != std::wstring::npos)
                                        s = s.substr(0, pos) + L"=s96-c";
                                    else
                                        s = s.substr(0, pos) + L"=s96-c";
                                }
                                // active profile এ save করো
                                if (auto* prof = GetActiveProfile()) {
                                    if (prof->activeAccount >= 0 && prof->activeAccount < (int)prof->accounts.size()) {
                                        prof->accounts[prof->activeAccount].photoUrl = s;
                                        LoadProfilePhotoAsync(s);
                                    }
                                }
                                return S_OK;
                            }).Get());
                    }
                }

                // ── AI Inject Script (platform-specific UI hiding) ──
                std::wstring injectScript = GetAiInjectScript(w.tabs[m_tabIdx].url);
                if (!injectScript.empty()) {
                    sender->ExecuteScript(injectScript.c_str(), nullptr);
                }

                // ── Layer 3b (AdBlocker.kt injectContentScanner()) ──
                // onPageFinished এর সমতুল্য — DOM text/image/meta scan করে adult
                // content ধরা পড়লে full-screen block overlay দেখায়।
                sender->ExecuteScript(GetContentScannerScript().c_str(), nullptr);

                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                BOOL canB = FALSE, canF = FALSE;
                sender->get_CanGoBack(&canB);
                sender->get_CanGoForward(&canF);
                w.tabs[m_tabIdx].canBack = !!canB;
                w.tabs[m_tabIdx].canFwd  = !!canF;
                InvalidateRect(m_hWnd, NULL, FALSE);

                return S_OK;
            }).Get(), nullptr);

        ComPtr<ICoreWebView2Controller3> ctl3;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl3)))) {
            EventRegistrationToken tok;
            ctl3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &tok);
        }

        // ── WebView focus → menu/panels বন্ধ করো ──
        // WebView-এ click করলে WM_LBUTTONDOWN আসে না, তাই
        // GotFocus event দিয়ে menu close করতে হয়।
        {
            EventRegistrationToken focusTok;
            ctl->add_GotFocus(
                Callback<ICoreWebView2FocusChangedEventHandler>(
                [this](ICoreWebView2Controller* /*sender*/, IUnknown* /*args*/) -> HRESULT {
                    if (!g_windows.count(m_hWnd)) return S_OK;
                    auto& w = g_windows[m_hWnd];
                    bool needRedraw = false;
                    if (w.isMenuOpen) {
                        w.isMenuOpen   = false;
                        w.hoverMenuIdx = -1;
                        RECT wvr = GetWebViewRect(m_hWnd);
                        if (w.active() && w.active()->controller)
                            w.active()->controller->put_Bounds(wvr);
                        needRedraw = true;
                    }
                    if (g_extensionPanelOpen) {
                        g_extensionPanelOpen = false;
                        // WebView full width restore করো
                        RECT wvr = GetWebViewRect(m_hWnd);
                        if (w.active() && w.active()->controller)
                            w.active()->controller->put_Bounds(wvr);
                        needRedraw = true;
                    }
                    if (g_bookmarkPanelOpen)  { g_bookmarkPanelOpen  = false; needRedraw = true; }
                    if (g_historyPanelOpen)   { g_historyPanelOpen   = false; needRedraw = true; }
                    if (g_downloadsPanelOpen) { g_downloadsPanelOpen = false; needRedraw = true; }
                    if (g_profilePanelOpen)   { g_profilePanelOpen   = false; needRedraw = true; }
                    if (needRedraw) InvalidateRect(m_hWnd, NULL, TRUE);
                    return S_OK;
                }).Get(),
                &focusTok);
        }

        bool isActive = (m_tabIdx == wd.activeTab);
        ctl->put_IsVisible(TRUE);  // Always visible — hiding causes visibilitychange -> YouTube pauses
        RECT wvr = isActive ? GetWebViewRect(m_hWnd) : RECT{-10000, -10000, -9000, -9000};
        ctl->put_Bounds(wvr);

        std::wstring nav = m_startUrl;
        if (!g_isPureViewerMode && (nav == L"RAS_BROWSER" || nav.empty() || nav == L"about:blank")) {
            nav = L"LOCAL_NTP"; 
        }
        
        if (nav == L"LOCAL_NTP") {
            tab.webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
        } else if (nav == L"LOCAL_HISTORY") {
            tab.webview->NavigateToString(GetLocalHistory_HTML(wd.isDarkMode).c_str());
        } else if (nav == L"LOCAL_DOWNLOADS") {
            tab.webview->NavigateToString(GetLocalDownloads_HTML(wd.isDarkMode).c_str());
        } else {
            tab.webview->Navigate(nav.c_str());
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 ENVIRONMENT HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    HWND  m_hWnd;
    int   m_tabIdx;
    ULONG m_ref = 1;

public:
    EnvCompletedHandler(HWND h, int idx) : m_hWnd(h), m_tabIdx(idx) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr) || !env) return S_OK;
        g_sharedEnv = env;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd  = g_windows[m_hWnd];
        auto& tab = wd.tabs[m_tabIdx];
        g_sharedEnv->CreateCoreWebView2Controller(
            m_hWnd, new TabControllerHandler(m_hWnd, m_tabIdx, tab.url));
        return S_OK;
    }
};

static void CreateWebViewForTab(HWND hWnd, int tabIdx) {
    if (!g_windows.count(hWnd)) return;
    auto& wd  = g_windows[hWnd];
    auto& tab = wd.tabs[tabIdx];

    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(
            hWnd, new TabControllerHandler(hWnd, tabIdx, tab.url));
    } else {
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(
            // Extension support
            L"--enable-features=msWebView2EnableExtensions "
            // GPU
            L"--enable-gpu-rasterization "
            L"--enable-zero-copy "
            // Bot detection bypass — সবচেয়ে গুরুত্বপূর্ণ
            L"--disable-blink-features=AutomationControlled "
            // Google Sign-in, OAuth popup, general compatibility
            // NOTE: সব --disable-features একটায় merge করা হয়েছে (একাধিক flag দিলে শেষেরটা override করে)
            L"--disable-features=SameSiteByDefaultCookies,CookiesWithoutSameSiteMustBeSecure,BlockInsecurePrivateNetworkRequests,Translate "
            L"--no-first-run "
            L"--no-default-browser-check "
            // Web Audio API — কিছু site sign-in verify তে ব্যবহার করে
            L"--autoplay-policy=no-user-gesture-required"
        );

        // Use active profile's user data folder (Chrome-style isolation)
        BrowserProfile* prof = GetActiveProfile();
        std::wstring udDir;
        if (prof) {
            udDir = prof->userDataFolder();
        } else {
            wchar_t appDataPath[MAX_PATH];
            SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath);
            udDir = std::wstring(appDataPath) + L"\\RasBrowserData\\Profile_1";
        }
        CreateDirectoryW(udDir.c_str(), NULL);

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, udDir.c_str(), options.Get(), new EnvCompletedHandler(hWnd, tabIdx));

        if (FAILED(hr)) {
            CreateCoreWebView2EnvironmentWithOptions(
                nullptr, nullptr, nullptr, new EnvCompletedHandler(hWnd, tabIdx));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DWM SHADOW HELPER
// ─────────────────────────────────────────────────────────────────────────────
static void ApplyDwmShadow(HWND hWnd) {
    MARGINS m = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(hWnd, &m);
    DWORD pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW PROCEDURE
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        LRESULT def = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (def == HTNOWHERE || def == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            UINT dpi    = GetWndDpi(hWnd);
            int  border = S(8, dpi);

            if (!g_windows.count(hWnd) || !g_windows[hWnd].isFullScreen) {
                if (pt.y < border && pt.x < border)                          return HTTOPLEFT;
                if (pt.y < border && pt.x >= cr.right - border)              return HTTOPRIGHT;
                if (pt.y >= cr.bottom - border && pt.x < border)             return HTBOTTOMLEFT;
                if (pt.y >= cr.bottom - border && pt.x >= cr.right - border) return HTBOTTOMRIGHT;
                if (pt.y < border)              return HTTOP;
                if (pt.y >= cr.bottom - border) return HTBOTTOM;
                if (pt.x < border)              return HTLEFT;
                if (pt.x >= cr.right - border)  return HTRIGHT;

                if (g_isPureViewerMode) {
                    int winBtnX = cr.right - WinBtnW(dpi) * 6; 
                    if (pt.y < TitleBarH(dpi)) {
                        if (pt.x >= winBtnX) return HTCLIENT; 
                        return HTCAPTION; 
                    }
                    return HTCLIENT;
                }

                if (pt.y < TitleBarH(dpi)) {
                    int winBtnX = cr.right - WinBtnW(dpi) * 6; 
                    if (pt.x >= winBtnX) return HTCLIENT; 
                    
                    bool onTab = false;
                    auto& wd = g_windows[hWnd];
                    int tc = (int)wd.tabs.size();
                    for (int i = 0; i < tc; i++) {
                        RECT tr = GetTabRect(cr.right, i, tc, dpi);
                        if (pt.x >= tr.left && pt.x < tr.right) { onTab = true; break; }
                    }
                    if (onTab || pt.x < LogoW(dpi)) return HTCLIENT; 
                    
                    RECT ntr = GetNewTabBtnRect(cr.right, tc, dpi);
                    if (pt.x >= ntr.left && pt.x <= ntr.right) return HTCLIENT; 

                    return HTCAPTION; 
                }
                if (pt.y < NavTotalH(hWnd)) return HTCLIENT;
            }
            return HTCLIENT;
        }
        return def;
    }

    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) {
            ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        break;

    case WM_CREATE:
        ApplyDwmShadow(hWnd);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        DrawBrowser(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND: return 1;

    case WM_WINDOWPOSCHANGING: {
        auto* wp = (WINDOWPOS*)lParam;
        wp->flags |= SWP_NOCOPYBITS;
        break;
    }

    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd) && (HWND)lParam == g_windows[hWnd].hAddressBar) {
            HDC hEdit = (HDC)wParam;
            bool isDark = g_windows[hWnd].isDarkMode;
            if (isDark) {
                SetTextColor(hEdit, RGB(255, 255, 255));
                SetBkColor  (hEdit, RGB(26, 26, 26)); 
                static HBRUSH hBrDark = CreateSolidBrush(RGB(26, 26, 26));
                return (LRESULT)hBrDark;
            } else {
                SetTextColor(hEdit, RGB(32, 33, 36));
                SetBkColor  (hEdit, RGB(241, 243, 244));
                static HBRUSH hBrLight = CreateSolidBrush(RGB(241, 243, 244));
                return (LRESULT)hBrLight;
            }
        }
        break;
    }

    case WM_SIZE: {
        if (!g_windows.count(hWnd)) break;
        auto& wd = g_windows[hWnd];
        if (wParam == SIZE_MINIMIZED) {
            // Window minimize হলে WebView2 কে off-screen এ রাখো।
            // put_IsVisible(FALSE) করলে visibilitychange event fire হয়
            // যেটা YouTube/media pause করে দেয়। Off-screen এ রাখলে
            // browser engine মনে করে tab visible আছে — media চলতে থাকে।
            RECT offscreen = {-10000, -10000, -9000, -9000};
            for (int i = 0; i < (int)wd.tabs.size(); i++)
                if (wd.tabs[i].controller)
                    wd.tabs[i].controller->put_Bounds(offscreen);
        } else {
            // Restore বা resize — normal bounds দাও
            RepositionAddressBar(hWnd);
            RECT wvr = GetWebViewRect(hWnd);
            for (int i = 0; i < (int)wd.tabs.size(); i++)
                if (wd.tabs[i].controller && i == wd.activeTab)
                    wd.tabs[i].controller->put_Bounds(wvr);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_DPICHANGED: {
        const RECT* newRect = (const RECT*)lParam;
        SetWindowPos(hWnd, NULL,
            newRect->left, newRect->top,
            newRect->right  - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        RepositionAddressBar(hWnd);
        RECT wvr = GetWebViewRect(hWnd);
        if (g_windows.count(hWnd))
            for (auto& tab : g_windows[hWnd].tabs)
                if (tab.controller) tab.controller->put_Bounds(wvr);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;
        bool dirty = false;

        { TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 }; TrackMouseEvent(&tme); }

        int titleH  = TitleBarH(dpi);
        int navH    = NavTotalH(hWnd);
        int winBtnW = WinBtnW(dpi);
        int toolY   = titleH;

        {
            int bx = W - winBtnW * 6; 
            bool fc = (y < titleH && x >= bx             && x < bx + winBtnW);
            bool p  = (y < titleH && x >= bx + winBtnW   && x < bx + winBtnW*2);
            bool dk = (y < titleH && x >= bx + winBtnW*2 && x < bx + winBtnW*3);
            bool nm = (y < titleH && x >= bx + winBtnW*3 && x < bx + winBtnW*4);
            bool mx = (y < titleH && x >= bx + winBtnW*4 && x < bx + winBtnW*5);
            bool cl = (y < titleH && x >= bx + winBtnW*5);
            if (wd.hFocus!=fc||wd.hPin!=p||wd.hDark!=dk||wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl)
                { wd.hFocus=fc; wd.hPin=p; wd.hDark=dk; wd.hMin=nm; wd.hMax=mx; wd.hClose=cl; dirty=true; }
        }

        if (!g_isPureViewerMode) {
            {
                int tc = (int)wd.tabs.size();
                int prev = wd.hoverTabIndex; wd.hoverTabIndex = -1;
                for (int i = 0; i < tc; i++) {
                    RECT tr = GetTabRect(W, i, tc, dpi);
                    if (x >= tr.left && x < tr.right && y >= tr.top && y < tr.bottom)
                    { wd.hoverTabIndex = i; break; }
                }
                if (prev != wd.hoverTabIndex) dirty = true;
            }

            {
                RECT ntr = GetNewTabBtnRect(W, (int)wd.tabs.size(), dpi);
                bool nt = (x>=ntr.left&&x<ntr.right&&y>=ntr.top&&y<ntr.bottom);
                if (wd.hNewTab != nt) { wd.hNewTab = nt; dirty = true; }
            }

            {
                int btnStep = S(36, dpi);
                int cx = S(8, dpi);
                bool b  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(36,dpi)); cx+=btnStep;
                bool f  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(36,dpi)); cx+=btnStep;
                bool rl = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(36,dpi));
                if (wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl)
                    { wd.hBack=b; wd.hFwd=f; wd.hRel=rl; dirty=true; }

                int rx = W - S(36*3+8, dpi);
                // Draw order: Ext → Profile → Menu (hover detection must match!)
                bool e  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep;
                bool pr = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep;
                bool m  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(36,dpi));
                if (wd.hProfile!=pr||wd.hExt!=e||wd.hMenu!=m)
                    { wd.hProfile=pr; wd.hExt=e; wd.hMenu=m; dirty=true; }
            }
            
            // 🟢 Menu Overlay Hover Logic — FIX: mY now computed via GetMenuY()
            if (wd.isMenuOpen) {
                float menuW  = (float)S(320, dpi);
                float mX     = (float)W - menuW - (float)S(10, dpi);
                float mY     = GetMenuY(hWnd, dpi);   // ← WAS UNDECLARED
                int   itemH  = S(34, dpi);
                float currY  = mY + (float)S(10, dpi);
                int   hoverIdx   = -1;
                int   itemIndex  = 0;
                
                for (int i = 0; i < kMenuTypeCount; i++) {
                    int t = kMenuTypes[i];
                    float h = (t == 2) ? (float)S(54,dpi) : (t == 1) ? (float)S(11,dpi) : (float)itemH;
                    if (t != 1) {
                        if ((float)x >= mX && (float)x <= mX + menuW &&
                            (float)y >= currY && (float)y <= currY + h)
                            hoverIdx = itemIndex;
                        itemIndex++;
                    }
                    currY += h;
                }
                // Dismiss hover if outside menu
                if ((float)x < mX || (float)x > mX + menuW || (float)y < mY || (float)y > currY)
                    hoverIdx = -1;

                if (wd.hoverMenuIdx != hoverIdx) {
                    wd.hoverMenuIdx = hoverIdx;
                    dirty = true;
                }
            }
        }

        if (dirty) {
            RECT r = { 0, 0, W, navH };
            InvalidateRect(hWnd, &r, FALSE);
            if (wd.isMenuOpen) InvalidateRect(hWnd, NULL, FALSE);
        }

        // ── Panel hover updates ──
        if (g_bookmarkPanelOpen || g_historyPanelOpen || g_downloadsPanelOpen ||
            g_findBarOpen || g_contextMenuOpen || g_extensionPanelOpen) {
            if (g_historyPanelOpen) {
                // history hover index 업데이트
                UINT dpi2 = GetWndDpi(hWnd);
                int panelY2  = TitleBarH(dpi2) + ToolbarH(dpi2);
                int headerH2 = MulDiv(56, (int)dpi2, 96);
                int itemH2   = MulDiv(52, (int)dpi2, 96);
                int newHover = -1;
                if (y > panelY2 + headerH2) {
                    int relY2 = y - panelY2 - headerH2;
                    int idx2  = relY2 / itemH2 + g_historyScrollOffset;
                    if (idx2 >= 0 && idx2 < (int)g_history.size()) newHover = idx2;
                }
                if (g_historyHoverIdx != newHover) {
                    g_historyHoverIdx = newHover;
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }
            if (g_bookmarkPanelOpen || g_downloadsPanelOpen ||
                g_contextMenuOpen   || g_extensionPanelOpen) {
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd = g_windows[hWnd];
            wd.hMin=wd.hMax=wd.hClose=false;
            wd.hBack=wd.hFwd=wd.hRel=false;
            wd.hPin=wd.hDark=wd.hFocus=wd.hProfile=wd.hExt=wd.hMenu=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            RECT cr; GetClientRect(hWnd, &cr);
            cr.bottom = NavTotalH(hWnd);
            InvalidateRect(hWnd, &cr, FALSE);
            if (wd.isMenuOpen) InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;
        RECT crFull; GetClientRect(hWnd, &crFull); int H = crFull.bottom;

        if (wd.hMin)   { ShowWindow(hWnd, SW_MINIMIZE); break; }
        if (wd.hMax)   { ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE); break; }
        if (wd.hClose) { DestroyWindow(hWnd); break; }

        // ── Context Menu Click ──
        if (g_contextMenuOpen) {
            std::wstring action = HandleContextMenuClick(x, y, (int)dpi);
            CloseContextMenu();
            if (!action.empty() && action != L"" && wd.active()) {
                if      (action == L"back"    && wd.active()->webview && wd.active()->canBack) wd.active()->webview->GoBack();
                else if (action == L"forward" && wd.active()->webview && wd.active()->canFwd)  wd.active()->webview->GoForward();
                else if (action == L"reload"  && wd.active()->webview) wd.active()->webview->Reload();
                else if (action == L"open_new_tab" && wd.active()) AddTab(hWnd, wd.active()->url);
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // ── Find Bar Click ──
        if (g_findBarOpen) {
            bool closed = HandleFindBarClick(x, y, W, H, (int)dpi);
            if (closed) { CloseFindBar(); InvalidateRect(hWnd, NULL, FALSE); return 0; }
        }

        // ── Bookmark Bar Click — শুধু home page (NTP) এ কাজ করবে ──
        if (wd.active() && (wd.active()->url == L"LOCAL_NTP" || wd.active()->url == L"about:blank"))
        {
            int bmkY = TitleBarH(dpi) + ToolbarH(dpi);
            int bmkH = S(D_BOOKMARK_H, dpi);
            std::wstring bmkUrl = HandleBookmarkBarClick(x, y, W, bmkY, bmkH, (int)dpi);
            if (!bmkUrl.empty()) {
                if (wd.active() && wd.active()->webview)
                    wd.active()->webview->Navigate(bmkUrl.c_str());
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // ── Bookmark Panel Click ──
        if (g_bookmarkPanelOpen) {
            std::wstring navUrl;
            int removeIdx = -1;
            bool hit = HandleBookmarkPanelClick(x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi, navUrl, removeIdx);
            if (removeIdx >= 0) {
                RemoveBookmark(removeIdx);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (!navUrl.empty()) {
                g_bookmarkPanelOpen = false;
                if (wd.active() && wd.active()->webview) wd.active()->webview->Navigate(navUrl.c_str());
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (hit) return 0; // click was inside panel but no action
        }

        // ── History Panel Click ──
        if (g_historyPanelOpen) {
            std::wstring navUrl = HandleHistoryPanelClick(x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi);
            if (navUrl == L"__cleared__") {
                // cleared — stay open, just refresh
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (!navUrl.empty()) {
                // g_historyPanelOpen already set false in handler
                if (wd.active() && wd.active()->webview) wd.active()->webview->Navigate(navUrl.c_str());
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            // empty return = delete click (refresh) OR click was inside panel
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // ── Downloads Panel Click ──
        if (g_downloadsPanelOpen) {
            std::wstring action = HandleDownloadsPanelClick(x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi);
            if (action == L"clear_all") {
                ClearCompletedDownloads();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (action.substr(0, 10) == L"open_file:") {
                std::wstring path = action.substr(10);
                ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return 0;
            } else if (action.substr(0, 12) == L"open_folder:") {
                std::wstring path = action.substr(12);
                ShellExecuteW(NULL, L"explore", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return 0;
            }
        }

        // ── Profile Panel Click ──
        if (g_profilePanelOpen) {
            auto S = [&](int v){ return v * dpi / 96; };
            int pw     = S(kProfPanelW);
            int panelX = W - pw - S(8);
            int panelY = S(36 + 36);
            int ph     = GetProfilePanelHeight((int)dpi);

            auto ClosePanel = [&]() {
                g_profilePanelOpen = false;
                g_profileView      = ProfilePanelView::MAIN;
                g_profileAddName   = L"";
                g_profileAddNameActive = false;
                RECT wvr = GetWebViewRect(hWnd);
                if (g_windows[hWnd].active() && g_windows[hWnd].active()->controller)
                    g_windows[hWnd].active()->controller->put_Bounds(wvr);
                InvalidateRect(hWnd, NULL, TRUE);
            };

            // Outside panel → close
            if (x < panelX || x >= panelX + pw || y < panelY || y >= panelY + ph) {
                ClosePanel();
                return 0;
            }

            // ── MAIN view clicks ──────────────────────────────────────────
            if (g_profileView == ProfilePanelView::MAIN) {
                if (g_profileAccountHover >= 0) {
                    // Switch active Google account within this profile
                    SwitchGoogleAccount(g_profileAccountHover);
                    ClosePanel();
                } else if (g_profilePanelHover == 0) {
                    // "Manage your Google Account" pill
                    ClosePanel();
                    AddTab(hWnd, L"https://myaccount.google.com");
                } else if (g_profilePanelHover == 20) {
                    // "Add Google Account" → open Google AddSession in new tab
                    // When user signs in, NavigationCompleted will detect the new account
                    ClosePanel();
                    AddTab(hWnd, L"https://accounts.google.com/AddSession");
                } else if (g_profilePanelHover == 21) {
                    // "Sign out of all accounts"
                    if (auto* prof = GetActiveProfile()) {
                        prof->accounts.clear();
                        prof->activeAccount = -1;
                        SaveProfiles();
                    }
                    ClosePanel();
                    AddTab(hWnd, L"https://accounts.google.com/Logout");
                } else if (g_profilePanelHover == 10) {
                    // "Add new profile" → switch to ADD_PROFILE view
                    g_profileView      = ProfilePanelView::ADD_PROFILE;
                    g_profileAddName   = L"";
                    g_profileAddNameActive = true;
                    InvalidateRect(hWnd, NULL, TRUE);
                } else if (g_profilePanelHover == 11) {
                    // "All profiles" → switch to ACCOUNTS view
                    g_profileView = ProfilePanelView::ACCOUNTS;
                    InvalidateRect(hWnd, NULL, TRUE);
                }
            }
            // ── ACCOUNTS view clicks ──────────────────────────────────────
            else if (g_profileView == ProfilePanelView::ACCOUNTS) {
                if (g_profileCardHover >= 0 && g_profileCardHover < (int)g_profiles.size()) {
                    // Switch to clicked profile — Chrome-style: new browser window for each profile
                    if (g_profileCardHover != g_activeProfileIdx) {
                        std::wstring newUd = SwitchProfile(g_profileCardHover);
                        // Open new RasBrowser window with new profile's user data folder
                        // Pass the profile user-data-dir as a command-line argument
                        wchar_t exePath[MAX_PATH] = {};
                        GetModuleFileNameW(NULL, exePath, MAX_PATH);
                        std::wstring args = L"\"" + std::wstring(exePath) + L"\" -profile \"" + newUd + L"\"";
                        STARTUPINFOW si = {}; si.cb = sizeof(si);
                        PROCESS_INFORMATION pi = {};
                        CreateProcessW(NULL, (LPWSTR)args.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
                        if (pi.hProcess) CloseHandle(pi.hProcess);
                        if (pi.hThread)  CloseHandle(pi.hThread);
                    }
                    ClosePanel();
                    InvalidateRect(hWnd, NULL, TRUE);
                } else if (g_profileCardHover == 100) {
                    // Add new profile from accounts view
                    g_profileView    = ProfilePanelView::ADD_PROFILE;
                    g_profileAddName = L"";
                    g_profileAddNameActive = true;
                    InvalidateRect(hWnd, NULL, TRUE);
                } else if (g_profileCardHover == 101) {
                    // Back to main
                    g_profileView = ProfilePanelView::MAIN;
                    InvalidateRect(hWnd, NULL, TRUE);
                }
            }
            // ── ADD_PROFILE view clicks ───────────────────────────────────
            else if (g_profileView == ProfilePanelView::ADD_PROFILE) {
                auto S2 = [&](int v){ return v * (int)dpi / 96; };
                // Click on input box → activate text input
                int inpY = panelY + S2(16) + S2(32) + S2(44) + S2(12);
                int inpH = S2(36);
                if (y >= inpY && y < inpY + inpH &&
                    x >= panelX + S2(12) && x < panelX + S2(12) + (pw - S2(24))) {
                    g_profileAddNameActive = true;
                    InvalidateRect(hWnd, NULL, TRUE);
                }
                if (g_profileCardHover == 200 && !g_profileAddName.empty()) {
                    // Create profile
                    AddProfile(g_profileAddName);
                    g_activeProfileIdx = (int)g_profiles.size() - 1;
                    SaveProfiles();
                    g_profileView    = ProfilePanelView::ACCOUNTS;
                    g_profileAddName = L"";
                    g_profileAddNameActive = false;
                    InvalidateRect(hWnd, NULL, TRUE);
                } else if (g_profileCardHover == 101) {
                    // Back
                    g_profileView = ProfilePanelView::ACCOUNTS;
                    g_profileAddName = L"";
                    g_profileAddNameActive = false;
                    InvalidateRect(hWnd, NULL, TRUE);
                }
            }
            return 0;
        }

        // ── Extension Panel Click ──
        if (g_extensionPanelOpen) {
            std::wstring action = HandleExtensionPanelClick(
                x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi);
            if (action == L"close" || action == L"manage") {
                g_extensionPanelOpen = false;
                // WebView full width restore করো
                {
                    RECT wvr = GetWebViewRect(hWnd);
                    if (wd.active() && wd.active()->controller)
                        wd.active()->controller->put_Bounds(wvr);
                }
                InvalidateRect(hWnd, NULL, TRUE);
                return 0;
            } else if (action == L"inside") {
                // Extension item area-তে click (pill ছাড়া) — consume করো, fall-through না
                return 0;
            } else if (action.substr(0, 7) == L"toggle:") {
                int idx = std::stoi(action.substr(7));
                // Live inject: active WebView pointer pass করো
                // ToggleExtension() সাথে সাথে enable/disable JS inject করবে
                ICoreWebView2* wv2 = nullptr;
                if (wd.active() && wd.active()->controller)
                    wd.active()->controller->get_CoreWebView2(&wv2);
                ToggleExtension(wv2, idx);
                if (wv2) {
                    // g_extensionScriptsDirty = true → পরের NavigationStarting এ
                    // AddScriptToExecuteOnDocumentCreated re-register হবে।
                    // কিন্তু current page এ সাথে সাথে reload ছাড়াই কাজ করবে
                    // কারণ ToggleExtension() নিজেই ExecuteScript() করে।
                    wv2->Release();
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (action.substr(0, 7) == L"remove:") {
                int idx = std::stoi(action.substr(7));
                UninstallExtension(idx);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            // Consume click inside panel (don't fall through)
            if (action != L"") return 0;
        }

        if (!g_isPureViewerMode) {
            
            // 🟢 Handle Menu Open/Click interactions
            if (wd.isMenuOpen) {
                float menuW = (float)S(320, dpi);
                float mX    = (float)W - menuW - (float)S(10, dpi);
                float mY    = GetMenuY(hWnd, dpi);   // ← WAS UNDECLARED
                
                int   itemH     = S(34, dpi);
                float currY     = mY + (float)S(10, dpi);
                int   clickIdx  = -1;
                int   itemIndex = 0;
                
                for (int i = 0; i < kMenuTypeCount; i++) {
                    int t = kMenuTypes[i];
                    float h = (t == 2) ? (float)S(54,dpi) : (t == 1) ? (float)S(11,dpi) : (float)itemH;
                    if (t != 1) {
                        if ((float)x >= mX && (float)x <= mX + menuW &&
                            (float)y >= currY && (float)y <= currY + h)
                            clickIdx = itemIndex;
                        itemIndex++;
                    }
                    currY += h;
                }

                wd.isMenuOpen   = false;
                wd.hoverMenuIdx = -1;
                // WebView bounds full restore করো
                {
                    RECT wvr = GetWebViewRect(hWnd);
                    if (wd.active() && wd.active()->controller)
                        wd.active()->controller->put_Bounds(wvr);
                }
                InvalidateRect(hWnd, NULL, TRUE);

                if (clickIdx != -1) {
                    // Helper lambda: সব panel বন্ধ করো
                    auto closeAllPanels = [&]() {
                        g_historyPanelOpen   = false;
                        g_bookmarkPanelOpen  = false;
                        g_downloadsPanelOpen = false;
                        g_extensionPanelOpen = false;
                    };
                    if      (clickIdx == 0) {
                        // Profile — Google account নতুন tab এ
                        AddTab(hWnd, L"https://myaccount.google.com");
                    }
                    else if (clickIdx == 1) AddTab(hWnd, L"LOCAL_NTP");
                    else if (clickIdx == 2) LaunchMiniBrowser(L"LOCAL_NTP", L"New Window");
                    else if (clickIdx == 3) {
                        // History — Chrome-style নতুন tab এ খোলো
                        closeAllPanels();
                        AddTab(hWnd, L"LOCAL_HISTORY");
                    }
                    else if (clickIdx == 4) {
                        // Downloads — Chrome-style নতুন tab এ খোলো
                        closeAllPanels();
                        AddTab(hWnd, L"LOCAL_DOWNLOADS");
                    }
                    else if (clickIdx == 5) {
                        // Bookmarks — full overlay
                        closeAllPanels();
                        g_bookmarkPanelOpen = true;
                        LoadBookmarks();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else if (clickIdx == 6) {
                        // Extensions — full overlay (WebView shrink)
                        closeAllPanels();
                        g_extensionPanelOpen = true;
                        ScanExtensionsFolderPublic();
                        RECT wvr = GetWebViewRect(hWnd);
                        if (wd.active() && wd.active()->controller)
                            wd.active()->controller->put_Bounds(wvr);
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    // ── clickIdx 7 = Dark Theme Toggle (all websites) ────────────
                    else if (clickIdx == 7) {
                        g_forceSiteDark = !g_forceSiteDark;
                        // সব open tabs এ inject/remove dark CSS করো
                        for (auto& tab : wd.tabs) {
                            if (!tab.webview) continue;
                            if (g_forceSiteDark) {
                                tab.webview->ExecuteScript(
                                    L"(() => {"
                                    L"  if (document.getElementById('ras-force-dark')) return;"
                                    L"  const s = document.createElement('style');"
                                    L"  s.id = 'ras-force-dark';"
                                    L"  s.textContent = 'html{filter:invert(90%) hue-rotate(180deg)!important}'"
                                    L"    + 'img,video,iframe,canvas,picture,svg image,[style*=\"background-image\"]'"
                                    L"    + '{filter:invert(100%) hue-rotate(180deg)!important}';"
                                    L"  (document.head||document.documentElement).appendChild(s);"
                                    L"})()", nullptr);
                            } else {
                                tab.webview->ExecuteScript(
                                    L"try{const s=document.getElementById('ras-force-dark');if(s)s.remove();}catch(e){}",
                                    nullptr);
                            }
                        }
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    // ── clickIdx 8 = YouTube Mobile/Desktop Mode Toggle ───────────
                    else if (clickIdx == 8) {
                        g_youtubeDesktopMode = !g_youtubeDesktopMode;
                        if (auto* tab = wd.active()) {
                            if (tab->webview) {
                                std::wstring curUrl = tab->url;
                                bool onYT = (curUrl.find(L"youtube.com") != std::wstring::npos);
                                if (onYT) {
                                    std::wstring targetUrl = g_youtubeDesktopMode
                                        ? L"https://www.youtube.com"
                                        : L"https://m.youtube.com";
                                    tab->webview->Navigate(targetUrl.c_str());
                                }
                            }
                        }
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    // ── clickIdx 9 = YouTube Ad Block Toggle ─────────────────────
                    else if (clickIdx == 9) {
                        g_youtubeAdBlockEnabled = !g_youtubeAdBlockEnabled;
                        if (auto* tab = wd.active()) {
                            if (tab->webview) {
                                std::wstring flagScript = g_youtubeAdBlockEnabled
                                    ? L"window.__rasAdBlockEnabled=true;"
                                    : L"window.__rasAdBlockEnabled=false;";
                                tab->webview->ExecuteScript(flagScript.c_str(), nullptr);
                                tab->webview->Reload();
                            }
                        }
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    else if (clickIdx == 10) AddTab(hWnd, L"https://gemini.google.com/app");
                    else if (clickIdx == 11) {
                        // Settings
                        if (auto* tab = wd.active()) {
                            if (tab->webview) {
                                tab->webview->NavigateToString(
                                    GetSettingsPageHTML(wd.isDarkMode).c_str());
                            }
                        }
                    }
                    else if (clickIdx == 12) DestroyWindow(hWnd);
                }
                // ── menu বন্ধ হয়েছে — সবসময় return 0 করো ──
                // নইলে নিচে wd.hMenu check আবার menu toggle করে ফেলে
                return 0;
            }

            {
                int tc = (int)wd.tabs.size();
                for (int i = 0; i < tc; i++) {
                    RECT tr = GetTabRect(W, i, tc, dpi);
                    if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom) {
                        if (x >= tr.right - S(26, dpi)) { CloseTab(hWnd, i); return 0; }
                        SwitchToTab(hWnd, i);
                        return 0;
                    }
                }
            }

            if (wd.hNewTab) { AddTab(hWnd, L"LOCAL_NTP"); break; }

            if (auto* tab = wd.active()) {
                if (wd.hBack && tab->webview && tab->canBack) tab->webview->GoBack();
                if (wd.hFwd  && tab->webview && tab->canFwd)  tab->webview->GoForward();
                if (wd.hRel  && tab->webview)                 tab->webview->Reload();
            }

            if (wd.hProfile) {
                g_profilePanelOpen   = !g_profilePanelOpen;
                g_extensionPanelOpen = false;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                // WebView shrink/restore করো — profile panel GDI+ এ draw হয়,
                // WebView2 সবসময় GDI+ এর উপরে থাকে তাই panel area shrink করতে হয়
                {
                    RECT wvr = GetWebViewRect(hWnd);
                    if (wd.active() && wd.active()->controller)
                        wd.active()->controller->put_Bounds(wvr);
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }
            if (wd.hExt) {
                g_extensionPanelOpen = !g_extensionPanelOpen;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                if (g_extensionPanelOpen) ScanExtensionsFolderPublic();
                // WebView shrink/restore করো — GDI+ panel এর উপরে আসতে
                {
                    RECT wvr = GetWebViewRect(hWnd);
                    if (wd.active() && wd.active()->controller)
                        wd.active()->controller->put_Bounds(wvr);
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }
            
            if (wd.hMenu) { 
                wd.isMenuOpen = !wd.isMenuOpen;
                // WebView bounds update করো — menu area খালি/ভরাট করতে
                RECT wvr = GetWebViewRect(hWnd);
                if (wd.active() && wd.active()->controller)
                    wd.active()->controller->put_Bounds(wvr);
                InvalidateRect(hWnd, NULL, TRUE);
                return 0;
            }
        }

        if (wd.hFocus) {
            ToggleFocusMode(hWnd);
        }

        if (wd.hPin) { 
            wd.isPinned = !wd.isPinned;
            SetWindowPos(hWnd, wd.isPinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        
        if (wd.hDark) {
            wd.isDarkMode = !wd.isDarkMode;

            const std::wstring darkCss =
                L"(() => {"
                L"  if (document.getElementById('ras-force-dark')) return;"
                L"  const s = document.createElement('style');"
                L"  s.id = 'ras-force-dark';"
                L"  s.textContent = 'html { filter: invert(90%) hue-rotate(180deg) !important; }'"
                L"    + 'img, video, iframe, canvas, picture, svg image, [style*=\"background-image\"] '"
                L"    + '{ filter: invert(100%) hue-rotate(180deg) !important; }';"
                L"  (document.head || document.documentElement).appendChild(s);"
                L"})();";

            // ── সব existing tabs এ dark/light apply করো ──
            for (int i = 0; i < (int)wd.tabs.size(); i++) {
                auto& tab = wd.tabs[i];
                if (!tab.webview || !tab.controller) continue;

                // Background color update
                ComPtr<ICoreWebView2Controller2> ctl2;
                if (SUCCEEDED(tab.controller->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
                    COREWEBVIEW2_COLOR bg = wd.isDarkMode
                        ? COREWEBVIEW2_COLOR{255, 30, 30, 30}
                        : COREWEBVIEW2_COLOR{255, 255, 255, 255};
                    ctl2->put_DefaultBackgroundColor(bg);
                }

                // পুরনো dark script unregister করো (navigate করলেও আর চলবে না)
                if (!tab.darkScriptId.empty()) {
                    tab.webview->RemoveScriptToExecuteOnDocumentCreated(tab.darkScriptId.c_str());
                    tab.darkScriptId.clear();
                }

                // Local pages reload করো
                if (tab.url == L"LOCAL_NTP" || tab.url == L"about:blank") {
                    tab.webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
                    continue;
                }
                if (tab.url.find(L"blocked by rasfocus") != std::wstring::npos) {
                    tab.webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
                    continue;
                }

                if (wd.isDarkMode) {
                    // Dark on: current page এ inject + নতুন navigations এর জন্য register
                    tab.webview->ExecuteScript(
                        L"(() => {"
                        L"  if (document.getElementById('ras-force-dark')) return;"
                        L"  const s = document.createElement('style');"
                        L"  s.id = 'ras-force-dark';"
                        L"  s.textContent = 'html { filter: invert(90%) hue-rotate(180deg) !important; }'"
                        L"    + 'img, video, iframe, canvas, picture, svg image, [style*=\"background-image\"] '"
                        L"    + '{ filter: invert(100%) hue-rotate(180deg) !important; }';"
                        L"  (document.head || document.documentElement).appendChild(s);"
                        L"})();",
                        nullptr);

                    // Re-register করো যাতে এই tab এ পরের navigate এও persist করে
                    int capturedIdx = i;
                    HWND capturedHwnd = hWnd;
                    tab.webview->AddScriptToExecuteOnDocumentCreated(
                        darkCss.c_str(),
                        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                            [capturedHwnd, capturedIdx](HRESULT hr, LPCWSTR id) -> HRESULT {
                                if (SUCCEEDED(hr) && id && g_windows.count(capturedHwnd)) {
                                    auto& w = g_windows[capturedHwnd];
                                    if (capturedIdx >= 0 && capturedIdx < (int)w.tabs.size())
                                        w.tabs[capturedIdx].darkScriptId = id;
                                }
                                return S_OK;
                            }).Get());
                } else {
                    // Light on: current page থেকে CSS সরাও (script unregister হয়ে গেছে উপরে)
                    tab.webview->ExecuteScript(
                        L"(() => {"
                        L"  try { const s = document.getElementById('ras-force-dark'); if (s) s.remove(); } catch(e) {}"
                        L"})();",
                        nullptr);
                }
            }

            InvalidateRect(hWnd, NULL, TRUE);
            InvalidateRect(wd.hAddressBar, NULL, TRUE);
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)) break;
        if (g_isPureViewerMode) break; 
        UINT dpi = GetWndDpi(hWnd);
        int y = GET_Y_LPARAM(lParam);
        int x = GET_X_LPARAM(lParam);
        if (y < TitleBarH(dpi) && x > LogoW(dpi)) {
            AddTab(hWnd, L"LOCAL_NTP");
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        UINT dpi = GetWndDpi(hWnd);
        auto* mm = (LPMINMAXINFO)lParam;
        mm->ptMinTrackSize.x = S(380, dpi);
        mm->ptMinTrackSize.y = S(260, dpi);

        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            mm->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mm->ptMaxPosition.y = mi.rcWork.top  - mi.rcMonitor.top;
            mm->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            mm->ptMaxSize.y     = (mi.rcWork.bottom - mi.rcWork.top) - 2; 
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // KEYBOARD SHORTCUTS
    // ─────────────────────────────────────────────────────────────────────────
    case WM_KEYDOWN: {
        if (!g_windows.count(hWnd)) break;
        auto& wd = g_windows[hWnd];
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

        // Profile "Add Profile" name input — Backspace & Escape
        if (g_profilePanelOpen && g_profileView == ProfilePanelView::ADD_PROFILE
            && g_profileAddNameActive) {
            if (wParam == VK_BACK) {
                if (!g_profileAddName.empty()) g_profileAddName.pop_back();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                g_profileView = ProfilePanelView::ACCOUNTS;
                g_profileAddName = L"";
                g_profileAddNameActive = false;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // Find bar open থাকলে input handle করো
        if (g_findBarOpen) {
            if (wParam == VK_ESCAPE) { CloseFindBar(); InvalidateRect(hWnd, NULL, FALSE); return 0; }
            if (wParam == VK_BACK)   { FindBarBackspace(); if (auto* t = wd.active()) if (t->webview) ExecuteFind(t->webview.Get()); InvalidateRect(hWnd, NULL, FALSE); return 0; }
            if (wParam == VK_RETURN) { if (auto* t = wd.active()) if (t->webview) ExecuteFind(t->webview.Get(), !shift); return 0; }
        }

        if (wParam == VK_ESCAPE) {
            // Focus mode বন্ধ করো সবার আগে
            if (g_windows.count(hWnd) && g_windows[hWnd].isFocusMode) {
                ToggleFocusMode(hWnd);
                return 0;
            }
            // সব panel বন্ধ করো
            bool any = g_bookmarkPanelOpen || g_historyPanelOpen ||
                       g_downloadsPanelOpen || g_findBarOpen ||
                       g_contextMenuOpen   || g_extensionPanelOpen;
            g_bookmarkPanelOpen  = g_historyPanelOpen = g_downloadsPanelOpen = false;
            g_extensionPanelOpen = false;
            g_findBarOpen        = false;
            g_contextMenuOpen    = false;
            if (any) { InvalidateRect(hWnd, NULL, FALSE); return 0; }
        }

        if (ctrl) {
            switch (wParam) {
            case 'T':  // Ctrl+T — New Tab
                AddTab(hWnd, L"LOCAL_NTP"); return 0;

            case 'W':  // Ctrl+W — Close Tab
                if (wd.tabs.size() > 1) CloseTab(hWnd, wd.activeTab);
                else DestroyWindow(hWnd);
                return 0;

            case 'N':  // Ctrl+N — New Window
                LaunchMiniBrowser(L"LOCAL_NTP", L"New Window"); return 0;

            case 'H':  // Ctrl+H — History (Chrome-style নতুন tab)
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                AddTab(hWnd, L"LOCAL_HISTORY"); return 0;

            case 'J':  // Ctrl+J — Downloads (Chrome-style নতুন tab)
                g_downloadsPanelOpen = false;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                AddTab(hWnd, L"LOCAL_DOWNLOADS"); return 0;

            case 'D':  // Ctrl+D — Bookmark current page
                if (auto* tab = wd.active()) {
                    ToggleBookmark(tab->url, tab->title);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;

            case 'B':  // Ctrl+B — Bookmarks Panel
                g_bookmarkPanelOpen  = !g_bookmarkPanelOpen;
                g_historyPanelOpen   = false;
                g_downloadsPanelOpen = false;
                g_extensionPanelOpen = false;
                if (g_bookmarkPanelOpen) LoadBookmarks();
                InvalidateRect(hWnd, NULL, FALSE); return 0;

            case 'E':  // Ctrl+E — Extensions Panel
                g_extensionPanelOpen = !g_extensionPanelOpen;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                if (g_extensionPanelOpen) ScanExtensionsFolderPublic();
                // WebView shrink/restore করো
                {
                    RECT wvr = GetWebViewRect(hWnd);
                    if (wd.active() && wd.active()->controller)
                        wd.active()->controller->put_Bounds(wvr);
                }
                InvalidateRect(hWnd, NULL, TRUE); return 0;

            case 'F':  // Ctrl+F — Find in Page
                if (g_findBarOpen) CloseFindBar();
                else OpenFindBar();
                InvalidateRect(hWnd, NULL, FALSE); return 0;

            case 'R':  // Ctrl+R — Reload
                if (auto* tab = wd.active())
                    if (tab->webview) tab->webview->Reload();
                return 0;

            case VK_TAB:  // Ctrl+Tab — Next Tab
                if (!wd.tabs.empty()) {
                    int next = (wd.activeTab + (shift ? -1 : 1) + (int)wd.tabs.size()) % (int)wd.tabs.size();
                    SwitchToTab(hWnd, next);
                }
                return 0;

            default:
                // Ctrl+1~9 — Switch Tab
                if (wParam >= '1' && wParam <= '9') {
                    int idx = (int)(wParam - '1');
                    if (idx < (int)wd.tabs.size()) SwitchToTab(hWnd, idx);
                    return 0;
                }
                break;
            }
        }

        if (wParam == VK_F5) {  // F5 — Reload
            if (auto* tab = wd.active())
                if (tab->webview) tab->webview->Reload();
            return 0;
        }
        break;
    }

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wParam;
        // Profile "Add Profile" name input
        if (g_profilePanelOpen && g_profileView == ProfilePanelView::ADD_PROFILE
            && g_profileAddNameActive) {
            if (ch >= L' ' && ch != 127) { // printable, not DEL
                if (g_profileAddName.size() < 32)
                    g_profileAddName += ch;
                InvalidateRect(hWnd, NULL, FALSE);
            } else if (ch == L'\r' || ch == L'\n') {
                // Enter → create profile
                if (!g_profileAddName.empty()) {
                    AddProfile(g_profileAddName);
                    g_activeProfileIdx = (int)g_profiles.size() - 1;
                    SaveProfiles();
                    g_profileView    = ProfilePanelView::ACCOUNTS;
                    g_profileAddName = L"";
                    g_profileAddNameActive = false;
                    InvalidateRect(hWnd, NULL, TRUE);
                }
            }
            break; // consume — don't pass to find bar
        }
        // Find bar text input
        if (g_findBarOpen && !g_windows.empty()) {
            if (ch >= L' ' && ch != VK_BACK) {
                FindBarAddChar(ch);
                if (g_windows.count(hWnd)) {
                    auto& wd2 = g_windows[hWnd];
                    if (auto* tab = wd2.active())
                        if (tab->webview) ExecuteFind(tab->webview.Get());
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_RBUTTONDOWN: {
        if (!g_windows.count(hWnd)) break;
        auto& wdR = g_windows[hWnd];
        UINT dpiR = GetWndDpi(hWnd);
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT crR; GetClientRect(hWnd, &crR); int WR = crR.right;

        // ── Bookmark bar right-click → শুধু home page (NTP) এ ──
        if (wdR.active() && (wdR.active()->url == L"LOCAL_NTP" || wdR.active()->url == L"about:blank"))
        {
            int bmkYR = TitleBarH(dpiR) + ToolbarH(dpiR);
            int bmkHR = S(D_BOOKMARK_H, dpiR);
            if (my >= bmkYR && my < bmkYR + bmkHR) {
                // কোন bookmark item এ right-click হয়েছে বের করো
                int curXR = S(8, dpiR), itemPadXR = S(10, dpiR);
                int iconWR = S(14, dpiR), gapR = S(4, dpiR);
                int hitIdx = -1;
                for (int i = 0; i < (int)g_bookmarks.size(); i++) {
                    std::wstring lbl = g_bookmarks[i].title;
                    if (lbl.size() > 16) lbl = lbl.substr(0, 14) + L"..";
                    int lblW  = (int)(lbl.size() * S(7, dpiR));
                    int itemW = iconWR + gapR + lblW + itemPadXR * 2;
                    if (curXR + itemW > WR - S(80, dpiR)) break;
                    if (mx >= curXR && mx < curXR + itemW) { hitIdx = i; break; }
                    curXR += itemW + S(2, dpiR);
                }
                if (hitIdx >= 0) {
                    // Native Win32 popup menu
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 1, L"Delete bookmark");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 2, L"Open");
                    POINT pt; GetCursorPos(&pt);
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                    if (cmd == 1) {
                        RemoveBookmark(hitIdx);
                        InvalidateRect(hWnd, NULL, FALSE);
                    } else if (cmd == 2 && hitIdx < (int)g_bookmarks.size()) {
                        if (wdR.active() && wdR.active()->webview)
                            wdR.active()->webview->Navigate(g_bookmarks[hitIdx].url.c_str());
                    }
                    return 0;
                }
                break; // bookmark bar area কিন্তু কোনো item এ না — ignore
            }
        }
        // WebView এর উপরে right-click হলে context menu খোলো
        if (my > NavTotalH(hWnd)) {
            OpenContextMenu(mx, my, false, false, false, L"");
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -3 : 3;
        if (g_historyPanelOpen) {
            HandleHistoryScroll(delta);
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (g_bookmarkPanelOpen || g_downloadsPanelOpen) {
            // panels handle their own scroll via redraw
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY: {
        if (g_windows.count(hWnd)) {
            for (auto& tab : g_windows[hWnd].tabs)
                if (tab.controller) tab.controller->Close();
            if (g_windows[hWnd].hAddrFont)
                DeleteObject(g_windows[hWnd].hAddrFont);
            g_windows.erase(hWnd);
        }
        if (g_isPureViewerMode && g_windows.empty())
            PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API — LaunchMiniBrowser()
// ─────────────────────────────────────────────────────────────────────────────
void LaunchMiniBrowser(std::wstring url, std::wstring /*title*/) {
    static ULONG_PTR gdiplusToken = 0;
    if (!gdiplusToken) {
        GdiplusStartupInput si;
        GdiplusStartup(&gdiplusToken, &si, nullptr);
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CreateDesktopShortcut();
    RegisterAppForDefaultBrowser();

    // ── Browser Data Init (প্রথমবার call এ) ──
    static bool dataLoaded = false;
    if (!dataLoaded) {
        LoadBookmarks();
        LoadSettings();
        LoadProfiles();   // Chrome-style multi-profile init
        dataLoaded = true;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ViewerWndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.lpszClassName = L"RasBrowserWnd";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND hWnd = CreateWindowExW(
        0, L"RasBrowserWnd", L"RasBrowser",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
        WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!hWnd) return;

    SetWindowLongW(hWnd, GWL_STYLE, GetWindowLongW(hWnd, GWL_STYLE) & ~WS_CAPTION);
    ApplyDwmShadow(hWnd);

    auto& wd = g_windows[hWnd];

    HWND hEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 100, 26, hWnd, (HMENU)IDC_ADDRESS_BAR, GetModuleHandle(NULL), NULL);

    SetWindowLongW(hEdit, GWL_STYLE, GetWindowLongW(hEdit, GWL_STYLE) & ~WS_BORDER);
    SetWindowTheme(hEdit, L"", L"");
    SetWindowSubclass(hEdit, AddrBarProc, 1, 0);
    wd.hAddressBar = hEdit;

    HICON hIco = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco) {
        SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIco);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
    }

    TabData firstTab;
    if (!g_isPureViewerMode && (url.empty() || url == L"RAS_BROWSER" || url == L"about:blank")) {
        url = L"LOCAL_NTP"; 
    }
    
    firstTab.url   = url;
    firstTab.title = L"New Tab";
    wd.tabs.push_back(firstTab);
    wd.activeTab = 0;

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd, 0);
}
// ============================================================
// EMBEDDED PREVIEW WEBVIEW2  (File Manager Preview Panel)
// ============================================================
// One dedicated controller for in-panel preview.
// Shares g_sharedEnv so no second browser process is spawned.

static ComPtr<ICoreWebView2Controller>  g_previewCtrl;
static ComPtr<ICoreWebView2>            g_previewWV;
static HWND                             g_previewParent = NULL;
static RECT                             g_previewBounds = {};

// Handler: environment ready → create preview controller
class PreviewControllerHandler
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
{
    ULONG m_ref = 1;
    RECT  m_bounds;
    std::wstring m_url;
public:
    PreviewControllerHandler(RECT b, const std::wstring& u) : m_bounds(b), m_url(u) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (FAILED(hr) || !ctl) return S_OK;
        g_previewCtrl = ctl;
        ctl->get_CoreWebView2(&g_previewWV);

        // Transparent background (default white → match our dark/light theme)
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = {255, 245, 248, 250};  // matches bBg
            ctl2->put_DefaultBackgroundColor(bg);
        }

        // Minimal settings — no context menu, no dev tools in preview
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(g_previewWV->get_Settings(&settings))) {
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
        }

        ctl->put_Bounds(m_bounds);
        ctl->put_IsVisible(TRUE);
        if (!m_url.empty()) g_previewWV->Navigate(m_url.c_str());
        return S_OK;
    }
};

// Handler: create env from scratch when g_sharedEnv not yet ready
class PreviewEnvHandler
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
{
    ULONG m_ref = 1;
    RECT  m_bounds;
    std::wstring m_url;
public:
    PreviewEnvHandler(RECT b, const std::wstring& u) : m_bounds(b), m_url(u) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr) || !env) return S_OK;
        if (!g_sharedEnv) g_sharedEnv = env;
        g_sharedEnv->CreateCoreWebView2Controller(
            g_previewParent, new PreviewControllerHandler(m_bounds, m_url));
        return S_OK;
    }
};

void CreateEmbeddedPreviewWebView(HWND parentHwnd, RECT bounds, const std::wstring& url)
{
    g_previewParent = parentHwnd;
    g_previewBounds = bounds;

    // If we already have a controller on the same parent, reuse it
    if (g_previewCtrl && g_previewWV) {
        g_previewCtrl->put_Bounds(bounds);
        g_previewCtrl->put_IsVisible(TRUE);
        if (!url.empty()) g_previewWV->Navigate(url.c_str());
        return;
    }

    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(
            parentHwnd, new PreviewControllerHandler(bounds, url));
    } else {
        // g_sharedEnv not yet initialised (browser tab not yet opened)
        // create a minimal env for preview
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, nullptr, nullptr,
            new PreviewEnvHandler(bounds, url));
    }
}

void UpdateEmbeddedPreviewBounds(RECT newBounds)
{
    g_previewBounds = newBounds;
    if (g_previewCtrl) {
        g_previewCtrl->put_Bounds(newBounds);
        g_previewCtrl->put_IsVisible(TRUE);
    }
}

void DestroyEmbeddedPreview()
{
    if (g_previewCtrl) {
        g_previewCtrl->put_IsVisible(FALSE);
        g_previewCtrl->Close();
        g_previewCtrl.Reset();
        g_previewWV.Reset();
    }
}
// COMPLETE FILE END ───────────────────────────────────────────────────────────
