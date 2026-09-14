// extensions.h — RasExtensions Engine
// Built-in extensions: uBlock Origin, Dark Reader, Video Speed, Custom JS
// Panel UI drawn via GDI+, scripts injected via AddScriptToExecuteOnDocumentCreated

#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <functional>
#include <WebView2.h>

using namespace Gdiplus;

// ─── Extension Data ───────────────────────────────────────────────────────────
struct RasExtension {
    std::wstring id;
    std::wstring name;
    std::wstring description;
    std::wstring version;
    std::wstring iconEmoji;   // fallback emoji icon
    Color        iconColor;
    bool         enabled;
    bool         builtin;     // true = cannot uninstall
};

// ─── Global State ─────────────────────────────────────────────────────────────
inline bool g_extensionPanelOpen = false;

inline std::vector<RasExtension> g_extensions = {
    { L"ras-ublock",    L"uBlock Origin",    L"Block ads, trackers & malware",       L"1.0", L"\U0001F6E1", Color(255,220,53,69),   true,  true  },
    { L"ras-darkreader",L"Dark Reader",       L"Dark mode for every website",         L"1.0", L"\U0001F319", Color(255,52,58,64),    false, true  },
    { L"ras-vidspeed",  L"Video Speed",       L"Control video playback speed",        L"1.0", L"\u23E9",     Color(255,108,117,125), false, true  },
    { L"ras-customjs",  L"Custom Scripts",    L"Inject your own JS on any site",      L"1.0", L"\u2699",     Color(255,13,110,253),  false, true  },
};

// ─── Scroll state ─────────────────────────────────────────────────────────────
inline int  g_extScrollY   = 0;
inline int  g_extHoverIdx  = -1;

// ─── Save / Load enabled state ────────────────────────────────────────────────
inline void SaveExtensionState() {
    std::wofstream f(L"ras_extensions_state.txt");
    for (auto& e : g_extensions)
        f << e.id << L" " << (e.enabled ? 1 : 0) << L"\n";
}

inline void LoadExtensionState() {
    std::wifstream f(L"ras_extensions_state.txt");
    if (!f.is_open()) return;
    std::wstring id; int en;
    while (f >> id >> en) {
        for (auto& e : g_extensions)
            if (e.id == id) { e.enabled = (en == 1); break; }
    }
}

// ─── ScanExtensionsFolderPublic ───────────────────────────────────────────────
inline void ScanExtensionsFolderPublic() {
    LoadExtensionState();
}

// ─── Script Generators ────────────────────────────────────────────────────────

