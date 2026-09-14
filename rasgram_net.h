// rasgram_net.h
// RasGram Desktop — Network & Backend Layer
// Firebase Firestore REST + LAN UDP/TCP (same protocol as Android LanChatManager)
// Audio/Video call via WASAPI + DirectShow + UDP

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// ============================================================
// CONSTANTS
// ============================================================
#define RG_FIREBASE_PROJECT   "rasfocus-c746d"
#define RG_FIRESTORE_HOST     "firestore.googleapis.com"
#define RG_STORAGE_HOST       "firebasestorage.googleapis.com"
#define RG_LAN_UDP_PORT       5555     // Beacon (same as Android)
#define RG_LAN_TCP_PORT       5556     // Message/file transfer
#define RG_CALL_UDP_PORT      5558     // Audio/Video RTP (5557 used by phone remote)
#define RG_BEACON_INTERVAL_MS 3000
#define RG_PEER_TIMEOUT_MS    10000

// ============================================================
// DATA STRUCTURES  (mirrors Android RasGram Kotlin data classes)
// ============================================================

struct RgUser {
    std::string uid;
    std::string mobile;       // phone number = unique ID in RasGram
    std::string name;
    std::string avatarUrl;
    bool   isOnline   = false;
    long long lastSeen = 0;
};

struct RgMessage {
    std::string   id;
    std::string   chatId;
    std::string   senderMobile;
    std::string   senderName;
    std::string   text;
    long long timestamp  = 0;
    std::string   timeString;
    std::string   fileUrl;
    std::string   fileName;
    std::string   fileType;   // "image/*", "audio/*", "video/*", etc.
    long long fileSizeBytes = 0;
    std::string   reaction;
    bool     read        = false;
    bool     delivered   = false;
    bool     isCallLog   = false;
    std::string   callStatus; // "missed", "answered", "declined"
    std::string   callType;   // "audio", "video"
    bool     isDeleted   = false;
    bool     isForwarded = false;
    bool     isPending   = false;
    std::string   replyToId;
    std::string   replyToText;
    std::string   replyToSender;
    int      duration    = 0; // voice message duration in seconds
    bool     deliveredViaLan = false;
};

struct RgChatPreview {
    std::string   contactMobile;
    std::string   contactName;
    std::string   contactAvatarUrl;
    std::string   lastMessageText;
    std::string   lastMessageSender;
    long long lastTimestamp = 0;
    std::string   lastTimeString;
    std::string   lastFileType;
    bool     lastIsCallLog = false;
    int      unreadCount   = 0;
    bool     isPinned      = false;
    bool     isMuted       = false;
};

struct RgLanPeer {
    std::string mobile;
    std::string name;
    std::string ip;
    int    port = RG_LAN_TCP_PORT;
};

// ============================================================
// CALLBACKS
// ============================================================
typedef std::function<void(const std::vector<RgChatPreview>&)>   RgChatsCallback;
typedef std::function<void(const std::vector<RgMessage>&)>       RgMessagesCallback;
typedef std::function<void(const RgMessage&)>               RgNewMessageCallback;
typedef std::function<void(const std::vector<RgLanPeer>&)>       RgLanPeersCallback;
typedef std::function<void(bool /*connected*/)>             RgCallStateCallback;
typedef std::function<void(const void* /*frameRGB*/,
                      int w, int h)>                   RgVideoFrameCallback;

// ============================================================
// RASGRAM NET — PUBLIC API
// ============================================================

// Initialization
void RgNet_Init(const std::string& myMobile, const std::string& myName,
                const std::string& myUid, const std::string& idToken);
void RgNet_Shutdown();

// ── Contacts & Chats ────────────────────────────────────────
// Fetch contact list from Firestore (users/{myMobile}/contacts)
void RgNet_FetchContacts(RgChatsCallback cb);

// Start polling chat list (calls cb every ~2s on background thread)
void RgNet_StartChatListPolling(RgChatsCallback cb);
void RgNet_StopChatListPolling();

// Load messages for a specific chat
void RgNet_FetchMessages(const std::string& chatId, RgMessagesCallback cb);

