// ============================================================
//  Statistics Tab - GDI+ Graphics & Analytics Visualization
// ============================================================
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>

using namespace Gdiplus;
using namespace std;

// ============================================================
//  Global: Animation progress (defined here, extern'd elsewhere)
// ============================================================
float stat_animProgress = 1.0f;

// ============================================================
//  Helper: RoundRect (GDI+ version — avoids WinGDI name clash)
// ============================================================
static void RoundRect(Graphics& g, Brush* brush, Pen* pen,
                      float x, float y, float w, float h, int radius)
{
    GraphicsPath path;
    float r = (float)radius;
    float d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180, 90);
    path.AddArc(x + w - d, y,         d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d,   0, 90);
    path.AddArc(x,         y + h - d, d, d,  90, 90);
    path.CloseFigure();
    if (brush) g.FillPath(brush, &path);
    if (pen)   g.DrawPath(pen,   &path);
}

// ============================================================
//  Helper: FillCircle (center + radius)
// ============================================================
static void FillCircle(Graphics& g, Brush& brush, float cx, float cy, float r)
{
    g.FillEllipse(&brush, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

// ============================================================
//  ២ This Week Tab - Weekly Trends, Top Time Wasters & Gamification
// ============================================================
void DrawWeeklyAnalytics(Graphics& g, float cx, float cY, float cw, float availH, float r1H, float r2H, float r3H, 
                        Font& fH1, Font& fH2, Font& fBody, Font& fBold, Font& fSm, Font& fMed, 
                        SolidBrush& bCard, SolidBrush& bTextMain, SolidBrush& bTextMuted, Pen& pBrd)
{
    float PAD = 24.0f;
    float r2Y = cY + PAD + r1H + PAD;
    float mcGap = 16.0f;
    float thirdW = (cw - PAD * 2 - mcGap * 2) / 3.0f; // Divided into 3 columns for new feature

    StringFormat fmtTrim; fmtTrim.SetAlignment(StringAlignmentNear); fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    // --- Column 1: Top Time-Wasters (This Week) ---
    RoundRect(g, &bCard, &pBrd, cx + PAD, r2Y, thirdW, r2H, 8);
    g.DrawString(L"Top Time-Wasters", -1, &fH2, RectF(cx + PAD + 20.0f, r2Y + 16.0f, thirdW, 22.0f), nullptr, &bTextMain);

    struct Waster { wstring name; wstring timeStr; float pct; Color c; };
    vector<Waster> wasters = {
        { L"YouTube", L"8h 45m", 85.0f, Color(255, 239, 68, 68) },
        { L"Facebook", L"5h 20m", 55.0f, Color(255, 59, 130, 246) },
        { L"Instagram", L"3h 15m", 35.0f, Color(255, 217, 70, 239) },
        { L"Netflix", L"2h 10m", 25.0f, Color(255, 0, 0, 0) }
    };

    float wRowH = (r2H - 50.0f) / 4.0f;
    float wY0 = r2Y + 50.0f;

    for (int i = 0; i < (int)wasters.size(); i++) {
        float wy = wY0 + i * wRowH;
        g.DrawString(wasters[i].name.c_str(), -1, &fBold, RectF(cx + PAD + 20.0f, wy + 8.0f, thirdW - 100.0f, 18.0f), &fmtTrim, &bTextMain);
        g.DrawString(wasters[i].timeStr.c_str(), -1, &fSm, RectF(cx + PAD + thirdW - 80.0f, wy + 8.0f, 60.0f, 18.0f), &fmtR, &bTextMuted);
        
        SolidBrush bgBr(Color(255, 241, 245, 249));
        RoundRect(g, &bgBr, nullptr, cx + PAD + 20.0f, wy + 32.0f, thirdW - 40.0f, 6.0f, 3);
        SolidBrush fillBr(wasters[i].c);
        float fillW = (thirdW - 40.0f) * (wasters[i].pct / 100.0f) * stat_animProgress;
        if(fillW > 6.0f) RoundRect(g, &fillBr, nullptr, cx + PAD + 20.0f, wy + 32.0f, fillW, 6.0f, 3);
    }

    // --- Column 2: Weekly Focus Rhythm ---
    float midX = cx + PAD + thirdW + mcGap;
    RoundRect(g, &bCard, &pBrd, midX, r2Y, thirdW, r2H, 8);
    g.DrawString(L"Focus Rhythm", -1, &fH2, RectF(midX + 20.0f, r2Y + 16.0f, thirdW, 22.0f), nullptr, &bTextMain);

    vector<wstring> days = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    vector<float> focusHrs = { 4.5f, 6.2f, 5.0f, 7.5f, 6.8f, 2.0f, 3.5f };
    float maxHrs = 8.0f; 
    
    float fRowH = (r2H - 50.0f) / 7.0f;
    float fY0 = r2Y + 45.0f;

    for (int i = 0; i < 7; i++) {
        float fy = fY0 + i * fRowH;
        g.DrawString(days[i].c_str(), -1, &fSm, RectF(midX + 20.0f, fy + 4.0f, 40.0f, 18.0f), nullptr, &bTextMuted);
        
        float barMaxW = thirdW - 90.0f;
        float barW = (focusHrs[i] / maxHrs) * barMaxW * stat_animProgress;
        
        SolidBrush barBg(Color(255, 241, 245, 249));
        RoundRect(g, &barBg, nullptr, midX + 60.0f, fy + 8.0f, barMaxW, 10.0f, 5);
        
        if(barW > 10.0f) {
            RectF gradRect(midX + 60.0f, fy + 8.0f, barW, 10.0f);
            LinearGradientBrush gradBr(gradRect, Color(255, 16, 185, 129), Color(255, 5, 150, 105), LinearGradientModeHorizontal);
            RoundRect(g, &gradBr, nullptr, midX + 60.0f, fy + 8.0f, barW, 10.0f, 5);
        }
    }

    // --- Column 3: Gamification & Achievements (NEW) ---
    float rightX = midX + thirdW + mcGap;
    RoundRect(g, &bCard, &pBrd, rightX, r2Y, thirdW, r2H, 8);
    g.DrawString(L"Recent Achievements", -1, &fH2, RectF(rightX + 20.0f, r2Y + 16.0f, thirdW, 22.0f), nullptr, &bTextMain);

    struct Badge { wstring icon; wstring title; wstring desc; Color bgC; Color textC; };
    vector<Badge> badges = {
        { L"[Trophy]", L"Iron Will", L"7-Day Strict Streak", Color(255, 254, 243, 199), Color(255, 180, 83, 9) }, // Gold
        { L"[Shield]", L"Halal Warrior", L"Blocked 100+ Ads", Color(255, 224, 242, 254), Color(255, 3, 105, 161) }, // Blue
        { L"[Fire]", L"Deep Work Guru", L"4h Unbroken Focus", Color(255, 254, 226, 226), Color(255, 185, 28, 28) } // Red
    };

    float bRowH = (r2H - 50.0f) / 3.0f;
    float bY0 = r2Y + 45.0f;

    for (int i = 0; i < (int)badges.size(); i++) {
        float by = bY0 + i * bRowH;
        
        // Badge Icon Circle
        SolidBrush badgeBg(badges[i].bgC);
        FillCircle(g, badgeBg, rightX + 40.0f, by + bRowH/2.0f, 20.0f);
        
        // Using fMed for Emoji/Icon to make it visible
        SolidBrush iconBr(badges[i].textC);
        g.DrawString(badges[i].icon.c_str(), -1, &fMed, RectF(rightX + 20.0f, by + bRowH/2.0f - 14.0f, 40.0f, 28.0f), &fmtC, &iconBr);
        
        g.DrawString(badges[i].title.c_str(), -1, &fBold, RectF(rightX + 70.0f, by + bRowH*0.2f, thirdW - 90.0f, 20.0f), &fmtTrim, &bTextMain);
        g.DrawString(badges[i].desc.c_str(), -1, &fSm, RectF(rightX + 70.0f, by + bRowH*0.6f, thirdW - 90.0f, 16.0f), &fmtTrim, &bTextMuted);
    }
}

// ============================================================
//  ៣ Monthly Tab - Heatmap & Chronotype Analysis
// ============================================================
void DrawMonthlyAnalytics(Graphics& g, float cx, float cY, float cw, float availH, float r1H, float r2H, float r3H, 
                        Font& fH1, Font& fH2, Font& fBody, Font& fBold, Font& fSm, Font& fMed, 
                        SolidBrush& bCard, SolidBrush& bTextMain, SolidBrush& bTextMuted, Pen& pBrd)
{
    float PAD = 24.0f;
    float r2Y = cY + PAD + r1H + PAD;
    float mcGap = 16.0f;
    float halfW = (cw - PAD * 2 - mcGap) / 2.0f;
    float rightX = cx + PAD + halfW + mcGap;

    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtTrim; fmtTrim.SetAlignment(StringAlignmentNear); fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);

    // --- Left Card: Productivity Heatmap (GitHub Style) ---
    RoundRect(g, &bCard, &pBrd, cx + PAD, r2Y, halfW, r2H, 8);
    g.DrawString(L"Deep Work Heatmap (Last 30 Days)", -1, &fH2, RectF(cx + PAD + 20.0f, r2Y + 16.0f, halfW, 22.0f), nullptr, &bTextMain);

    int cols = 6; 
    int rows = 7; 
    float boxSize = 18.0f;
    float boxGap = 4.0f;
    float gridW = cols * (boxSize + boxGap);
    float startX = cx + PAD + (halfW - gridW) / 2.0f; 
    float startY = r2Y + 60.0f;

    Color hColors[] = {
        Color(255, 241, 245, 249), Color(255, 167, 243, 208), Color(255, 52, 211, 153),  
        Color(255, 16, 185, 129),  Color(255, 4, 120, 87)     
    };

    int simData[42] = {0,1,2,3,4,0,0, 1,2,4,4,3,1,0, 2,3,3,4,4,2,0, 0,1,2,3,4,0,0, 4,4,4,3,2,0,0, 1,2,3,0,0,0,0};

    for(int c = 0; c < cols; c++) {
        for(int r = 0; r < rows; r++) {
            int idx = c * rows + r;
            if (idx >= 30) continue; 
            
            int level = simData[idx];
            SolidBrush boxBr(hColors[level]);
            float bx = startX + c * (boxSize + boxGap);
            float by = startY + r * (boxSize + boxGap);
            
            float popSize = boxSize * stat_animProgress;
            float offset = (boxSize - popSize) / 2.0f;
            if(popSize > 2.0f) {
                RoundRect(g, &boxBr, nullptr, bx + offset, by + offset, popSize, popSize, 4);
            }
        }
    }

    // --- Right Card: Chronotype & Digital Wellbeing (NEW) ---
    RoundRect(g, &bCard, &pBrd, rightX, r2Y, halfW, r2H, 8);
    g.DrawString(L"Peak Productivity & Wellbeing", -1, &fH2, RectF(rightX + 20.0f, r2Y + 16.0f, halfW, 22.0f), nullptr, &bTextMain);

    // Peak Time Analysis (Chronotype)
    float chronoY = r2Y + 50.0f;
    SolidBrush highlightBg(Color(255, 238, 242, 255)); // Light Indigo
    SolidBrush highlightText(Color(255, 67, 56, 202)); // Deep Indigo
    RoundRect(g, &highlightBg, nullptr, rightX + 20.0f, chronoY, halfW - 40.0f, 60.0f, 6);
    
    g.DrawString(L"[Clock] Peak Focus Time: 9:00 AM - 12:00 PM", -1, &fBold, RectF(rightX + 30.0f, chronoY + 10.0f, halfW - 60.0f, 20.0f), &fmtTrim, &highlightText);
    g.DrawString(L"Schedule your hardest tasks during this window.", -1, &fSm, RectF(rightX + 30.0f, chronoY + 32.0f, halfW - 60.0f, 18.0f), &fmtTrim, &bTextMain);

    // Ergonomics & Posture Reminders Stats
    float ergoY = chronoY + 75.0f;
    g.DrawString(L"Ergonomics Check (This Month)", -1, &fBold, RectF(rightX + 20.0f, ergoY, halfW - 40.0f, 20.0f), nullptr, &bTextMain);

    struct ErgoStat { wstring lbl; wstring val; float pct; Color c; };
    vector<ErgoStat> ergos = {
        { L"20-20-20 Rule Followed", L"68%", 68.0f, Color(255, 16, 185, 129) }, // Green
        { L"Posture Breaks Taken", L"42%", 42.0f, Color(255, 245, 158, 11) }   // Amber
    };

    float eRowH = 35.0f;
    for (int i = 0; i < (int)ergos.size(); i++) {
        float ey = ergoY + 25.0f + i * eRowH;
        g.DrawString(ergos[i].lbl.c_str(), -1, &fSm, RectF(rightX + 20.0f, ey, halfW - 100.0f, 18.0f), nullptr, &bTextMuted);
        g.DrawString(ergos[i].val.c_str(), -1, &fBold, RectF(rightX + halfW - 70.0f, ey, 50.0f, 18.0f), &fmtR, &bTextMain);
        
        SolidBrush bgBr(Color(255, 241, 245, 249));
        RoundRect(g, &bgBr, nullptr, rightX + 20.0f, ey + 20.0f, halfW - 40.0f, 4.0f, 2);
        
        SolidBrush fillBr(ergos[i].c);
        float fillW = (halfW - 40.0f) * (ergos[i].pct / 100.0f) * stat_animProgress;
        if(fillW > 4.0f) RoundRect(g, &fillBr, nullptr, rightX + 20.0f, ey + 20.0f, fillW, 4.0f, 2);
    }
}

// ============================================================
//  Tab state
// ============================================================
static int  stat_activeSubTab = 0;   // 0=Today 1=ThisWeek 2=Monthly
static float stat_animTimer   = 0.0f;

// ============================================================
//  DrawStatisticsTab — main entry point called from main.cpp
// ============================================================
void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    // Animate progress (clamp to 1.0)
    stat_animProgress = min(stat_animProgress + 0.04f, 1.0f);

    float PAD  = 24.0f;
    float availH = ch - PAD * 2;

    // Simple row heights — adjust as needed
    float r1H = 80.0f;
    float r3H = 100.0f;
    float r2H = availH - r1H - r3H - PAD * 2;

    // Shared brushes / fonts / pen
    SolidBrush bCard    (Color(255, 255, 255, 255));
    SolidBrush bTextMain(Color(255,  30,  41,  59));
    SolidBrush bTextMuted(Color(255, 100, 116, 139));
    Pen        pBrd     (Color(255, 226, 232, 240), 1.0f);

    Font fH1(L"Segoe UI", 18.0f, FontStyleBold,   UnitPixel);
    Font fH2(L"Segoe UI", 13.0f, FontStyleBold,   UnitPixel);
    Font fBody(L"Segoe UI", 11.0f, FontStyleRegular, UnitPixel);
    Font fBold(L"Segoe UI", 11.0f, FontStyleBold,   UnitPixel);
    Font fSm  (L"Segoe UI",  9.0f, FontStyleRegular, UnitPixel);
    Font fMed (L"Segoe UI", 10.0f, FontStyleBold,   UnitPixel);

    // Sub-tab selector row
    const wchar_t* tabs[] = { L"Today", L"This Week", L"Monthly" };
    float tabW = 110.0f, tabH = 32.0f, tabGap = 8.0f;
    float tabsStartX = cx + PAD;
    for (int i = 0; i < 3; i++) {
        float tx = tabsStartX + i * (tabW + tabGap);
        bool  active = (i == stat_activeSubTab);
        SolidBrush tabBg(active ? Color(255, 99, 102, 241) : Color(255, 241, 245, 249));
        RoundRect(g, &tabBg, nullptr, tx, cy + PAD, tabW, tabH, 6);
        SolidBrush tabTxt(active ? Color(255, 255, 255, 255) : Color(255, 100, 116, 139));
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(tabs[i], -1, &fBold, RectF(tx, cy + PAD, tabW, tabH), &fmt, &tabTxt);
    }

    // Dispatch to sub-tab renderer
    float contentY = cy + PAD + tabH + PAD;
    float contentH = ch - (contentY - cy) - PAD;

    if (stat_activeSubTab == 1)
        DrawWeeklyAnalytics(g, cx, contentY, cw, contentH, r1H, r2H, r3H,
                            fH1, fH2, fBody, fBold, fSm, fMed,
                            bCard, bTextMain, bTextMuted, pBrd);
    else if (stat_activeSubTab == 2)
        DrawMonthlyAnalytics(g, cx, contentY, cw, contentH, r1H, r2H, r3H,
                             fH1, fH2, fBody, fBold, fSm, fMed,
                             bCard, bTextMain, bTextMuted, pBrd);
    else {
        // Today placeholder
        SolidBrush bg(Color(255, 248, 250, 252));
        g.FillRectangle(&bg, RectF(cx + PAD, contentY, cw - PAD * 2, contentH));
        StringFormat fc; fc.SetAlignment(StringAlignmentCenter); fc.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Today's statistics coming soon", -1, &fBody,
                     RectF(cx + PAD, contentY, cw - PAD * 2, contentH), &fc, &bTextMuted);
    }
}

// ============================================================
//  Mouse interaction
// ============================================================
void ProcessStatisticsMouseMove(float mx, float my, float cx, float cy, float cw)
{
    // Reserved for hover effects — extend as needed
    (void)mx; (void)my; (void)cx; (void)cy; (void)cw;
}

void ProcessStatisticsMouseClick(float mx, float my, float cx, float cy, float cw)
{
    // Hit-test the three sub-tabs
    float PAD  = 24.0f;
    float tabW = 110.0f, tabH = 32.0f, tabGap = 8.0f;
    float tabsStartX = cx + PAD;

    for (int i = 0; i < 3; i++) {
        float tx = tabsStartX + i * (tabW + tabGap);
        float ty = cy + PAD;
        if (mx >= tx && mx <= tx + tabW && my >= ty && my <= ty + tabH) {
            stat_activeSubTab  = i;
            stat_animProgress  = 0.0f;   // restart animation on tab switch
            return;
        }
    }
    (void)cw;
}
