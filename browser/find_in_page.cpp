// find_in_page.cpp — RasBrowser Find-in-Page Bar
// TODO: এখানে সব function implement করো

#include "find_in_page.h"
#include <gdiplus.h>

using namespace Gdiplus;

bool         g_findBarOpen      = false;
std::wstring g_findQuery        = L"";
int          g_findMatchCount   = 0;
int          g_findCurrentMatch = 0;

void OpenFindBar() {
    g_findBarOpen = true;
    g_findQuery   = L"";
    // TODO: focus find bar input এ দাও (HWND edit control বা custom draw)
}

void CloseFindBar() {
    g_findBarOpen   = false;
    g_findQuery     = L"";
    g_findMatchCount = 0;
    // TODO: WebView2 এ highlight clear করো
    // webview->ExecuteScript(L"window.getSelection().removeAllRanges()", nullptr);
}

void FindBarAddChar(wchar_t ch) {
    // TODO: g_findQuery তে character append করো, তারপর ExecuteFind() call করো
    g_findQuery += ch;
}

void FindBarBackspace() {
    // TODO: g_findQuery থেকে শেষ character সরাও, ExecuteFind() call করো
    if (!g_findQuery.empty())
        g_findQuery.pop_back();
}

void ExecuteFind(void* webview, bool forward) {
    if (!webview || g_findQuery.empty()) return;

    // TODO: ICoreWebView2* wv = (ICoreWebView2*)webview;
    // WebView2 Find API (Edge 98+):
    // ComPtr<ICoreWebView2_11> wv11;
    // wv->QueryInterface(IID_PPV_ARGS(&wv11));
    // wv11->ExecuteScript(findScript.c_str(), nullptr);

    // Simple JS fallback (যদি Find API না থাকে):
    // window.find(query, false, !forward, true, false, false, false)
    std::wstring js = L"window.find('" + g_findQuery + L"', false, " +
                      (forward ? L"false" : L"true") + L", true)";
    // wv->ExecuteScript(js.c_str(), nullptr);
    (void)js;
}

void DrawFindBar(Graphics& g, int windowWidth, int windowHeight, bool isDarkMode, int dpi) {
    // TODO: window এর নিচে (bottom) একটা strip আঁকো — Chrome find bar এর মতো
    // Height: ~40px
    // Elements:
    //   [🔍 search text input]  [1/5 matches]  [▲] [▼] [✕]
    // g_findQuery দিয়ে text দেখাও
    // g_findMatchCount / g_findCurrentMatch দিয়ে match count দেখাও

    if (!g_findBarOpen) return;

    int barH  = (int)(40.0f * dpi / 96.0f);
    int barY  = windowHeight - barH;
    int barW  = (int)(400.0f * dpi / 96.0f);
    int barX  = windowWidth - barW - (int)(10.0f * dpi / 96.0f);

    // Background
    Color bgColor = isDarkMode ? Color(255, 41, 42, 45) : Color(255, 255, 255, 255);
    SolidBrush bgBrush(bgColor);
    g.FillRectangle(&bgBrush, barX, barY, barW, barH);

    // Border
    Color borderColor = isDarkMode ? Color(255, 80, 80, 80) : Color(255, 200, 200, 200);
    Pen borderPen(borderColor, 1.0f);
    g.DrawRectangle(&borderPen, barX, barY, barW, barH);

    // TODO: query text, match count, up/down/close buttons আঁকো
    FontFamily ff(L"Segoe UI");
    Font fNorm(&ff, 13, FontStyleRegular, UnitPixel);
    SolidBrush txtBrush(isDarkMode ? Color(255,220,220,220) : Color(255,40,40,40));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);

    std::wstring display = g_findQuery.empty() ? L"Find in page..." : g_findQuery;
    g.DrawString(display.c_str(), -1, &fNorm,
        RectF((float)(barX + 12), (float)barY, (float)(barW - 100), (float)barH),
        &sf, &txtBrush);
}

bool HandleFindBarClick(int mouseX, int mouseY, int windowWidth, int windowHeight, int dpi) {
    if (!g_findBarOpen) return false;

    int barH = (int)(40.0f * dpi / 96.0f);
    int barY = windowHeight - barH;
    int barW = (int)(400.0f * dpi / 96.0f);
    int barX = windowWidth - barW - (int)(10.0f * dpi / 96.0f);

    // TODO: ▲ ▼ বাটনে click হলে ExecuteFind(forward/backward)
    // ✕ বাটনে click হলে CloseFindBar(), return true

    // Close button area (right side)
    int closeX = barX + barW - (int)(30.0f * dpi / 96.0f);
    if (mouseX >= closeX && mouseX <= barX + barW &&
        mouseY >= barY   && mouseY <= barY + barH) {
        CloseFindBar();
        return true;
    }

    return false;
}
