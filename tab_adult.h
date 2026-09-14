#pragma once
#include <windows.h>
#include <gdiplus.h>

// --- Adult Block Tab Functions ---
void DrawAdultBlockTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessAdultMouseMove(float x, float y);
void ProcessAdultMouseClick(float x, float y);
void ProcessAdultKeyPress(wchar_t c);
void ProcessAdultKeyDown(WPARAM key);
void ProcessAdultMouseWheel(float x, float y, int delta);

// --- Aliases used by tab_blocks.cpp ---
void ProcessAdultBlockMouseMove(float x, float y);
void ProcessAdultBlockMouseClick(float x, float y);
void ProcessAdultBlockKeyPress(wchar_t c);
void ProcessAdultBlockKeyDown(WPARAM key);
void ProcessAdultBlockMouseWheel(float x, float y, int delta);

// --- Schedule Integration API ---
// Schedule Block এই function call করে Adult blocking চালু/বন্ধ করবে।
// enable=true  → Adult focus active করে, hosts+PAC block enforce করে
// enable=false → Adult focus deactivate করে, strict protocols তুলে নেয়
void AdultBlock_ApplyForSchedule(bool enable);
