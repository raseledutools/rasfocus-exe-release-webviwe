#include "prewindow.h"
#include <fstream>
#include <ctime>
#include <windows.h>
#include <string>
#include <direct.h>   // _mkdir

using namespace std;

// Global Variables
int onboardingStep = 1; 
bool showDailyMessage = false;

// Local Hover States
static bool hNext = false, hBack = false, hSkip = false, hCloseDaily = false;

// --- .rasfocus directory তৈরি করার helper ---
static void EnsureRasFocusDir(const string& appData) {
    string dir = appData + "\\.rasfocus";
    _mkdir(dir.c_str());   // already exists হলে silently ignore করে
}

// --- প্রথমবার রান হচ্ছে কি না চেক করার জন্য ---
void CheckFirstRun() {
    char appData[MAX_PATH];
    GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
    EnsureRasFocusDir(appData);

    string path = string(appData) + "\\.rasfocus\\first_run_done.txt";

    ifstream inFile(path.c_str());
    if (inFile.good()) {
        onboardingStep = 0;
    }
    inFile.close();
}

// --- অনবোর্ডিং শেষ হলে ফাইল সেভ করে রাখার জন্য ---
void SaveFirstRunDone() {
    char appData[MAX_PATH];
    GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
    EnsureRasFocusDir(appData);   // ← directory নিশ্চিত করে তারপর file লেখো

    string path = string(appData) + "\\.rasfocus\\first_run_done.txt";

    ofstream outFile(path.c_str());
    outFile << "done";
    outFile.close();
}

void CheckDailyMessage() {
    char appData[MAX_PATH];
    GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
    EnsureRasFocusDir(appData);   // ← এখানেও directory নিশ্চিত করো

    string path = string(appData) + "\\.rasfocus\\last_run.txt";

    time_t t = time(0);
    struct tm * now = localtime(&t);
    string currentDate = to_string(now->tm_mday) + "-" + to_string(now->tm_mon + 1) + "-" + to_string(now->tm_year + 1900);

    ifstream inFile(path.c_str());
    string lastDate;
    getline(inFile, lastDate);
    inFile.close();

    if (lastDate != currentDate) {
        showDailyMessage = true;
        ofstream outFile(path.c_str());
        outFile << currentDate;
        outFile.close();
    }
}

// হেল্পার ফাংশন: সুন্দর গোল বাটন আঁকার জন্য
void DrawRoundedRect(Graphics& g, Brush* brush, float x, float y, float width, float height, float radius) {
    GraphicsPath path;
    float d = radius * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + width - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + width - d, y + height - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
    g.FillPath(brush, &path);
}

