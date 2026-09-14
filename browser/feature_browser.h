#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

// ব্রাউজারের ফিচার লিস্ট ড্র করার ফাংশন
void DrawFeatureBrowser(Gdiplus::Graphics& g, float x, float y, float width);