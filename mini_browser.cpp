// mini_browser.cpp — RasBrowser | Premium UI, Smart Omnibox, Dynamic Bookmarks
// FIXED: Build error (ICoreWebView2Settings2), Local NTP on startup, Gemini support,
//        Desktop shortcut creation DISABLED, Chrome-like Profile/Extensions/Settings menus,
//        Gemini Developer Mode, NewWindowRequested properly handled.

#define _CRT_SECURE_NO_WARNINGS
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#define GDIPVER      0x0110

#include "mini_browser.h"
#include "html_tools.h"
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <wrl.h>
#include <shlobj.h>
#include <shlwapi.h>

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
// RESOURCE IDs
// ─────────────────────────────────────────────────────────────────────────────
#define IDI_APP_ICON    101
#define IDC_ADDRESS_BAR 1005

// ─────────────────────────────────────────────────────────────────────────────
// FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND, int);
static void AddTab(HWND, std::wstring);
static void CloseTab(HWND, int);
static void CreateWebViewForTab(HWND, int);

// ─────────────────────────────────────────────────────────────────────────────
// 1. DYNAMIC LOCAL NTP
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetLocalNTP_HTML(bool isDark) {
    std::wstring bg        = isDark ? L"#1e1e24" : L"#f8fafc";
    std::wstring text      = isDark ? L"#ffffff" : L"#323232";
    std::wstring subText   = isDark ? L"#a0a0b0" : L"#666666";
    std::wstring boxBg     = isDark ? L"#2b2b36" : L"#ffffff";
    std::wstring boxBorder = isDark ? L"#444444" : L"#dcdfe6";
    std::wstring shadow    = isDark ? L"0 4px 12px rgba(0,0,0,0.3)" : L"0 4px 12px rgba(0,0,0,0.05)";
    std::wstring teal      = L"#0ca8b0";

    return L"<!DOCTYPE html>"
    L"<html><head><meta charset='utf-8'><title>New Tab</title><style>"
    L"body{margin:0;display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;background:" + bg + L";font-family:'Segoe UI',sans-serif;color:" + text + L";}"
    L".logo{font-size:64px;font-weight:bold;margin-bottom:5px;letter-spacing:-1.5px;user-select:none;}"
    L".logo span{color:" + teal + L";}"
    L".subtitle{font-size:15px;color:" + subText + L";margin-bottom:35px;font-weight:500;letter-spacing:1px;text-transform:uppercase;}"
    L".search-wrap{width:100%;max-width:620px;position:relative;margin-bottom:50px;}"
    L".search-box{width:100%;padding:18px 50px;font-size:16px;border-radius:30px;border:1px solid " + boxBorder + L";background:" + boxBg + L";color:" + text + L";outline:none;box-shadow:" + shadow + L";box-sizing:border-box;transition:all 0.3s ease;}"
    L".search-box:focus{border-color:" + teal + L";box-shadow:0 4px 20px rgba(12,168,176,0.25);}"
    L".icon-search{position:absolute;left:20px;top:50%;transform:translateY(-50%);width:22px;fill:#9aa0a6;pointer-events:none;}"
    L".quick-links{display:flex;gap:30px;}"
    L".link-item{display:flex;flex-direction:column;align-items:center;text-decoration:none;color:" + text + L";font-size:14px;font-weight:600;transition:transform 0.2s;}"
    L".link-item:hover{transform:translateY(-5px);}"
    L".link-icon{width:56px;height:56px;border-radius:50%;background:" + boxBg + L";display:flex;align-items:center;justify-content:center;box-shadow:" + shadow + L";margin-bottom:12px;font-size:22px;color:" + teal + L";border:1px solid " + boxBorder + L";transition:all 0.3s ease;}"
    L".link-item:hover .link-icon{background:" + teal + L";color:#fff;border-color:" + teal + L";box-shadow:0 8px 15px rgba(12,168,176,0.3);}"
    L"</style></head><body>"
    L"<div class='logo'><span>Ras</span>Browser</div>"
    L"<div class='subtitle'>A Powerful &amp; Safe Browsing Experience</div>"
    L"<div class='search-wrap'>"
    L"<svg class='icon-search' viewBox='0 0 24 24'><path d='M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z'/></svg>"
    L"<input id='q' type='text' class='search-box' placeholder='Search or type a URL' autocomplete='off' autofocus />"
    L"</div>"
    L"<div class='quick-links'>"
    L"<a href='https://www.youtube.com' class='link-item'><div class='link-icon'>&#9654;</div>YouTube</a>"
    L"<a href='https://gemini.google.com' class='link-item'><div class='link-icon'>AI</div>Gemini</a>"
    L"<a href='https://www.facebook.com' class='link-item'><div class='link-icon'>f</div>Facebook</a>"
    L"<a href='https://chatgpt.com' class='link-item'><div class='link-icon'>&#129302;</div>ChatGPT</a>"
    L"<a href='https://github.com' class='link-item'><div class='link-icon'>&lt;/&gt;</div>GitHub</a>"
    L"</div>"
    L"<script>"
    L"document.getElementById('q').addEventListener('keydown',function(e){"
    L"  if(e.key==='Enter'){"
    L"    var v=this.value.trim();if(!v)return;"
    L"    var url=v.indexOf(' ')===-1&&(v.indexOf('.')!==-1||v.startsWith('http'))?"
    L"            (v.startsWith('http')?v:'https://'+v):"
    L"            'https://www.google.com/search?q='+encodeURIComponent(v);"
    L"    if(window.chrome&&window.chrome.webview)"
    L"      window.chrome.webview.postMessage(url);"
    L"    else window.location.href=url;"
    L"  }"
    L"});"
    L"</script>"
    L"</body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. DYNAMIC BLOCKED PAGE
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetBlocked_HTML(bool isDark) {
    std::wstring bg     = isDark ? L"#1a1a1f" : L"#f4f6f8";
    std::wstring text   = isDark ? L"#ffffff" : L"#323232";
    std::wstring boxBg  = isDark ? L"#2b2b36" : L"#ffffff";
    std::wstring red    = L"#e74c3c";
    std::wstring border = isDark ? L"#444444" : L"#e2e8f0";

    return L"<!DOCTYPE html>"
    L"<html><head><meta charset='utf-8'><title>Blocked</title><style>"
    L"body{margin:0;display:flex;align-items:center;justify-content:center;height:100vh;background:" + bg + L";font-family:'Segoe UI',sans-serif;color:" + text + L";}"
    L".container{max-width:600px;text-align:center;padding:40px;background:" + boxBg + L";border-radius:16px;box-shadow:0 10px 30px rgba(0,0,0,0.15);border:1px solid " + border + L";}"
    L".icon{font-size:70px;margin-bottom:10px;}"
    L"h1{margin:0 0 10px 0;color:" + red + L";font-size:32px;}"
    L"p{font-size:16px;color:#888;margin-bottom:30px;line-height:1.5;}"
    L".quote-box{background:rgba(12,168,176,0.1);border-left:4px solid #0ca8b0;padding:20px;border-radius:0 8px 8px 0;text-align:left;}"
    L".quote-title{font-size:14px;font-weight:bold;color:#0ca8b0;margin-bottom:10px;text-transform:uppercase;letter-spacing:1px;}"
    L".quote-text{font-size:18px;font-style:italic;line-height:1.6;font-weight:500;}"
    L"</style></head><body>"
    L"<div class='container'>"
    L"<div class='icon'>&#128683;</div>"
    L"<h1>Access Denied</h1>"
    L"<p>This content has been blocked by <b>RasFocus</b> to protect your mind and productivity.</p>"
    L"<div class='quote-box'>"
    L"<div class='quote-title'>&#128161; Motivational Quote</div>"
    L"<div class='quote-text' id='quote'></div>"
    L"</div></div>"
    L"<script>"
    L"const q=['\"Discipline is the bridge between goals and accomplishment.\" - Jim Rohn',"
    L"'\"You have power over your mind, not outside events.\" - Marcus Aurelius',"
    L"'\"The successful warrior is the average man with laser-like focus.\" - Bruce Lee',"
    L"'\"Control yourself or someone else will.\" - John C. Maxwell',"
    L"'\"It does not matter how slowly you go as long as you do not stop.\" - Confucius'];"
    L"document.getElementById('quote').innerText=q[Math.floor(Math.random()*q.length)];"
    L"</script></body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. YOUTUBE AD BLOCK SCRIPT
// uBlock Origin-এর মতো network-layer block + DOM manipulation দুটোই।
// NavigationCompleted এ inject হয়, তারপর MutationObserver দিয়ে continuously চলে।
// ─────────────────────────────────────────────────────────────────────────────

// Early-stage script: ContentLoading এ inject হয়, page parse শুরুর আগেই।
// ytInitialPlayerResponse.playerAds ও yt.setConfig() intercept করে silent pre-roll বন্ধ করে।
std::wstring GetYouTubeEarlyAdBlockScript() {
    return
        L"(function(){"
        L"if(location.hostname.indexOf('youtube.com')===-1)return;"

        // ── ytInitialPlayerResponse wipe: page-embedded ad config মুছে দাও ─
        // YouTube inline script এ window.ytInitialPlayerResponse set করে।
        // defineProperty trap দিয়ে যখনই সেট হবে, playerAds মুছে দাও।
        L"(function(){"
        L"  var _ytIPR=undefined;"
        L"  function wipeAds(v){"
        L"    if(!v||typeof v!=='object')return v;"
        L"    try{delete v.playerAds;}catch(e){}"
        L"    try{delete v.adPlacements;}catch(e){}"
        L"    try{delete v.adSlots;}catch(e){}"
        L"    try{delete v.playerConfig.adParams;}catch(e){}"
        L"    try{if(v.playabilityStatus)delete v.playabilityStatus.liveStreamability;}catch(e){}"
        L"    try{if(v.streamingData)delete v.streamingData.adaptiveFormats;}catch(e){}"
        L"    return v;"
        L"  }"
        L"  try{"
        L"    Object.defineProperty(window,'ytInitialPlayerResponse',{"
        L"      get:function(){return _ytIPR;},"
        L"      set:function(v){_ytIPR=wipeAds(v);},"
        L"      configurable:true"
        L"    });"
        L"  }catch(e){}"

        // ── yt.setConfig intercept: runtime ad config injection বন্ধ করো ──
        // YouTube player runtime এ yt.setConfig({PLAYER_VARS:{...}}) call করে।
        // এই call intercept করে adformat, ad_tag ইত্যাদি মুছে দাও।
        L"  var _ytObj=window.yt||{};"
        L"  var _origSetConfig=_ytObj.setConfig;"
        L"  function patchYtSetConfig(){"
        L"    if(!window.yt||window.yt.__RAS_PATCHED__)return;"
        L"    window.yt.__RAS_PATCHED__=true;"
        L"    var orig=window.yt.setConfig||function(){};"
        L"    window.yt.setConfig=function(cfg){"
        L"      if(cfg&&typeof cfg==='object'){"
        L"        try{delete cfg.PLAYER_VARS;}catch(e){}"
        L"        try{"
        L"          if(cfg.EXPERIMENT_FLAGS){"
        L"            var f=cfg.EXPERIMENT_FLAGS;"
        L"            var adKeys=Object.keys(f).filter(function(k){"
        L"              return k.indexOf('ad')!==-1||k.indexOf('Ad')!==-1;"
        L"            });"
        L"            adKeys.forEach(function(k){try{delete f[k];}catch(e){}});"
        L"          }"
        L"        }catch(e){}"
        L"      }"
        L"      return orig.apply(this,arguments);"
        L"    };"
        L"  }"
        // yt object later set হতে পারে, poll করো
        L"  var _ytPatchTry=0;"
        L"  var _ytPatchTimer=setInterval(function(){"
        L"    _ytPatchTry++;"
        L"    if(window.yt){patchYtSetConfig();clearInterval(_ytPatchTimer);}"
        L"    if(_ytPatchTry>30)clearInterval(_ytPatchTimer);"
        L"  },100);"
        L"})();"

        L"if(!window.__RAS_SPA_HOOKED__){"
        L"  window.__RAS_SPA_HOOKED__=true;"
        L"  document.addEventListener('yt-navigate-finish',function(){"
        L"    if(window.__RAS_ADBLOCK_TIMER__)clearInterval(window.__RAS_ADBLOCK_TIMER__);"
        L"    if(window.__RAS_OBS__)try{window.__RAS_OBS__.disconnect();}catch(e){}"
        L"    window.__RAS_FETCH_PATCHED__=false;"
        L"  },true);"
        L"}"

        L"})();";
}

std::wstring GetYouTubeAdBlockScript() {
    return
        L"(function(){"
        L"if(location.hostname.indexOf('youtube.com')===-1)return;"
        // No singleton guard — allow re-run on SPA navigation
        // Instead guard per-timer so we don't double-stack intervals
        L"if(window.__RAS_ADBLOCK_TIMER__)clearInterval(window.__RAS_ADBLOCK_TIMER__);"
        L"if(window.__RAS_OBS__)try{window.__RAS_OBS__.disconnect();}catch(e){}"

        // ── Helper: click skip button ────────────────────────────────────
        L"function clickSkip(){"
        //   All known skip selectors incl. 2025 bumper/non-skippable
        L"  var btns=document.querySelectorAll("
        L"    '.ytp-skip-ad-button,.ytp-ad-skip-button,.ytp-ad-skip-button-modern"
        L"    ,.ytp-ad-skip-button-container button,.ytp-ad-skip-button-slot button"
        L"    ,.ytp-preview-ad .ytp-ad-skip-button,.videoAdUiSkipContainer button'"
        L"  );"
        L"  btns.forEach(function(b){try{b.click();}catch(e){}});"
        L"}"

        // ── Helper: mute + speed 16x + seek to end ───────────────────────
        L"function skipAdVideo(){"
        L"  var v=document.querySelector('video');"
        L"  if(!v)return;"
        L"  var isAd=document.querySelector('.ad-showing,.ad-interrupting,.ytp-ad-player-overlay');"
        L"  if(!isAd)return;"
        L"  if(!v.muted)v.muted=true;"
        L"  try{if(v.playbackRate<16)v.playbackRate=16;}catch(e){}"
        L"  if(isFinite(v.duration)&&v.duration>0&&v.currentTime<v.duration-0.1){"
        L"    try{v.currentTime=v.duration;}catch(e){}"
        L"  }else if(!isFinite(v.duration)||isNaN(v.duration)){"
        L"    try{v.currentTime=v.currentTime+9999;}catch(e){}"
        L"  }"
        L"}"

        // ── Helper: hide ad overlay elements ─────────────────────────────
        L"function removeAdOverlays(){"
        L"  var sel=["
        L"    'ytd-banner-promo-renderer','ytd-statement-banner-renderer',"
        L"    'ytd-ad-slot-renderer','ytd-in-feed-ad-layout-renderer',"
        L"    'ytd-promoted-sparkles-web-renderer','ytd-promoted-video-renderer',"
        L"    'ytd-display-ad-renderer','.ytd-action-companion-ad-renderer',"
        L"    '#masthead-ad',"
        L"    '.ytp-ad-overlay-container','.ytp-ad-text-overlay',"
        L"    '.ytp-ad-image-overlay','.ytp-ad-player-overlay-instream-info',"
        L"    '.ytp-ad-player-overlay-skip-or-preview','.ytp-ad-simple-ad-badge',"
        L"    '.ytp-ad-persistent-progress-bar-container',"
        L"    '.ytp-preview-ad','.video-ads.ytp-ad-module',"
        // 2025 bumper / companion overlay
        L"    '.ytp-ad-overlay-close-container','.ytp-ad-overlay-slot',"
        L"    'ytd-player-legacy-desktop-watch-ads-renderer',"
        L"    '.ytp-paid-content-overlay'"
        L"  ];"
        L"  sel.forEach(function(s){"
        L"    document.querySelectorAll(s).forEach(function(el){"
        L"      if(el&&el.parentNode)el.parentNode.removeChild(el);"
        L"    });"
        L"  });"
        // Also hide via CSS so even mid-render ads don't flash
        L"  var sid='__ras_adcss__';"
        L"  if(!document.getElementById(sid)){"
        L"    var st=document.createElement('style');st.id=sid;"
        L"    st.textContent="
        L"      '.ad-showing .ytp-ad-player-overlay{display:none!important;}'"
        L"      '.ytp-ad-module{display:none!important;}'"
        L"      '.ytp-paid-content-overlay{display:none!important;}';"
        L"    (document.head||document.documentElement).appendChild(st);"
        L"  }"
        L"}"

        // ── Tick ─────────────────────────────────────────────────────────
        L"function adBlockTick(){"
        L"  skipAdVideo();"
        L"  clickSkip();"
        L"  removeAdOverlays();"
        L"}"
        L"window.__RAS_ADBLOCK_TIMER__=setInterval(adBlockTick,100);"

        // ── MutationObserver ─────────────────────────────────────────────
        L"window.__RAS_OBS__=new MutationObserver(function(){"
        L"  skipAdVideo();"
        L"  clickSkip();"
        L"  removeAdOverlays();"
        L"});"
        L"window.__RAS_OBS__.observe(document.documentElement,{"
        L"  childList:true,subtree:true,attributes:true,"
        L"  attributeFilter:['class']"   // catch .ad-showing class change on player
        L"});"

        // ── XHR/Fetch intercept ───────────────────────────────────────────
        L"(function(){"
        L"  var PAT=["
        L"    '/pagead/','/ptracking','/api/stats/ads','/api/stats/atr',"
        L"    'adsid=','adformat=','get_midroll_info','ad_survey',"
        L"    '/get_video_info?','doubleclick.net','googlesyndication.com',"
        L"    'googleadservices.com','/api/stats/watchtime?adformat'"
        L"  ];"
        L"  function isAd(url){"
        L"    if(!url)return false;"
        L"    var u=url.toString().toLowerCase();"
        L"    for(var i=0;i<PAT.length;i++)if(u.indexOf(PAT[i])!==-1)return true;"
        L"    return false;"
        L"  }"
        L"  if(!window.__RAS_FETCH_PATCHED__){"
        L"    window.__RAS_FETCH_PATCHED__=true;"
        L"    var _f=window.fetch;"
        L"    window.fetch=function(input,init){"
        L"      var url=typeof input==='string'?input:(input&&input.url?input.url:'');"
        L"      if(isAd(url))return Promise.resolve(new Response('',{status:204}));"
        L"      return _f.apply(this,arguments);"
        L"    };"
        L"    var _xo=XMLHttpRequest.prototype.open;"
        L"    XMLHttpRequest.prototype.open=function(m,url){"
        L"      if(isAd(url)){this._rasBlocked=true;return;}"
        L"      return _xo.apply(this,arguments);"
        L"    };"
        L"    var _xs=XMLHttpRequest.prototype.send;"
        L"    XMLHttpRequest.prototype.send=function(){"
        L"      if(this._rasBlocked)return;"
        L"      return _xs.apply(this,arguments);"
        L"    };"
        L"  }"
        L"})();"

        L"})();";
}

std::wstring GetAiInjectScript(const std::wstring& currentUrl) {
    std::wifstream in(L"rasfocus_ai_data.txt");
    if (!in.is_open()) return L"";

    bool isAiEngineActive=false,cbAiImageBlur=false,cbFemaleDetectWeb=false,cbFemaleDetectVideo=false;
    int aiSensitivityIdx=0;
    bool ytHideHome=false,ytHideShorts=false,ytHideComments=false,ytHideRecVideos=false,
         ytHideThumbnails=false,ytBlurThumbnails=false,ytHideSubs=false,ytHideExplore=false,
         ytHideTopBar=false,ytDisableEndCards=false,ytBlackWhiteMode=false,ytDisableAutoplay=false;
    bool ttHideExplore=false,ttHideLive=false,ttHideComments=false,ttHideSearch=false,ttBlackWhiteMode=false;
    bool igHideStories=false,igHideReels=false,igHideExplore=false,igHideComments=false,
         igHideSuggested=false,igBlackWhiteMode=false;

    in>>isAiEngineActive>>cbAiImageBlur>>cbFemaleDetectWeb>>cbFemaleDetectVideo>>aiSensitivityIdx;
    in>>ytHideHome>>ytHideShorts>>ytHideComments>>ytHideRecVideos>>ytHideThumbnails
      >>ytBlurThumbnails>>ytHideSubs>>ytHideExplore>>ytHideTopBar>>ytDisableEndCards
      >>ytBlackWhiteMode>>ytDisableAutoplay;
    in>>ttHideExplore>>ttHideLive>>ttHideComments>>ttHideSearch>>ttBlackWhiteMode;
    in>>igHideStories>>igHideReels>>igHideExplore>>igHideComments>>igHideSuggested>>igBlackWhiteMode;
    in.close();

    std::wstring css=L"";
    if (currentUrl.find(L"youtube.com")!=std::wstring::npos) {
        if (ytHideHome)        css+=L"ytd-browse[page-subtype='home']{display:none!important;}";
        if (ytHideShorts)      css+=L"ytd-reel-shelf-renderer,ytd-rich-shelf-renderer[is-shorts],a[title='Shorts']{display:none!important;}";
        if (ytHideComments)    css+=L"ytd-comments{display:none!important;}";
        if (ytHideRecVideos)   css+=L"ytd-watch-next-secondary-results-renderer{display:none!important;}";
        if (ytHideThumbnails)  css+=L"ytd-thumbnail{display:none!important;}";
        if (ytBlurThumbnails)  css+=L"ytd-thumbnail img{filter:blur(15px)!important;}";
        if (ytHideSubs)        css+=L"a[title='Subscriptions']{display:none!important;}";
        if (ytHideTopBar)      css+=L"ytd-masthead{display:none!important;}#page-manager{margin-top:0!important;}";
        if (ytDisableEndCards) css+=L".ytp-ce-element{display:none!important;}";
        if (ytBlackWhiteMode)  css+=L"html{filter:grayscale(100%)!important;}";
    } else if (currentUrl.find(L"tiktok.com")!=std::wstring::npos) {
        if (ttHideExplore)  css+=L"[data-e2e='nav-explore']{display:none!important;}";
        if (ttHideLive)     css+=L"[data-e2e='nav-live']{display:none!important;}";
        if (ttHideComments) css+=L".comment-container{display:none!important;}";
        if (ttBlackWhiteMode) css+=L"html{filter:grayscale(100%)!important;}";
    } else if (currentUrl.find(L"instagram.com")!=std::wstring::npos) {
        if (igHideReels)   css+=L"a[href*='/reels/']{display:none!important;}";
        if (igHideExplore) css+=L"a[href*='/explore/']{display:none!important;}";
        if (igBlackWhiteMode) css+=L"html{filter:grayscale(100%)!important;}";
    }

    if (css.empty()) return L"";
    return L"(function(){let s=document.createElement('style');s.innerHTML=\"" + css + L"\";document.head.appendChild(s);})();";
}

// ─────────────────────────────────────────────────────────────────────────────
// DPI HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static inline int S(int px, UINT dpi)         { return MulDiv(px,(int)dpi,96); }
static inline float Sf(float px, UINT dpi)    { return px*(float)dpi/96.0f; }
static UINT GetWndDpi(HWND hWnd) {
    UINT dpi=GetDpiForWindow(hWnd); return dpi?dpi:96;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAYOUT CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────
static const int D_TITLEBAR_H = 42;
static const int D_TOOLBAR_H  = 40;
static const int D_BOOKMARK_H = 32;
static const int D_TAB_W_MAX  = 240;
static const int D_TAB_W_MIN  = 80;
static const int D_TAB_PAD    = 10;
static const int D_WIN_BTN_W  = 46;
static const int D_LOGO_W     = 140;
static const int D_NEW_TAB_BTN= 28;

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

    bool hMin=false, hMax=false, hClose=false;
    bool hPin=false, hDark=false;
    bool isPinned = false;
    bool hBack=false, hFwd=false, hRel=false;
    bool hProfile=false, hExt=false, hMenu=false;
    int  hoverTabIndex = -1;
    bool hNewTab       = false;

    TabData* active() {
        if (activeTab>=0 && activeTab<(int)tabs.size()) return &tabs[activeTab];
        return nullptr;
    }
};

static std::map<HWND, BrowserWindowData> g_windows;
static ComPtr<ICoreWebView2Environment>  g_sharedEnv;

// ─────────────────────────────────────────────────────────────────────────────
// NAV HEIGHT
// ─────────────────────────────────────────────────────────────────────────────
static int NavTotalH(HWND hWnd) {
    UINT dpi = GetWndDpi(hWnd);
    if (g_isPureViewerMode) return S(D_TITLEBAR_H, dpi);
    int h = S(D_TITLEBAR_H + D_TOOLBAR_H, dpi);
    if (g_windows.count(hWnd)) {
        auto* tab = g_windows[hWnd].active();
        if (tab && tab->url == L"LOCAL_NTP")
            h += S(D_BOOKMARK_H, dpi);
    }
    return h;
}

static int TitleBarH(UINT dpi) { return S(D_TITLEBAR_H, dpi); }
static int ToolbarH (UINT dpi) { return S(D_TOOLBAR_H,  dpi); }
static int WinBtnW  (UINT dpi) { return S(D_WIN_BTN_W,  dpi); }
static int LogoW    (UINT dpi) { return S(D_LOGO_W,     dpi); }

// ─────────────────────────────────────────────────────────────────────────────
// URL ENCODE
// ─────────────────────────────────────────────────────────────────────────────
static std::string utf8_encode(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8,0,wstr.data(),(int)wstr.size(),NULL,0,NULL,NULL);
    std::string s(n,0);
    WideCharToMultiByte(CP_UTF8,0,wstr.data(),(int)wstr.size(),s.data(),n,NULL,NULL);
    return s;
}
static std::wstring UrlEncode(const std::wstring& text) {
    std::string u = utf8_encode(text);
    std::wstringstream esc;
    for (unsigned char c : u) {
        if (isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') esc<<(wchar_t)c;
        else if (c==' ') esc<<L"+";
        else { wchar_t b[8]; swprintf(b,8,L"%%%02X",c); esc<<b; }
    }
    return esc.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// FIX: Desktop shortcut creation — DISABLED as requested
// ─────────────────────────────────────────────────────────────────────────────
static void CreateDesktopShortcut() {
    // DISABLED: Desktop shortcut creation turned off
}
static void RegisterAppForDefaultBrowser() {}

// ─────────────────────────────────────────────────────────────────────────────
// CONTENT BLOCKER
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// RasFocus Adult + Ad + Tracker Block System
// Ported from Android AdBlocker.kt + UnifiedBlockerService.kt
// ─────────────────────────────────────────────────────────────────────────────

static const std::vector<std::wstring> ADULT_KEYWORDS = {
    L"porn", L"xxx", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34", L"milf",
    L"blowjob", L"tits", L"boobs", L"pussy", L"dick", L"cock", L"escort", L"bdsm",
    L"fetish", L"erotica", L"dildo", L"webcam", L"camgirls", L"onlyfans", L"chaturbate",
    L"hot dance", L"seductive dance", L"item song", L"belly dance", L"kissing scene",
    L"bikini", L"swimsuit", L"sexy dance", L"cleavage", L"hot scene", L"romantic kiss",
    L"bedroom scene", L"bath scene", L"rain dance", L"bold scene", L"semi nude",
    L"lingerie", L"erotic", L"hot song", L"romantic video hot", L"navel show",
    L"deep neck", L"short dress sexy", L"unfaithful scene",
    L"18videosz", L"24porn", L"3movs", L"4tube", L"adulttime", L"beeg", L"brazzers",
    L"eporner", L"redtube", L"spankbang", L"stripchat", L"xhamster", L"xnxx",
    L"xvideos", L"youporn"
};

static const std::vector<std::wstring> ADULT_DOMAINS = {
    L"pornhub.com", L"xvideos.com", L"xnxx.com", L"xhamster.com", L"redtube.com",
    L"youporn.com", L"brazzers.com", L"spankbang.com", L"eporner.com", L"chaturbate.com",
    L"onlyfans.com", L"stripchat.com", L"beeg.com", L"hentaigasm.com", L"hentaihaven.org",
    L"playboy.com", L"pornmd.com", L"tube8.com", L"tubegalore.com", L"txxx.com",
    L"realitykings.com", L"digitalplayground.com", L"fakehub.com", L"evilangel.com",
    L"teamskeet.com", L"mofosex.com", L"bangbrosnetwork.com", L"jerkmate.com",
    L"luckycrush.live", L"redgifs.com", L"motherless.com", L"hardsextube.com"
};

static const std::vector<std::wstring> AD_DOMAINS = {
    L"doubleclick.net", L"googlesyndication.com", L"adservice.google.com",
    L"googleadservices.com", L"pagead2.googlesyndication.com", L"tpc.googlesyndication.com",
    L"securepubads.g.doubleclick.net", L"imasdk.googleapis.com", L"amazon-adsystem.com",
    L"an.facebook.com", L"adnxs.com", L"rubiconproject.com", L"pubmatic.com",
    L"openx.net", L"criteo.com", L"criteo.net", L"adsrvr.org", L"advertising.com",
    L"appnexus.com", L"bidswitch.net", L"taboola.com", L"outbrain.com", L"revcontent.com",
    L"mgid.com", L"zergnet.com", L"adblade.com", L"ads.twitter.com", L"moatads.com",
    L"scorecardresearch.com", L"quantserve.com", L"demdex.net", L"turn.com"
};

static const std::vector<std::wstring> TRACKER_DOMAINS = {
    L"google-analytics.com", L"googletagmanager.com", L"googletagservices.com",
    L"analytics.google.com", L"ssl.google-analytics.com", L"stats.wp.com",
    L"bat.bing.com", L"analytics.twitter.com", L"piwik.org", L"matomo.org",
    L"statcounter.com", L"crazyegg.com",
    L"hotjar.com", L"mouseflow.com", L"fullstory.com", L"logrocket.com"
    // NOTE: sentry.io, segment.com, amplitude.com, mixpanel.com, heap.io, rollbar.com
    // সরানো হয়েছে — এগুলো claude.ai, Notion, Linear, Vercel সহ অনেক modern
    // web app-এর core functionality-র জন্য দরকার। block করলে সাইট load হয় না।
};

// Web apps যেগুলো sentry/segment ছাড়া কাজ করে না — এদের জন্য
// tracker block bypass করা হবে
static const std::vector<std::wstring> TRACKER_WHITELIST = {
    L"sentry.io",       // Error tracking — claude.ai, Notion, Linear, etc.
    L"ingest.sentry.io",
    L"o*.ingest.sentry.io",
    L"segment.com",     // Analytics infra — claude.ai, many SPAs
    L"cdn.segment.com",
    L"api.segment.io",
    L"amplitude.com",   // Product analytics — many web apps depend on it
    L"api.amplitude.com",
    L"mixpanel.com",
    L"api.mixpanel.com",
    L"heap.io",
    L"heapanalytics.com",
    L"rollbar.com",
    L"statsig.com",     // Feature flags — claude.ai uses this
    L"statsigapi.net",
    L"featuregates.org",
    L"launchdarkly.com",// Feature flags
    L"app.launchdarkly.com",
    L"events.launchdarkly.com",
    L"intercom.io",     // Support chat
    L"intercomcdn.com",
    L"widget.intercom.io"
};

// Extract domain/host from URL
static std::wstring ExtractHost(const std::wstring& url) {
    std::wstring s = url;
    // remove scheme
    auto pos = s.find(L"://");
    if (pos != std::wstring::npos) s = s.substr(pos + 3);
    // remove path
    auto slash = s.find(L'/');
    if (slash != std::wstring::npos) s = s.substr(0, slash);
    // remove port
    auto colon = s.rfind(L':');
    if (colon != std::wstring::npos && colon > s.find(L'.')) s = s.substr(0, colon);
    // lowercase
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    // remove www.
    if (s.substr(0,4) == L"www.") s = s.substr(4);
    return s;
}

static bool HostMatchesDomain(const std::wstring& host, const std::wstring& domain) {
    if (host == domain) return true;
    std::wstring suffix = L"." + domain;
    if (host.size() >= suffix.size() &&
        host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0)
        return true;
    return false;
}

bool IsBlockedContent(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(),lower.end(),lower.begin(),::towlower);
    // keyword check
    for (const auto& kw : ADULT_KEYWORDS)
        if (lower.find(kw) != std::wstring::npos) return true;
    // domain check
    std::wstring host = ExtractHost(text);
    for (const auto& d : ADULT_DOMAINS)
        if (HostMatchesDomain(host, d)) return true;
    return false;
}

bool IsAdDomain(const std::wstring& url) {
    std::wstring host = ExtractHost(url);
    for (const auto& d : AD_DOMAINS)
        if (HostMatchesDomain(host, d)) return true;
    return false;
}

bool IsTrackerDomain(const std::wstring& url) {
    std::wstring host = ExtractHost(url);
    for (const auto& d : TRACKER_DOMAINS)
        if (HostMatchesDomain(host, d)) return true;
    return false;
}

bool IsTrackerWhitelisted(const std::wstring& url) {
    std::wstring host = ExtractHost(url);
    for (const auto& d : TRACKER_WHITELIST) {
        if (d.find(L'*') == std::wstring::npos) {
            if (HostMatchesDomain(host, d)) return true;
        } else {
            auto star = d.find(L'*');
            std::wstring suffix = d.substr(star + 1);
            if (host.size() >= suffix.size() &&
                host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0)
                return true;
        }
    }
    return false;
}

// SafeSearch enforcer
static std::wstring ApplySafeSearch(const std::wstring& url) {
    std::wstring lower = url;
    std::transform(lower.begin(),lower.end(),lower.begin(),::towlower);

    if (lower.find(L"google.") != std::wstring::npos && lower.find(L"/search") != std::wstring::npos) {
        if (url.find(L"safe=strict") != std::wstring::npos) return L"";
        if (url.find(L"safe=") != std::wstring::npos) {
            // replace existing safe param
            std::wstring r = url;
            auto p = r.find(L"safe=");
            if (p != std::wstring::npos) {
                auto end = r.find(L'&', p);
                r = r.substr(0, p) + L"safe=strict" + (end != std::wstring::npos ? r.substr(end) : L"");
                return r;
            }
        }
        return url + (url.find(L'?') != std::wstring::npos ? L"&" : L"?") + L"safe=strict";
    }
    if (lower.find(L"bing.com") != std::wstring::npos && lower.find(L"/search") != std::wstring::npos) {
        if (url.find(L"adlt=strict") != std::wstring::npos) return L"";
        return url + (url.find(L'?') != std::wstring::npos ? L"&" : L"?") + L"adlt=strict";
    }
    return L"";
}

// ─────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static int CalcTabWidth(int W, int tc, UINT dpi) {
    int avail = W - WinBtnW(dpi)*5 - LogoW(dpi) - S(D_NEW_TAB_BTN+16,dpi);
    int w = tc>0 ? avail/tc : S(D_TAB_W_MAX,dpi);
    return max(S(D_TAB_W_MIN,dpi), min(S(D_TAB_W_MAX,dpi),w));
}
static RECT GetTabRect(int W, int i, int tc, UINT dpi) {
    int tw=CalcTabWidth(W,tc,dpi), x=LogoW(dpi)+i*tw;
    RECT r={x,S(8,dpi),x+tw,TitleBarH(dpi)}; return r;
}
static RECT GetNewTabBtnRect(int W, int tc, UINT dpi) {
    int tw=CalcTabWidth(W,tc,dpi), x=LogoW(dpi)+tc*tw+S(4,dpi);
    int cy=S(8,dpi)+(TitleBarH(dpi)-S(8,dpi))/2, sz=S(22,dpi);
    RECT r={x,cy-sz/2,x+sz,cy+sz/2}; return r;
}
static RECT GetWebViewRect(HWND hWnd) {
    RECT b; GetClientRect(hWnd,&b); b.top+=NavTotalH(hWnd); return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR
// ─────────────────────────────────────────────────────────────────────────────
static void RepositionAddressBar(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (!wd.hAddressBar) return;
    if (g_isPureViewerMode || wd.isFullScreen) { ShowWindow(wd.hAddressBar,SW_HIDE); return; }

    UINT dpi=GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd,&cr); int W=cr.right;
    int navBtnArea=S(8+36*3+8,dpi), rightIconArea=S(38*3+12,dpi);
    int addrH=S(30,dpi), toolY=TitleBarH(dpi);
    int addrX=navBtnArea, addrW=W-navBtnArea-rightIconArea-S(8,dpi);
    int leftDecorW=S(35,dpi), rightDecorW=S(95,dpi);
    int editH=S(18,dpi), editY=toolY+(ToolbarH(dpi)-addrH)/2+(addrH-editH)/2;

    ShowWindow(wd.hAddressBar,SW_SHOW);
    SetWindowPos(wd.hAddressBar,NULL,addrX+leftDecorW,editY,
        addrW-leftDecorW-rightDecorW,editH,SWP_NOZORDER|SWP_NOACTIVATE);

    if (wd.hAddrFont) DeleteObject(wd.hAddrFont);
    wd.hAddrFont=CreateFontW(S(14,dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    SendMessage(wd.hAddressBar,WM_SETFONT,(WPARAM)wd.hAddrFont,TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// FULLSCREEN TOGGLE
// ─────────────────────────────────────────────────────────────────────────────
void ToggleFullScreen(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd=g_windows[hWnd];
    DWORD style=GetWindowLong(hWnd,GWL_STYLE);
    if (!wd.isFullScreen) {
        MONITORINFO mi={sizeof(mi)};
        if (GetWindowPlacement(hWnd,&wd.wpPrev)&&
            GetMonitorInfo(MonitorFromWindow(hWnd,MONITOR_DEFAULTTOPRIMARY),&mi)) {
            SetWindowLong(hWnd,GWL_STYLE,style&~(WS_CAPTION|WS_THICKFRAME));
            SetWindowPos(hWnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
                mi.rcMonitor.right-mi.rcMonitor.left,
                mi.rcMonitor.bottom-mi.rcMonitor.top-2,
                SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
            wd.isFullScreen=true;
        }
    } else {
        SetWindowLong(hWnd,GWL_STYLE,style|WS_CAPTION|WS_THICKFRAME);
        SetWindowPlacement(hWnd,&wd.wpPrev);
        SetWindowPos(hWnd,NULL,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        wd.isFullScreen=false;
    }
    RepositionAddressBar(hWnd);
    RECT wvr=GetWebViewRect(hWnd);
    if (wd.isFullScreen) wvr.top=0;
    for (auto& t:wd.tabs) if (t.controller) t.controller->put_Bounds(wvr);
    InvalidateRect(hWnd,NULL,TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// ACCELERATOR HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class AcceleratorHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
    HWND m_hWnd; ULONG m_ref=1;
public:
    AcceleratorHandler(HWND h):m_hWnd(h){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid==IID_IUnknown||riid==__uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler))
            {*ppv=this;AddRef();return S_OK;}
        *ppv=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&m_ref);}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r=InterlockedDecrement(&m_ref); if(!r)delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller*,
        ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
        if (kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||
            kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            UINT vk; args->get_VirtualKey(&vk);
            if (vk==VK_F11){ToggleFullScreen(m_hWnd);args->put_Handled(TRUE);}
            if (vk==VK_ESCAPE&&g_windows.count(m_hWnd)&&g_windows[m_hWnd].isFullScreen)
                {ToggleFullScreen(m_hWnd);args->put_Handled(TRUE);}
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR SUBCLASS
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK AddrBarProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR){
    if (msg==WM_KEYDOWN&&wParam==VK_RETURN) {
        HWND hParent=GetParent(hWnd);
        if (!g_windows.count(hParent)) return 0;
        auto& wd=g_windows[hParent];
        auto* tab=wd.active();
        if (!tab||!tab->webview) return 0;

        wchar_t buf[2048]; GetWindowTextW(hWnd,buf,2048);
        std::wstring input=buf;
        input.erase(0,input.find_first_not_of(L" \t"));
        if (!input.empty()) input.erase(input.find_last_not_of(L" \t")+1);
        if (input.empty()) return 0;

        if (IsBlockedContent(input)) {
            SetWindowTextW(hWnd,L"blocked by rasfocus");
            tab->url=L"blocked by rasfocus";
            tab->webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
            return 0;
        }

        std::wstring url;
        if      (input.find(L" ")!=std::wstring::npos)                            url=L"https://www.google.com/search?q="+UrlEncode(input);
        else if (input.find(L"http://")==0||input.find(L"https://")==0)            url=input;
        else if (input.find(L".")!=std::wstring::npos)                             url=L"https://"+input;
        else                                                                        url=L"https://www.google.com/search?q="+UrlEncode(input);

        tab->webview->Navigate(url.c_str());
        return 0;
    }
    if (msg==WM_NCPAINT) return 0;
    return DefSubclassProc(hWnd,msg,wParam,lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// DOUBLE-BUFFERED PAINT
// ─────────────────────────────────────────────────────────────────────────────
static void DoubleBufferedPaint(HWND hWnd,HDC hdcReal,std::function<void(HDC,int,int)> fn){
    RECT cr; GetClientRect(hWnd,&cr);
    int W=cr.right,H=cr.bottom;
    if(W<=0||H<=0)return;
    HDC hdcMem=CreateCompatibleDC(hdcReal);
    HBITMAP hBmp=CreateCompatibleBitmap(hdcReal,W,H);
    HBITMAP hOld=(HBITMAP)SelectObject(hdcMem,hBmp);
    fn(hdcMem,W,H);
    BitBlt(hdcReal,0,0,W,H,hdcMem,0,0,SRCCOPY);
    SelectObject(hdcMem,hOld); DeleteObject(hBmp); DeleteDC(hdcMem);
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI+ PATH HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static void AddRoundRect(GraphicsPath& p,float x,float y,float w,float h,float r){
    if(r<=0.f){p.AddRectangle(RectF(x,y,w,h));return;}
    p.AddArc(x,y,r*2,r*2,180,90);
    p.AddArc(x+w-r*2,y,r*2,r*2,270,90);
    p.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);
    p.AddArc(x,y+h-r*2,r*2,r*2,90,90);
    p.CloseFigure();
}
static void BuildChromeTabPath(GraphicsPath& path,float x,float y,float w,float h,float cr){
    float bl=x,br=x+w,top=y,bot=y+h,nw=cr*1.6f;
    path.StartFigure();
    path.AddLine(bl,bot,bl+nw,bot);
    path.AddBezier(bl+nw,bot,bl+nw*0.5f,bot,bl+cr*0.25f,bot-cr,bl+cr,top+cr);
    path.AddArc(bl+cr,top,cr*2,cr*2,180,90);
    path.AddLine(bl+cr*3,top,br-cr*3,top);
    path.AddArc(br-cr*3,top,cr*2,cr*2,270,90);
    path.AddBezier(br-cr,top+cr,br-cr*0.25f,bot-cr,br-nw*0.5f,bot,br-nw,bot);
    path.AddLine(br-nw,bot,br,bot);
    path.CloseFigure();
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE / EXTENSION / SETTINGS / GEMINI MENU HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static void ShowProfileMenu(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    // Show a context menu mimicking Chrome-like profile menu
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING|MF_GRAYED, 0, L"RasBrowser User");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1001, L"Sync & Google services");
    AppendMenuW(hMenu, MF_STRING, 1002, L"Passwords");
    AppendMenuW(hMenu, MF_STRING, 1003, L"Payment methods");
    AppendMenuW(hMenu, MF_STRING, 1004, L"Addresses and more");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1005, L"Open Gemini AI");
    AppendMenuW(hMenu, MF_STRING, 1006, L"Open ChatGPT");

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int rx = cr.right - S(36*3+8, dpi);
    POINT pt = { rx, TitleBarH(dpi) + ToolbarH(dpi) };
    ClientToScreen(hWnd, &pt);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (!g_windows.count(hWnd)) return;
    auto* tab = wd.active();
    if (!tab || !tab->webview) return;

    if      (cmd==1005) tab->webview->Navigate(L"https://gemini.google.com");
    else if (cmd==1006) tab->webview->Navigate(L"https://chatgpt.com");
}

static void ShowExtensionsMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING|MF_GRAYED, 0, L"Extensions");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING|MF_GRAYED, 0, L"No extensions installed");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 2001, L"Manage Extensions");

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int rx = cr.right - S(36*2+8, dpi);
    POINT pt = { rx, TitleBarH(dpi) + ToolbarH(dpi) };
    ClientToScreen(hWnd, &pt);

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

static void ApplyTheme(HWND hWnd, bool dark);  // forward decl

static void ShowMainMenu(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];

    // ── Appearance submenu (Chrome-style theme picker) ────────────────────
    HMENU hAppear = CreatePopupMenu();
    // System default
    AppendMenuW(hAppear, MF_STRING, 4001, L"Use system default");
    AppendMenuW(hAppear, MF_SEPARATOR, 0, NULL);
    // Light
    AppendMenuW(hAppear, MF_STRING, 4002, L"Light");
    // Dark
    AppendMenuW(hAppear, MF_STRING, 4003, L"Dark");
    // Check the active one
    if (!wd.isDarkMode) CheckMenuItem(hAppear, 4002, MF_BYCOMMAND|MF_CHECKED);
    else                CheckMenuItem(hAppear, 4003, MF_BYCOMMAND|MF_CHECKED);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 3001, L"New Tab\tCtrl+T");
    AppendMenuW(hMenu, MF_STRING, 3002, L"New Window");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 3003, L"History");
    AppendMenuW(hMenu, MF_STRING, 3004, L"Downloads\tCtrl+J");
    AppendMenuW(hMenu, MF_STRING, 3005, L"Bookmarks");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    // Appearance submenu
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hAppear, L"Appearance");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    // Gemini Developer Mode toggle
    AppendMenuW(hMenu, MF_STRING, 3010, L"Open Gemini (Developer Mode)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 3006, L"Settings");
    AppendMenuW(hMenu, MF_STRING, 3007, L"Help");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 3008, L"Exit");

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int rx = cr.right - S(36+8, dpi);
    POINT pt = { rx, TitleBarH(dpi) + ToolbarH(dpi) };
    ClientToScreen(hWnd, &pt);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
    DestroyMenu(hAppear);

    if (!g_windows.count(hWnd)) return;
    auto* tab = wd.active();

    switch (cmd) {
    case 3001: AddTab(hWnd, L"LOCAL_NTP"); break;
    case 3002: {
        AddTab(hWnd, L"LOCAL_NTP");
        break;
    }
    case 3003: if (tab&&tab->webview) tab->webview->Navigate(L"about:history"); break;
    case 3004: if (tab&&tab->webview) tab->webview->Navigate(L"about:downloads"); break;
    case 3006:
        MessageBoxW(hWnd,
            L"RasBrowser Settings\n\n"
            L"Dark Mode: Toggle with Moon/Sun icon or Menu > Appearance\n"
            L"Always on Top: Toggle with Pin icon\n"
            L"Fullscreen: Press F11\n"
            L"New Tab: Double-click titlebar or Ctrl+T\n"
            L"Close Tab: Click X on tab\n\n"
            L"Gemini Developer Mode: Menu > Open Gemini (Developer Mode)",
            L"RasBrowser Settings", MB_OK|MB_ICONINFORMATION);
        break;
    case 3010:
        // Gemini Developer Mode: open with special user agent for full API access
        if (tab && tab->webview) {
            ComPtr<ICoreWebView2Settings> s;
            if (SUCCEEDED(tab->webview->get_Settings(&s)) && s) {
                ComPtr<ICoreWebView2Settings2> s2;
                if (SUCCEEDED(s->QueryInterface(IID_PPV_ARGS(&s2)))) {
                    s2->put_UserAgent(
                        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                        L"AppleWebKit/537.36 (KHTML, like Gecko) "
                        L"Chrome/137.0.0.0 Safari/537.36");
                }
            }
            tab->webview->Navigate(L"https://gemini.google.com/app");
        }
        break;
    // ── Appearance theme choices ─────────────────────────────────────────
    case 4001: {
        // System default: check Windows dark mode setting
        HKEY hk; bool sysDark = false;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hk) == ERROR_SUCCESS) {
            DWORD val = 1, sz = sizeof(val);
            RegQueryValueExW(hk, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &sz);
            RegCloseKey(hk);
            sysDark = (val == 0);
        }
        ApplyTheme(hWnd, sysDark);
        break;
    }
    case 4002: ApplyTheme(hWnd, false); break;  // Light
    case 4003: ApplyTheme(hWnd, true);  break;  // Dark
    case 3008: DestroyWindow(hWnd); break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// APPLY THEME — shared by dark button + Appearance menu
