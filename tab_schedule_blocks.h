#ifndef TAB_SCHEDULE_BLOCKS_H
#define TAB_SCHEDULE_BLOCKS_H

#include <windows.h>
#include <objidl.h>   // <--- MinGW এর হাজার হাজার এরর বন্ধ করার ম্যাজিক ফিক্স
#include <gdiplus.h>

void DrawScheduleBlocksTab(Gdiplus::Graphics& g, float x, float y, float w, float h);
void ProcessScheduleBlocksMouseMove(float x, float y);
void ProcessScheduleBlocksMouseClick(float x, float y);
void ProcessScheduleBlocksKeyPress(wchar_t c);
void ProcessScheduleBlocksKeyDown(WPARAM key);
void ProcessScheduleBlocksMouseWheel(float x, float y, int delta);
void TriggerAddNewProfile(); // Dashboard shortcut: সরাসরি Add Profile form খোলা

#endif // TAB_SCHEDULE_BLOCKS_H