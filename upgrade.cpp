#define _CRT_SECURE_NO_WARNINGS
#include "upgrade.h"
#include <gdiplus.h>
#include <string>
#include <vector>
#include <windows.h> // for SetCursor

using namespace Gdiplus;
using namespace std;

// গ্লোবাল ভেরিয়েবল যা দিয়ে পপ-আপ কন্ট্রোল করা হবে
bool g_showUpgradePopup = false;

extern int selectedTab; // main.cpp থেকে লগিন ট্যাবে যাওয়ার জন্য
extern string g_loggedInUserUid; // accounts.cpp থেকে ইউজারের UID আনার জন্য

// Hover States
static bool s_hoverClose   = false;
static bool s_hoverUpgrade = false;
static bool s_hoverLogin   = false;

// ── Layout Calculation ──
struct UpgradeLayout {
    float popX, popY, popW, popH;
    float closeX, closeY, closeW;
    float upgBtnX, upgBtnY, upgBtnW, upgBtnH;
    float loginX, loginY, loginW, loginH;
};

static UpgradeLayout GetUpgLayout(int w, int h) {
    UpgradeLayout L = {};
    L.popW = 460.0f;
    L.popH = 480.0f;
    L.popX = (w - L.popW) / 2.0f;
    L.popY = (h - L.popH) / 2.0f;

    L.closeW = 32.0f;
    L.closeX = L.popX + L.popW - L.closeW - 10.0f;
    L.closeY = L.popY + 10.0f;

    L.upgBtnW = 240.0f;
    L.upgBtnH = 46.0f;
    L.upgBtnX = L.popX + (L.popW - L.upgBtnW) / 2.0f;
    L.upgBtnY = L.popY + L.popH - 110.0f;

    L.loginW = 200.0f;
    L.loginH = 20.0f;
    L.loginX = L.popX + (L.popW - L.loginW) / 2.0f;
    L.loginY = L.upgBtnY + L.upgBtnH + 15.0f;

    return L;
}

