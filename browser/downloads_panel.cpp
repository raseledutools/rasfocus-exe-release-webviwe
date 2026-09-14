// downloads_panel.cpp — RasBrowser Downloads Panel (Full Implementation)
#define _CRT_SECURE_NO_WARNINGS
#include "downloads_panel.h"
#include "advanced_feature.h"
#include <gdiplus.h>
#include <shellapi.h>
#include <algorithm>
#include <sstream>

using namespace Gdiplus;

bool g_downloadsPanelOpen = false;
int  g_downloadsHoverIdx  = -1;

static int S(int px, int dpi) { return MulDiv(px, dpi, 96); }

// format bytes → "1.2 MB" etc.
static std::wstring FmtBytes(INT64 b) {
    wchar_t buf[64];
    if      (b >= 1024LL*1024*1024) swprintf(buf, 64, L"%.1f GB", (double)b/(1024*1024*1024));
    else if (b >= 1024*1024)        swprintf(buf, 64, L"%.1f MB", (double)b/(1024*1024));
    else if (b >= 1024)             swprintf(buf, 64, L"%.1f KB", (double)b/1024);
    else                            swprintf(buf, 64, L"%lld B",  b);
    return buf;
}

void DrawDownloadsPanel(
    Graphics& g,
    int W, int H,
    int titleBarH, int toolbarH,
    bool dark, int dpi,
    int mx, int my
) {
    if (!g_downloadsPanelOpen) return;

    int panelY = titleBarH + toolbarH;
    int panelH = H - panelY;

    Color cBg   = dark ? Color(255,26,26,29)   : Color(255,250,250,250);
    Color cHdr  = dark ? Color(255,35,36,39)   : Color(255,255,255,255);
    Color cBrd  = dark ? Color(255,55,56,60)   : Color(255,225,225,225);
    Color cTxt  = dark ? Color(255,230,232,235) : Color(255,30,30,30);
    Color cDim  = dark ? Color(255,140,142,145) : Color(255,110,110,110);
    Color cHov  = dark ? Color(255,45,46,50)   : Color(255,240,242,255);
    Color cGreen= Color(255,30,180,80);
    Color cBlue = Color(255,26,115,232);
    Color cRed  = Color(255,220,50,50);
    Color cBar  = dark ? Color(255,55,56,62)   : Color(255,220,220,220);

    SolidBrush bgBr(cBg);
    g.FillRectangle(&bgBr, 0, panelY, W, panelH);

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

    SolidBrush brTxt(cTxt), brDim(cDim), brBlue(cBlue), brGreen(cGreen), brRed(cRed);

    g.DrawString(L"Downloads", -1, &fTitle,
        RectF((float)S(20,dpi), (float)panelY, 250.f, (float)hdrH), &sfL, &brTxt);

    // "Clear all" button
    int clearW = S(80,dpi), clearH = S(28,dpi);
    int clearX = W - clearW - S(16,dpi);
    int clearY = panelY + (hdrH - clearH)/2;
    Pen clrPen(cBrd, 1.f);
    g.DrawRectangle(&clrPen, clearX, clearY, clearW, clearH);
    g.DrawString(L"Clear all", -1, &fBtn,
        RectF((float)clearX, (float)clearY, (float)clearW, (float)clearH), &sfC, &brRed);

    if (g_downloads.empty()) {
        g.DrawString(L"No downloads yet.", -1, &fNorm,
            RectF((float)S(20,dpi), (float)(panelY+hdrH+S(24,dpi)), (float)W, (float)S(30,dpi)),
            &sfL, &brDim);
        return;
    }

    int itemH  = S(72, dpi);
    int listY  = panelY + hdrH;
    int barH   = S(4, dpi);
    int visN   = (panelH - hdrH) / itemH + 1;
    int total  = (int)g_downloads.size();

    for (int ri = 0; ri < total && ri < visN; ri++) {
        int i  = total - 1 - ri; // newest first
        auto& dl = g_downloads[i];
        int iy = listY + ri * itemH;

        bool hov = (mx >= 0 && my >= iy && my < iy + itemH);
        if (hov) { SolidBrush hb(cHov); g.FillRectangle(&hb, 0, iy, W, itemH); }

        Pen divPen(cBrd, 1.f);
        g.DrawLine(&divPen, S(16,dpi), iy + itemH - 1, W, iy + itemH - 1);

        // File icon
        SolidBrush iconBr(dl.isCompleted ? cGreen : dl.isInterrupted ? cRed : cBlue);
        g.FillRectangle(&iconBr, S(20,dpi), iy + S(14,dpi), S(32,dpi), S(40,dpi));
        g.DrawString(L"\uE8A5", -1, &fBtn,
            RectF((float)S(20,dpi), (float)(iy+S(14,dpi)), (float)S(32,dpi), (float)S(40,dpi)),
            &sfC, &brTxt);

        int textX = S(64, dpi);
        int textW = W - textX - S(160,dpi);

        // Filename
        g.DrawString(dl.fileName.c_str(), -1, &fNorm,
            RectF((float)textX, (float)(iy+S(10,dpi)), (float)textW, (float)S(20,dpi)),
            &sfL, &brTxt);

        // Progress bar
        int pbX = textX, pbY = iy + S(32,dpi), pbW = textW, pbH2 = barH;
        SolidBrush barBg(cBar);
        g.FillRectangle(&barBg, pbX, pbY, pbW, pbH2);
        if (dl.totalBytes > 0) {
            float pct = (float)dl.receivedBytes / (float)dl.totalBytes;
            if (pct > 1.f) pct = 1.f;
            Color fillC = dl.isCompleted ? cGreen : dl.isInterrupted ? cRed : cBlue;
            SolidBrush fillBr(fillC);
            g.FillRectangle(&fillBr, pbX, pbY, (int)(pbW * pct), pbH2);
        } else if (dl.isDownloading) {
            // indeterminate — fill half
            SolidBrush fillBr(cBlue);
            g.FillRectangle(&fillBr, pbX, pbY, pbW/2, pbH2);
        }

        // Status text
        std::wstring status;
        if (dl.isCompleted)   status = FmtBytes(dl.totalBytes) + L"  •  Done";
        else if (dl.isInterrupted) status = L"Failed";
        else if (dl.totalBytes > 0)
            status = FmtBytes(dl.receivedBytes) + L" / " + FmtBytes(dl.totalBytes);
        else status = L"Downloading…";
        g.DrawString(status.c_str(), -1, &fSmall,
            RectF((float)textX, (float)(iy+S(42,dpi)), (float)textW, (float)S(18,dpi)),
            &sfL, &brDim);

        // Action buttons
        int btnW = S(90,dpi), btnH = S(24,dpi);
        int b1X  = W - btnW*2 - S(36,dpi);
        int b2X  = W - btnW   - S(20,dpi);
        int bY   = iy + (itemH - btnH)/2;

        Pen btnPen(cBrd, 1.f);
        if (dl.isCompleted) {
            g.DrawRectangle(&btnPen, b1X, bY, btnW, btnH);
            g.DrawString(L"Open file", -1, &fBtn,
                RectF((float)b1X,(float)bY,(float)btnW,(float)btnH), &sfC, &brBlue);
            g.DrawRectangle(&btnPen, b2X, bY, btnW, btnH);
            g.DrawString(L"Show folder", -1, &fBtn,
                RectF((float)b2X,(float)bY,(float)btnW,(float)btnH), &sfC, &brDim);
        } else if (dl.isInterrupted) {
            g.DrawRectangle(&btnPen, b2X, bY, btnW, btnH);
            g.DrawString(L"Remove", -1, &fBtn,
                RectF((float)b2X,(float)bY,(float)btnW,(float)btnH), &sfC, &brRed);
        } else {
            g.DrawRectangle(&btnPen, b2X, bY, btnW, btnH);
            g.DrawString(L"Cancel", -1, &fBtn,
                RectF((float)b2X,(float)bY,(float)btnW,(float)btnH), &sfC, &brRed);
        }
    }
}

