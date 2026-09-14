#pragma once

// ════════════════════════════════════════════════════════════════════
// tab_phone_remote.h  —  PC generates 6-digit code, phone connects
//
// Flow:
//   1. PC clicks "Generate Code" → random 6-digit code shown on screen
//   2. PC starts WebSocket SERVER on port 9224
//   3. Phone user types code in RasFocus app → connects to PC IP:9224
//   4. Phone sends {"type":"auth","code":"XXXXXX"} → PC verifies
//   5. PC starts H.264 screen capture → streams video to phone
//   6. Phone sends mouse/key input JSON back → PC injects via SendInput
//
// Inspired by RustDesk open source (MIT License)
// ════════════════════════════════════════════════════════════════════

#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Crypt32.lib")

#include <string>

// ── Connection state ─────────────────────────────────────────────
enum class RdState {
    Idle,         // no code generated yet
    WaitPhone,    // code shown, server listening
    Connected,    // phone connected & verified
    Error
};

extern RdState      g_rdState;
extern std::string  g_rdCode;         // 6-digit code displayed on PC
extern std::string  g_rdPhoneName;    // phone device name (after connect)
extern std::string  g_rdPhoneIp;      // phone's IP (informational)
extern std::string  g_rdStatusMsg;
extern int          g_rdFps;
extern bool         g_rdInputEnabled;

// legacy compat
extern std::string  g_rdPhoneId;
extern int          g_rdPhonePort;
extern int          g_rdPhoneW;
extern int          g_rdPhoneH;

// ── Public API ────────────────────────────────────────────────────
void RdGenerateCode();   // generate new code + start WS server
void RdStopServer();     // stop server + disconnect
void RdTimerTick();      // call every 100ms

// ── UI ────────────────────────────────────────────────────────────
void DrawPhoneRemoteTab          (Gdiplus::Graphics& g, float x, float y, float w, float h);
void ProcessPhoneRemoteMouseMove (float mx, float my, float cX, float cY);
void ProcessPhoneRemoteMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void ProcessPhoneRemoteKey       (WPARAM vk, bool keyDown);

// ── Legacy compat (main.cpp calls these, keep them) ──────────────
inline void PhoneRemoteStartServer() {}
inline void PhoneRemoteStopServer () {}
inline void PhoneRemoteTimerTick  () { RdTimerTick(); }

inline void PhoneRemoteMouseDrag(float x, float y)
    { ProcessPhoneRemoteMouseMove(x, y, x, y); }

inline void PhoneRemoteMouseWheel(float x, float y, int delta) {
    float off = (delta > 0) ? -40.f : 40.f;
    ProcessPhoneRemoteMouseMove(x, y,       x, y);
    ProcessPhoneRemoteMouseMove(x, y + off, x, y + off);
}

// old globals referenced elsewhere
extern bool        g_phoneRemoteRunning;
extern int         g_phoneRemotePort;
extern int         g_phoneRemoteUdpPort;
extern int         g_connectedClients;
extern std::string g_phoneRemotePin;
