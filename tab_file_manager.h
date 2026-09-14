#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

void DrawFileManagerTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessFileManagerMouseMove(float x, float y);
void ProcessFileManagerMouseClick(float x, float y, HWND hWnd);
void ProcessFileManagerMouseWheel(float x, float y, int delta);
void NavigateFileManagerTo(const std::wstring& path);   // navigate sidebar/drive clicks
// Call from WM_USER+50 handler in main message loop (Google Drive API response)
void ProcessDriveApiResponse(const std::string& json);
