#ifndef TAB_DEEP_STUDY_H
#define TAB_DEEP_STUDY_H

#include <windows.h>
#include <gdiplus.h>

// Deep Study / Pomodoro ট্যাবের মেইন ড্রয়িং ফাংশন
void DrawDeepStudyTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);

// মাউস ইভেন্ট কন্ট্রোল
void ProcessDeepStudyMouseMove(float x, float y);
void ProcessDeepStudyMouseClick(float x, float y);

// কীবোর্ড ইনপুট কন্ট্রোল (ওয়েবসাইট ও অ্যাপের নাম লেখার জন্য)
void ProcessDeepStudyKeyPress(wchar_t c);
void ProcessDeepStudyKeyDown(WPARAM key);

#endif // TAB_DEEP_STUDY_H