// uBlock Origin — network-level block list (EasyList subset) injected as fetch/XHR interceptor
inline std::wstring GetUBlockScript() {
    return
    LR"js((() => {
    if (window.__rasUblockInstalled) return;
    window.__rasUblockInstalled = true;

    const BLOCK_PATTERNS = [
        /doubleclick\.net/i, /googlesyndication\.com/i, /adservice\.google\./i,
        /pagead2\.googlesyndication/i, /adnxs\.com/i, /advertising\.com/i,
        /ads\.yahoo\.com/i, /ad\.doubleclick/i, /googleadservices\.com/i,
        /amazon-adsystem\.com/i, /media\.net/i, /outbrain\.com/i,
        /taboola\.com/i, /revcontent\.com/i, /zergnet\.com/i,
        /bidswitch\.net/i, /rubiconproject\.com/i, /openx\.net/i,
        /pubmatic\.com/i, /criteo\.com/i, /moatads\.com/i,
        /scorecardresearch\.com/i, /quantserve\.com/i, /comscore\.com/i,
        /hotjar\.com/i, /mouseflow\.com/i, /fullstory\.com/i,
        /ads\.twitter\.com/i, /ads\.linkedin\.com/i, /ads\.facebook\.com/i,
        /connect\.facebook\.net\/en_US\/fbevents/i,
        /static\.ads-twitter\.com/i, /analytics\.tiktok\.com/i,
        /bat\.bing\.com/i, /mc\.yandex\.ru/i,
        /cdn\.jsdelivr\.net\/npm\/bootstrap@.*\/dist\/js\/bootstrap\.bundle/i,
        /pagead\/js\/adsbygoogle/i, /ads-twitter/i,
        /pop-under/i, /popunder/i, /popup-ad/i,
    ];

    function isBlocked(url) {
        if (!url) return false;
        return BLOCK_PATTERNS.some(p => p.test(url));
    }

    // Intercept fetch
    const origFetch = window.fetch;
    window.fetch = function(input, init) {
        const url = (typeof input === 'string') ? input : (input && input.url) ? input.url : '';
        if (isBlocked(url)) {
            return Promise.resolve(new Response('', { status: 200 }));
        }
        return origFetch.apply(this, arguments);
    };

    // Intercept XMLHttpRequest
    const origOpen = XMLHttpRequest.prototype.open;
    XMLHttpRequest.prototype.open = function(method, url) {
        this._url = url;
        if (isBlocked(url)) {
            this._blocked = true;
            return;
        }
        return origOpen.apply(this, arguments);
    };
    const origSend = XMLHttpRequest.prototype.send;
    XMLHttpRequest.prototype.send = function() {
        if (this._blocked) return;
        return origSend.apply(this, arguments);
    };

    // Remove ad elements from DOM
    const AD_SELECTORS = [
        'iframe[src*="doubleclick"]','iframe[src*="googlesyndication"]',
        'iframe[src*="adnxs"]','iframe[src*="advertising"]',
        '.ad','#ad','.ads','#ads','.advertisement','.adsbygoogle',
        '[class*="advert"i]','[id*="advert"i]',
        '[class*="sponsored"i]','[data-ad]','[data-advertisement]',
        '.dfp-ad','ins.adsbygoogle',
        '[class*="outbrain"i]','[class*="taboola"i]',
    ].join(',');

    function pruneAds() {
        try {
            document.querySelectorAll(AD_SELECTORS).forEach(el => {
                el.style.display = 'none';
                el.remove();
            });
        } catch(e) {}
    }

    pruneAds();
    const mo = new MutationObserver(pruneAds);
    mo.observe(document.documentElement, { childList: true, subtree: true });
})();)js";
}

// Dark Reader — CSS filter inversion for dark mode on all sites
inline std::wstring GetDarkReaderScript() {
    return
    LR"js((() => {
    if (window.__rasDarkReaderInstalled) return;
    window.__rasDarkReaderInstalled = true;

    const SKIP_HOSTS = ['youtube.com','netflix.com','primevideo.com','disneyplus.com'];
    if (SKIP_HOSTS.some(h => location.hostname.includes(h))) return;

    const style = document.createElement('style');
    style.id = '__ras_dark_reader__';
    style.textContent = `
        html {
            filter: invert(1) hue-rotate(180deg) !important;
            background: #1a1a1a !important;
        }
        img, video, iframe, canvas, svg, picture {
            filter: invert(1) hue-rotate(180deg) !important;
        }
        [style*="background-image"] {
            filter: invert(1) hue-rotate(180deg) !important;
        }
    `;
    document.documentElement.appendChild(style);
})();)js";
}