std::wstring HandleDownloadsPanelClick(
    int mx, int my,
    int W, int H,
    int titleBarH, int toolbarH,
    int dpi
) {
    if (!g_downloadsPanelOpen) return L"";

    int panelY = titleBarH + toolbarH;
    int hdrH   = S(56, dpi);

    // "Clear all"
    int clearW = S(80,dpi), clearH = S(28,dpi);
    int clearX = W - clearW - S(16,dpi);
    int clearY = panelY + (hdrH - clearH)/2;
    if (mx >= clearX && mx <= clearX+clearW && my >= clearY && my <= clearY+clearH)
        return L"clear_all";

    if (my < panelY + hdrH || g_downloads.empty()) return L"";

    int itemH = S(72, dpi);
    int listY = panelY + hdrH;
    int rel   = my - listY;
    if (rel < 0) return L"";
    int ri = rel / itemH;
    int i  = (int)g_downloads.size() - 1 - ri;
    if (i < 0 || i >= (int)g_downloads.size()) return L"";

    auto& dl = g_downloads[i];
    int iy   = listY + ri * itemH;

    int btnW = S(90,dpi), btnH = S(24,dpi);
    int b1X  = W - btnW*2 - S(36,dpi);
    int b2X  = W - btnW   - S(20,dpi);
    int bY   = iy + (itemH - btnH)/2;

    if (dl.isCompleted) {
        if (mx >= b1X && mx <= b1X+btnW && my >= bY && my <= bY+btnH)
            return L"open_file:" + dl.fullPath;
        if (mx >= b2X && mx <= b2X+btnW && my >= bY && my <= bY+btnH) {
            // show folder
            std::wstring folder = dl.fullPath;
            size_t sep = folder.rfind(L'\\');
            if (sep != std::wstring::npos) folder = folder.substr(0, sep);
            return L"open_folder:" + folder;
        }
    } else {
        if (mx >= b2X && mx <= b2X+btnW && my >= bY && my <= bY+btnH) {
            g_downloads.erase(g_downloads.begin() + i);
            return L"";
        }
    }
    return L"";
}

void ClearCompletedDownloads() {
    g_downloads.erase(
        std::remove_if(g_downloads.begin(), g_downloads.end(),
            [](const DownloadInfo& d){ return d.isCompleted || d.isInterrupted; }),
        g_downloads.end());
}