// ============================================================
//  DRAW POP-UP
// ============================================================
void DrawUpgradePopup(Graphics& g, int w, int h) {
    if (!g_showUpgradePopup) return;

    // ── Dark Overlay ──
    SolidBrush overlay(Color(160, 0, 0, 0));
    g.FillRectangle(&overlay, 0.0f, 0.0f, (float)w, (float)h);

    UpgradeLayout L = GetUpgLayout(w, h);

    // ── Shadow ──
    for (int i = 4; i >= 0; --i) {
        SolidBrush shadowBrush(Color(15 + i * 5, 0, 0, 0));
        GraphicsPath shadowPath;
        float sr = 12.0f, sd = sr * 2.0f;
        float sx = L.popX - i, sy = L.popY + i, sw2 = L.popW + i * 2.0f, sh2 = L.popH;
        shadowPath.AddArc(sx, sy, sd, sd, 180.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy, sd, sd, 270.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy + sh2 - sd, sd, sd, 0.0f, 90.0f);
        shadowPath.AddArc(sx, sy + sh2 - sd, sd, sd, 90.0f, 90.0f);
        shadowPath.CloseFigure();
        g.FillPath(&shadowBrush, &shadowPath);
    }

    // ── Main Card ──
    GraphicsPath cardPath;
    float rc = 12.0f, dc = rc * 2.0f;
    cardPath.AddArc(L.popX, L.popY, dc, dc, 180.0f, 90.0f);
    cardPath.AddArc(L.popX + L.popW - dc, L.popY, dc, dc, 270.0f, 90.0f);
    cardPath.AddArc(L.popX + L.popW - dc, L.popY + L.popH - dc, dc, dc, 0.0f, 90.0f);
    cardPath.AddArc(L.popX, L.popY + L.popH - dc, dc, dc, 90.0f, 90.0f);
    cardPath.CloseFigure();
    SolidBrush cardBg(Color(255, 255, 255, 255));
    g.FillPath(&cardBg, &cardPath);

    // Top Header Banner (Teal)
    GraphicsPath hdrPath;
    hdrPath.AddArc(L.popX, L.popY, dc, dc, 180.0f, 90.0f);
    hdrPath.AddArc(L.popX + L.popW - dc, L.popY, dc, dc, 270.0f, 90.0f);
    hdrPath.AddLine(L.popX + L.popW, L.popY + 120.0f, L.popX, L.popY + 120.0f);
    hdrPath.CloseFigure();
    SolidBrush hdrBg(Color(255, 0, 150, 160)); // Teal Theme
    g.FillPath(&hdrBg, &hdrPath);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush white(Color(255, 255, 255, 255));

    // ── Crown Icon ──
    Font fCrown(&ffIcons, 42, FontStyleRegular, UnitPixel);
    SolidBrush crownColor(Color(255, 255, 215, 0)); // Gold
    g.DrawString(L"\xE77A", -1, &fCrown, RectF(L.popX, L.popY + 20.0f, L.popW, 50.0f), &fmtC, &crownColor);

    // ── Title ──
    Font fTitle(&ff, 20, FontStyleBold, UnitPixel);
    g.DrawString(L"Unlock RasFocus+ Premium", -1, &fTitle, RectF(L.popX, L.popY + 75.0f, L.popW, 30.0f), &fmtC, &white);

    // ── Close Button (X) ──
    Font fClose(&ffIcons, 14, FontStyleBold, UnitPixel);
    SolidBrush closeColor(s_hoverClose ? Color(255, 255, 50, 50) : Color(255, 255, 255, 255));
    g.DrawString(L"\xE8BB", -1, &fClose, RectF(L.closeX, L.closeY, L.closeW, L.closeW), &fmtC, &closeColor);

    // ── Benefits List ──
    float listY = L.popY + 140.0f;
    vector<wstring> benefits = {
        L"Strict Protocols (DNS & Private Mode Block)",
        L"Advanced Device Blocking (USB & Ports)",
        L"Deep Study Mode & Detailed Statistics",
        L"24-Hour Lockdown & Panic Mode",
        L"Cloud Sync & Automatic Backup"
    };

    Font fCheck(&ffIcons, 16, FontStyleBold, UnitPixel);
    Font fBenTxt(&ff, 14, FontStyleRegular, UnitPixel);
    SolidBrush checkColor(Color(255, 0, 150, 160)); // Teal
    SolidBrush dark(Color(255, 60, 60, 60));

    for (size_t i = 0; i < benefits.size(); ++i) {
        g.DrawString(L"\xE73E", -1, &fCheck, RectF(L.popX + 40.0f, listY, 30.0f, 30.0f), &fmtC, &checkColor);
        g.DrawString(benefits[i].c_str(), -1, &fBenTxt, RectF(L.popX + 75.0f, listY, L.popW - 90.0f, 30.0f), &fmtL, &dark);
        listY += 38.0f;
    }

    // ── Upgrade Button ──
    GraphicsPath btnPath;
    float br = 8.0f, bd = br * 2.0f;
    btnPath.AddArc(L.upgBtnX, L.upgBtnY, bd, bd, 180.0f, 90.0f);
    btnPath.AddArc(L.upgBtnX + L.upgBtnW - bd, L.upgBtnY, bd, bd, 270.0f, 90.0f);
    btnPath.AddArc(L.upgBtnX + L.upgBtnW - bd, L.upgBtnY + L.upgBtnH - bd, bd, bd, 0.0f, 90.0f);
    btnPath.AddArc(L.upgBtnX, L.upgBtnY + L.upgBtnH - bd, bd, bd, 90.0f, 90.0f);
    btnPath.CloseFigure();
    
    // Orange Gradient/Solid for Upgrade
    SolidBrush upgBg(s_hoverUpgrade ? Color(255, 255, 120, 0) : Color(255, 243, 156, 18));
    g.FillPath(&upgBg, &btnPath);
    
    Font fUpgBtn(&ff, 16, FontStyleBold, UnitPixel);
    g.DrawString(L"Get Premium Now", -1, &fUpgBtn, RectF(L.upgBtnX, L.upgBtnY, L.upgBtnW, L.upgBtnH), &fmtC, &white);

    // ── Log In Link ──
    Font fLogin(&ff, 12, FontStyleRegular, UnitPixel);
    Font fLoginU(&ff, 12, FontStyleBold, UnitPixel); // Underline ba Bold
    SolidBrush grayText(Color(255, 120, 120, 120));
    SolidBrush linkColor(s_hoverLogin ? Color(255, 0, 100, 110) : Color(255, 0, 150, 160));

    // Measure to center the text "Already have an account? Log In"
    wstring part1 = L"Already have an account? ";
    wstring part2 = L"Log In";
    SizeF sz1, sz2;
    RectF _r;
    StringFormat sf; sf.SetAlignment(StringAlignmentNear);
    g.MeasureString(part1.c_str(), -1, &fLogin, PointF(0,0), &sf, &_r); sz1 = SizeF(_r.Width, _r.Height);
    g.MeasureString(part2.c_str(), -1, &fLoginU, PointF(0,0), &sf, &_r); sz2 = SizeF(_r.Width, _r.Height);

    float totalW = sz1.Width + sz2.Width;
    float startX = L.popX + (L.popW - totalW) / 2.0f;

    g.DrawString(part1.c_str(), -1, &fLogin, RectF(startX, L.loginY, sz1.Width, L.loginH), &fmtL, &grayText);
    g.DrawString(part2.c_str(), -1, &fLoginU, RectF(startX + sz1.Width, L.loginY, sz2.Width, L.loginH), &fmtL, &linkColor);
}