void DrawPreWindowOverlay(Graphics& g, int w, int h, float scaleFactor) {
    // --- Dark Overlay with deeper blur effect ---
    SolidBrush overlayBg(Color(230, 10, 12, 15)); 
    g.FillRectangle(&overlayBg, 0.0f, 0.0f, (float)w, (float)h);

    float cardW = 750.0f, cardH = 480.0f;
    float cardX = ((float)w - cardW) / 2.0f;
    float cardY = ((float)h - cardH) / 2.0f;

    // --- Premium Black Glass Card Path ---
    GraphicsPath cardPath;
    int r = 20; float d = r * 2.0f;
    cardPath.AddArc(cardX, cardY, d, d, 180.0f, 90.0f);
    cardPath.AddArc(cardX + cardW - d, cardY, d, d, 270.0f, 90.0f);
    cardPath.AddArc(cardX + cardW - d, cardY + cardH - d, d, d, 0.0f, 90.0f);
    cardPath.AddArc(cardX, cardY + cardH - d, d, d, 90.0f, 90.0f);
    cardPath.CloseFigure();
    
    // Black Glass Background & Glow Border
    SolidBrush glassBr(Color(245, 20, 22, 28)); // ডার্ক ট্রান্সলুসেন্ট ফিল
    g.FillPath(&glassBr, &cardPath);
    
    Pen glassBorder(Color(100, 12, 168, 176), 1.5f); // চারপাশের চিকন গ্লো বর্ডার
    g.DrawPath(&glassBorder, &cardPath);

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    
    // Text Colors for Dark Theme
    SolidBrush textWhite(Color(255, 250, 250, 250));
    SolidBrush textGray(Color(255, 170, 170, 170));

    if (showDailyMessage) {
        // --- Daily Motivational Message (Dark Theme) ---
        Font fT(&ff, 30, FontStyleBold, UnitPixel);
        Font fM(&ff, 18, FontStyleRegular, UnitPixel);
        g.DrawString(L"Good Morning, Rasel!", -1, &fT, RectF(cardX, cardY + 90.0f, cardW, 40.0f), &fmtC, &textWhite);
        
        g.DrawString(L"\"Your productivity is the result of your commitment to excellence.\"\nFocus on your engineering goals today.", -1, &fM, RectF(cardX + 50.0f, cardY + 160.0f, cardW - 100.0f, 80.0f), &fmtC, &textGray);
        
        // Rounded Close Button
        SolidBrush btnBr(hCloseDaily ? Color(255, 30, 185, 195) : Color(255, 12, 168, 176));
        float btnX = cardX + (cardW - 180.0f) / 2.0f;
        DrawRoundedRect(g, &btnBr, btnX, cardY + 310.0f, 180.0f, 50.0f, 10.0f);
        
        Font fB(&ff, 16, FontStyleBold, UnitPixel);
        SolidBrush wBr(Color(255, 255, 255, 255));
        g.DrawString(L"Start Working", -1, &fB, RectF(btnX, cardY + 310.0f, 180.0f, 50.0f), &fmtC, &wBr);
    } 
    else if (onboardingStep > 0) {
        // --- World-Class Onboarding Highlights ---
        Color themeColor = (onboardingStep == 1) ? Color(255, 12, 168, 176) : 
                           (onboardingStep == 2) ? Color(255, 235, 87, 87) : Color(255, 138, 43, 226);
        SolidBrush activeTheme(themeColor);
        
        FontFamily ffIcons(L"Segoe MDL2 Assets");
        Font fI(&ffIcons, 85, FontStyleRegular, UnitPixel); 
        Font fT(&ff, 32, FontStyleBold, UnitPixel);
        Font fD(&ff, 18, FontStyleRegular, UnitPixel); 
        
        wstring icons[] = { L"", L"\xEA18", L"\xE7F6", L"\xE753" }; 
        wstring titles[] = { L"", L"Absolute Focus", L"Smart Eye Care & Zen Mode", L"Cloud Sync & Gamification" };
        
        wstring descriptions[] = { 
            L"", 
            L"Block distracting apps and websites instantly.\nAdvanced AI keyword monitoring keeps your workspace fully clean.", 
            L"Built-in screen warmth controller and ambient white noise.\nProtect your eyes and mind during long, deep study sessions.", 
            L"Seamlessly sync your custom protocols across all devices.\nBuild clean streaks, earn badges, and track real-time analytics." 
        };
        
        // Drawing Layout
        g.DrawString(icons[onboardingStep].c_str(), -1, &fI, RectF(cardX, cardY + 50.0f, cardW, 100.0f), &fmtC, &activeTheme);
        g.DrawString(titles[onboardingStep].c_str(), -1, &fT, RectF(cardX, cardY + 170.0f, cardW, 40.0f), &fmtC, &textWhite);
        g.DrawString(descriptions[onboardingStep].c_str(), -1, &fD, RectF(cardX + 50.0f, cardY + 230.0f, cardW - 100.0f, 80.0f), &fmtC, &textGray);

        // --- Progress Dots ---
        float dotTotalWidth = (3 * 10.0f) + (2 * 12.0f); 
        float dotStartX = cardX + (cardW - dotTotalWidth) / 2.0f;
        float dotY = cardY + cardH - 50.0f;
        
        for (int i = 1; i <= 3; i++) {
            SolidBrush dotBr((i == onboardingStep) ? themeColor : Color(255, 60, 60, 60));
            g.FillEllipse(&dotBr, dotStartX + (i - 1) * 22.0f, dotY, 10.0f, 10.0f);
        }

        // --- Next / Get Started Button (Rounded) ---
        SolidBrush nextBr(hNext ? Color(255, 30, 185, 195) : themeColor);
        float btnW = 160.0f; float btnH = 48.0f;
        float btnX = cardX + cardW - btnW - 30.0f;
        float btnY = cardY + cardH - btnH - 30.0f;
        
        DrawRoundedRect(g, &nextBr, btnX, btnY, btnW, btnH, 8.0f);
        
        Font fB(&ff, 16, FontStyleBold, UnitPixel);
        SolidBrush wBr(Color(255, 255, 255, 255));
        g.DrawString(onboardingStep == 3 ? L"Get Started" : L"Next", -1, &fB, RectF(btnX, btnY, btnW, btnH), &fmtC, &wBr);

        // --- Skip Button ---
        if (onboardingStep < 3) {
            SolidBrush skipBr(hSkip ? Color(255, 200, 200, 200) : Color(255, 120, 120, 120));
            Font fS(&ff, 16, FontStyleBold, UnitPixel);
            g.DrawString(L"Skip Tour", -1, &fS, RectF(cardX + 30.0f, btnY, 100.0f, btnH), &fmtC, &skipBr);
        }
    }
}

bool HandlePreWindowMouseMove(float x, float y, int scaledW, int scaledH) {
    float cardW = 750.0f, cardH = 480.0f;
    float cardX = ((float)scaledW - cardW) / 2.0f;
    float cardY = ((float)scaledH - cardH) / 2.0f;
    
    // Hover logic for Next Button
    float btnW = 160.0f; float btnH = 48.0f;
    float btnX = cardX + cardW - btnW - 30.0f;
    float btnY = cardY + cardH - btnH - 30.0f;
    hNext = (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH);
    
    // Hover logic for Skip Button
    hSkip = (onboardingStep < 3 && x >= cardX + 30.0f && x <= cardX + 130.0f && y >= btnY && y <= btnY + btnH);
    
    // Hover logic for Daily Message Close Button
    float dailyBtnW = 180.0f; float dailyBtnH = 50.0f;
    float dailyBtnX = cardX + (cardW - dailyBtnW) / 2.0f;
    float dailyBtnY = cardY + 310.0f;
    hCloseDaily = (showDailyMessage && x >= dailyBtnX && x <= dailyBtnX + dailyBtnW && y >= dailyBtnY && y <= dailyBtnY + dailyBtnH);
    
    return true; 
}

bool HandlePreWindowClick(float x, float y, int& selectedTab) {
    if (showDailyMessage && hCloseDaily) { 
        showDailyMessage = false; 
        return true; 
    }
    
    if (onboardingStep > 0) {
        if (hSkip) {
            onboardingStep = 0;
            SaveFirstRunDone();
            return true;
        }
        else if (hNext) {
            if (onboardingStep < 3) {
                onboardingStep++;
            } else {
                onboardingStep = 0;
                SaveFirstRunDone(); 
            }
            return true;
        }
    }
    return false;
}
