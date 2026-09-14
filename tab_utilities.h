#ifndef TAB_UTILITIES_H
#define TAB_UTILITIES_H

#include <windows.h>
#include <gdiplus.h>

void InitUtilities();
void DrawUtilitiesTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessUtilitiesMouseMove(float x, float y);
void ProcessUtilitiesMouseClick(float x, float y);

#endif // TAB_UTILITIES_H