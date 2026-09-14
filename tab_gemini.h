#ifndef TAB_GEMINI_H
#define TAB_GEMINI_H

#include <windows.h>
#include <gdiplus.h>
#include <string>

// --- Gemini AI Tab Functions ---

// কন্ট্রোলগুলো ইনিশিয়ালাইজ করার জন্য (উইন্ডো হ্যান্ডেল তৈরি)
void InitGeminiControls(HWND parent);

// ট্যাব সুইচ করার সময় কন্ট্রোলগুলো দেখানো বা লুকানোর জন্য
void ShowGeminiControls(bool show);

// উইন্ডো বড়-ছোট করলে বা ট্যাব চেঞ্জ হলে পজিশন ঠিক করার জন্য
void ResizeGeminiControls(int cx, int cy, int cw, int ch);

// জেমিনি ইউআই ড্র করার জন্য (GDI+)
void DrawGeminiTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);

// মাউস হোভার ইফেক্ট হ্যান্ডেল করার জন্য
void ProcessGeminiMouseMove(float x, float y);

// মাউস ক্লিক (বাটন বা থিম চেঞ্জ) হ্যান্ডেল করার জন্য
void ProcessGeminiMouseClick(float x, float y);

// উইন৩২ বাটন কমান্ড প্রসেস করার জন্য
void ProcessGeminiCommand(int id, int code);

// --- Compatibility Aliases (যদি tab_special.cpp এ Diary নামেই কল করতে চান) ---
// তবে ভালো হয় tab_special.cpp এ নামগুলো বদলে Gemini করে দেওয়া।
#define InitDiaryControls InitGeminiControls
#define ShowDiaryControls ShowGeminiControls
#define ResizeDiaryControls ResizeGeminiControls
#define DrawDiaryTab DrawGeminiTab
#define ProcessDiaryMouseMove ProcessGeminiMouseMove
#define ProcessDiaryMouseClick ProcessGeminiMouseClick
#define ProcessDiaryCommand ProcessGeminiCommand

#endif // TAB_GEMINI_H