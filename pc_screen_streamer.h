#pragma once

// ════════════════════════════════════════════════════════════════
// pc_screen_streamer.h  —  PC screen → Phone (H264 over WebSocket)
//
// PC side server on port 9225:
//   GDI+ screen capture → H264 encode (MFT) → WebSocket broadcast
// Phone connects and decodes via MediaCodec.
//
// Also receives mouse/key JSON from phone → injects via SendInput.
//
// Inspired by RustDesk open source (MIT License)
// ════════════════════════════════════════════════════════════════

#include <windows.h>
#include <string>

extern bool g_pcStreamerRunning;
extern int  g_pcStreamerClients;
extern int  g_pcStreamerFps;

void PcStreamerStart();   // start H264 capture + WS server on port 9225
void PcStreamerStop();
