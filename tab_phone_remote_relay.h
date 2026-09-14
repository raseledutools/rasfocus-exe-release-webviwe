#pragma once
// ════════════════════════════════════════════════════════════════════
// tab_phone_remote_relay.h  —  Firebase Firestore relay signaling
//
// PC side: when "Generate Code" is clicked:
//   1. 6-digit code is generated (same as before)
//   2. PC's public/local IP + port + code → uploaded to Firestore
//      Collection: "rd_sessions"   Document: <code>
//      Fields:     code, ip, port, ts, hostname
//   3. Phone only needs the 6-digit code — no IP required
//   4. Phone looks up code in Firestore → gets IP → connects
//
// This makes it work across NAT/internet (with relay) and LAN both.
//
// Relay flow (internet / different networks):
//   Phone → Firestore lookup(code) → ws://relay.rasfocus.com/relay/<code>
//   PC    → ws://relay.rasfocus.com/relay/<code>  (registers as host)
//   Relay server bridges the two WebSocket connections transparently.
//
// LAN flow (same network):
//   Phone → Firestore lookup(code) → ws://local-ip:9224  (direct)
//
// ════════════════════════════════════════════════════════════════════
#include <string>

// ── Relay server URL (self-hosted or cloud) ──────────────────────
// Change this to your actual relay server domain.
// The relay server bridges two WebSocket peers identified by "code".
// Format: ws://host/relay/<code>   or   wss://host/relay/<code>
static const std::string RELAY_SERVER_URL = "wss://relay.rasfocus.com";

// ── Firebase REST endpoint (no SDK needed on PC) ─────────────────
// Uses Firebase REST API: no C++ SDK required on Windows.
static const std::string FIREBASE_PROJECT = "rasfocus-c746d";
static const std::string FIREBASE_API_KEY = "AIzaSyBcsyn2COOfawUn0MVvQsM08_FUdlDd8Mw";
static const std::string FIRESTORE_BASE   =
    "https://firestore.googleapis.com/v1/projects/" + FIREBASE_PROJECT +
    "/databases/(default)/documents/rd_sessions/";

// ── Upload session to Firestore (called after code generation) ────
//    Runs in a background thread — does not block UI.
void RelayRegisterSession(const std::string& code,
                          const std::string& localIp,
                          int port);

// ── Delete session from Firestore (called on stop/disconnect) ─────
void RelayUnregisterSession(const std::string& code);

// ── Check if relay is needed (returns true if phone is not on LAN) ─
//    Not needed for basic implementation — phone always tries LAN first.
bool RelayIsAvailable();

// ── Relay HOST (PC connects to relay as host) ────────────────────
void RelayHostStart(const std::string& code);  // call after RdGenerateCode
void RelayHostStop();                           // call on RdStopServer
void RelaySendBinary(const void* data, size_t len); // call for each H264 frame