// ─────────────────────────────────────────────────────────────────────────────
static void ApplyTheme(HWND hWnd, bool dark) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isDarkMode == dark) return;  // no change needed
    wd.isDarkMode = dark;

    const std::wstring darkInjectScript =
        L"window.__RAS_DARK__=true;"
        L"(function(){"
        L"  var s=document.getElementById('__ras_dark_mode__');"
        L"  if(!s){"
        L"    s=document.createElement('style');"
        L"    s.id='__ras_dark_mode__';"
        L"    (document.head||document.documentElement).appendChild(s);"
        L"  }"
        L"  s.textContent='"
        L"    html{filter:invert(1) hue-rotate(180deg)!important;background:#1e1e1e!important;}"
        L"    img,video,canvas,picture,svg,iframe{"
        L"      filter:invert(1) hue-rotate(180deg)!important;"
        L"    }"
        L"  ';"
        L"})();";

    const std::wstring lightRemoveScript =
        L"window.__RAS_DARK__=false;"
        L"(function(){"
        L"  var s=document.getElementById('__ras_dark_mode__');"
        L"  if(s) s.remove();"
        L"})();";

    for (auto& t : wd.tabs) {
        if (!t.controller || !t.webview) continue;
        ComPtr<ICoreWebView2Controller2> c2;
        if (SUCCEEDED(t.controller->QueryInterface(IID_PPV_ARGS(&c2)))) {
            COREWEBVIEW2_COLOR bg = wd.isDarkMode
                ? COREWEBVIEW2_COLOR{255,30,30,30}
                : COREWEBVIEW2_COLOR{255,255,255,255};
            c2->put_DefaultBackgroundColor(bg);
        }
        if (t.url == L"LOCAL_NTP" || t.url == L"about:blank") {
            t.webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
        } else if (t.url.find(L"blocked by rasfocus") != std::wstring::npos) {
            t.webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
        } else {
            t.webview->ExecuteScript(
                wd.isDarkMode ? darkInjectScript.c_str() : lightRemoveScript.c_str(),
                nullptr);
        }
    }
    InvalidateRect(hWnd, NULL, TRUE);
    if (wd.hAddressBar) InvalidateRect(wd.hAddressBar, NULL, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN DRAW FUNCTION
// ─────────────────────────────────────────────────────────────────────────────
static void DrawBrowserContent(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;

    UINT dpi=GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd,&cr); int W=cr.right;
    int titleH=TitleBarH(dpi),toolH=ToolbarH(dpi),navH=NavTotalH(hWnd),winBtnW=WinBtnW(dpi);

    Color cBgFrame  =wd.isDarkMode?Color(255,30,30,30)   :Color(255,230,230,235);
    Color cBgTool   =wd.isDarkMode?Color(255,43,43,43)   :Color(255,255,255,255);
    Color cTxtPrim  =wd.isDarkMode?Color(255,255,255,255):Color(255,32,33,36);
    Color cTxtDim   =wd.isDarkMode?Color(255,154,156,160):Color(255,95,99,104);
    Color cTabActive=wd.isDarkMode?Color(255,43,43,43)   :Color(255,255,255,255);
    Color cTabHover =wd.isDarkMode?Color(255,45,45,45)   :Color(255,235,236,240);
    Color cAddrBg   =wd.isDarkMode?Color(255,26,26,26)   :Color(255,241,243,244);
    Color cAddrBord =wd.isDarkMode?Color(255,68,68,68)   :Color(255,160,180,210);
    Color cDivLine  =wd.isDarkMode?Color(255,45,45,45)   :Color(255,218,220,224);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    // AntiAlias (not ClearTypeGridFit) — symbol/icon fonts like Segoe MDL2 Assets
    // render as blank boxes under ClearType because ClearType uses sub-pixel LCD
    // rendering which requires RGB stripe geometry; icon glyphs don't have that.
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    SolidBrush bFrame(cBgFrame);
    g.FillRectangle(&bFrame,0,0,W,titleH);

    if (!g_isPureViewerMode) {
        SolidBrush bTool(cBgTool);
        g.FillRectangle(&bTool,0,titleH,W,toolH);
        Pen sepPen(cDivLine,1.0f);
        if (!(wd.active()&&(wd.active()->url==L"LOCAL_NTP"||wd.active()->url==L"about:blank")))
            g.DrawLine(&sepPen,0,navH-1,W,navH-1);
    }

    FontFamily ffSeg(L"Segoe UI");
    // Segoe MDL2 Assets — present on all Windows 10+ machines.
    // Guard: if IsAvailable() fails (e.g. broken font cache), fall back to
    // Segoe UI Symbol which ships with Windows 7+ and covers the same PUA range.
    FontFamily ffMDLa(L"Segoe MDL2 Assets");
    FontFamily ffMDLb(L"Segoe UI Symbol");
    FontFamily& ffMDL = (ffMDLa.IsAvailable() ? ffMDLa : ffMDLb);

    Font fSmall  (&ffSeg,Sf(12.f,dpi),FontStyleRegular,UnitPixel);
    Font fSmallBd(&ffSeg,Sf(12.f,dpi),FontStyleBold,UnitPixel);
    Font fBrand  (&ffSeg,Sf(16.f,dpi),FontStyleBold,UnitPixel);
    Font fIcon   (&ffMDL,Sf(16.f,dpi),FontStyleRegular,UnitPixel);  // was 14 — larger for toolbar
    Font fIconSm (&ffMDL,Sf(13.f,dpi),FontStyleRegular,UnitPixel);  // was 11 — larger for title/tab

    StringFormat sfC,sfL;
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(StringFormatFlagsNoWrap);
    sfL.SetTrimming(StringTrimmingEllipsisCharacter);

    SolidBrush brPrim(cTxtPrim), brDim(cTxtDim);

    // Branding
    {
        int iconX=S(15,dpi);
        SolidBrush brTeal(Color(255,12,168,176));
        SolidBrush brWhite(Color(255,255,255,255));
        SolidBrush brDark2(Color(255,32,33,36));
        g.DrawString(L"Ras",-1,&fBrand,RectF((float)iconX,0.f,(float)S(40,dpi),(float)titleH),&sfL,&brTeal);
        g.DrawString(L"Browser",-1,&fBrand,RectF((float)iconX+S(32,dpi),0.f,(float)S(100,dpi),(float)titleH),&sfL,wd.isDarkMode?&brWhite:&brDark2);
    }

    // Window buttons
    {
        int bx=W-winBtnW*5;
        auto DrawWinBtn=[&](int x,bool hover,bool isClose,const wchar_t* ico){
            if (hover){
                SolidBrush hb(isClose?Color(255,232,17,35):(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0)));
                g.FillRectangle(&hb,x,0,winBtnW,titleH);
            }
            SolidBrush tc2(isClose&&hover?Color(255,255,255,255):cTxtPrim);
            g.DrawString(ico,-1,&fIconSm,RectF((float)x,0.f,(float)winBtnW,(float)titleH),&sfC,&tc2);
        };
        DrawWinBtn(bx,           wd.hPin,   false, wd.isPinned?L"\xE840":L"\xE718");
        DrawWinBtn(bx+winBtnW,   wd.hDark,  false, wd.isDarkMode?L"\xE708":L"\xE706");
        DrawWinBtn(bx+winBtnW*2, wd.hMin,   false, L"\xE921");
        DrawWinBtn(bx+winBtnW*3, wd.hMax,   false, IsZoomed(hWnd)?L"\xE923":L"\xE922");
        DrawWinBtn(bx+winBtnW*4, wd.hClose, true,  L"\xE8BB");
    }

    if (!g_isPureViewerMode) {
        // Tab strip
        {
            int tc=(int)wd.tabs.size();
            float cornerR=Sf(8.f,dpi);
            for (int i=0;i<tc;i++) {
                RECT tr=GetTabRect(W,i,tc,dpi);
                float tx=(float)tr.left,ty=(float)tr.top,tw=(float)(tr.right-tr.left),th=(float)(tr.bottom-tr.top);
                bool isActive=(i==wd.activeTab), isHover=(i==wd.hoverTabIndex);
                GraphicsPath tp2;
                BuildChromeTabPath(tp2,tx,ty,tw,th,cornerR);
                if (isActive||isHover){
                    SolidBrush bTab(isActive?cTabActive:cTabHover);
                    g.FillPath(&bTab,&tp2);
                }
                float iconSz=Sf(14.f,dpi),iconX2=tx+Sf((float)D_TAB_PAD+4,dpi),iconY2=ty+(th-iconSz)*0.5f;
                SolidBrush fvBrush(isActive?Color(255,12,168,176):cTxtDim);
                g.FillEllipse(&fvBrush,iconX2,iconY2,iconSz,iconSz);
                const auto& tab2=wd.tabs[i];
                SolidBrush tBrush(isActive?cTxtPrim:cTxtDim);
                float titleX=iconX2+iconSz+Sf(6.f,dpi),closeW=Sf(24.f,dpi),titleW=tw-(titleX-tx)-closeW;
                if (titleW>0){
                    std::wstring dt=tab2.title;
                    if (dt.empty()||tab2.url==L"LOCAL_NTP"||tab2.url==L"about:blank") dt=L"New Tab";
                    if (tab2.url.find(L"blocked by rasfocus")!=std::wstring::npos) dt=L"Blocked";
                    g.DrawString(dt.c_str(),-1,&fSmall,RectF(titleX,ty,titleW,th),&sfL,&tBrush);
                }
                if (isActive||isHover){
                    float cSz=Sf(16.f,dpi),cx2=tx+tw-cSz-Sf(6.f,dpi),cy2=ty+(th-cSz)*0.5f;
                    if (isHover&&!isActive){SolidBrush hbx(Color(20,255,255,255));g.FillEllipse(&hbx,cx2,cy2,cSz,cSz);}
                    g.DrawString(L"\xE8BB",-1,&fIconSm,RectF(cx2,cy2,cSz,cSz),&sfC,&brDim);
                }
            }
            RECT ntr=GetNewTabBtnRect(W,tc,dpi);
            if (wd.hNewTab){SolidBrush hb(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0));g.FillEllipse(&hb,(float)ntr.left,(float)ntr.top,(float)(ntr.right-ntr.left),(float)(ntr.bottom-ntr.top));}
            g.DrawString(L"\xE710",-1,&fIconSm,RectF((float)ntr.left,(float)ntr.top,(float)(ntr.right-ntr.left),(float)(ntr.bottom-ntr.top)),&sfC,&brDim);
        }

        // Toolbar
        {
            int toolY=titleH,curX=S(8,dpi),btnSz=S(32,dpi),btnStep=S(36,dpi);
            float btnHf=(float)toolH;
            auto DrawNavBtn=[&](bool hover,bool enabled,const wchar_t* ico,int& x){
                if (hover&&enabled){SolidBrush hb(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0));g.FillEllipse(&hb,(float)(x+S(2,dpi)),(float)(toolY+S(4,dpi)),(float)S(28,dpi),(float)S(28,dpi));}
                SolidBrush ic2(enabled?cTxtPrim:cDivLine);
                g.DrawString(ico,-1,&fIcon,RectF((float)x,(float)toolY,(float)btnSz,btnHf),&sfC,&ic2);
                x+=btnStep;
            };
            auto* atab=wd.active();
            DrawNavBtn(wd.hBack,atab&&atab->canBack,L"\xE72B",curX);
            DrawNavBtn(wd.hFwd, atab&&atab->canFwd, L"\xE72A",curX);
            DrawNavBtn(wd.hRel, true,                L"\xE72C",curX);

            // Address bar background
            {
                int addrX=curX+S(4,dpi),rightIX=W-S(38*3+12,dpi);
                int addrW2=rightIX-addrX-S(8,dpi),addrH2=S(30,dpi),addrY=toolY+(toolH-addrH2)/2;
                SolidBrush addrBg(cAddrBg); Pen addrPen(cAddrBord,1.0f);
                GraphicsPath pill;
                AddRoundRect(pill,(float)addrX,(float)addrY,(float)addrW2,(float)addrH2,Sf(15.f,dpi));
                g.FillPath(&addrBg,&pill); g.DrawPath(&addrPen,&pill);
                SolidBrush gBrush(wd.isDarkMode?Color(255,200,200,200):Color(255,80,80,80));
                g.DrawString(L"G",-1,&fBrand,RectF((float)addrX+Sf(12.f,dpi),(float)addrY,Sf(20.f,dpi),(float)addrH2),&sfC,&gBrush);
                // AI Mode pill
                float aiW=Sf(85.f,dpi),aiH=(float)addrH2-Sf(6.f,dpi);
                float aiX=(float)addrX+addrW2-aiW-Sf(3.f,dpi),aiY=(float)addrY+Sf(3.f,dpi);
                GraphicsPath aiPill; AddRoundRect(aiPill,aiX,aiY,aiW,aiH,Sf(10.f,dpi));
                LinearGradientBrush aiBg(PointF(aiX,aiY),PointF(aiX+aiW,aiY),Color(255,12,168,176),Color(255,0,92,230));
                g.FillPath(&aiBg,&aiPill);
                SolidBrush aiTxt(Color(255,255,255,255));
                g.DrawString(L"\x2728 AI Mode",-1,&fSmallBd,RectF(aiX,aiY,aiW,aiH),&sfC,&aiTxt);
            }

            // Right buttons: Profile, Extensions, Menu
            int rx=W-S(36*3+8,dpi);
            auto DrawRightBtn=[&](bool hover,const wchar_t* ico,int x){
                if (hover){SolidBrush hb(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0));g.FillEllipse(&hb,(float)(x+S(2,dpi)),(float)(toolY+S(4,dpi)),(float)S(28,dpi),(float)S(28,dpi));}
                g.DrawString(ico,-1,&fIcon,RectF((float)x,(float)toolY,(float)btnSz,btnHf),&sfC,&brPrim);
            };
            DrawRightBtn(wd.hProfile,L"\xE77B",rx); rx+=btnStep;
            DrawRightBtn(wd.hExt,    L"\xE9D2",rx); rx+=btnStep;
            DrawRightBtn(wd.hMenu,   L"\xE712",rx);
        }

        // Bookmark bar — শুধু homepage (LOCAL_NTP) এ দেখাবে, অন্য কোনো website-এ নয়
        if (wd.active() && wd.active()->url == L"LOCAL_NTP") {
            int bmkY=titleH+toolH, bmkH=S(D_BOOKMARK_H,dpi);
            SolidBrush bmkBg(cBgTool); g.FillRectangle(&bmkBg,0,bmkY,W,bmkH);
            Pen sepPen2(cDivLine,1.0f); g.DrawLine(&sepPen2,0,bmkY+bmkH-1,W,bmkY+bmkH-1);
            SolidBrush brTxt(cTxtDim);
            // Bookmark items
            struct BMK { const wchar_t* icon; const wchar_t* label; int x; };
            BMK items[] = {
                {L"\xE8A4", L"Web Store",       15},
                {L"\xE8A4", L"RasFocus Admin", 120},
                {L"\uE7AD", L"Gemini AI",       270}, // Star icon for Gemini
            };
            for (auto& bm : items) {
                g.DrawString(bm.icon,-1,&fIconSm,RectF((float)S(bm.x,dpi),(float)bmkY,(float)S(20,dpi),(float)bmkH),&sfC,&brTxt);
                g.DrawString(bm.label,-1,&fSmall,RectF((float)S(bm.x+20,dpi),(float)bmkY,(float)S(100,dpi),(float)bmkH),&sfL,&brTxt);
            }
            g.DrawString(L"\xE838",-1,&fIconSm,RectF((float)(W-S(130,dpi)),(float)bmkY,(float)S(20,dpi),(float)bmkH),&sfC,&brTxt);
            g.DrawString(L"All Bookmarks",-1,&fSmall,RectF((float)(W-S(110,dpi)),(float)bmkY,(float)S(100,dpi),(float)bmkH),&sfL,&brTxt);
        }
    }
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    if (g_windows[hWnd].isFullScreen) return;
    DoubleBufferedPaint(hWnd,hdc,[&](HDC memDC,int W,int H){
        bool dark=g_windows[hWnd].isDarkMode;
        HBRUSH hbg=CreateSolidBrush(dark?RGB(30,30,30):RGB(230,230,235));
        RECT fr={0,0,W,H}; FillRect(memDC,&fr,hbg); DeleteObject(hbg);
        DrawBrowserContent(hWnd,memDC);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB MANAGEMENT
// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND hWnd, int idx) {
    auto& wd=g_windows[hWnd];
    if (idx<0||idx>=(int)wd.tabs.size()) return;
    if (wd.activeTab!=idx&&wd.activeTab<(int)wd.tabs.size())
        if (wd.tabs[wd.activeTab].controller)
            wd.tabs[wd.activeTab].controller->put_IsVisible(FALSE);
    wd.activeTab=idx;
    auto& tab=wd.tabs[idx];
    if (tab.controller) {
        tab.controller->put_IsVisible(TRUE);
        RECT wvr=GetWebViewRect(hWnd); tab.controller->put_Bounds(wvr);
    } else {
        CreateWebViewForTab(hWnd,idx);
    }
    if (wd.hAddressBar) {
        if (tab.url==L"LOCAL_NTP"||tab.url==L"about:blank"||
            tab.url.find(L"blocked by rasfocus")!=std::wstring::npos)
            SetWindowTextW(wd.hAddressBar,L"");
        else SetWindowTextW(wd.hAddressBar,tab.url.c_str());
    }
    RepositionAddressBar(hWnd);
    InvalidateRect(hWnd,NULL,TRUE);
}

static void CloseTab(HWND hWnd, int idx) {
    auto& wd=g_windows[hWnd];
    if (wd.tabs.empty()) return;
    if (idx<0||idx>=(int)wd.tabs.size()) return;  // guard: bad index
    {
        // Release controller inside its own scope so the ComPtr destructor
        // fires (decrements ref) before we erase the TabData from the vector.
        auto& tab=wd.tabs[idx];
        if (tab.controller) {
            tab.controller->put_IsVisible(FALSE);
            tab.controller->Close();
            tab.controller = nullptr;  // explicit release — avoids dangling ref after erase
        }
        tab.webview = nullptr;
    }
    wd.tabs.erase(wd.tabs.begin()+idx);
    if (wd.tabs.empty()){DestroyWindow(hWnd);return;}
    wd.activeTab=min(wd.activeTab,(int)wd.tabs.size()-1);
    SwitchToTab(hWnd,wd.activeTab);
}

static void AddTab(HWND hWnd, std::wstring url) {
    auto& wd=g_windows[hWnd];
    TabData tab; tab.url=url; tab.title=L"New Tab";
    wd.tabs.push_back(tab);
    int newIdx=(int)wd.tabs.size()-1;
    // SwitchToTab already calls CreateWebViewForTab when controller is null —
    // do NOT call it again here, that caused a double WebView creation making new tabs slow.
    SwitchToTab(hWnd,newIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 CONTROLLER HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class TabControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    HWND         m_hWnd;
    int          m_tabIdx;
    std::wstring m_startUrl;
    ULONG        m_ref=1;
public:
    TabControllerHandler(HWND h,int idx,std::wstring url):m_hWnd(h),m_tabIdx(idx),m_startUrl(std::move(url)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** ppv) override {*ppv=this;return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&m_ref);}
    ULONG STDMETHODCALLTYPE Release() override {ULONG r=InterlockedDecrement(&m_ref);if(!r)delete this;return r;}

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (FAILED(hr)||!ctl) return S_OK;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd=g_windows[m_hWnd];
        if (m_tabIdx>=(int)wd.tabs.size()) return S_OK;

        auto& tab=wd.tabs[m_tabIdx];
        tab.controller=ctl;
        ctl->get_CoreWebView2(&tab.webview);

        // Background color
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg=wd.isDarkMode?COREWEBVIEW2_COLOR{255,30,30,30}:COREWEBVIEW2_COLOR{255,255,255,255};
            ctl2->put_DefaultBackgroundColor(bg);
        }

        // ── Settings ──────────────────────────────────────────────────────────
        ICoreWebView2Settings* settings=nullptr;
        if (SUCCEEDED(tab.webview->get_Settings(&settings))&&settings) {
            settings->put_IsScriptEnabled(TRUE);
            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            settings->put_IsWebMessageEnabled(TRUE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_IsStatusBarEnabled(TRUE);

            // FIX: DOM Storage via Settings2 (fixes Gemini & Facebook storage)
            ComPtr<ICoreWebView2Settings2> s2;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s2)))) {
                // UserAgent: Latest Chrome — YouTube, Google, Gemini সব সাইটে Chrome-এর মতো দেখাবে
                s2->put_UserAgent(
                    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                    L"AppleWebKit/537.36 (KHTML, like Gecko) "
                    L"Chrome/137.0.0.0 Safari/537.36");
            }

            // FIX BUILD ERROR: IsDomStorageEnabled is on Settings3, not Settings
            ComPtr<ICoreWebView2Settings3> s3;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s3)))) {
                s3->put_IsDomStorageEnabled(TRUE);
            }
        }

        // ── Dark Mode: inject CSS on every document created ─────────────────────
        // This runs before page paint, so no white flash occurs.
        // We always inject the helper; the style element is only added when
        // isDarkMode is true at the time of navigation.
        // ── AddScriptToExecuteOnDocumentCreated: checks window.__RAS_DARK__ at runtime ──
        // This way toggle always works even after tab was created.
        {
            // AddScriptToExecuteOnDocumentCreated fires before page renders.
            // It reads window.__RAS_DARK__ (set by ExecuteScript below) so it
            // correctly injects or removes dark CSS on every navigation.
            tab.webview->AddScriptToExecuteOnDocumentCreated(
                L"(function(){"
                L"  var id='__ras_dark_mode__';"
                L"  var existing=document.getElementById(id);"
                L"  if(window.__RAS_DARK__){"
                L"    if(existing) return;"
                L"    var s=document.createElement('style');"
                L"    s.id=id;"
                L"    s.textContent='"
                L"      html{filter:invert(1) hue-rotate(180deg)!important;background:#1e1e1e!important;}"
                L"      img,video,canvas,picture,svg,iframe{"
                L"        filter:invert(1) hue-rotate(180deg)!important;"
                L"      }"
                L"    ';"
                L"    (document.head||document.documentElement).appendChild(s);"
                L"  } else {"
                L"    if(existing) existing.remove();"
                L"  }"
                L"})();",
                nullptr);
            // Prime the flag for this tab's first load
            std::wstring setVar = std::wstring(L"window.__RAS_DARK__=")
                                + (wd.isDarkMode ? L"true;" : L"false;");
            tab.webview->ExecuteScript(setVar.c_str(), nullptr);
        }

        // ── Anti-bot / Cloudflare bypass: Full Chrome fingerprint spoofing ──────
        // Cloudflare, YouTube, Google সব সাইটে real Chrome-এর মতো দেখাবে
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            // 1. webdriver flag সম্পূর্ণভাবে লুকানো
            L"(function(){"
            L"'use strict';"

            // webdriver
            L"const _wdOD={get:()=>false,configurable:true};"
            L"try{Object.defineProperty(navigator,'webdriver',_wdOD);}catch(e){}"

            // 2. window.chrome — পুরোপুরি real Chrome-এর মতো
            // FIX: Gemini/ChatGPT/Facebook detect fake extension context via chrome.runtime.id
            // → for those sites skip injecting chrome.runtime entirely so the native
            //   WebView2 chrome stub stays untouched and their JS doesn't crash.
            // For all other sites we inject a full no-op shim with safe addListener
            // objects (addListener now returns {remove:fn} so callers can chain safely).
            L"(function(){"
            L"  var h=location.hostname;"
            L"  var isSpecial=(h.indexOf('gemini.google.com')!==-1||"
            L"                 h.indexOf('chatgpt.com')!==-1||"
            L"                 h.indexOf('chat.openai.com')!==-1||"
            L"                 h.indexOf('facebook.com')!==-1||"
            L"                 h.indexOf('messenger.com')!==-1);"
            L"  if(isSpecial) return;" // ← skip shim for these sites
            // safe no-op listener factory — addListener returns {remove:fn}
            L"  function _noopl(){return {addListener:function(){return{remove:function(){}};},removeListener:function(){},hasListener:function(){return false;},hasListeners:function(){return false;}};"
            L"  }"
            // port returned by connect()
            L"  function _port(){return{postMessage:function(){},disconnect:function(){},onDisconnect:_noopl(),onMessage:_noopl()};}"
            L"  if(!window.chrome||!window.chrome.runtime){"
            L"    window.chrome={"
            L"      runtime:{"
            L"        id:undefined,"
            L"        connect:_port,"
            L"        sendMessage:function(){},"
            L"        onMessage:_noopl(),"
            L"        onConnect:_noopl(),"
            L"        onInstalled:_noopl(),"
            L"        onStartup:_noopl(),"
            L"        onSuspend:_noopl(),"
            L"        onUpdateAvailable:_noopl(),"
            L"        getManifest:function(){return{manifest_version:3,name:'',version:'1.0'};},"
            L"        getURL:function(p){return'chrome-extension://invalid/'+p;},"
            L"        lastError:null"
            L"      },"
            L"      loadTimes:function(){return{firstPaintTime:performance.now()/1000+0.1,firstPaintAfterLoadTime:0,requestTime:Date.now()/1000,startLoadTime:Date.now()/1000,commitLoadTime:Date.now()/1000,finishDocumentLoadTime:0,finishLoadTime:0,navigationType:'Other',wasFetchedViaSpdy:true,wasNpnNegotiated:true,npnNegotiatedProtocol:'h2',wasAlternateProtocolAvailable:false,connectionInfo:'h2'};},"
            L"      csi:function(){return{startE:Date.now(),onloadT:Date.now(),pageT:1.5,tran:15};},"
            L"      app:{isInstalled:false,InstallState:{DISABLED:'disabled',INSTALLED:'installed',NOT_INSTALLED:'not_installed'},RunningState:{CANNOT_RUN:'cannot_run',READY_TO_RUN:'ready_to_run',RUNNING:'running'}},"
            L"      webstore:{},"
            L"      cast:{},"
            L"      i18n:{getMessage:function(){return'';},getUILanguage:function(){return'en-US';},detectLanguage:function(t,cb){if(cb)cb({isReliable:false,languages:[]});}},"
            L"      storage:{local:{get:function(k,cb){if(cb)cb({});},set:function(o,cb){if(cb)cb();},remove:function(k,cb){if(cb)cb();}}}"
            L"    };"
            L"  }"
            L"})();"

            // 3. plugins — Chrome PDF + NaCl
            L"try{Object.defineProperty(navigator,'plugins',{get:function(){"
            L"var a=Object.create(PluginArray.prototype);"
            L"var p0=Object.create(Plugin.prototype);"
            L"Object.defineProperty(p0,'name',{get:()=>'Chrome PDF Plugin'});"
            L"Object.defineProperty(p0,'filename',{get:()=>'internal-pdf-viewer'});"
            L"Object.defineProperty(p0,'description',{get:()=>'Portable Document Format'});"
            L"Object.defineProperty(p0,'length',{get:()=>1});"
            L"var p1=Object.create(Plugin.prototype);"
            L"Object.defineProperty(p1,'name',{get:()=>'Chrome PDF Viewer'});"
            L"Object.defineProperty(p1,'filename',{get:()=>'mhjfbmdgcfjbbpaeojofohoefgiehjai'});"
            L"Object.defineProperty(p1,'description',{get:()=>''});"
            L"Object.defineProperty(p1,'length',{get:()=>1});"
            L"var p2=Object.create(Plugin.prototype);"
            L"Object.defineProperty(p2,'name',{get:()=>'Native Client'});"
            L"Object.defineProperty(p2,'filename',{get:()=>'internal-nacl-plugin'});"
            L"Object.defineProperty(p2,'description',{get:()=>''});"
            L"Object.defineProperty(p2,'length',{get:()=>0});"
            L"Object.defineProperty(a,'0',{get:()=>p0});Object.defineProperty(a,'1',{get:()=>p1});Object.defineProperty(a,'2',{get:()=>p2});"
            L"Object.defineProperty(a,'length',{get:()=>3});"
            L"a.item=function(i){return[p0,p1,p2][i]||null;};"
            L"a.namedItem=function(n){return{'Chrome PDF Plugin':p0,'Chrome PDF Viewer':p1,'Native Client':p2}[n]||null;};"
            L"a.refresh=function(){};"
            L"return a;},configurable:true});}catch(e){}"

            // 4. mimeTypes
            L"try{Object.defineProperty(navigator,'mimeTypes',{get:function(){"
            L"var m=Object.create(MimeTypeArray.prototype);"
            L"Object.defineProperty(m,'length',{get:()=>2});"
            L"return m;},configurable:true});}catch(e){}"

            // 5. languages
            L"try{Object.defineProperty(navigator,'languages',{get:()=>['en-US','en'],configurable:true});}catch(e){}"

            // 6. hardwareConcurrency (real PC-এর মতো)
            L"try{Object.defineProperty(navigator,'hardwareConcurrency',{get:()=>8,configurable:true});}catch(e){}"

            // 7. deviceMemory
            L"try{Object.defineProperty(navigator,'deviceMemory',{get:()=>8,configurable:true});}catch(e){}"

            // 8. platform
            L"try{Object.defineProperty(navigator,'platform',{get:()=>'Win32',configurable:true});}catch(e){}"

            // 9. vendor
            L"try{Object.defineProperty(navigator,'vendor',{get:()=>'Google Inc.',configurable:true});}catch(e){}"

            // 10. vendorSub, productSub
            L"try{Object.defineProperty(navigator,'vendorSub',{get:()=>'',configurable:true});}catch(e){}"
            L"try{Object.defineProperty(navigator,'productSub',{get:()=>'20030107',configurable:true});}catch(e){}"

            // 11. maxTouchPoints — desktop Chrome = 0
            L"try{Object.defineProperty(navigator,'maxTouchPoints',{get:()=>0,configurable:true});}catch(e){}"

            // 12. Permissions API — Cloudflare এটা দিয়ে পরীক্ষা করে
            L"try{"
            L"const origQuery=window.navigator.permissions&&window.navigator.permissions.query;"
            L"if(origQuery){"
            L"  window.navigator.permissions.query=(parameters)=>{"
            L"    if(parameters.name==='notifications'){"
            L"      return Promise.resolve({state:Notification.permission,onchange:null});"
            L"    }"
            L"    return origQuery(parameters);"
            L"  };"
            L"}"
            L"}catch(e){}"

            // 13. userAgentData — Chrome 137 full
            L"if(!navigator.userAgentData){"
            L"  try{Object.defineProperty(navigator,'userAgentData',{get:()=>({"
            L"    brands:[{brand:'Chromium',version:'137'},{brand:'Google Chrome',version:'137'},{brand:'Not/A)Brand',version:'99'}],"
            L"    mobile:false,"
            L"    platform:'Windows',"
            L"    getHighEntropyValues:function(hints){"
            L"      return Promise.resolve({"
            L"        architecture:'x86',"
            L"        bitness:'64',"
            L"        brands:[{brand:'Chromium',version:'137'},{brand:'Google Chrome',version:'137'},{brand:'Not/A)Brand',version:'99'}],"
            L"        fullVersionList:[{brand:'Chromium',version:'137.0.0.0'},{brand:'Google Chrome',version:'137.0.0.0'},{brand:'Not/A)Brand',version:'99.0.0.0'}],"
            L"        mobile:false,"
            L"        model:'',"
            L"        platform:'Windows',"
            L"        platformVersion:'10.0.0',"
            L"        uaFullVersion:'137.0.0.0'"
            L"      });"
            L"    },"
            L"    toJSON:function(){return{brands:[{brand:'Chromium',version:'137'},{brand:'Google Chrome',version:'137'},{brand:'Not/A)Brand',version:'99'}],mobile:false,platform:'Windows'};}"
            L"  }),configurable:true});}catch(e){}"
            L"}"

            // 14. Canvas fingerprint noise — Cloudflare canvas test bypass
            L"(function(){"
            L"  const origToDataURL=HTMLCanvasElement.prototype.toDataURL;"
            L"  const origGetImageData=CanvasRenderingContext2D.prototype.getImageData;"
            L"  const origToBlob=HTMLCanvasElement.prototype.toBlob;"
            L"  HTMLCanvasElement.prototype.toDataURL=function(){"
            L"    const ctx=this.getContext('2d');"
            L"    if(ctx){"
            L"      const id=ctx.getImageData(0,0,1,1);"
            L"      id.data[0]=(id.data[0]+1)%256;"
            L"      ctx.putImageData(id,0,0);"
            L"    }"
            L"    return origToDataURL.apply(this,arguments);"
            L"  };"
            L"  CanvasRenderingContext2D.prototype.getImageData=function(){"
            L"    const imageData=origGetImageData.apply(this,arguments);"
            L"    if(imageData&&imageData.data&&imageData.data.length>0){"
            L"      imageData.data[0]=(imageData.data[0]+1)%256;"
            L"    }"
            L"    return imageData;"
            L"  };"
            L"})();"

            // 15. WebGL fingerprint — vendor/renderer real Chrome-এর মতো
            L"(function(){"
            L"  const origGetParam=WebGLRenderingContext.prototype.getParameter;"
            L"  WebGLRenderingContext.prototype.getParameter=function(param){"
            L"    if(param===37445)return'Google Inc. (Intel)';"   // VENDOR
            L"    if(param===37446)return'ANGLE (Intel, Intel(R) UHD Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)';" // RENDERER
            L"    return origGetParam.apply(this,arguments);"
            L"  };"
            L"  const origGetParam2=WebGL2RenderingContext.prototype.getParameter;"
            L"  WebGL2RenderingContext.prototype.getParameter=function(param){"
            L"    if(param===37445)return'Google Inc. (Intel)';"
            L"    if(param===37446)return'ANGLE (Intel, Intel(R) UHD Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)';"
            L"    return origGetParam2.apply(this,arguments);"
            L"  };"
            L"})();"

            // 16. screen properties — real desktop
            L"try{Object.defineProperty(screen,'colorDepth',{get:()=>24,configurable:true});}catch(e){}"
            L"try{Object.defineProperty(screen,'pixelDepth',{get:()=>24,configurable:true});}catch(e){}"

            // 17. connection (network info)
            L"try{if(!navigator.connection){"
            L"  Object.defineProperty(navigator,'connection',{get:()=>({effectiveType:'4g',rtt:50,downlink:10,saveData:false}),configurable:true});"
            L"}}catch(e){}"

            // 18. Cloudflare-specific: __cf_chl_opt guard & turnstile helper
            L"if(typeof window.__cf_chl_opt==='undefined'){window.__cf_chl_opt={};}"

            L"})();",
            nullptr);

        // NavigationStarting — block bad content + enforce desktop URLs
        tab.webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
                LPWSTR uri=nullptr; args->get_Uri(&uri);
                if (uri) {
                    std::wstring urlStr(uri);
                    CoTaskMemFree(uri);

                    // ── 1. Adult/blocked content check ──────────────────────
                    if (IsBlockedContent(urlStr)) {
                        args->put_Cancel(TRUE);
                        if (g_windows.count(m_hWnd)) {
                            auto& w=g_windows[m_hWnd];
                            if (w.hAddressBar) SetWindowTextW(w.hAddressBar,L"blocked by rasfocus");
                            if (m_tabIdx>=0&&m_tabIdx<(int)w.tabs.size()) {
                                w.tabs[m_tabIdx].url=L"blocked by rasfocus";
                                w.tabs[m_tabIdx].webview->NavigateToString(GetBlocked_HTML(w.isDarkMode).c_str());
                            }
                        }
                        return S_OK;
                    }

                    // ── 1b. Ad / Tracker domain block (network-level, like uBlock Origin) ──
                    // Main-frame navigation to known ad/tracker domains is cancelled silently.
                    // Sub-resource requests (scripts, images, XHR) are handled separately in
                    // WebResourceRequested. Together they cover all request types.
                    if (!IsTrackerWhitelisted(urlStr) &&
                        (IsAdDomain(urlStr) || IsTrackerDomain(urlStr))) {
                        args->put_Cancel(TRUE);
                        return S_OK;
                    }

                    // ── 2. Force desktop version for mobile URLs ─────────────
                    // YouTube: m.youtube.com → www.youtube.com
                    {
                        std::wstring lower = urlStr;
                        std::transform(lower.begin(),lower.end(),lower.begin(),::towlower);

                        std::wstring desktopUrl;

                        // m.youtube.com → www.youtube.com
                        if (lower.find(L"://m.youtube.com") != std::wstring::npos) {
                            desktopUrl = urlStr;
                            size_t pos = desktopUrl.find(L"://m.youtube.com");
                            desktopUrl.replace(pos, 16, L"://www.youtube.com");
                        }
                        // youtu.be short links → full desktop
                        else if (lower.find(L"youtu.be/") != std::wstring::npos) {
                            size_t pos = lower.find(L"youtu.be/");
                            std::wstring vid = urlStr.substr(pos + 9);
                            // remove query params from vid if any
                            size_t q = vid.find(L'?');
                            std::wstring query = (q != std::wstring::npos) ? vid.substr(q) : L"";
                            if (q != std::wstring::npos) vid = vid.substr(0, q);
                            desktopUrl = L"https://www.youtube.com/watch?v=" + vid + query;
                        }
                        // m.facebook.com → www.facebook.com
                        else if (lower.find(L"://m.facebook.com") != std::wstring::npos) {
                            desktopUrl = urlStr;
                            size_t pos = desktopUrl.find(L"://m.facebook.com");
                            desktopUrl.replace(pos, 17, L"://www.facebook.com");
                        }
                        // mobile.twitter.com or m.twitter.com → twitter.com
                        else if (lower.find(L"://m.twitter.com") != std::wstring::npos) {
                            desktopUrl = urlStr;
                            size_t pos = desktopUrl.find(L"://m.twitter.com");
                            desktopUrl.replace(pos, 16, L"://twitter.com");
                        }
                        else if (lower.find(L"://mobile.twitter.com") != std::wstring::npos) {
                            desktopUrl = urlStr;
                            size_t pos = desktopUrl.find(L"://mobile.twitter.com");
                            desktopUrl.replace(pos, 21, L"://twitter.com");
                        }
                        // m.twitch.tv → www.twitch.tv
                        else if (lower.find(L"://m.twitch.tv") != std::wstring::npos) {
                            desktopUrl = urlStr;
                            size_t pos = desktopUrl.find(L"://m.twitch.tv");
                            desktopUrl.replace(pos, 14, L"://www.twitch.tv");
                        }

                        if (!desktopUrl.empty()) {
                            args->put_Cancel(TRUE);
                            if (g_windows.count(m_hWnd)) {
                                auto& w = g_windows[m_hWnd];
                                if (m_tabIdx>=0 && m_tabIdx<(int)w.tabs.size() && w.tabs[m_tabIdx].webview)
                                    w.tabs[m_tabIdx].webview->Navigate(desktopUrl.c_str());
                            }
                            return S_OK;
                        }
                    }
                }
                return S_OK;
            }).Get(),nullptr);

        // NewWindowRequested — open in new tab instead of popup
        tab.webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{
                LPWSTR uri=nullptr; args->get_Uri(&uri);
                std::wstring newUrl = uri ? std::wstring(uri) : L"LOCAL_NTP";
                if (uri) CoTaskMemFree(uri);
                // Open in new tab
                if (g_windows.count(m_hWnd)) {
                    args->put_Handled(TRUE);
                    AddTab(m_hWnd, newUrl);
                }
                return S_OK;
            }).Get(),nullptr);

        // DocumentTitleChanged
        tab.webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender,IUnknown*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx>=(int)w.tabs.size()) return S_OK;
                LPWSTR dt=nullptr; sender->get_DocumentTitle(&dt);
                if (dt){w.tabs[m_tabIdx].title=dt;CoTaskMemFree(dt);InvalidateRect(m_hWnd,NULL,FALSE);}
                return S_OK;
            }).Get(),nullptr);

        // SourceChanged — update address bar & resize
        tab.webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender,ICoreWebView2SourceChangedEventArgs*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx!=w.activeTab) return S_OK;
                LPWSTR src=nullptr; sender->get_Source(&src);
                if (src) {
                    std::wstring urlStr(src);
                    w.tabs[m_tabIdx].url=urlStr;
                    if (w.hAddressBar) {
                        if (urlStr==L"LOCAL_NTP"||urlStr==L"about:blank"||
                            urlStr.find(L"blocked by rasfocus")!=std::wstring::npos)
                            SetWindowTextW(w.hAddressBar,L"");
                        else SetWindowTextW(w.hAddressBar,src);
                    }
                    CoTaskMemFree(src);
                }
                if (m_tabIdx==w.activeTab && w.tabs[m_tabIdx].controller) {
                    // IMPORTANT: url এখন update হয়েছে, তাই NavTotalH() সঠিক value দেবে।
                    // Bookmark bar hide/show হলে WebView bounds পাল্টায় —
                    // তাই url update এর পরে bounds আবার set করতে হবে।
                    RECT wvr=GetWebViewRect(m_hWnd);
                    w.tabs[m_tabIdx].controller->put_Bounds(wvr);
                    InvalidateRect(m_hWnd,NULL,TRUE);
                }
                return S_OK;
            }).Get(),nullptr);

        // ContentLoading — YouTube early ad block injection (before page scripts run)
        // ytInitialPlayerResponse এবং yt.setConfig() intercept এখানে হয়,
        // তাই silent pre-roll ad গুলো player load হওয়ার আগেই বন্ধ হয়।
        tab.webview->add_ContentLoading(
            Callback<ICoreWebView2ContentLoadingEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2ContentLoadingEventArgs*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                const std::wstring& tabUrl = w.tabs[m_tabIdx].url;
                if (tabUrl.find(L"youtube.com") != std::wstring::npos) {
                    std::wstring earlyScript = GetYouTubeEarlyAdBlockScript();
                    sender->ExecuteScript(earlyScript.c_str(), nullptr);
                }
                return S_OK;
            }).Get(), nullptr);

        // NavigationCompleted — re-apply dark flag + AI filter + bounds
        tab.webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* sender,ICoreWebView2NavigationCompletedEventArgs*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx>=(int)w.tabs.size()) return S_OK;
                // bookmark bar bounds
                if (m_tabIdx==w.activeTab && w.tabs[m_tabIdx].controller) {
                    // NavigationCompleted এ url পড়ে নিই — SourceChanged আসার আগেই
                    // bounds set হওয়া দরকার। url টা এখানেই update করা নিরাপদ।
                    ICoreWebView2* wv = w.tabs[m_tabIdx].webview.Get();
                    if (wv) {
                        LPWSTR curSrc = nullptr;
                        wv->get_Source(&curSrc);
                        if (curSrc) {
                            w.tabs[m_tabIdx].url = std::wstring(curSrc);
                            CoTaskMemFree(curSrc);
                        }
                    }
                    RECT wvr=GetWebViewRect(m_hWnd);
                    w.tabs[m_tabIdx].controller->put_Bounds(wvr);
                    InvalidateRect(m_hWnd,NULL,TRUE);
                }
                // Re-prime __RAS_DARK__ after navigation (window var is reset per page)
                // then re-run the inject/remove logic so persistent dark mode works.
                {
                    std::wstring reapply =
                        std::wstring(L"window.__RAS_DARK__=") +
                        (w.isDarkMode ? L"true;" : L"false;") +
                        L"(function(){"
                        L"  var id='__ras_dark_mode__';"
                        L"  var existing=document.getElementById(id);"
                        L"  if(window.__RAS_DARK__){"
                        L"    if(existing) return;"
                        L"    var s=document.createElement('style');"
                        L"    s.id=id;"
                        L"    s.textContent='"
                        L"      html{filter:invert(1) hue-rotate(180deg)!important;background:#1e1e1e!important;}"
                        L"      img,video,canvas,picture,svg,iframe{"
                        L"        filter:invert(1) hue-rotate(180deg)!important;"
                        L"      }"
                        L"    ';"
                        L"    (document.head||document.documentElement).appendChild(s);"
                        L"  } else {"
                        L"    if(existing) existing.remove();"
                        L"  }"
                        L"})();";
                    sender->ExecuteScript(reapply.c_str(), nullptr);
                }
                // YouTube Ad Block inject (always, for all YouTube navigations)
                {
                    const std::wstring& tabUrl = w.tabs[m_tabIdx].url;
                    if (tabUrl.find(L"youtube.com") != std::wstring::npos) {
                        std::wstring adScript = GetYouTubeAdBlockScript();
                        sender->ExecuteScript(adScript.c_str(), nullptr);
                    }
                }
                // AI inject
                std::wstring script=GetAiInjectScript(w.tabs[m_tabIdx].url);
                if (!script.empty()) sender->ExecuteScript(script.c_str(),nullptr);
                return S_OK;
            }).Get(),nullptr);

        // HistoryChanged
        tab.webview->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2* sender,IUnknown*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx>=(int)w.tabs.size()) return S_OK;
                BOOL canB=FALSE,canF=FALSE;
                sender->get_CanGoBack(&canB); sender->get_CanGoForward(&canF);
                w.tabs[m_tabIdx].canBack=!!canB; w.tabs[m_tabIdx].canFwd=!!canF;
                InvalidateRect(m_hWnd,NULL,FALSE);
                return S_OK;
            }).Get(),nullptr);

        // Ad + Tracker Block + Anti-bot Header Spoofing (WebResourceRequested)
        tab.webview->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT{
                ComPtr<ICoreWebView2WebResourceRequest> req;
                args->get_Request(&req);
                if (!req) return S_OK;

                // ── Cloudflare/Bot-detection Header Spoofing ──────────────
                // Sec-CH-UA headers: WebView2 এ "Microsoft Edge" থাকে →
                // এটা Chrome 137-এর মতো করে দিতে হবে
                ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                if (SUCCEEDED(req->get_Headers(&headers)) && headers) {
                    // Sec-CH-UA: Chrome 137 brands
                    headers->SetHeader(L"Sec-CH-UA",
                        L"\"Chromium\";v=\"137\", \"Google Chrome\";v=\"137\", \"Not/A)Brand\";v=\"99\"");
                    // Sec-CH-UA-Mobile: desktop = ?0
                    headers->SetHeader(L"Sec-CH-UA-Mobile", L"?0");
                    // Sec-CH-UA-Platform: Windows
                    headers->SetHeader(L"Sec-CH-UA-Platform", L"\"Windows\"");
                    // Sec-CH-UA-Platform-Version
                    headers->SetHeader(L"Sec-CH-UA-Platform-Version", L"\"10.0.0\"");
                    // Sec-CH-UA-Arch
                    headers->SetHeader(L"Sec-CH-UA-Arch", L"\"x86\"");
                    // Sec-CH-UA-Bitness
                    headers->SetHeader(L"Sec-CH-UA-Bitness", L"\"64\"");
                    // Sec-CH-UA-Full-Version-List
                    headers->SetHeader(L"Sec-CH-UA-Full-Version-List",
                        L"\"Chromium\";v=\"137.0.0.0\", \"Google Chrome\";v=\"137.0.0.0\", \"Not/A)Brand\";v=\"99.0.0.0\"");
                    // Sec-Fetch-Site, Mode, User — realistic values
                    // Accept-Language: real Chrome-এর মতো
                    headers->SetHeader(L"Accept-Language", L"en-US,en;q=0.9");
                    // X-Forwarded-For বা অন্য automation header থাকলে সরানো
                    headers->RemoveHeader(L"X-Forwarded-For");
                }

                // ── Ad / Tracker Block ──────────────────────────────────
                LPWSTR uri=nullptr; req->get_Uri(&uri);
                if (uri) {
                    std::wstring url(uri); CoTaskMemFree(uri);
                    std::wstring urlLow = url;
                    std::transform(urlLow.begin(),urlLow.end(),urlLow.begin(),::towlower);

                    bool shouldBlock = false;

                    // 1. Known ad/tracker domains
                    bool whitelisted = IsTrackerWhitelisted(url);
                    if (!whitelisted && (IsAdDomain(url) || IsTrackerDomain(url)))
                        shouldBlock = true;

                    // 2. YouTube ad URL patterns
                    // YouTube serves ads from youtube.com itself, so domain block won't work.
                    // These URL patterns identify ad requests specifically.
                    if (!shouldBlock) {
                        // YouTube ad video request: /videoplayback?...&ctier=L&... or adsid= param
                        bool isYT = (urlLow.find(L"youtube.com") != std::wstring::npos ||
                                     urlLow.find(L"googlevideo.com") != std::wstring::npos ||
                                     urlLow.find(L"ytimg.com") != std::wstring::npos);
                        if (isYT) {
                            // pagead: Google ads on YouTube
                            if (urlLow.find(L"/pagead/") != std::wstring::npos)         shouldBlock = true;
                            // ptracking: ad impression tracking
                            else if (urlLow.find(L"ptracking") != std::wstring::npos)    shouldBlock = true;
                            // YouTube ad video streams: adsid= in videoplayback URL
                            else if (urlLow.find(L"adsid=") != std::wstring::npos)       shouldBlock = true;
                            // YouTube ad metrics: /api/stats/ads
                            else if (urlLow.find(L"/api/stats/ads") != std::wstring::npos) shouldBlock = true;
                            // YouTube ad tracking: /api/stats/atr
                            else if (urlLow.find(L"/api/stats/atr") != std::wstring::npos) shouldBlock = true;
                            // YouTube ad impression: /api/stats/qoe and &adformat= param
                            else if (urlLow.find(L"adformat=") != std::wstring::npos)    shouldBlock = true;
                            // YouTube get_midroll_info: mid-roll ad metadata
                            else if (urlLow.find(L"get_midroll_info") != std::wstring::npos) shouldBlock = true;
                            // YouTube ad survey
                            else if (urlLow.find(L"ad_survey") != std::wstring::npos)    shouldBlock = true;
                            // googlevideo.com with ctier=L — ad video stream flag
                            else if (urlLow.find(L"googlevideo.com") != std::wstring::npos &&
                                     urlLow.find(L"ctier=l") != std::wstring::npos)       shouldBlock = true;
                        }
                    }

                    // 3. Generic ad URL patterns across all sites
                    if (!shouldBlock && !whitelisted) {
                        if (urlLow.find(L"/ads/") != std::wstring::npos &&
                            urlLow.find(L"loads") == std::wstring::npos)                 shouldBlock = true;
                        else if (urlLow.find(L"/adserve") != std::wstring::npos)         shouldBlock = true;
                        else if (urlLow.find(L"/adserver") != std::wstring::npos)        shouldBlock = true;
                        else if (urlLow.find(L"googlesyndication.com") != std::wstring::npos) shouldBlock = true;
                        else if (urlLow.find(L"doubleclick.net") != std::wstring::npos)  shouldBlock = true;
                    }

                    if (shouldBlock) {
                        // Return empty 204 No Content — safest way to silently block
                        // (200 with empty body sometimes causes JS parse errors)
                        if (g_sharedEnv) {
                            ComPtr<ICoreWebView2WebResourceResponse> resp;
                            g_sharedEnv->CreateWebResourceResponse(
                                nullptr, 204, L"No Content", L"",
                                &resp);
                            if (resp) args->put_Response(resp.Get());
                        }
                    }
                }
                return S_OK;
            }).Get(), nullptr);

        // Register WebResourceRequested filter — all URIs
        tab.webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

        // WebMessage — NTP search + block overlay bridge
        tab.webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
                LPWSTR msg=nullptr; args->TryGetWebMessageAsString(&msg);
                if (msg) {
                    std::wstring str(msg); CoTaskMemFree(msg);
                    if (!str.empty() && g_windows.count(m_hWnd)) {
                        auto& w=g_windows[m_hWnd];
                        if (m_tabIdx<(int)w.tabs.size()&&w.tabs[m_tabIdx].webview) {
                            if (str == L"BLOCK_CLOSE") {
                                // Navigate to blank/NTP after block
                                w.tabs[m_tabIdx].url = L"LOCAL_NTP";
                                w.tabs[m_tabIdx].webview->NavigateToString(
                                    GetLocalNTP_HTML(w.isDarkMode).c_str());
                            } else {
                                // NTP search navigation
                                w.tabs[m_tabIdx].webview->Navigate(str.c_str());
                            }
                        }
                    }
                }
                return S_OK;
            }).Get(),nullptr);

        // ── YouTube Ad Block + Desktop Force: AddScriptToExecuteOnDocumentCreated ──
        // Fires before page renders — earliest possible injection point.
        // Early script first: ytInitialPlayerResponse/yt.setConfig intercept করে silent pre-roll বন্ধ করে
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            GetYouTubeEarlyAdBlockScript().c_str(),
            nullptr);
        // Main ad block script: DOM/skip/fetch intercept
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            GetYouTubeAdBlockScript().c_str(),
            nullptr);

        // ── YouTube Desktop View Force ────────────────────────────────────────
        // YouTube তিনটা জিনিস দেখে mobile/desktop decide করে:
        // 1. User-Agent string (WebResourceRequested এ already fixed)
        // 2. Sec-CH-UA-Mobile header (WebResourceRequested এ already fixed)
        // 3. PREF cookie এবং window.innerWidth — এটা JS দিয়ে fix করতে হবে
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            L"(function(){"
            L"  if(location.hostname.indexOf('youtube.com')===-1)return;"
            // PREF cookie: f6=400 মানে desktop layout force
            L"  var c=document.cookie;"
            L"  if(c.indexOf('PREF=')===-1||c.indexOf('f6=400')===-1){"
            L"    document.cookie='PREF=f6=400;domain=.youtube.com;path=/;max-age=31536000';"
            L"  }"
            // window.outerWidth spoof — YouTube checks this for responsive layout
            // যদি 768 এর নিচে হয় তাহলে mobile layout দেয়
            L"  try{"
            L"    Object.defineProperty(window,'outerWidth',{get:()=>1280,configurable:true});"
            L"    Object.defineProperty(window,'innerWidth',{get:()=>1280,configurable:true});"
            L"    Object.defineProperty(screen,'width',{get:()=>1920,configurable:true});"
            L"    Object.defineProperty(screen,'availWidth',{get:()=>1920,configurable:true});"
            L"  }catch(e){}"
            // navigator.userAgent ensure desktop
            L"  try{"
            L"    Object.defineProperty(navigator,'userAgent',{get:()=>"
            L"      'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36',"
            L"      configurable:true"
            L"    });"
            L"  }catch(e){}"
            // maxTouchPoints=0 → desktop (mobile devices have >0)
            L"  try{Object.defineProperty(navigator,'maxTouchPoints',{get:()=>0,configurable:true});}catch(e){}"
            L"})();",
            nullptr);

        // F11 accelerator
        ComPtr<ICoreWebView2Controller3> ctl3;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl3)))) {
            EventRegistrationToken tok;
            ctl3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd),&tok);
        }

        bool isActive=(m_tabIdx==wd.activeTab);
        ctl->put_IsVisible(isActive?TRUE:FALSE);
        RECT wvr=GetWebViewRect(m_hWnd);
        ctl->put_Bounds(wvr);

        // ── NAVIGATE ─────────────────────────────────────────────────────────
        // FIX: Always show Local NTP on startup, never navigate to google.com
        std::wstring nav=m_startUrl;

        // Treat empty/default values as LOCAL_NTP
        if (!g_isPureViewerMode &&
            (nav.empty() || nav==L"LOCAL_NTP" || nav==L"RAS_BROWSER" ||
             nav==L"about:blank" || nav==L"about:newtab"))
        {
            nav=L"LOCAL_NTP";
        }

        if (nav==L"LOCAL_NTP") {
            tab.url=L"LOCAL_NTP";
            tab.webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
        } else {
            tab.webview->Navigate(nav.c_str());
        }

        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ENVIRONMENT HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    HWND m_hWnd; int m_tabIdx; ULONG m_ref=1;
