#pragma once
#include <windows.h>
#include <gdiplus.h>

extern bool g_showUpgradePopup;

void DrawUpgradePopup(Gdiplus::Graphics& g, int w, int h);
void ProcessUpgradeMouseMove(float x, float y);
void ProcessUpgradeMouseClick(float x, float y, HWND hWnd);