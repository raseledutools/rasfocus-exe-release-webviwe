#ifndef TAB_SETTINGS_H
#define TAB_SETTINGS_H

#include <windows.h>
#include <gdiplus.h>

void DrawSettingsTab(Gdiplus::Graphics& g, float contentX, float contentY, float contentW, float contentH);
void ProcessSettingsMouseMove(float x, float y);
void ProcessSettingsMouseClick(float x, float y);

#endif // TAB_SETTINGS_H