public:
    EnvCompletedHandler(HWND h,int i):m_hWnd(h),m_tabIdx(i){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** ppv) override{*ppv=this;return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&m_ref);}
    ULONG STDMETHODCALLTYPE Release() override{ULONG r=InterlockedDecrement(&m_ref);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,ICoreWebView2Environment* env) override {
        if (FAILED(hr)||!env) return S_OK;
        g_sharedEnv=env;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd=g_windows[m_hWnd];
        auto& tab=wd.tabs[m_tabIdx];
        g_sharedEnv->CreateCoreWebView2Controller(m_hWnd,new TabControllerHandler(m_hWnd,m_tabIdx,tab.url));
        return S_OK;
    }
};

static void CreateWebViewForTab(HWND hWnd, int tabIdx) {
    if (!g_windows.count(hWnd)) return;
    auto& wd=g_windows[hWnd];
    auto& tab=wd.tabs[tabIdx];

    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(hWnd,new TabControllerHandler(hWnd,tabIdx,tab.url));
    } else {
        auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(
            // enable-features: একটাই flag এ সব merge — দুটো আলাদা দিলে Chrome দ্বিতীয়টা ignore করে
            L"--enable-features=msWebView2EnableExtensions,CookiesWithoutSameSiteMustBeSecure "
            L"--enable-gpu-rasterization "
            L"--enable-zero-copy "
            L"--disable-features=Translate "
            // Cloudflare + anti-bot: automation flag সম্পূর্ণ বন্ধ
            L"--disable-blink-features=AutomationControlled "
            L"--no-proxy-server "
            // Chrome desktop-এর মতো দেখানোর জন্য
            L"--lang=en-US "
            L"--no-first-run "
            L"--no-default-browser-check "
            L"--force-color-profile=srgb "
            // YouTube সহ সব সাইটে desktop view force করতে
            L"--disable-mobile-layout "
            // Cloudflare TLS fingerprint-এর জন্য: QUIC/H3 সক্রিয়
            L"--enable-quic "
            L"--quic-version=h3 "
            // GPU দিয়ে Canvas/WebGL render করলে fingerprint real-এর মতো হয়
            L"--enable-webgl "
            L"--use-angle=d3d11 "
            // Automation ইনফো header বন্ধ
            L"--disable-infobars "
            L"--exclude-switches=enable-automation "
            // Real Chrome-এর মতো user-data isolation
            L"--disable-background-timer-throttling "
            L"--disable-renderer-backgrounding "
            L"--disable-backgrounding-occluded-windows "
        );

        // User data dir in LocalAppData (required for Gemini login persistence)
        wchar_t appDataPath[MAX_PATH];
        SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,0,appDataPath);
        std::wstring udDir=std::wstring(appDataPath)+L"\\RasBrowserData";
        CreateDirectoryW(udDir.c_str(),NULL);

        HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(
            nullptr,udDir.c_str(),options.Get(),new EnvCompletedHandler(hWnd,tabIdx));
        if (FAILED(hr))
            CreateCoreWebView2EnvironmentWithOptions(nullptr,nullptr,nullptr,new EnvCompletedHandler(hWnd,tabIdx));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DWM SHADOW