// Video Speed Controller — shows floating speed overlay on any video page
inline std::wstring GetVideoSpeedScript() {
    return
    LR"js((() => {
    if (window.__rasVidSpeedInstalled) return;
    window.__rasVidSpeedInstalled = true;

    function attachController(video) {
        if (video.__rasCtrl) return;
        video.__rasCtrl = true;
        video.playbackRate = parseFloat(localStorage.getItem('__rasSpeed') || '1');

        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position:absolute; top:8px; left:8px; z-index:99999;
            background:rgba(0,0,0,0.7); color:#fff;
            padding:4px 10px; border-radius:6px; font-size:14px;
            font-family:sans-serif; cursor:default; user-select:none;
            transition:opacity .3s; opacity:0;
        `;
        overlay.textContent = video.playbackRate.toFixed(2) + 'x';

        function showOverlay() {
            overlay.style.opacity = '1';
            clearTimeout(overlay._t);
            overlay._t = setTimeout(() => overlay.style.opacity='0', 1500);
        }

        function setSpeed(s) {
            s = Math.max(0.1, Math.min(16, s));
            video.playbackRate = s;
            localStorage.setItem('__rasSpeed', s);
            overlay.textContent = s.toFixed(2) + 'x';
            showOverlay();
        }

        document.addEventListener('keydown', e => {
            if (['INPUT','TEXTAREA'].includes(document.activeElement.tagName)) return;
            if (e.key === 'd') setSpeed(video.playbackRate + 0.25);
            if (e.key === 's') setSpeed(video.playbackRate - 0.25);
            if (e.key === 'r') setSpeed(1.0);
        });

        const wrap = video.parentElement;
        if (wrap) {
            wrap.style.position = wrap.style.position || 'relative';
            wrap.appendChild(overlay);
        }
    }

    document.querySelectorAll('video').forEach(attachController);
    new MutationObserver(() => {
        document.querySelectorAll('video').forEach(attachController);
    }).observe(document.documentElement, { childList: true, subtree: true });
})();)js";
}

// ─── Get combined inject script for a given tab ───────────────────────────────
inline std::wstring GetExtensionInjectScript() {
    std::wstring js = L"";
    for (auto& e : g_extensions) {
        if (!e.enabled) continue;
        if (e.id == L"ras-ublock")     js += GetUBlockScript();
        if (e.id == L"ras-darkreader") js += GetDarkReaderScript();
        if (e.id == L"ras-vidspeed")   js += GetVideoSpeedScript();
    }
    return js;
}

// ─── Toggle extension ─────────────────────────────────────────────────────────
// ─── Toggle extension + Live WebView inject/remove ────────────────────────────
//
// আগের সমস্যা:
//   ToggleExtension() শুধু g_extensions[i].enabled flip করতো এবং state সেভ করতো।
//   কিন্তু AddScriptToExecuteOnDocumentCreated() শুধু নতুন page load এ কাজ করে।
//   ফলে toggle করার পর page reload না করলে পরিবর্তন দেখা যেত না।
//
// নতুন behavior:
//   ToggleExtension(webview, idx) →
//     enable হলে  → সাথে সাথে active page এ ExecuteScript() দিয়ে inject করো
//     disable হলে → সাথে সাথে active page এ cleanup script inject করো (DOM elements remove)
//   এরপর AddScriptToExecuteOnDocumentCreated() re-register করার জন্য
//   caller (mini_browser.cpp) কে signal দেওয়া হয় (return value / flag)।
//
// এই function WebView2 pointer নেয় — nullptr হলে শুধু state toggle (fallback)।
inline bool g_extensionScriptsDirty = false; // true = re-register scripts needed

inline void ToggleExtension(ICoreWebView2* webview, int index) {
    if (index < 0 || index >= (int)g_extensions.size()) return;
    auto& ext = g_extensions[index];
    ext.enabled = !ext.enabled;
    SaveExtensionState();
    g_extensionScriptsDirty = true;  // caller কে জানাও re-register দরকার

    if (!webview) return;  // no live WebView — next page load এ কাজ করবে

    if (ext.enabled) {
        // ── Enable: সাথে সাথে inject করো ─────────────────────────────────────
        std::wstring js;
        if (ext.id == L"ras-ublock")     js = GetUBlockScript();
        if (ext.id == L"ras-darkreader") js = GetDarkReaderScript();
        if (ext.id == L"ras-vidspeed")   js = GetVideoSpeedScript();
        if (!js.empty())
            webview->ExecuteScript(js.c_str(), nullptr);
    } else {
        // ── Disable: DOM cleanup + flag reset (page reload না করেও কাজ করবে) ─
        std::wstring cleanup;
        if (ext.id == L"ras-ublock") {
            // uBlock: fetch/XHR interceptor কে restore করা কঠিন (prototype override)
            // তাই পেজের hidden ad elements আনহাইড করো এবং flag reset করো
            cleanup = LR"js(
                (() => {
                    window.__rasUblockInstalled = false;
                    // Restore fetch/XHR — page reload ছাড়া পুরোপুরি সম্ভব নয়,
                    // কিন্তু DOM এ লুকানো elements দেখাও
                    document.querySelectorAll('[style*="display: none"]').forEach(el => {
                        const cls = (el.className || '') + (el.id || '');
                        if (/ad|sponsor|outbrain|taboola/i.test(cls))
                            el.style.display = '';
                    });
                })();
            )js";
        }
        if (ext.id == L"ras-darkreader") {
            cleanup = LR"js(
                (() => {
                    window.__rasDarkReaderInstalled = false;
                    const s = document.getElementById('__ras_dark_reader__');
                    if (s) s.remove();
                    // html filter reset
                    document.documentElement.style.filter = '';
                    // image/video filter reset
                    document.querySelectorAll('img,video,iframe,canvas,svg,picture').forEach(el => {
                        el.style.filter = '';
                    });
                })();
            )js";
        }
        if (ext.id == L"ras-vidspeed") {
            cleanup = LR"js(
                (() => {
                    window.__rasVidSpeedInstalled = false;
                    // Speed overlay সরাও
                    document.querySelectorAll('[class*="ras"][style*="position:absolute"]').forEach(el => el.remove());
                    // Video speed 1.0 এ রিসেট করো
                    document.querySelectorAll('video').forEach(v => { v.playbackRate = 1.0; });
                })();
            )js";
        }
        if (!cleanup.empty())
            webview->ExecuteScript(cleanup.c_str(), nullptr);
    }
}

// Legacy overload — controller pointer (old call sites এর জন্য backward compatible)
inline void ToggleExtension(ICoreWebView2Controller* ctrl, int index) {
    ICoreWebView2* wv = nullptr;
    if (ctrl) ctrl->get_CoreWebView2(&wv);
    ToggleExtension(wv, index);
    if (wv) wv->Release();
}

inline void UninstallExtension(int index) {
    if (index < 0 || index >= (int)g_extensions.size()) return;
    if (g_extensions[index].builtin) return;
    g_extensions.erase(g_extensions.begin() + index);
    SaveExtensionState();
}

inline void InstallExtensionFromFolder(ICoreWebView2Environment*, ICoreWebView2Controller*, const wchar_t*) {
    // Future: parse manifest.json and add to g_extensions
}

// ─── Draw Extension Panel ─────────────────────────────────────────────────────
inline void DrawExtensionPanel(
    Graphics& g,
    int W, int H,
    int titleBarH, int toolbarH,
    bool dark, int dpi,
    int mouseX, int mouseY)
{
    auto S = [&](int px) { return MulDiv(px, dpi, 96); };

    // Panel dimensions — Chrome extension popup style
    const int PW     = S(320);
    const int PH     = S(480);
    const int PAD    = S(12);
    const int ITEM_H = S(72);
    const int HEADER_H = S(52);

    // WebView ডানদিক থেকে (PW+16)px shrink করা হয় GetWebViewRect এ
    // তাই panel সেই exact area তে আঁকো — WebView overlap হবে না
    int px = W - PW - S(8);
    int py = titleBarH + toolbarH;

    // Full height panel (toolbar নিচ থেকে window নিচ পর্যন্ত)
    const int PH_FULL = H - py;
    // Clamp PH to window height
    int actualPH = (PH_FULL > PH) ? PH_FULL : PH;

    // ── Shadow ──
    {
        SolidBrush shadow(Color(40, 0, 0, 0));
        g.FillRectangle(&shadow, px + S(3), py + S(3), PW, actualPH);
    }

    // ── Panel background ──
    Color bgCol    = dark ? Color(255, 32, 33, 36)   : Color(255, 255, 255, 255);
    Color borderC  = dark ? Color(255, 70, 72, 75)   : Color(255, 218, 220, 224);
    Color headerC  = dark ? Color(255, 41, 42, 45)   : Color(255, 248, 249, 250);
    Color textC    = dark ? Color(255, 232, 234, 237) : Color(255, 32, 33, 36);
    Color subC     = dark ? Color(255, 154, 160, 166) : Color(255, 95, 99, 104);
    Color hoverC   = dark ? Color(255, 50, 52, 55)   : Color(255, 241, 243, 244);
    Color divC     = dark ? Color(255, 55, 57, 60)   : Color(255, 232, 234, 237);
    Color enabledC = Color(255, 26, 115, 232);  // Google blue
    Color pillOffC = dark ? Color(255, 95, 99, 104) : Color(255, 189, 193, 198);

    // Round rect panel (top corners only — bottom extends to window edge)
    GraphicsPath panelPath;
    panelPath.AddRectangle(RectF((float)px,(float)py,(float)PW,(float)actualPH));

    SolidBrush bgBr(bgCol);
    g.FillPath(&bgBr, &panelPath);
    Pen borderPen(borderC, 1.0f);
    g.DrawPath(&borderPen, &panelPath);

    // ── Header ──
    {
        SolidBrush hdrBr(headerC);
        g.FillRectangle(&hdrBr, px, py, PW, HEADER_H);

        // puzzle icon + title
        FontFamily ff(L"Segoe MDL2 Assets");
        Font iconFont(&ff, (REAL)S(18), FontStyleRegular, UnitPixel);
        SolidBrush iconBr(Color(255, 108, 117, 125));
        PointF iconPt((REAL)(px + PAD), (REAL)(py + (HEADER_H - S(20)) / 2));
        g.DrawString(L"\uE9D2", -1, &iconFont, iconPt, &iconBr);

        FontFamily sansFF(L"Segoe UI");
        Font titleFont(&sansFF, (REAL)S(14), FontStyleBold, UnitPixel);
        SolidBrush titleBr(textC);
        PointF titlePt((REAL)(px + PAD + S(28)), (REAL)(py + (HEADER_H - S(16)) / 2));
        g.DrawString(L"Extensions", -1, &titleFont, titlePt, &titleBr);

        // divider
        Pen divPen(divC, 1.0f);
        g.DrawLine(&divPen, px, py + HEADER_H, px + PW, py + HEADER_H);
    }

    // ── Extension list ──
    int listY = py + HEADER_H;
    int listH = actualPH - HEADER_H - S(48); // bottom reserve for "Manage" button
    g_extHoverIdx = -1;

    RectF clipR((REAL)px, (REAL)listY, (REAL)PW, (REAL)listH);
    g.SetClip(clipR);

    for (int i = 0; i < (int)g_extensions.size(); i++) {
        auto& ext = g_extensions[i];
        int iy = listY + i * ITEM_H - g_extScrollY;
        if (iy + ITEM_H < listY || iy > listY + listH) continue;

        // hover detect
        bool hov = (mouseX >= px && mouseX < px + PW &&
                    mouseY >= iy && mouseY < iy + ITEM_H);
        if (hov) g_extHoverIdx = i;

        if (hov) {
            SolidBrush hovBr(hoverC);
            g.FillRectangle(&hovBr, px, iy, PW, ITEM_H);
        }

        // ── Icon circle ──
        int cx = px + PAD + S(20);
        int cy = iy + ITEM_H / 2;
        int cr = S(20);
        Color ic = ext.iconColor;
        Color icLight(80, ic.GetR(), ic.GetG(), ic.GetB());
        SolidBrush circBr(icLight);
        g.FillEllipse(&circBr, cx - cr, cy - cr, cr*2, cr*2);

        // emoji / icon
        FontFamily emojiFF(L"Segoe UI Emoji");
        Font emojiFont(&emojiFF, (REAL)S(18), FontStyleRegular, UnitPixel);
        SolidBrush emojiBr(ic);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        RectF emojiR((REAL)(cx - cr), (REAL)(cy - cr), (REAL)(cr*2), (REAL)(cr*2));
        g.DrawString(ext.iconEmoji.c_str(), -1, &emojiFont, emojiR, &sf, &emojiBr);

        // ── Name + description ──
        int textX = cx + cr + S(12);
        FontFamily sfFF(L"Segoe UI");
        Font nameFont(&sfFF, (REAL)S(13), FontStyleBold, UnitPixel);
        SolidBrush nameBr(textC);
        g.DrawString(ext.name.c_str(), -1, &nameFont,
                     PointF((REAL)textX, (REAL)(iy + S(14))), &nameBr);

        // Clip desc to panel width
        Font descFont(&sfFF, (REAL)S(11), FontStyleRegular, UnitPixel);
        SolidBrush descBr(subC);
        int descMaxW = PW - (textX - px) - S(60);
        RectF descR((REAL)textX, (REAL)(iy + S(32)), (REAL)descMaxW, (REAL)S(30));
        StringFormat descSf;
        descSf.SetTrimming(StringTrimmingEllipsisCharacter);
        descSf.SetFormatFlags(StringFormatFlagsLineLimit);
        g.DrawString(ext.description.c_str(), -1, &descFont, descR, &descSf, &descBr);

        // ── Toggle pill ──
        int pillW = S(40), pillH = S(20);
        int pillX = px + PW - PAD - pillW;
        int pillY2 = iy + (ITEM_H - pillH) / 2;

        Color pillBg = ext.enabled ? enabledC : pillOffC;
        SolidBrush pillBr(pillBg);
        GraphicsPath pillPath;
        int pr2 = pillH / 2;
        pillPath.AddArc(pillX, pillY2, pillH, pillH, 90, 180);
        pillPath.AddArc(pillX + pillW - pillH, pillY2, pillH, pillH, 270, 180);
        pillPath.CloseFigure();
        g.FillPath(&pillBr, &pillPath);

        // thumb
        int thumbX = ext.enabled ? pillX + pillW - pillH + S(3) : pillX + S(3);
        int thumbY = pillY2 + S(3);
        int thumbD = pillH - S(6);
        SolidBrush thumbBr(Color(255,255,255,255));
        g.FillEllipse(&thumbBr, thumbX, thumbY, thumbD, thumbD);

        // divider
        Pen divPen2(divC, 1.0f);
        g.DrawLine(&divPen2, px + PAD, iy + ITEM_H - 1, px + PW - PAD, iy + ITEM_H - 1);
    }

    g.ResetClip();

    // ── Bottom "Get more extensions" button ──
    {
        int btnY = py + actualPH - S(44);
        Pen divPen3(divC, 1.0f);
        g.DrawLine(&divPen3, px, btnY, px + PW, btnY);

        FontFamily sfFF(L"Segoe UI");
        Font btnFont(&sfFF, (REAL)S(12), FontStyleRegular, UnitPixel);
        SolidBrush btnBr(Color(255, 26, 115, 232));
        StringFormat btnSf;
        btnSf.SetAlignment(StringAlignmentCenter);
        btnSf.SetLineAlignment(StringAlignmentCenter);
        RectF btnR((REAL)px, (REAL)btnY, (REAL)PW, (REAL)S(44));
        g.DrawString(L"Manage Extensions", -1, &btnFont, btnR, &btnSf, &btnBr);
    }
}

// ─── Handle Extension Panel Click ─────────────────────────────────────────────
inline std::wstring HandleExtensionPanelClick(
    int x, int y, int W, int H,
    int titleBarH, int toolbarH, int dpi)
{
    auto S = [&](int px) { return MulDiv(px, dpi, 96); };

    const int PW       = S(320);
    const int PH       = S(480);
    const int ITEM_H   = S(72);
    const int HEADER_H = S(52);
    const int PAD      = S(12);

    int px2 = W - PW - S(4);
    int py2 = titleBarH + toolbarH;

    int actualPH2 = H - py2;

    // Outside panel → close
    if (x < px2 || x > px2 + PW || y < py2 || y > py2 + actualPH2)
        return L"close";

    int listY = py2 + HEADER_H;

    // Bottom "Manage" button
    if (y >= py2 + actualPH2 - S(44))
        return L"manage";

    // Item click — check toggle pill area
    for (int i = 0; i < (int)g_extensions.size(); i++) {
        int iy = listY + i * ITEM_H - g_extScrollY;
        if (y < iy || y >= iy + ITEM_H) continue;

        int pillW = S(40), pillH = S(20);
        int pillX = px2 + PW - PAD - pillW;
        int pillY2 = iy + (ITEM_H - pillH) / 2;

        if (x >= pillX && x <= pillX + pillW &&
            y >= pillY2 && y <= pillY2 + pillH)
            return L"toggle:" + std::to_wstring(i);

        return L"inside";  // clicked item area but not on pill — consume the click, do nothing
    }

    return L"";
}

