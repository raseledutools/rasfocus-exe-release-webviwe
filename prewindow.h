#ifndef PREWINDOW_H
#define PREWINDOW_H

#include <windows.h>
#include <gdiplus.h>
#include <string>

using namespace Gdiplus;
using namespace std;

// --- States ---
extern int onboardingStep; 
extern bool showDailyMessage;

// --- Logic Functions ---
void CheckDailyMessage(); // Check daily message logic
void DrawPreWindowOverlay(Graphics& g, int w, int h, float scaleFactor);
bool HandlePreWindowMouseMove(float x, float y, int scaledW, int scaledH);
bool HandlePreWindowClick(float x, float y, int& selectedTab);

#endif