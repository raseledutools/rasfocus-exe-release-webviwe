#pragma once
#include <windows.h>
#include <gdiplus.h>

// --- Device Block Tab Functions ---
void DrawDeviceBlockTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessDeviceBlockMouseMove(float x, float y);
void ProcessDeviceBlockMouseClick(float x, float y);
// --- Aliases used by tab_blocks.cpp ---
void ProcessDeviceBlockKeyPress(wchar_t c);
void ProcessDeviceBlockKeyDown(WPARAM key);
void ProcessDeviceBlockMouseWheel(float x, float y, int delta);
