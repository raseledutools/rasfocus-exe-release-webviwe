// ================================================================
// tab_phone_remote.cpp  —  RustDesk-style layout
//
// Layout (RustDesk exact):
//   ┌──────────────┬──────────────────────────────────┐
//   │ Your Desktop │  Control Remote Desktop           │
//   │  ID: XXX XXX │  [ID input box]   [Connect ▼]    │
//   │  Password: - │                                   │
//   ├──────────────┴──────────────────────────────────┤
//   │  Recent connections (cards grid)                 │
//   └─────────────────────────────────────────────────┘
//
// PC = WebSocket SERVER (port 9224)
//   1. ID auto-generated from hostname hash (stable, like RustDesk)
//   2. Phone/PC types ID → Firestore lookup → connects
//   3. Auth via one-time password
// ================================================================

#pragma warning(disable: 4996)
#pragma warning(disable: 4244)

#include <winsock2.h>
#include <ws2tcpip.h>
#include "tab_phone_remote.h"
#include "tab_phone_remote_relay.h"
#include "globals.h"
#include "pc_screen_streamer.h"

#include <windows.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfidl.h>
#include <mferror.h>
#include <codecapi.h>
#include <wincrypt.h>
#include <shellapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Crypt32.lib")

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace Gdiplus;
using namespace std;

// ── Globals (extern in .h) ────────────────────────────────────────
RdState     g_rdState       = RdState::Idle;
string      g_rdCode        = "";
string      g_rdPhoneName   = "";
string      g_rdPhoneIp     = "";
string      g_rdStatusMsg   = "Ready";
int         g_rdFps         = 0;
bool        g_rdInputEnabled= true;

// legacy compat
string      g_rdPhoneId     = "";
int         g_rdPhonePort   = 9224;
int         g_rdPhoneW      = 1080;
int         g_rdPhoneH      = 1920;
bool        g_phoneRemoteRunning = false;
int         g_phoneRemotePort    = 9224;
int         g_phoneRemoteUdpPort = 9225;
int         g_connectedClients   = 0;
string      g_phoneRemotePin     = "";

// ── Internal ──────────────────────────────────────────────────────
static const int  RD_PORT     = 9224;
static const int  TARGET_FPS  = 30;
static const int  TARGET_BPS  = 4'000'000;

static atomic<bool> s_active  { false };
static SOCKET       s_listenSock = INVALID_SOCKET;
SOCKET              s_clientSock = INVALID_SOCKET;
static mutex        s_sendMtx;

// ── ID & Password (stable like RustDesk) ─────────────────────────
static string s_myId       = "";   // 9-digit ID from hostname hash
static string s_myPassword = "";   // 6-char one-time password
static string s_inputId    = "";   // what user is typing in Connect box
static bool   s_inputFocused = false;

// ── Recent connections ────────────────────────────────────────────
struct RecentConn {
    string id;
    string name;
    string platform; // "windows" or "android"
    DWORD  lastUsed;
};
static vector<RecentConn> s_recent;

// ── UI hit areas ──────────────────────────────────────────────────
static float s_drawX=0, s_drawY=0, s_drawW=0, s_drawH=0;

// Hover states
static bool s_hovConnect   = false;
static bool s_hovStop      = false;
static bool s_hovInput     = false;

// Hover for recent cards
static int  s_hovCard      = -1;

// Stored rects for hit-testing (set during draw)
static RectF s_rcConnect;
static RectF s_rcInput;
static RectF s_rcStop;
static vector<RectF> s_rcCards;

extern HWND hParentWnd;

// ── String helpers ────────────────────────────────────────────────
static string WStr(const wstring& w) {
    if(w.empty()) return "";
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    string s(n,' ');
    WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),&s[0],n,nullptr,nullptr);
    return s;
}
static wstring ToWStr(const string& s) {
    if(s.empty()) return L"";
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    wstring w(n,L' ');
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),&w[0],n);
    return w;
}
static string Jget(const string& j, const string& k) {
    string sk="\""+k+"\""; size_t p=j.find(sk);
    if(p==string::npos) return "";
    p=j.find(':',p+sk.size()); if(p==string::npos) return "";
    p++; while(p<j.size()&&(j[p]==' '||j[p]=='\t')) p++;
    if(p>=j.size()) return "";
    if(j[p]=='"'){size_t e=j.find('"',p+1);if(e==string::npos)return "";return j.substr(p+1,e-p-1);}
    size_t e=p; while(e<j.size()&&j[e]!=','&&j[e]!='}'&&j[e]!=']') e++;
    string v=j.substr(p,e-p);
    while(!v.empty()&&(v.back()==' '||v.back()=='\r'||v.back()=='\n')) v.pop_back();
    return v;
}

// ── Format ID: "135310219" → "135 310 219" ───────────────────────
static wstring FormatId(const string& id) {
    // 9 digits → "XXX XXX XXX"
    if(id.size()==9)
        return ToWStr(id.substr(0,3)+" "+id.substr(3,3)+" "+id.substr(6,3));
    // 6 digits → "XXX XXX"
    if(id.size()==6)
        return ToWStr(id.substr(0,3)+" "+id.substr(3,3));
    return ToWStr(id);
}