// ─────────────────────────────────────────────────────────────────────────────
static void ApplyDwmShadow(HWND hWnd) {
    MARGINS m={0,0,0,1}; DwmExtendFrameIntoClientArea(hWnd,&m);
    DWORD pref=DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd,DWMWA_WINDOW_CORNER_PREFERENCE,&pref,sizeof(pref));
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW PROCEDURE
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_NCCALCSIZE:
        if (wParam==TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        LRESULT def=DefWindowProcW(hWnd,msg,wParam,lParam);
        if (def==HTNOWHERE||def==HTCLIENT) {
            POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
            ScreenToClient(hWnd,&pt);
            RECT cr; GetClientRect(hWnd,&cr);
            UINT dpi=GetWndDpi(hWnd); int border=S(8,dpi);
            if (!g_windows.count(hWnd)||!g_windows[hWnd].isFullScreen) {
                if (pt.y<border&&pt.x<border)                         return HTTOPLEFT;
                if (pt.y<border&&pt.x>=cr.right-border)               return HTTOPRIGHT;
                if (pt.y>=cr.bottom-border&&pt.x<border)              return HTBOTTOMLEFT;
                if (pt.y>=cr.bottom-border&&pt.x>=cr.right-border)    return HTBOTTOMRIGHT;
                if (pt.y<border)            return HTTOP;
                if (pt.y>=cr.bottom-border) return HTBOTTOM;
                if (pt.x<border)            return HTLEFT;
                if (pt.x>=cr.right-border)  return HTRIGHT;
                if (g_isPureViewerMode) {
                    int wx=cr.right-WinBtnW(dpi)*5;
                    if (pt.y<TitleBarH(dpi)) return pt.x>=wx?HTCLIENT:HTCAPTION;
                    return HTCLIENT;
                }
                if (pt.y<TitleBarH(dpi)) {
                    int wx=cr.right-WinBtnW(dpi)*5;
                    if (pt.x>=wx) return HTCLIENT;
                    auto& wd=g_windows[hWnd]; int tc=(int)wd.tabs.size();
                    for(int i=0;i<tc;i++){RECT tr=GetTabRect(cr.right,i,tc,dpi);if(pt.x>=tr.left&&pt.x<tr.right)return HTCLIENT;}
                    if (pt.x<LogoW(dpi)) return HTCLIENT;
                    RECT ntr=GetNewTabBtnRect(cr.right,tc,dpi);
                    if (pt.x>=ntr.left&&pt.x<=ntr.right) return HTCLIENT;
                    return HTCAPTION;
                }
                if (pt.y<NavTotalH(hWnd)) return HTCLIENT;
            }
            return HTCLIENT;
        }
        return def;
    }

    case WM_NCLBUTTONDBLCLK:
        if (wParam==HTCAPTION){ShowWindow(hWnd,IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE);return 0;}
        break;

    case WM_CREATE: ApplyDwmShadow(hWnd); break;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hWnd,&ps);
        DrawBrowser(hWnd,hdc); EndPaint(hWnd,&ps); return 0;
    }

    case WM_ERASEBKGND: return 1;

    case WM_WINDOWPOSCHANGING: {
        auto* wp=(WINDOWPOS*)lParam; wp->flags|=SWP_NOCOPYBITS; break;
    }

    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd)&&(HWND)lParam==g_windows[hWnd].hAddressBar) {
            HDC he=(HDC)wParam; bool isDark=g_windows[hWnd].isDarkMode;
            if (isDark){SetTextColor(he,RGB(255,255,255));SetBkColor(he,RGB(26,26,26));static HBRUSH hBrD=CreateSolidBrush(RGB(26,26,26));return(LRESULT)hBrD;}
            else{SetTextColor(he,RGB(32,33,36));SetBkColor(he,RGB(241,243,244));static HBRUSH hBrL=CreateSolidBrush(RGB(241,243,244));return(LRESULT)hBrL;}
        }
        break;
    }

    case WM_SIZE: {
        if (!g_windows.count(hWnd)) break;
        auto& wd=g_windows[hWnd];
        RepositionAddressBar(hWnd);
        RECT wvr=GetWebViewRect(hWnd);
        for (int i=0;i<(int)wd.tabs.size();i++)
            if (wd.tabs[i].controller&&i==wd.activeTab)
                wd.tabs[i].controller->put_Bounds(wvr);
        InvalidateRect(hWnd,NULL,FALSE); break;
    }

    case WM_DPICHANGED: {
        const RECT* nr=(const RECT*)lParam;
        SetWindowPos(hWnd,NULL,nr->left,nr->top,nr->right-nr->left,nr->bottom-nr->top,SWP_NOZORDER|SWP_NOACTIVATE);
        RepositionAddressBar(hWnd);
        RECT wvr=GetWebViewRect(hWnd);
        if (g_windows.count(hWnd)) for (auto& t:g_windows[hWnd].tabs) if(t.controller)t.controller->put_Bounds(wvr);
        InvalidateRect(hWnd,NULL,TRUE); return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd)||g_windows[hWnd].isFullScreen) break;
        auto& wd=g_windows[hWnd]; UINT dpi=GetWndDpi(hWnd);
        int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd,&cr); int W=cr.right; bool dirty=false;
        {TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hWnd,0};TrackMouseEvent(&tme);}
        int titleH=TitleBarH(dpi),navH=NavTotalH(hWnd),winBtnW2=WinBtnW(dpi),toolY=titleH;
        {
            int bx=W-winBtnW2*5;
            bool p=(y<titleH&&x>=bx&&x<bx+winBtnW2);
            bool dk=(y<titleH&&x>=bx+winBtnW2&&x<bx+winBtnW2*2);
            bool nm=(y<titleH&&x>=bx+winBtnW2*2&&x<bx+winBtnW2*3);
            bool mx=(y<titleH&&x>=bx+winBtnW2*3&&x<bx+winBtnW2*4);
            bool cl=(y<titleH&&x>=bx+winBtnW2*4);
            if (wd.hPin!=p||wd.hDark!=dk||wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl)
                {wd.hPin=p;wd.hDark=dk;wd.hMin=nm;wd.hMax=mx;wd.hClose=cl;dirty=true;}
        }
        if (!g_isPureViewerMode) {
            int tc=(int)wd.tabs.size(); int prev=wd.hoverTabIndex; wd.hoverTabIndex=-1;
            for(int i=0;i<tc;i++){RECT tr=GetTabRect(W,i,tc,dpi);if(x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom){wd.hoverTabIndex=i;break;}}
            if(prev!=wd.hoverTabIndex)dirty=true;
            RECT ntr=GetNewTabBtnRect(W,(int)wd.tabs.size(),dpi);
            bool nt=(x>=ntr.left&&x<ntr.right&&y>=ntr.top&&y<ntr.bottom);
            if(wd.hNewTab!=nt){wd.hNewTab=nt;dirty=true;}
            int btnStep=S(36,dpi),cx=S(8,dpi);
            bool b=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi));cx+=btnStep;
            bool f=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi));cx+=btnStep;
            bool rl=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi));
            if(wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl){wd.hBack=b;wd.hFwd=f;wd.hRel=rl;dirty=true;}
            int rx=W-S(36*3+8,dpi);
            bool pr=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi));rx+=btnStep;
            bool e=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi));rx+=btnStep;
            bool m=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi));
            if(wd.hProfile!=pr||wd.hExt!=e||wd.hMenu!=m){wd.hProfile=pr;wd.hExt=e;wd.hMenu=m;dirty=true;}
        }
        if(dirty){RECT r={0,0,W,navH};InvalidateRect(hWnd,&r,FALSE);}
        break;
    }

    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd=g_windows[hWnd];
            wd.hMin=wd.hMax=wd.hClose=false;
            wd.hBack=wd.hFwd=wd.hRel=false;
            wd.hPin=wd.hDark=wd.hProfile=wd.hExt=wd.hMenu=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            RECT cr; GetClientRect(hWnd,&cr); cr.bottom=NavTotalH(hWnd);
            InvalidateRect(hWnd,&cr,FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd)||g_windows[hWnd].isFullScreen) break;
        auto& wd=g_windows[hWnd]; UINT dpi=GetWndDpi(hWnd);
        int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd,&cr); int W=cr.right;

        if (wd.hMin)  {ShowWindow(hWnd,SW_MINIMIZE);break;}
        if (wd.hMax)  {ShowWindow(hWnd,IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE);break;}
        if (wd.hClose){DestroyWindow(hWnd);break;}
        if (wd.hPin)  {wd.isPinned=!wd.isPinned;SetWindowPos(hWnd,wd.isPinned?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);InvalidateRect(hWnd,NULL,TRUE);break;}
        if (wd.hDark) {
            // Toggle via shared ApplyTheme so button + menu stay in sync
            ApplyTheme(hWnd, !wd.isDarkMode);
            break;
        }

        if (!g_isPureViewerMode) {
            int tc=(int)wd.tabs.size();
            for (int i=0;i<tc;i++){
                RECT tr=GetTabRect(W,i,tc,dpi);
                if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom){
                    if (x>=tr.right-S(26,dpi)){CloseTab(hWnd,i);return 0;}
                    SwitchToTab(hWnd,i);return 0;
                }
            }
            // New-tab button: use hover flag (no modal loop issues here)
            if (wd.hNewTab){AddTab(hWnd,L"LOCAL_NTP");break;}

            // Back/Forward/Reload: use hover flags (no modal loop issues)
            if (auto* tab=wd.active()) {
                if (wd.hBack&&tab->webview&&tab->canBack) tab->webview->GoBack();
                if (wd.hFwd &&tab->webview&&tab->canFwd)  tab->webview->GoForward();
                if (wd.hRel &&tab->webview)               tab->webview->Reload();
            }

            // FIX: Three-dot / Profile / Extensions menus — do NOT rely on hover flags here.
            // TrackPopupMenu runs a modal message loop that generates WM_MOUSELEAVE while
            // the menu is open, which resets hMenu/hProfile/hExt to false. On the next click
            // (after menu closes) the flags are still false → ShowMainMenu never fires.
            // Instead, re-compute hit from the raw click coordinates every time.
            {
                int toolY2 = TitleBarH(dpi);
                int btnStep2 = S(36,dpi);
                int rx2 = W - S(36*3+8, dpi);
                bool clickProfile = (y>=toolY2&&y<toolY2+ToolbarH(dpi)&&x>=rx2&&x<rx2+S(34,dpi)); rx2+=btnStep2;
                bool clickExt     = (y>=toolY2&&y<toolY2+ToolbarH(dpi)&&x>=rx2&&x<rx2+S(34,dpi)); rx2+=btnStep2;
                bool clickMenu    = (y>=toolY2&&y<toolY2+ToolbarH(dpi)&&x>=rx2&&x<rx2+S(34,dpi));
                if (clickProfile) { ShowProfileMenu(hWnd); break; }
                if (clickExt)     { ShowExtensionsMenu(hWnd); break; }
                if (clickMenu)    { ShowMainMenu(hWnd); break; }
            }
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)||g_isPureViewerMode) break;
        UINT dpi=GetWndDpi(hWnd);
        int y=GET_Y_LPARAM(lParam),x=GET_X_LPARAM(lParam);
        if (y<TitleBarH(dpi)&&x>LogoW(dpi)) AddTab(hWnd,L"LOCAL_NTP");
        break;
    }

    case WM_GETMINMAXINFO: {
        UINT dpi=GetWndDpi(hWnd); auto* mm=(LPMINMAXINFO)lParam;
        mm->ptMinTrackSize.x=S(640,dpi); mm->ptMinTrackSize.y=S(480,dpi);
        HMONITOR hMon=MonitorFromWindow(hWnd,MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi={sizeof(mi)};
        if (GetMonitorInfo(hMon,&mi)) {
            mm->ptMaxPosition.x=mi.rcWork.left-mi.rcMonitor.left;
            mm->ptMaxPosition.y=mi.rcWork.top-mi.rcMonitor.top;
            mm->ptMaxSize.x=mi.rcWork.right-mi.rcWork.left;
            mm->ptMaxSize.y=mi.rcWork.bottom-mi.rcWork.top-2;
        }
        return 0;
    }

    case WM_CLOSE: DestroyWindow(hWnd); break;

    case WM_DESTROY: {
        if (g_windows.count(hWnd)) {
            for (auto& t:g_windows[hWnd].tabs) if(t.controller)t.controller->Close();
            if (g_windows[hWnd].hAddrFont) DeleteObject(g_windows[hWnd].hAddrFont);
            g_windows.erase(hWnd);
        }
        if (g_isPureViewerMode&&g_windows.empty()) PostQuitMessage(0);
        break;
    }

    default: return DefWindowProcW(hWnd,msg,wParam,lParam);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API — LaunchMiniBrowser()
// ─────────────────────────────────────────────────────────────────────────────
void LaunchMiniBrowser(std::wstring url, std::wstring /*title*/) {
    static ULONG_PTR gdiplusToken=0;
    if (!gdiplusToken){GdiplusStartupInput si;GdiplusStartup(&gdiplusToken,&si,nullptr);}

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Desktop shortcut: DISABLED
    // CreateDesktopShortcut();
    RegisterAppForDefaultBrowser();

    static bool registered=false;
    if (!registered){
        WNDCLASSEXW wc={};
        wc.cbSize=sizeof(wc); wc.lpfnWndProc=ViewerWndProc;
        wc.hInstance=GetModuleHandle(NULL); wc.lpszClassName=L"RasBrowserWnd";
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.style=CS_DBLCLKS|CS_HREDRAW|CS_VREDRAW;
        wc.hbrBackground=CreateSolidBrush(RGB(30,30,30)); // dark bg before WebView2 loads
        RegisterClassExW(&wc); registered=true;
    }

    HWND hWnd=CreateWindowExW(0,L"RasBrowserWnd",L"RasBrowser",
        WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MAXIMIZEBOX|WS_MINIMIZEBOX|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        CW_USEDEFAULT,CW_USEDEFAULT,1100,780,NULL,NULL,GetModuleHandle(NULL),NULL);
    if (!hWnd) return;

    SetWindowLongW(hWnd,GWL_STYLE,GetWindowLongW(hWnd,GWL_STYLE)&~WS_CAPTION);
    ApplyDwmShadow(hWnd);
    auto& wd=g_windows[hWnd];

    HWND hEdit=CreateWindowExW(0,L"EDIT",L"",
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_LEFT,
        0,0,100,26,hWnd,(HMENU)IDC_ADDRESS_BAR,GetModuleHandle(NULL),NULL);
    SetWindowLongW(hEdit,GWL_STYLE,GetWindowLongW(hEdit,GWL_STYLE)&~WS_BORDER);
    SetWindowTheme(hEdit,L"",L"");
    SetWindowSubclass(hEdit,AddrBarProc,1,0);
    wd.hAddressBar=hEdit;

    HICON hIco=LoadIcon(GetModuleHandle(NULL),MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco){SendMessage(hWnd,WM_SETICON,ICON_BIG,(LPARAM)hIco);SendMessage(hWnd,WM_SETICON,ICON_SMALL,(LPARAM)hIco);}

    // FIX: Always start with LOCAL_NTP, never google.com
    TabData firstTab;
    if (!g_isPureViewerMode &&
        (url.empty()||url==L"LOCAL_NTP"||url==L"RAS_BROWSER"||
         url==L"about:blank"||url==L"about:newtab"))
    {
        url=L"LOCAL_NTP";
    }
    firstTab.url=url; firstTab.title=L"New Tab";
    wd.tabs.push_back(firstTab); wd.activeTab=0;

    ShowWindow(hWnd,SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);
    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd,0);
}
// ─────────────────────────────────────────────────────────────────────────────
// END OF FILE
// ─────────────────────────────────────────────────────────────────────────────
