#pragma once
#include <windows.h>
#include <gdiplus.h>

void DrawDashboardTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessDashboardMouseMove(float x, float y);
void ProcessDashboardMouseClick(float x, float y, int& selectedTab);
void ProcessDashboardMouseWheel(int delta);
