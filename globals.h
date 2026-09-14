#ifndef GLOBALS_H
#define GLOBALS_H

#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>

using namespace Gdiplus;
using namespace std;

// --- Global Variables ---
extern float g_scaleFactor;
extern int windowWidth;
extern int windowHeight;
extern const int SIDEBAR_WIDTH;
extern const int TITLEBAR_HEIGHT;
extern const int SUBHEADER_HEIGHT;

// --- Shared Colors ---
extern const Color ColTeal;
extern const Color ColWhite;
extern const Color ColBgContent;
extern const Color ColTextDark;
extern const Color ColTextGray;

#endif // GLOBALS_H