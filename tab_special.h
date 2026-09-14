#ifndef TAB_SPECIAL_H
#define TAB_SPECIAL_H

#include <windows.h>
#include <gdiplus.h>

void DrawSpecialFeatureTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessSpecialFeatureMouseMove(float x, float y);
void ProcessSpecialFeatureMouseClick(float x, float y);

#endif // TAB_SPECIAL_H