// ── Generate stable 9-digit ID from hostname + IP ────────────────
// Must be deterministic: same PC → same ID across every restart.
// GetTickCount() was here before — that made ID change every launch. Removed.
static string MakeStableId() {
    // 1. Hostname
    char host[256] = {};
    DWORD len = sizeof(host);
    GetComputerNameA(host, &len);

    unsigned long long h = 5381;
    for(char* p=host; *p; p++) h = h*31 + (unsigned char)*p;

    // 2. Mix in local IP for uniqueness across PCs with same hostname
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    char myHost[256] = {};
    gethostname(myHost, sizeof(myHost));
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(myHost, nullptr, &hints, &res) == 0 && res) {
        sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        unsigned long ip = sa->sin_addr.s_addr;
        h = h * 31 + ip;
        freeaddrinfo(res);
    }
    WSACleanup();

    // NOTE: do NOT mix in GetTickCount(), time(), or any runtime-variable.
    // The ID must be identical every time this function is called on the same PC.

    unsigned long long id = (h % 900000000ULL) + 100000000ULL;
    char buf[16]; sprintf(buf, "%llu", id);
    return string(buf);
}

// ── Generate random 6-char password ─────────────────────────────
static string MakePassword() {
    static const char* chars = "23456789abcdefghjkmnpqrstuvwxyz";
    srand((unsigned)time(nullptr) ^ GetTickCount());
    string pw;
    for(int i=0;i<6;i++) pw += chars[rand()%31];
    return pw;
}

// ── Get local IP ──────────────────────────────────────────────────
static string GetLocalIp() {
    char host[256] = {};
    gethostname(host, sizeof(host));
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return "127.0.0.1";
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, ip, sizeof(ip));
    freeaddrinfo(res);
    return string(ip);
}

// ── Copy text to clipboard ────────────────────────────────────────
static void CopyToClipboard(const string& text) {
    if (!OpenClipboard(hParentWnd)) return;
    EmptyClipboard();
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!hg) { CloseClipboard(); return; }
    memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
    GlobalUnlock(hg);
    SetClipboardData(CF_TEXT, hg);
    CloseClipboard();
}

// ── SHA1 + Base64 (for WS handshake) ─────────────────────────────
static string B64Enc(const vector<BYTE>& d) {
    static const char* t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string o; o.reserve(((d.size()+2)/3)*4);
    for(size_t i=0;i<d.size();i+=3){
        BYTE b0=d[i],b1=(i+1<d.size())?d[i+1]:0,b2=(i+2<d.size())?d[i+2]:0;
        o+=t[b0>>2]; o+=t[((b0&3)<<4)|(b1>>4)];
        o+=(i+1<d.size())?t[((b1&0xF)<<2)|(b2>>6)]:'=';
        o+=(i+2<d.size())?t[b2&0x3F]:'=';
    }
    return o;
}
static string Sha1B64(const string& s) {
    HCRYPTPROV hp=0; HCRYPTHASH hh=0;
    CryptAcquireContextA(&hp,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hp,CALG_SHA1,0,0,&hh);
    CryptHashData(hh,(const BYTE*)s.c_str(),(DWORD)s.size(),0);
    BYTE hash[20]; DWORD hl=20;
    CryptGetHashParam(hh,HP_HASHVAL,hash,&hl,0);
    CryptDestroyHash(hh); CryptReleaseContext(hp,0);
    return B64Enc(vector<BYTE>(hash,hash+20));
}

