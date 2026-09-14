#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include "firebase/app.h"

using namespace Gdiplus;
using namespace std;

// ── Global premium status (main.cpp reads this) ──
extern bool g_isPremiumUser;
extern wstring g_loggedInEmail;   // login হলে email থাকবে
extern string g_loggedInUserUid;  // login হলে Firebase UID থাকবে

// ── Called from WinMain after Firebase is created ──
void InitAccountsModule(firebase::App* app);

// ── Draw the full My Account tab content area ──
void DrawAccountsTab(Graphics& g, float x, float y, float w, float h);

// ── Input handlers (called from WndProc) ──
void ProcessAccountsMouseMove (float x, float y);
void ProcessAccountsMouseClick(float x, float y, HWND hWnd);
void ProcessAccountsChar      (wchar_t c);
void ProcessAccountsKeyDown   (WPARAM wp);