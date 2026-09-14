#pragma once

// ── Winsock2 must come before windows.h everywhere ───────────────────
// See winsock_fix.h for explanation.
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

// ── Windows core ─────────────────────────────────────────────────────
#include <windows.h>
#include <shellapi.h>

// ── COM / OLE (IStream, PROPID) needed by GDI+ ───────────────────────
#include <objbase.h>
#include <propidl.h>

// ── GDI+ (after IStream / PROPID) ────────────────────────────────────
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>

// ════════════════════════════════════════════════════════════════════
// FAMILY LINK — Parent Control State
// ════════════════════════════════════════════════════════════════════

extern bool g_isLinkedToParent;
extern std::string g_parentUid;

extern bool g_parentLockAllTabs;
extern bool g_parentForceAdultBlock;
extern bool g_parentForceReelsBlock;
extern bool g_parentForceShortsBlock;

extern bool g_parentAppControlEnabled;
extern std::string g_parentAppMode;
extern std::string g_parentAllowedAppsCSV;
extern std::string g_parentBlockedAppsCSV;

extern bool g_parentWebBlockEnabled;
extern std::string g_parentBlockedWebsCSV;

extern bool g_parentBlockTaskManager;
extern bool g_parentBlockSettings;
extern bool g_parentBlockFileManager;
extern std::string g_parentBlockedFoldersCSV;

extern bool g_parentInternetFasting;
extern int  g_parentPowerAction;

extern int  g_parentTimeLimitMinutes;
extern ULONGLONG g_parentTimeLimitStart;

// ════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ════════════════════════════════════════════════════════════════════

void DrawFamilyLinkTab(Gdiplus::Graphics& g, float x, float y, float w, float h);

void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY);
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void ProcessFamilyLinkChar(wchar_t c);
void ProcessFamilyLinkKeyDown(WPARAM wp);

void ProcessFamilyLinkTimer(UINT_PTR timerId, HWND hWnd);
void FamilyLink_EnforceParentCommands(HWND hWnd);