// ── WebSocket helpers ─────────────────────────────────────────────
static bool WsRecvAll(SOCKET s, char* buf, int n) {
    int got=0; while(got<n){int r=recv(s,buf+got,n-got,0);if(r<=0)return false;got+=r;}return true;
}
static pair<int,vector<BYTE>> WsRecvFrame(SOCKET s) {
    BYTE h[2]; if(!WsRecvAll(s,(char*)h,2)) return {-1,{}};
    int op=h[0]&0xF; bool masked=(h[1]&0x80)!=0;
    size_t len=h[1]&0x7F;
    if(len==126){BYTE e[2];if(!WsRecvAll(s,(char*)e,2))return{-1,{}};len=((size_t)e[0]<<8)|e[1];}
    else if(len==127){BYTE e[8];if(!WsRecvAll(s,(char*)e,8))return{-1,{}};
        len=0;for(int i=0;i<8;i++)len=(len<<8)|e[i];}
    BYTE mask[4]={};
    if(masked&&!WsRecvAll(s,(char*)mask,4)) return{-1,{}};
    vector<BYTE> data(len);
    if(len>0&&!WsRecvAll(s,(char*)data.data(),(int)len)) return{-1,{}};
    if(masked) for(size_t i=0;i<len;i++) data[i]^=mask[i%4];
    return{op,data};
}
static bool WsSendText(SOCKET s, const string& txt) {
    lock_guard<mutex> lk(s_sendMtx);
    vector<BYTE> frame;
    size_t len=txt.size();
    frame.push_back(0x81);
    if(len<126){ frame.push_back((BYTE)len); }
    else if(len<65536){ frame.push_back(126); frame.push_back((len>>8)&0xFF); frame.push_back(len&0xFF); }
    else { frame.push_back(127); for(int i=7;i>=0;i--) frame.push_back((len>>(i*8))&0xFF); }
    for(char c:txt) frame.push_back((BYTE)c);
    return send(s,(char*)frame.data(),(int)frame.size(),0)>0;
}
static bool WsSendBinary(SOCKET s, const BYTE* data, size_t len) {
    lock_guard<mutex> lk(s_sendMtx);
    vector<BYTE> frame;
    frame.push_back(0x82);
    if(len<126){ frame.push_back((BYTE)len); }
    else if(len<65536){ frame.push_back(126); frame.push_back((len>>8)&0xFF); frame.push_back(len&0xFF); }
    else { frame.push_back(127); for(int i=7;i>=0;i--) frame.push_back((len>>(i*8))&0xFF); }
    frame.insert(frame.end(), data, data+len);
    return send(s,(char*)frame.data(),(int)frame.size(),0)>0;
}
static bool DoHandshake(SOCKET s) {
    char buf[4096]={};
    int total=0;
    while(total<(int)sizeof(buf)-1){
        int r=recv(s,buf+total,(int)sizeof(buf)-1-total,0);
        if(r<=0) return false;
        total+=r; buf[total]=0;
        if(strstr(buf,"\r\n\r\n")) break;
    }
    const char* keyHdr=strstr(buf,"Sec-WebSocket-Key:");
    if(!keyHdr) return false;
    keyHdr+=18;
    while(*keyHdr==' ') keyHdr++;
    char key[256]={}; int ki=0;
    while(*keyHdr&&*keyHdr!='\r'&&*keyHdr!='\n'&&ki<(int)sizeof(key)-1) key[ki++]=*keyHdr++;
    while(ki>0&&(key[ki-1]==' '||key[ki-1]=='\r'||key[ki-1]=='\n')) ki--;
    key[ki]=0;
    string accept=Sha1B64(string(key)+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    string resp="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+accept+"\r\n\r\n";
    return send(s,resp.c_str(),(int)resp.size(),0)>0;
}

// ── Handle one connected client ───────────────────────────────────
static void HandlePhone(SOCKET sock, string phoneIp) {
    s_clientSock = sock;
    g_phoneRemoteRunning = true;
    g_connectedClients = 1;

    auto [op, data] = WsRecvFrame(sock);
    bool authed = false;
    string deviceName = "Unknown";

    if(op==1) {
        string msg(data.begin(), data.end());
        string type=Jget(msg,"type");
        string pw=Jget(msg,"password");
        string code=Jget(msg,"code");
        deviceName=Jget(msg,"device");
        if(type=="auth" && (pw==s_myPassword || code==g_rdCode)) {
            authed=true;
        } else {
            WsSendText(sock, "{\"type\":\"error\",\"msg\":\"wrong password\"}");
        }
    }

    if(!authed) {
        closesocket(sock);
        s_clientSock=INVALID_SOCKET;
        g_phoneRemoteRunning=false;
        g_connectedClients=0;
        return;
    }

    int sw=GetSystemMetrics(SM_CXSCREEN);
    int sh=GetSystemMetrics(SM_CYSCREEN);
    float scale=min(1.f,1280.f/sw);
    int vw=(int)(sw*scale), vh=(int)(sh*scale);

    WsSendText(sock, "{\"type\":\"ready\",\"width\":"+to_string(vw)+
                     ",\"height\":"+to_string(vh)+
                     ",\"fps\":"+to_string(TARGET_FPS)+
                     ",\"mode\":\"h264\""+
                     ",\"video_port\":9225}");

    g_rdPhoneName=deviceName;
    g_rdPhoneIp=phoneIp;
    g_rdState=RdState::Connected;
    g_phoneRemoteRunning=true;
    g_connectedClients=1;

    // Add to recent
    RecentConn rc;
    rc.id = s_inputId.empty() ? phoneIp : s_inputId;
    rc.name = deviceName.empty() ? phoneIp : deviceName;
    rc.platform = "android";
    rc.lastUsed = GetTickCount();
    // Remove duplicate
    s_recent.erase(remove_if(s_recent.begin(),s_recent.end(),[&](const RecentConn& r){ return r.id==rc.id; }),s_recent.end());
    s_recent.insert(s_recent.begin(), rc);
    if(s_recent.size()>6) s_recent.resize(6);

    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);

    PcStreamerStart();

    while(s_active && sock!=INVALID_SOCKET) {
        auto [iop, idata] = WsRecvFrame(sock);
        if(iop<0) break;
        if(iop==1) {
            string msg(idata.begin(), idata.end());
            string type=Jget(msg,"type");
            if(type=="mouse") {
                float nx=0,ny=0; int mask=0;
                try{ nx=stof(Jget(msg,"nx")); ny=stof(Jget(msg,"ny")); mask=stoi(Jget(msg,"mask")); }catch(...){}
                INPUT inp={}; inp.type=INPUT_MOUSE;
                inp.mi.dx=(LONG)(nx*65535); inp.mi.dy=(LONG)(ny*65535);
                inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE;
                if(mask&1) inp.mi.dwFlags|=MOUSEEVENTF_LEFTDOWN;
                if(mask&2) inp.mi.dwFlags|=MOUSEEVENTF_LEFTUP;
                if(mask&4) inp.mi.dwFlags|=MOUSEEVENTF_RIGHTDOWN;
                if(mask&8) inp.mi.dwFlags|=MOUSEEVENTF_RIGHTUP;
                if(g_rdInputEnabled) SendInput(1,&inp,sizeof(INPUT));
            } else if(type=="key") {
                int vk=0; string action;
                try{ vk=stoi(Jget(msg,"vk")); }catch(...){}
                action=Jget(msg,"action");
                INPUT ki={}; ki.type=INPUT_KEYBOARD;
                ki.ki.wVk=(WORD)vk;
                if(action=="up") ki.ki.dwFlags=KEYEVENTF_KEYUP;
                if(g_rdInputEnabled) SendInput(1,&ki,sizeof(INPUT));
            } else if(type=="scroll") {
                float x=0,y=0; string dir;
                try{ x=stof(Jget(msg,"nx")); y=stof(Jget(msg,"ny")); }catch(...){}
                dir=Jget(msg,"dir");
                INPUT si={}; si.type=INPUT_MOUSE;
                si.mi.dx=(LONG)(x*65535); si.mi.dy=(LONG)(y*65535);
                si.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_WHEEL;
                si.mi.mouseData=(dir=="up")?WHEEL_DELTA:(DWORD)-(int)WHEEL_DELTA;
                if(g_rdInputEnabled) SendInput(1,&si,sizeof(INPUT));
            } else if(type=="ping") {
                WsSendText(sock,"{\"type\":\"pong\"}");
            }
        } else if(iop==8) break;
    }

    PcStreamerStop();
    closesocket(sock);
    s_clientSock=INVALID_SOCKET;
    g_rdState=RdState::WaitPhone;
    g_rdPhoneName=""; g_rdPhoneIp="";
    g_rdStatusMsg="Ready. For faster connection, please set up your own server";
    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

static void AcceptLoop() {
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    s_listenSock=socket(AF_INET,SOCK_STREAM,0);
    if(s_listenSock==INVALID_SOCKET) return;
    int reuse=1;
    setsockopt(s_listenSock,SOL_SOCKET,SO_REUSEADDR,(char*)&reuse,sizeof(reuse));
    sockaddr_in addr={}; addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((u_short)RD_PORT);
    if(bind(s_listenSock,(sockaddr*)&addr,sizeof(addr))<0){
        closesocket(s_listenSock); s_listenSock=INVALID_SOCKET; return;
    }
    listen(s_listenSock,5);
    DWORD timeout=500;
    setsockopt(s_listenSock,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
    while(s_active) {
        sockaddr_in ca={}; int cal=sizeof(ca);
        SOCKET cl=accept(s_listenSock,(sockaddr*)&ca,&cal);
        if(cl==INVALID_SOCKET) continue;
        char cip[INET_ADDRSTRLEN]={};
        inet_ntop(AF_INET,&ca.sin_addr,cip,sizeof(cip));
        if(!DoHandshake(cl)){closesocket(cl);continue;}
        if(s_clientSock!=INVALID_SOCKET){
            WsSendText(cl,"{\"type\":\"error\",\"msg\":\"busy\"}");
            closesocket(cl); continue;
        }
        thread(HandlePhone,cl,string(cip)).detach();
    }
    closesocket(s_listenSock); s_listenSock=INVALID_SOCKET;
    WSACleanup();
}

// ── Public API ────────────────────────────────────────────────────
void RdGenerateCode(){
    // Init ID & password if needed
    if(s_myId.empty())    s_myId       = MakeStableId();
    if(s_myPassword.empty()) s_myPassword = MakePassword();
    g_rdCode = s_myId; // use ID as the code for relay

    RdStopServer();
    g_rdCode = s_myId;
    g_rdState = RdState::WaitPhone;
    g_rdStatusMsg = "Ready";

    string ip = GetLocalIp();
    RelayRegisterSession(g_rdCode, ip, RD_PORT);
    RelayHostStart(g_rdCode);  // PC connects to relay as host

    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    s_active=true;
    thread(AcceptLoop).detach();
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

// Called by relay thread when phone sends input via relay (internet path)
void RelayInjectInput(const string& json) {
    string type=Jget(json,"type");
    if(type=="mouse") {
        float nx=0,ny=0; int mask=0;
        try{ nx=stof(Jget(json,"nx")); ny=stof(Jget(json,"ny")); mask=stoi(Jget(json,"mask")); }catch(...){}
        INPUT inp={}; inp.type=INPUT_MOUSE;
        inp.mi.dx=(LONG)(nx*65535); inp.mi.dy=(LONG)(ny*65535);
        inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE;
        if(mask&1) inp.mi.dwFlags|=MOUSEEVENTF_LEFTDOWN;
        if(mask&2) inp.mi.dwFlags|=MOUSEEVENTF_LEFTUP;
        if(mask&4) inp.mi.dwFlags|=MOUSEEVENTF_RIGHTDOWN;
        if(mask&8) inp.mi.dwFlags|=MOUSEEVENTF_RIGHTUP;
        if(g_rdInputEnabled) SendInput(1,&inp,sizeof(INPUT));
    } else if(type=="key") {
        int vk=0; string action;
        try{ vk=stoi(Jget(json,"vk")); }catch(...){}
        action=Jget(json,"action");
        INPUT ki={}; ki.type=INPUT_KEYBOARD;
        ki.ki.wVk=(WORD)vk;
        if(action=="up") ki.ki.dwFlags=KEYEVENTF_KEYUP;
        if(g_rdInputEnabled) SendInput(1,&ki,sizeof(INPUT));
    } else if(type=="scroll") {
        float x=0,y=0; string dir;
        try{ x=stof(Jget(json,"nx")); y=stof(Jget(json,"ny")); }catch(...){}
        dir=Jget(json,"dir");
        INPUT si={}; si.type=INPUT_MOUSE;
        si.mi.dx=(LONG)(x*65535); si.mi.dy=(LONG)(y*65535);
        si.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_WHEEL;
        si.mi.mouseData=(dir=="up")?WHEEL_DELTA:(DWORD)-(int)WHEEL_DELTA;
        if(g_rdInputEnabled) SendInput(1,&si,sizeof(INPUT));
    }
}

void RdStopServer(){
    s_active=false;
    RelayHostStop();
    if(!g_rdCode.empty()) RelayUnregisterSession(g_rdCode);
    if(s_clientSock!=INVALID_SOCKET){closesocket(s_clientSock);s_clientSock=INVALID_SOCKET;}
    if(s_listenSock!=INVALID_SOCKET){closesocket(s_listenSock);s_listenSock=INVALID_SOCKET;}
    g_rdState=RdState::Idle;
    g_rdPhoneName=""; g_rdPhoneIp="";
    g_rdStatusMsg="Ready";
    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

void RdTimerTick(){
    // Auto-start server on first open
    if(g_rdState==RdState::Idle && s_myId.empty()) {
        s_myId       = MakeStableId();
        s_myPassword = MakePassword();
        RdGenerateCode();
    }
}

void ProcessPhoneRemoteKey(WPARAM vk, bool keyDown) {
    if(!s_inputFocused) return;
    if(!keyDown) return;
    if(vk==VK_BACK) {
        if(!s_inputId.empty()) s_inputId.pop_back();
    } else if(vk==VK_RETURN) {
        // Connect action
    } else if(vk==VK_ESCAPE) {
        s_inputId.clear();
    }
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

// ─────────────────────────────────────────────────────────────────
// DRAW HELPERS
// ─────────────────────────────────────────────────────────────────
static void RdRoundRect(Graphics& g, Brush& fill, Pen* pen,
                        float x,float y,float w,float h,float r=10.f){
    if(w<=0||h<=0) return;
    GraphicsPath p; float d=r*2;
    if(d>w) d=w; if(d>h) d=h;
    p.AddArc(x,y,d,d,180,90);
    p.AddArc(x+w-d,y,d,d,270,90);
    p.AddArc(x+w-d,y+h-d,d,d,0,90);
    p.AddArc(x,y+h-d,d,d,90,90);
    p.CloseFigure();
    g.FillPath(&fill,&p);
    if(pen) g.DrawPath(pen,&p);
}

// Draw the ID in formatted chunks: "135 310 219"
static void DrawIdNumber(Graphics& g, const wstring& idFmt,
                         float x, float y, float w, float h,
                         const Color& col) {
    FontFamily ff(L"Segoe UI");
    Font fId(&ff, 26, FontStyleBold, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    SolidBrush br(col);
    g.DrawString(idFmt.c_str(),-1,&fId,RectF(x,y,w,h),&fmt,&br);
}

// ─────────────────────────────────────────────────────────────────
// MAIN DRAW
// ─────────────────────────────────────────────────────────────────
void DrawPhoneRemoteTab(Graphics& g, float x, float y, float w, float h){
    s_drawX=x; s_drawY=y; s_drawW=w; s_drawH=h;
    s_rcCards.clear();

    // Auto-init
    if(s_myId.empty()) {
        s_myId       = MakeStableId();
        s_myPassword = MakePassword();
    }
    if(g_rdState==RdState::Idle) RdGenerateCode();

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);

    // ── Background ────────────────────────────────────────────────
    // Dark navy like RustDesk
    SolidBrush bgMain(Color(255, 22, 24, 35));
    g.FillRectangle(&bgMain, x, y, w, h);

    // ── TOP PANEL ─────────────────────────────────────────────────
    float topH   = 220.f;   // height of top section
    float leftW  = 260.f;   // width of "Your Desktop" panel
    float pad    = 20.f;

    // Top panel background
    SolidBrush topBg(Color(255, 28, 30, 44));
    g.FillRectangle(&topBg, x, y, w, topH);

    // Divider line between left & right panels
    float divX = x + leftW;
    SolidBrush divBr(Color(255, 50, 52, 70));
    g.FillRectangle(&divBr, divX, y+pad, 1.f, topH-pad*2);

    // ── LEFT: "Your Desktop" ─────────────────────────────────────
    float lx = x + pad;
    float ly = y + pad;

    // Title
    {
        Font fTitle(&ff, 14, FontStyleBold, UnitPixel);
        SolidBrush white(Color(255, 220, 222, 235));
        g.DrawString(L"Your Desktop", -1, &fTitle, RectF(lx, ly, leftW-pad, 24), &fmtL, &white);
    }
    ly += 28;

    // Subtitle
    {
        Font fSub(&ff, 10, FontStyleRegular, UnitPixel);
        SolidBrush gray(Color(255, 130, 132, 155));
        g.DrawString(L"Your desktop can be accessed\nwith this ID and password.",
                     -1, &fSub, RectF(lx, ly, leftW-pad*2, 36), &fmtL, &gray);
    }
    ly += 46;

    // Left blue accent bar + "ID" label
    {
        SolidBrush accentBar(Color(255, 0, 120, 215));
        g.FillRectangle(&accentBar, lx, ly, 4.f, 56.f);
        float tx = lx + 12;

        Font fLbl(&ff, 10, FontStyleRegular, UnitPixel);
        SolidBrush gray(Color(255, 130, 132, 155));
        g.DrawString(L"ID", -1, &fLbl, RectF(tx, ly+2, leftW, 16), &fmtL, &gray);

        // ID number — big
        SolidBrush white(Color(255, 235, 237, 250));
        DrawIdNumber(g, FormatId(s_myId), tx, ly+20, leftW-tx+lx-8, 30, Color(255,235,237,250));

        // Small copy icon / 3-dot menu
        Font fDot(&ff, 16, FontStyleBold, UnitPixel);
        SolidBrush grayDot(Color(255, 110, 112, 140));
        g.DrawString(L"⋮", -1, &fDot, RectF(x+leftW-40, ly+12, 24, 28), &fmtC, &grayDot);
    }
    ly += 68;

    // Password row
    {
        SolidBrush accentBar(Color(255, 0, 120, 215));
        g.FillRectangle(&accentBar, lx, ly, 4.f, 46.f);
        float tx = lx + 12;

        Font fLbl(&ff, 10, FontStyleRegular, UnitPixel);
        SolidBrush gray(Color(255, 130, 132, 155));
        g.DrawString(L"One-time password", -1, &fLbl, RectF(tx, ly+2, leftW, 16), &fmtL, &gray);

        // Password dots or value
        Font fPw(&ff, 16, FontStyleBold, UnitPixel);
        SolidBrush white(Color(255, 200, 202, 220));
        // Show as dots for privacy
        wstring dots(s_myPassword.size(), L'•');
        g.DrawString(dots.c_str(), -1, &fPw, RectF(tx, ly+22, leftW-50, 20), &fmtL, &white);

        // Edit/pencil icon
        Font fPen(&ff, 14, FontStyleRegular, UnitPixel);
        SolidBrush grayPen(Color(255, 110, 112, 140));
        g.DrawString(L"✎", -1, &fPen, RectF(x+leftW-38, ly+12, 24, 24), &fmtC, &grayPen);
    }

    // ── RIGHT: "Control Remote Desktop" ──────────────────────────
    float rx  = divX + pad;
    float ry  = y + pad;
    float rw  = w - leftW - pad*2;

    // Title + help icon
    {
        Font fTitle(&ff, 14, FontStyleBold, UnitPixel);
        SolidBrush white(Color(255, 220, 222, 235));
        g.DrawString(L"Control Remote Desktop", -1, &fTitle,
                     RectF(rx, ry, rw-30, 24), &fmtL, &white);
        // ? icon
        Font fQ(&ff, 13, FontStyleBold, UnitPixel);
        SolidBrush gray(Color(255, 110, 112, 140));
        g.DrawString(L"?", -1, &fQ, RectF(rx+rw-24, ry, 20, 24), &fmtC, &gray);
    }
    ry += 34;

    // ID input box
    {
        float inputH = 48.f;
        float inputW = rw - 130.f;

        // Box background
        bool focused = s_inputFocused;
        SolidBrush inputBg(Color(255, 38, 40, 58));
        Pen inputBrd(focused ? Color(255,0,120,215) : Color(255,65,67,90), focused?2.f:1.f);
        RdRoundRect(g, inputBg, &inputBrd, rx, ry, inputW, inputH, 6);
        s_rcInput = RectF(rx, ry, inputW, inputH);

        // Input text or placeholder
        Font fInput(&ff, 18, FontStyleRegular, UnitPixel);
        if(s_inputId.empty()) {
            SolidBrush gray(Color(255, 90, 92, 120));
            // Show current target or placeholder
            wstring placeholder = g_rdState==RdState::Connected ?
                FormatId(g_rdPhoneIp) : L"Enter ID";
            g.DrawString(placeholder.c_str(), -1, &fInput,
                         RectF(rx+12, ry, inputW-24, inputH), &fmtL, &gray);
        } else {
            SolidBrush white(Color(255, 230, 232, 245));
            g.DrawString(FormatId(s_inputId).c_str(), -1, &fInput,
                         RectF(rx+12, ry, inputW-24, inputH), &fmtL, &white);
        }

        // Cursor blink if focused
        if(focused && (GetTickCount()/500)%2==0) {
            SolidBrush cursor(Color(255, 0, 120, 215));
            // Measure text width
            int n = (int)s_inputId.size();
            float cx2 = rx + 12 + n * 10.5f;
            g.FillRectangle(&cursor, cx2, ry+10.f, 2.f, inputH-20.f);
        }

        // Connect button (blue)
        float bx = rx + inputW + 8;
        float bw = rw - inputW - 8;
        bool hovConn = s_hovConnect;
        SolidBrush connBg(hovConn ? Color(255,0,100,190) : Color(255,0,120,215));
        RdRoundRect(g, connBg, nullptr, bx, ry, bw-32, inputH, 6);
        s_rcConnect = RectF(bx, ry, bw-32, inputH);

        Font fConn(&ff, 13, FontStyleBold, UnitPixel);
        SolidBrush white2(Color(255,255,255,255));
        g.DrawString(L"Connect", -1, &fConn, RectF(bx, ry, bw-32, inputH), &fmtC, &white2);

        // Dropdown arrow
        SolidBrush dropBg(Color(255, 50, 52, 75));
        Pen dropBrd(Color(255,65,67,90),1.f);
        RdRoundRect(g, dropBg, &dropBrd, bx+bw-30, ry, 24, inputH, 6);
        Font fArrow(&ff, 10, FontStyleBold, UnitPixel);
        SolidBrush grayArr(Color(255,180,182,200));
        g.DrawString(L"▾", -1, &fArrow, RectF(bx+bw-30, ry, 24, inputH), &fmtC, &grayArr);
    }
    ry += 60;

    // Connected status line (if connected)
    if(g_rdState == RdState::Connected) {
        Font fStat(&ff, 10, FontStyleRegular, UnitPixel);
        SolidBrush green(Color(255, 50, 205, 100));
        wstring connected = L"● Connected to " + ToWStr(g_rdPhoneName) +
                            L" (" + to_wstring(g_rdFps) + L" fps)";
        g.DrawString(connected.c_str(), -1, &fStat, RectF(rx, ry, rw, 18), &fmtL, &green);
    }

    // ── BOTTOM: Tabs + Recent connections ────────────────────────
    float gridY = y + topH + 1;
    float gridH = h - topH - 1;

    // Separator line
    SolidBrush sepBr(Color(255, 40, 42, 60));
    g.FillRectangle(&sepBr, x, y+topH, w, 1.f);

    // Tab bar (Recent / Favorites / Discovery / Address / Group)
    float tabY  = gridY + 6;
    float tabH2 = 32.f;

    struct TabItem { const wchar_t* icon; bool active; };
    TabItem tabs[] = {
        {L"🕐", true},   // Recent
        {L"★", false},   // Favorites
        {L"✦", false},   // Discovery
        {L"👤", false},  // Address
        {L"⊞", false},   // Group
    };

    float tabX2 = x + pad;
    for(auto& t : tabs) {
        Font fTab(&ff, 14, FontStyleRegular, UnitPixel);
        SolidBrush tabC(t.active ? Color(255,0,120,215) : Color(255,110,112,140));
        g.DrawString(t.icon, -1, &fTab, RectF(tabX2, tabY, 28, tabH2), &fmtC, &tabC);
        if(t.active) {
            // Underline
            SolidBrush ul(Color(255,0,120,215));
            g.FillRectangle(&ul, tabX2, tabY+tabH2-2.f, 28.f, 2.f);
        }
        tabX2 += 36;
    }

    // Search + view toggle on right
    {
        Font fSearch(&ff, 14, FontStyleRegular, UnitPixel);
        SolidBrush gray(Color(255,110,112,140));
        g.DrawString(L"🔍", -1, &fSearch, RectF(x+w-100, tabY, 28, tabH2), &fmtC, &gray);
        g.DrawString(L"☑", -1, &fSearch, RectF(x+w-68, tabY, 28, tabH2), &fmtC, &gray);
        g.DrawString(L"⊞", -1, &fSearch, RectF(x+w-36, tabY, 28, tabH2), &fmtC, &gray);
    }

    // ── Recent connection CARDS ───────────────────────────────────
    float cardsY = tabY + tabH2 + 10;
    float cardW  = 180.f;
    float cardH  = 130.f;
    float cardGap = 12.f;
    float cardX  = x + pad;

    // Status bar at bottom (like RustDesk green dot)
    float statusBarH = 28.f;
    float statusBarY = y + h - statusBarH;
    SolidBrush statusBg(Color(255, 22, 24, 35));
    g.FillRectangle(&statusBg, x, statusBarY, w, statusBarH);
    // Green dot
    SolidBrush greenDot(Color(255, 50, 200, 90));
    g.FillEllipse(&greenDot, x+pad, statusBarY+10.f, 8.f, 8.f);
    Font fStatus(&ff, 10, FontStyleRegular, UnitPixel);
    SolidBrush grayStatus(Color(255, 130, 132, 155));
    wstring statusLine = ToWStr(g_rdStatusMsg);
    if(statusLine.empty()) statusLine = L"Ready. For faster connection, please set up your own server";
    g.DrawString(statusLine.c_str(), -1, &fStatus,
                 RectF(x+pad+14, statusBarY, w-pad*2-14, statusBarH), &fmtL, &grayStatus);

    // Compute available card area
    float cardsMaxH = statusBarY - cardsY - 8;

    // Draw cards (recent connections)
    int cardIdx = 0;
    for(auto& rc : s_recent) {
        if(cardX + cardW > x + w - pad) break; // no overflow

        bool hov = (s_hovCard == cardIdx);
        SolidBrush cardBg(rc.platform=="windows" ?
            (hov ? Color(255,78,65,148) : Color(255,68,55,138)) :
            (hov ? Color(255,35,60,90) : Color(255,28,52,80)));

        RdRoundRect(g, cardBg, nullptr, cardX, cardsY, cardW, cardH, 10);
        s_rcCards.push_back(RectF(cardX, cardsY, cardW, cardH));

        // Platform icon
        Font fIcon(&ff, 36, FontStyleRegular, UnitPixel);
        SolidBrush iconBr(Color(255,255,255,255));
        const wchar_t* platformIcon = (rc.platform=="windows") ? L"⊞" : L"🤖";
        g.DrawString(platformIcon, -1, &fIcon,
                     RectF(cardX, cardsY+20, cardW, 52), &fmtC, &iconBr);

        // Device name
        Font fName(&ff, 10, FontStyleRegular, UnitPixel);
        SolidBrush nameC(Color(255,210,212,230));
        wstring nameW = ToWStr(rc.name);
        if(nameW.size()>18) nameW = nameW.substr(0,17)+L"…";
        g.DrawString(nameW.c_str(), -1, &fName,
                     RectF(cardX+8, cardsY+cardH-44, cardW-16, 18), &fmtC, &nameC);

        // ID / IP
        Font fCardId(&ff, 11, FontStyleBold, UnitPixel);
        SolidBrush idC(Color(255,160,162,190));
        // Orange dot for offline, green for online
        bool online = (rc.id == g_rdPhoneIp && g_rdState==RdState::Connected);
        SolidBrush dotC(online ? Color(255,50,205,100) : Color(255,255,140,0));
        g.FillEllipse(&dotC, cardX+10.f, cardsY+cardH-22.f, 8.f, 8.f);
        g.DrawString(FormatId(rc.id).c_str(), -1, &fCardId,
                     RectF(cardX+22, cardsY+cardH-24, cardW-54, 18), &fmtL, &idC);

        // 3-dot menu
        Font fDot2(&ff, 14, FontStyleBold, UnitPixel);
        SolidBrush grayD(Color(255,160,162,190));
        g.DrawString(L"⋮", -1, &fDot2,
                     RectF(cardX+cardW-28, cardsY+cardH-26, 22, 22), &fmtC, &grayD);

        cardX += cardW + cardGap;
        cardIdx++;
    }

    // If no recent, show empty state
    if(s_recent.empty()) {
        Font fEmpty(&ff, 11, FontStyleRegular, UnitPixel);
        SolidBrush gray(Color(255,90,92,120));
        g.DrawString(L"No recent connections",
                     -1, &fEmpty, RectF(x, cardsY, w, 40), &fmtC, &gray);
    }

    // ── If connected: overlay disconnect button ───────────────────
    if(g_rdState == RdState::Connected) {
        float bW=140, bH=36;
        float bX = x+w-bW-pad, bY2 = y+topH+tabH2+20;
        bool hovStop = s_hovStop;
        SolidBrush stopBg(hovStop?Color(255,180,30,30):Color(255,140,20,20));
        Pen stopBrd(Color(255,200,50,50),1.f);
        RdRoundRect(g,stopBg,&stopBrd,bX,bY2,bW,bH,8);
        Font fBtn(&ff,12,FontStyleBold,UnitPixel);
        SolidBrush wh(Color(255,255,255,255));
        g.DrawString(L"Disconnect",-1,&fBtn,RectF(bX,bY2,bW,bH),&fmtC,&wh);
        s_rcStop = RectF(bX,bY2,bW,bH);
    }
}

// ── Mouse hover tracking ──────────────────────────────────────────
void ProcessPhoneRemoteMouseMove(float mx, float my, float, float){
    s_hovConnect = s_rcConnect.Contains(mx,my);
    s_hovInput   = s_rcInput.Contains(mx,my);
    s_hovStop    = s_rcStop.Contains(mx,my);
    s_hovCard    = -1;
    for(int i=0;i<(int)s_rcCards.size();i++){
        if(s_rcCards[i].Contains(mx,my)){ s_hovCard=i; break; }
    }
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

// ── Mouse click ───────────────────────────────────────────────────
void ProcessPhoneRemoteMouseClick(float mx, float my, float, float, HWND hWnd){
    // Input box — focus
    if(s_rcInput.Contains(mx,my)){
        s_inputFocused = true;
        InvalidateRect(hWnd,NULL,FALSE);
        return;
    } else {
        s_inputFocused = false;
    }

    // Connect button
    if(s_rcConnect.Contains(mx,my)){
        if(!s_inputId.empty() && (g_rdState==RdState::Idle||g_rdState==RdState::WaitPhone)) {
            // Connect to remote PC/phone by ID
            // TODO: Firestore lookup → connect
            g_rdStatusMsg = "Connecting to " + s_inputId + "...";
        }
        InvalidateRect(hWnd,NULL,FALSE);
        return;
    }

    // Disconnect
    if(s_rcStop.Contains(mx,my)){
        RdStopServer();
        RdGenerateCode();
        InvalidateRect(hWnd,NULL,FALSE);
        return;
    }

    // Recent card click
    for(int i=0;i<(int)s_rcCards.size();i++){
        if(s_rcCards[i].Contains(mx,my)){
            s_inputId = s_recent[i].id;
            s_inputFocused = false;
            InvalidateRect(hWnd,NULL,FALSE);
            return;
        }
    }
}

extern "C" void PhoneRemoteChar(wchar_t ch){
    if(!s_inputFocused) return;
    if(ch=='\b'){
        if(!s_inputId.empty()) s_inputId.pop_back();
    } else if(ch>=L'0'&&ch<=L'9'){
        if(s_inputId.size()<9) {
            char c=(char)ch;
            s_inputId.push_back(c);
        }
    }
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}
