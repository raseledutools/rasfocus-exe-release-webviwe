#ifndef TAB_BLOCKS_H
#define TAB_BLOCKS_H

#include <windows.h>
#include <gdiplus.h>

void DrawBlocksTab(Gdiplus::Graphics& g, float contentX, float contentY, float contentW, float contentH);
void ProcessBlocksMouseMove(float x, float y);
void ProcessBlocksMouseClick(float x, float y);
void SetBlockSubTab(int subTab); // 0=Simple, 1=Schedule, 2=Adult, 3=Device
void SetBlockSubTabWithAction(int subTab, int action); // action: 0=tab only, 1=Add Profile form, 2=Allow mode

#endif // TAB_BLOCKS_H