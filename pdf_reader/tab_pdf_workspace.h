#ifndef TAB_PDF_WORKSPACE_H
#define TAB_PDF_WORKSPACE_H

#include <windows.h>
#include <gdiplus.h>
#include <string>

void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessPdfWorkspaceMouseMove(float x, float y);
void ProcessPdfWorkspaceMouseClick(float x, float y);

// এই ফাংশনটি Edge ব্রাউজারকে ডান পাশের সঠিক জায়গায় বসানোর জন্য ডাইমেনশন দেবে
RECT GetPdfWebViewArea(float cx, float cy, float cw, float ch);

#endif // TAB_PDF_WORKSPACE_H
