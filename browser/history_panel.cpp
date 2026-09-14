// history_panel.cpp — RasBrowser History Panel (Full Implementation)
#define _CRT_SECURE_NO_WARNINGS
#include "history_panel.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace Gdiplus;

std::vector<HistoryItem> g_history;
bool g_historyPanelOpen   = false;
int  g_historyHoverIdx    = -1;
int  g_historyScrollOffset = 0;

static std::wstring GetHistoryFilePath() {
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path);
    return std::wstring(path) + L"\\RasBrowserData\\rasbrowser_history.txt";
}

void LoadHistory() {
    g_history.clear();
    std::wifstream in(GetHistoryFilePath());
    if (!in.is_open()) return;
    std::wstring line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        HistoryItem item;
        size_t closeBracket = line.find(L']');
        size_t pipePos      = line.rfind(L'|');
        if (closeBracket != std::wstring::npos && pipePos != std::wstring::npos) {
            item.timestamp = line.substr(1, closeBracket - 1); // strip [ ]
            item.title     = line.substr(closeBracket + 2, pipePos - closeBracket - 3);
            item.url       = line.substr(pipePos + 2);
            // trim whitespace
            while (!item.title.empty() && item.title.back() == L' ') item.title.pop_back();
            while (!item.url.empty()   && item.url.back()   == L' ') item.url.pop_back();
            g_history.insert(g_history.begin(), item);
        }
    }
    // deduplicate by url (keep newest)
    std::vector<HistoryItem> deduped;
    for (auto& h : g_history) {
        bool found = false;
        for (auto& d : deduped) if (d.url == h.url) { found = true; break; }
        if (!found) deduped.push_back(h);
    }
    g_history = deduped;
    g_historyScrollOffset = 0;
}

void ClearHistory() {
    g_history.clear();
    g_historyScrollOffset = 0;
    std::wofstream out(GetHistoryFilePath(), std::ios::trunc);
    out.close();
}

// ── Layout helpers ─────────────────────────────────────────────────────────────
static int S(int px, int dpi) { return MulDiv(px, dpi, 96); }

void DrawHistoryPanel(
    Graphics& g,
    int W, int H,
    int titleBarH, int toolbarH,
    bool dark, int dpi,
    int mx, int my
) {
    if (!g_historyPanelOpen) return;

    int panelY = titleBarH + toolbarH;
    int panelH = H - panelY;

    // ── Background ──────────────────────────────────────────────────────────
    Color cBg    = dark ? Color(255,26,26,29)   : Color(255,250,250,250);
    Color cHdr   = dark ? Color(255,35,36,39)   : Color(255,255,255,255);
    Color cBrd   = dark ? Color(255,55,56,60)   : Color(255,225,225,225);
    Color cTxt   = dark ? Color(255,230,232,235) : Color(255,30,30,30);
    Color cDim   = dark ? Color(255,140,142,145) : Color(255,110,110,110);
    Color cHov   = dark ? Color(255,45,46,50)   : Color(255,240,242,255);
    Color cBlue  = Color(255,26,115,232);
    Color cRed   = Color(255,220,50,50);

    SolidBrush bgBr(cBg);
    g.FillRectangle(&bgBr, 0, panelY, W, panelH);

    // ── Header ──────────────────────────────────────────────────────────────
    int hdrH = S(56, dpi);
    SolidBrush hdrBr(cHdr);
    g.FillRectangle(&hdrBr, 0, panelY, W, hdrH);
    Pen hdrLine(cBrd, 1.f);
    g.DrawLine(&hdrLine, 0, panelY + hdrH, W, panelY + hdrH);

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, (REAL)S(18,dpi), FontStyleBold,    UnitPixel);
    Font fNorm (&ff, (REAL)S(13,dpi), FontStyleRegular, UnitPixel);
    Font fSmall(&ff, (REAL)S(11,dpi), FontStyleRegular, UnitPixel);
    Font fBtn  (&ff, (REAL)S(12,dpi), FontStyleRegular, UnitPixel);

    StringFormat sfL, sfC, sfR;
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfR.SetAlignment(StringAlignmentFar);    sfR.SetLineAlignment(StringAlignmentCenter);
    sfL.SetTrimming(StringTrimmingEllipsisCharacter);
    sfL.SetFormatFlags(StringFormatFlagsNoWrap);

    SolidBrush brTxt(cTxt), brDim(cDim), brBlue(cBlue), brRed(cRed);

    // Title
    g.DrawString(L"History", -1, &fTitle,
        RectF((float)S(20,dpi), (float)panelY, 200.f, (float)hdrH), &sfL, &brTxt);

    // "Clear all" button (top-right)
    int clearW = S(80,dpi), clearH = S(28,dpi);
    int clearX = W - clearW - S(16,dpi);
    int clearY = panelY + (hdrH - clearH)/2;
    Pen clrPen(cBrd, 1.f);
    g.DrawRectangle(&clrPen, clearX, clearY, clearW, clearH);
    g.DrawString(L"Clear all", -1, &fBtn,
        RectF((float)clearX, (float)clearY, (float)clearW, (float)clearH), &sfC, &brRed);

    // ── Items ───────────────────────────────────────────────────────────────
    if (g_history.empty()) {
        g.DrawString(L"No history yet.", -1, &fNorm,
            RectF((float)S(20,dpi), (float)(panelY+hdrH+S(24,dpi)), (float)W, (float)S(30,dpi)),
            &sfL, &brDim);
        return;
    }

    int itemH = S(52, dpi);
    int listY = panelY + hdrH;
    int visN  = (panelH - hdrH) / itemH + 1;

    for (int i = g_historyScrollOffset; i < (int)g_history.size() && i < g_historyScrollOffset + visN; i++) {
        auto& h = g_history[i];
        int iy = listY + (i - g_historyScrollOffset) * itemH;

        // hover
        bool hov = (mx >= 0 && mx < W - S(40,dpi) && my >= iy && my < iy + itemH);
        if (i == g_historyHoverIdx || hov) {
            SolidBrush hb(cHov);
            g.FillRectangle(&hb, 0, iy, W, itemH);
        }

        // divider
        Pen divPen(cBrd, 1.f);
        g.DrawLine(&divPen, S(16,dpi), iy + itemH - 1, W, iy + itemH - 1);

        // title
        g.DrawString(h.title.c_str(), -1, &fNorm,
            RectF((float)S(20,dpi), (float)iy, (float)(W - S(200,dpi)), (float)(itemH/2)),
            &sfL, &brTxt);

        // url + timestamp
        std::wstring sub = h.url + L"  •  " + h.timestamp;
        g.DrawString(sub.c_str(), -1, &fSmall,
            RectF((float)S(20,dpi), (float)(iy + itemH/2), (float)(W - S(200,dpi)), (float)(itemH/2)),
            &sfL, &brDim);

        // "Open" button
        int btnW = S(50,dpi), btnH = S(24,dpi);
        int btnX = W - btnW - S(48,dpi);
        int btnY = iy + (itemH - btnH)/2;
        Pen btnPen(cBlue, 1.f);
        g.DrawRectangle(&btnPen, btnX, btnY, btnW, btnH);
        g.DrawString(L"Open", -1, &fBtn,
            RectF((float)btnX, (float)btnY, (float)btnW, (float)btnH), &sfC, &brBlue);

        // ✕ delete
        SolidBrush xBr(my >= iy && my < iy + itemH && mx >= W - S(36,dpi) ? Color(255,255,80,80) : cDim);
        g.DrawString(L"\uE711", -1, &fBtn,
            RectF((float)(W - S(36,dpi)), (float)iy, (float)S(36,dpi), (float)itemH), &sfC, &xBr);
    }
}

