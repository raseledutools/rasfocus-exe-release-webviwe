// tab_rasgram_native.h
// RasGram Desktop — Native Win32 GDI+ tab (replaces WebView2-based version)
//
// Drop-in replacement for tab_rasgram.h
// All functions have identical signatures so tab_special.cpp compiles unchanged.

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <ole2.h>
#include <gdiplus.h>
#include <string>

using namespace std;

// ── Initialization (call once when Special tab first loads) ──
void InitRasGramDesktop();

// ── Called when tab becomes visible / hidden ─────────────────
void ShowRasGramControls(bool show);

// ── Main draw function (called from DrawSpecialFeatureTab) ───
void DrawRasGramTab(Gdiplus::Graphics& g,
                    float cx, float cy, float cw, float ch);

// ── Mouse / keyboard input ────────────────────────────────────
void ProcessRasGramMouseMove (float x, float y);
void ProcessRasGramMouseClick(float x, float y);
void ProcessRasGramMouseWheel(int delta);
void ProcessRasGramChar      (wchar_t c);
void ProcessRasGramKeyDown   (WPARAM vk);

// ── WndProc message hook (always returns false — no WebView2) ─
bool RgHandleParentWndMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// ── Shutdown helpers ─────────────────────────────────────────
void RgNotify_Destroy();
void RgNet_StopIncomingCallPolling();

// Keep the same WM_USER IDs so main.cpp compiles unchanged
#define WM_RG_INCOMING_CALL (WM_USER + 70)
#define WM_RG_CALL_ENDED    (WM_USER + 71)
#define WM_RG_VIDEO_FRAME   (WM_USER + 72)
#define WM_RG_NEW_MESSAGE   (WM_USER + 73)
#define WM_RG_LOGIN_OK      (WM_USER + 74)
#define WM_RG_LOGIN_ERR     (WM_USER + 75)