// Start polling new messages for open chat
void RgNet_StartMessagePolling(const std::string& chatId, long long sinceTimestamp,
                               RgNewMessageCallback cb);
void RgNet_StopMessagePolling();

// Send a text message
void RgNet_SendText(const std::string& chatId,
                    const std::string& text,
                    const std::string& receiverMobile);

// Mark messages as read
void RgNet_MarkRead(const std::string& chatId, const std::string& myMobile);

// ── File / Voice ─────────────────────────────────────────────
// Upload a local file to Firebase Storage, then send message
void RgNet_SendFile(const std::string& chatId,
                    const std::string& receiverMobile,
                    const std::wstring& localFilePath,
                    const std::string& mimeType);

// ── LAN Mode ─────────────────────────────────────────────────
void RgNet_StartLan(RgLanPeersCallback peersCb,
                    RgNewMessageCallback msgCb);
void RgNet_StopLan();
void RgNet_LanSendText(const RgLanPeer& peer,
                       const std::string& chatId,
                       const std::string& text);
void RgNet_LanSendFile(const RgLanPeer& peer,
                       const std::string& chatId,
                       const std::wstring& filePath,
                       const std::string& mimeType);

// ── Audio / Video Calls ──────────────────────────────────────
struct RgCallParams {
    std::string   chatId;
    std::string   peerMobile;
    std::string   peerName;
    std::string   peerIp;     // for LAN direct call
    bool     isVideo  = false;
    bool     isLan    = false; // true = LAN direct, false = relay via Firebase
};

void RgCall_StartOutgoing(const RgCallParams& p,
                          RgCallStateCallback stateCb,
                          RgVideoFrameCallback videoCb = nullptr);

void RgCall_AcceptIncoming(const RgCallParams& p,
                           RgCallStateCallback stateCb,
                           RgVideoFrameCallback videoCb = nullptr);

void RgCall_Hangup();
void RgCall_Decline();          // decline incoming — writes "declined" to Firestore
void RgCall_ToggleMute(bool mute);
void RgCall_ToggleSpeaker(bool on);
void RgCall_ToggleCamera(bool on);

bool RgCall_IsActive();
bool RgCall_IsMuted();
bool RgCall_IsVideo();

// Incoming call polling — starts a background thread that watches
// Firestore calls/{myMobile}/incoming and fires cb when a new call arrives.
typedef std::function<void(const RgCallParams&)> RgIncomingCallCallback;
void RgNet_StartIncomingCallPolling(RgIncomingCallCallback cb);
void RgNet_StopIncomingCallPolling();

// Desktop notification (Win32 Shell_NotifyIcon balloon)
void RgNotify_Message(const std::string& senderName,
                      const std::string& text);
void RgNotify_IncomingCall(const std::string& callerName, bool isVideo);
void RgNotify_Init(HWND ownerHwnd);   // call once at startup with main HWND
void RgNotify_Destroy();              // call at shutdown
int  RgCall_GetDurationSeconds();

// ── Presence ─────────────────────────────────────────────────
void RgNet_SetOnline(bool online);

// ── Helpers ──────────────────────────────────────────────────
// Build Firestore REST path
std::string RgBuildPath(const std::string& collection, const std::string& docId = "",
                   const std::string& sub = "", const std::string& subId = "");
// Build chatId from two mobiles (pure, no "pvt_msg_" prefix — matches Android generateChatId)
std::string RgBuildChatId(const std::string& mobileA, const std::string& mobileB);
// Build Firestore collection name for a private chat
std::string RgChatCollection(const std::string& chatId);
// Resolve my phone mobile from chat_users by scanning for matching uid
std::string RgNet_ResolveMyMobile(const std::string& uid);
// HTTP GET to Firestore
std::string RgFirestoreGet(const std::string& path);
// HTTP POST/PATCH to Firestore
std::string RgFirestorePost(const std::string& method, const std::string& path,
                       const std::string& jsonBody);
// Parse a simple string field from Firestore JSON response
std::string RgParseField(const std::string& json, const std::string& field);
// Parse integer field
long long RgParseIntField(const std::string& json, const std::string& field);
// Format timestamp to "HH:MM" string
std::string RgFormatTime(long long timestampMs);
