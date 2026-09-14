// rasgram_qr_session.h
// RasGram Desktop — QR Login Session Manager
//
// Flow (same as WhatsApp Web / Telegram Desktop):
//   1. PC creates a random token → writes  qr_sessions/{token}  in Firestore
//      with  status="waiting", expiry=now+60s
//   2. PC encodes that token as a real QR code (nayuki library) and shows it
//   3. Phone user opens RasGram → Settings → Link Device → scans QR
//   4. Phone reads token from QR, looks up qr_sessions/{token}, writes back:
//         status="confirmed", uid, mobile, name, idToken
//   5. PC polls qr_sessions/{token} every 2s.  On "confirmed" it reads the
//      user fields, calls RgNet_Init(), switches to App screen.
//
// Dependencies (all already in the project):
//   • WinINet HTTPS helpers  (same as rasgram_net.cpp)
//   • nayuki QrCode.hpp / QrCode.cpp  (add two files from the scanLib/ folder
//     OR grab the header-only port below — see bottom of this file)
//
// Usage (in tab_rasgram.cpp):
//   #include "rasgram_qr_session.h"
//
//   // On login screen show — start session
//   RgQr_StartSession();
//
//   // Every WM_TIMER tick (100 ms)
//   RgQrStatus st = RgQr_Poll();
//   if (st == RgQrStatus::Confirmed) {
//       RgQrUser u = RgQr_GetUser();
//       // u.uid / u.mobile / u.name / u.idToken are now valid
//       // store them, call RgNet_Init(), switch to App screen
//       RgQr_Clear();
//   }
//
//   // Draw the QR:
//   //   get bit matrix from RgQr_GetMatrix() and render with GDI+
//   const RgQrMatrix& mat = RgQr_GetMatrix();
//   // mat.size × mat.size, mat.cells[row][col] == true → dark module
//
//   // Refresh button clicked:
//   RgQr_StartSession();   // generates new token, writes new Firestore doc

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>   // DWORD

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

// ── Result status ────────────────────────────────────────────
enum class RgQrStatus {
    Waiting,      // token written, waiting for phone scan
    Confirmed,    // phone confirmed — user fields are ready
    Expired,      // 60 s elapsed without scan
    Error         // network or Firestore write failed
};

// ── Confirmed user ────────────────────────────────────────────
struct RgQrUser {
    std::string uid;
    std::string mobile;
    std::string name;
    std::string idToken;   // Firebase Auth ID token sent by the phone
    std::string email;
};

// ── QR bit matrix (for GDI+ drawing) ─────────────────────────
struct RgQrMatrix {
    int  size    = 0;                              // modules per side
    std::vector<std::vector<bool>> cells;          // [row][col] true = dark
    std::string token;                             // the raw token string
    bool ready   = false;
};

// ── Public API ────────────────────────────────────────────────

// Generate a new session token, write it to Firestore, build the QR matrix.
// Non-blocking — result available via RgQr_GetStatus() after a moment.
void       RgQr_StartSession();

// Call on WM_TIMER ~every 200 ms.  Polls Firestore for phone confirmation.
// Returns current status.  Once Confirmed, call RgQr_GetUser() then RgQr_Clear().
RgQrStatus RgQr_Poll();

// Returns a snapshot of the current status without polling Firestore.
RgQrStatus RgQr_GetStatus();

// Returns the QR matrix (valid after StartSession sets up the token).
const RgQrMatrix& RgQr_GetMatrix();

// Returns confirmed user data (valid only when status == Confirmed).
const RgQrUser& RgQr_GetUser();

// How many ms have elapsed since the session was created.
DWORD RgQr_ElapsedMs();

// Reset all state (call after consuming Confirmed result, or on explicit refresh).
void RgQr_Clear();

// Must be called with a valid Firebase API key before StartSession.
// (Same key already used in accounts.cpp)
void RgQr_Init(const std::string& firebaseApiKey,
               const std::string& firestoreHost = "firestore.googleapis.com",
               const std::string& projectId     = "rasfocus-c746d");