// ============================================================
//  HIT TEST HELPERS
// ============================================================
static bool HitRect(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// ============================================================
//  MOUSE MOVE (Added Hand Cursor logic)
// ============================================================
void ProcessUpgradeMouseMove(float x, float y) {
    if (!g_showUpgradePopup) return;

    // Use current window size approximation (you can pass w, h if needed, assuming global width/height)
    extern int windowWidth, windowHeight;
    extern float g_scaleFactor;
    int w = (int)(windowWidth / g_scaleFactor);
    int h = (int)(windowHeight / g_scaleFactor);
    
    UpgradeLayout L = GetUpgLayout(w, h);

    s_hoverClose   = HitRect(x, y, L.closeX, L.closeY, L.closeW, L.closeW);
    s_hoverUpgrade = HitRect(x, y, L.upgBtnX, L.upgBtnY, L.upgBtnW, L.upgBtnH);
    s_hoverLogin   = HitRect(x, y, L.loginX, L.loginY, L.loginW, L.loginH);

    // Change cursor to hand if hovering over clickable areas
    if (s_hoverClose || s_hoverUpgrade || s_hoverLogin) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
    } else {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
}

// ============================================================
//  MOUSE CLICK (Updated Dynamic Link Logic)
// ============================================================
void ProcessUpgradeMouseClick(float x, float y, HWND hWnd) {
    if (!g_showUpgradePopup) return;

    extern int windowWidth, windowHeight;
    extern float g_scaleFactor;
    int w = (int)(windowWidth / g_scaleFactor);
    int h = (int)(windowHeight / g_scaleFactor);
    
    UpgradeLayout L = GetUpgLayout(w, h);

    if (HitRect(x, y, L.closeX, L.closeY, L.closeW, L.closeW)) {
        g_showUpgradePopup = false;
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }

    if (HitRect(x, y, L.upgBtnX, L.upgBtnY, L.upgBtnW, L.upgBtnH)) {
        // যদি ইউজার লগইন করা না থাকে, তাহলে সরাসরি Sign Up পপ-আপ খুলবে
        if (g_loggedInUserUid.empty()) {
            extern bool  g_showSignupFromUpgrade; // accounts.cpp তে define করা
            g_showSignupFromUpgrade = true;

            selectedTab = 7; // My Account Tab এ যেতে হবে যাতে signup form দেখা যায়
            g_showUpgradePopup = false;

            extern void HideAllWebViews();
            HideAllWebViews();
        }
        // ইউজার লগইন করা থাকলে তার UID সহ চেকআউট লিংক ওপেন করো
        else {
            string urlStr = "https://raseledutools.github.io/checkout.html?uid=" + g_loggedInUserUid;
            wstring wUrl(urlStr.begin(), urlStr.end());
            ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
            g_showUpgradePopup = false;
        }

        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }

    if (HitRect(x, y, L.loginX, L.loginY, L.loginW, L.loginH)) {
        // লগিন ট্যাবে (My Account - Tab 7) নিয়ে যাবে
        selectedTab = 7;
        g_showUpgradePopup = false;
        
        // Hide Webviews if any
        extern void HideAllWebViews();
        HideAllWebViews();
        
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }
}