std::wstring HandleHistoryPanelClick(
    int mx, int my,
    int W, int H,
    int titleBarH, int toolbarH,
    int dpi
) {
    if (!g_historyPanelOpen) return L"";

    int panelY = titleBarH + toolbarH;
    int hdrH   = S(56, dpi);

    // "Clear all" button
    int clearW = S(80,dpi), clearH = S(28,dpi);
    int clearX = W - clearW - S(16,dpi);
    int clearY = panelY + (hdrH - clearH)/2;
    if (mx >= clearX && mx <= clearX+clearW && my >= clearY && my <= clearY+clearH) {
        ClearHistory();
        return L"__cleared__";
    }

    if (my < panelY + hdrH) return L"";

    int itemH = S(52, dpi);
    int listY = panelY + hdrH;
    int rel   = my - listY;
    if (rel < 0) return L"";
    int idx = rel / itemH + g_historyScrollOffset;
    if (idx < 0 || idx >= (int)g_history.size()) return L"";

    int iy = listY + (idx - g_historyScrollOffset) * itemH;

    // ✕ delete
    if (mx >= W - S(36,dpi)) {
        g_history.erase(g_history.begin() + idx);
        // rewrite file
        std::wofstream out(GetHistoryFilePath(), std::ios::trunc);
        // write in original (oldest-first) order
        for (int i = (int)g_history.size()-1; i >= 0; i--)
            out << L"[" << g_history[i].timestamp << L"] "
                << g_history[i].title << L" | " << g_history[i].url << L"\n";
        return L"";
    }

    // "Open" button
    int btnW = S(50,dpi), btnH = S(24,dpi);
    int btnX = W - btnW - S(48,dpi);
    int btnY = iy + (itemH - btnH)/2;
    if (mx >= btnX && mx <= btnX+btnW && my >= btnY && my <= btnY+btnH)
        return g_history[idx].url;

    // anywhere on row → navigate
    g_historyPanelOpen = false;
    return g_history[idx].url;
}

void HandleHistoryScroll(int delta) {
    g_historyScrollOffset += delta;
    if (g_historyScrollOffset < 0) g_historyScrollOffset = 0;
    int maxScroll = (int)g_history.size() - 8;
    if (maxScroll < 0) maxScroll = 0;
    if (g_historyScrollOffset > maxScroll) g_historyScrollOffset = maxScroll;
}
