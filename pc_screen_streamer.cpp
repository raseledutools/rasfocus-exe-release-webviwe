// ================================================================
// pc_screen_streamer.cpp  —  PC screen → Phone (H264 WebSocket)
//
// Architecture:
//   GDI+ BitBlt (30fps) → RGB bitmap
//   → H264 encode via Media Foundation Transform (MFT)
//   → WebSocket server (port 9225) binary broadcast
//   → phone MediaCodec decoder → SurfaceView
//
// Phone → PC input: JSON {type,mask,x,y} / {type,vk,action}
//   → Windows SendInput()
//
// Uses only Windows built-in APIs (no external DLL needed).
// Inspired by RustDesk open source (MIT License)
// ================================================================

#pragma warning(disable: 4996)
#pragma warning(disable: 4244)

#include <winsock2.h>
#include <ws2tcpip.h>
#include "pc_screen_streamer.h"
#include "tab_phone_remote.h"  // WsSendText, WsRecvFrame helpers
#include "tab_phone_remote_relay.h"  // RelaySendBinary

#include <windows.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfidl.h>
#include <mferror.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <wincrypt.h>
#include <shellapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "strmiids.lib")
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

using namespace std;
using namespace Gdiplus;

// ── Globals ───────────────────────────────────────────────────────
bool g_pcStreamerRunning = false;
int  g_pcStreamerClients = 0;
int  g_pcStreamerFps     = 0;

static const int  WS_PORT   = 9225;
static const int  TARGET_FPS = 30;
static const int  TARGET_BPS = 3'000'000;  // 3 Mbps

static atomic<bool>   s_active { false };
static SOCKET         s_listenSock = INVALID_SOCKET;
static vector<SOCKET> s_clients;
static mutex          s_mtx;

// ── SHA1/Base64 for WS handshake ─────────────────────────────────
static string PcB64Enc(const vector<BYTE>& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string o; o.reserve(((d.size()+2)/3)*4);
    for(size_t i=0;i<d.size();i+=3){
        BYTE b0=d[i],b1=(i+1<d.size())?d[i+1]:0,b2=(i+2<d.size())?d[i+2]:0;
        o+=t[b0>>2];o+=t[((b0&3)<<4)|(b1>>4)];
        o+=(i+1<d.size())?t[((b1&0xF)<<2)|(b2>>6)]:'=';
        o+=(i+2<d.size())?t[b2&0x3F]:'=';
    }
    return o;
}
static string PcSha1B64(const string& s){
    HCRYPTPROV hp=0;HCRYPTHASH hh=0;
    CryptAcquireContextA(&hp,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hp,CALG_SHA1,0,0,&hh);
    CryptHashData(hh,(const BYTE*)s.c_str(),(DWORD)s.size(),0);
    BYTE hash[20];DWORD hl=20;
    CryptGetHashParam(hh,HP_HASHVAL,hash,&hl,0);
    CryptDestroyHash(hh);CryptReleaseContext(hp,0);
    return PcB64Enc(vector<BYTE>(hash,hash+20));
}

// ── extern: auth socket from tab_phone_remote.cpp ────────────────
// H264 frames go to this socket (port 9224 auth connection) AND
// to any 9225 clients — single connection handles everything.
extern SOCKET s_clientSock;
static mutex  s_mainSockMtx;

// ── WebSocket send binary (server — no masking needed) ───────────
static void WsBroadcastBin(const BYTE* data, size_t len) {
    vector<BYTE> frame;
    frame.push_back(0x82); // FIN + binary opcode
    if(len<126)      frame.push_back((BYTE)len);
    else if(len<65536){frame.push_back(126);frame.push_back((BYTE)(len>>8));frame.push_back((BYTE)(len&0xFF));}
    else{frame.push_back(127);for(int i=7;i>=0;i--)frame.push_back((BYTE)((len>>(8*i))&0xFF));}
    frame.insert(frame.end(),data,data+len);

    // Send to main auth socket (port 9224) — this is where Android is listening
    {
        lock_guard<mutex> lk(s_mainSockMtx);
        if(s_clientSock != INVALID_SOCKET) {
            send(s_clientSock, (char*)frame.data(), (int)frame.size(), 0);
        }
    }
    // Also send to any dedicated 9225 clients
    lock_guard<mutex> lk(s_mtx);
    for(SOCKET c : s_clients){
        send(c,(char*)frame.data(),(int)frame.size(),0);
    }
}
static void WsSendText(SOCKET s, const string& txt){
    size_t len=txt.size(); vector<BYTE> f;
    f.push_back(0x81);
    if(len<126) f.push_back((BYTE)len);
    else if(len<65536){f.push_back(126);f.push_back((BYTE)(len>>8));f.push_back((BYTE)(len&0xFF));}
    f.insert(f.end(),txt.begin(),txt.end());
    send(s,(char*)f.data(),(int)f.size(),0);
}

// ── JSON field extract ────────────────────────────────────────────
static string PcJget(const string& j, const string& k) {
    string sk="\""+k+"\""; size_t p=j.find(sk);
    if(p==string::npos) return "";
    p=j.find(':',p+sk.size());if(p==string::npos)return "";
    p++;while(p<j.size()&&(j[p]==' '||j[p]=='\t'))p++;
    if(p>=j.size())return "";
    if(j[p]=='"'){size_t e=j.find('"',p+1);if(e==string::npos)return "";return j.substr(p+1,e-p-1);}
    size_t e=p;while(e<j.size()&&j[e]!=','&&j[e]!='}'&&j[e]!=']')e++;
    string v=j.substr(p,e-p);
    while(!v.empty()&&(v.back()==' '||v.back()=='\r'||v.back()=='\n'))v.pop_back();
    return v;
}

// ── Media Foundation H264 Encoder ────────────────────────────────
class MfH264Encoder {
public:
    IMFTransform* mft   = nullptr;
    int  width=0, height=0;
    bool ready = false;

    bool Init(int w, int h) {
        width=w; height=h;
        MFStartup(MF_VERSION);

        // Find H264 encoder MFT
        IMFActivate** acts=nullptr; UINT32 cnt=0;
        MFT_REGISTER_TYPE_INFO outInfo{MFMediaType_Video, MFVideoFormat_H264};
        if(FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
            MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_LOCALMFT|MFT_ENUM_FLAG_SORTANDFILTER,
            nullptr, &outInfo, &acts, &cnt)) || cnt==0) {
            return false;
        }
        acts[0]->ActivateObject(__uuidof(IMFTransform),(void**)&mft);
        for(UINT32 i=0;i<cnt;i++) acts[i]->Release();
        CoTaskMemFree(acts);

        // Set output type: H264
        IMFMediaType* outType=nullptr;
        MFCreateMediaType(&outType);
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE,    MFVideoFormat_H264);
        MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, w, h);
        MFSetAttributeRatio(outType,MF_MT_FRAME_RATE, TARGET_FPS, 1);
        outType->SetUINT32(MF_MT_AVG_BITRATE, TARGET_BPS);
        outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        // Low-latency (RustDesk approach)
        outType->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
                           eAVEncCommonRateControlMode_CBR);
        outType->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE);
        mft->SetOutputType(0, outType, 0);
        outType->Release();

        // Set input type: RGB32
        IMFMediaType* inType=nullptr;
        MFCreateMediaType(&inType);
        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inType->SetGUID(MF_MT_SUBTYPE,    MFVideoFormat_RGB32);
        MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, w, h);
        MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, TARGET_FPS, 1);
        inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        UINT32 stride = w * 4;
        inType->SetUINT32(MF_MT_DEFAULT_STRIDE, stride);
        mft->SetInputType(0, inType, 0);
        inType->Release();

        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        ready = true;
        return true;
    }

    // Encode one RGB32 frame, returns H264 NAL bytes (may be empty)
    vector<BYTE> EncodeFrame(const BYTE* rgb32, size_t dataSize, LONGLONG pts) {
        if(!ready) return {};

        // Create input sample
        IMFSample* inSample=nullptr; MFCreateSample(&inSample);
        IMFMediaBuffer* inBuf=nullptr;
        MFCreateMemoryBuffer((DWORD)dataSize, &inBuf);
        BYTE* ptr=nullptr; inBuf->Lock(&ptr, nullptr, nullptr);
        memcpy(ptr, rgb32, dataSize);
        inBuf->Unlock();
        inBuf->SetCurrentLength((DWORD)dataSize);
        inSample->AddBuffer(inBuf); inBuf->Release();
        inSample->SetSampleTime(pts);
        inSample->SetSampleDuration(10'000'000LL / TARGET_FPS);
        mft->ProcessInput(0, inSample, 0);
        inSample->Release();

        // Get output
        vector<BYTE> result;
        MFT_OUTPUT_DATA_BUFFER outData{};
        DWORD status=0;
        // Pre-allocate output buffer
        IMFSample* outSample=nullptr; MFCreateSample(&outSample);
        IMFMediaBuffer* outBuf=nullptr;
        MFCreateMemoryBuffer(width*height*4, &outBuf);
        outSample->AddBuffer(outBuf); outBuf->Release();
        outData.pSample=outSample;

        HRESULT hr = mft->ProcessOutput(0, 1, &outData, &status);
        if(SUCCEEDED(hr)) {
            IMFMediaBuffer* buf=nullptr;
            outSample->ConvertToContiguousBuffer(&buf);
            if(buf){
                BYTE* p=nullptr; DWORD len=0;
                buf->Lock(&p,nullptr,&len);
                result.assign(p,p+len);
                buf->Unlock(); buf->Release();
            }
        }
        outSample->Release();
        return result;
    }

    void Shutdown() {
        if(mft){ mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0); mft->Release(); mft=nullptr; }
        MFShutdown();
        ready=false;
    }
};

// ── GDI+ screen capture (RGB32) ──────────────────────────────────
static vector<BYTE> CaptureScreen(int& outW, int& outH) {
    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    // Scale to max 1280 width (RustDesk MAX_DIM)
    float scale = max(1.0f, (float)max(sw,sh)/1280.0f);
    int dw = ((int)(sw/scale)/16)*16;
    int dh = ((int)(sh/scale)/16)*16;
    outW=dw; outH=dh;

    HDC hScr=GetDC(NULL), hMem=CreateCompatibleDC(hScr);
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth=dw; bi.bmiHeader.biHeight=-dh; // top-down
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    BYTE* bits=nullptr;
    HBITMAP hBmp=CreateDIBSection(hMem,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
    HBITMAP hOld=(HBITMAP)SelectObject(hMem,hBmp);
    SetStretchBltMode(hMem,HALFTONE);
    StretchBlt(hMem,0,0,dw,dh,hScr,0,0,sw,sh,SRCCOPY);
    GdiFlush();

    size_t sz=(size_t)dw*dh*4;
    vector<BYTE> data(bits, bits+sz);
    SelectObject(hMem,hOld); DeleteObject(hBmp); DeleteDC(hMem); ReleaseDC(NULL,hScr);
    return data;
}

// ── Input injection (Phone → PC) ─────────────────────────────────
static void InjectPointer(int mask, float rx, float ry) {
    // rx,ry = 0..1 (normalized phone coords)
    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    int px=(int)(rx*sw), py=(int)(ry*sh);
    INPUT inp{};
    // Move mouse
    inp.type=INPUT_MOUSE;
    inp.mi.dx=(LONG)((float)px/sw*65535);
    inp.mi.dy=(LONG)((float)py/sh*65535);
    inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE;
    SendInput(1,&inp,sizeof(INPUT));
    // Click
    if(mask==1){inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTDOWN;SendInput(1,&inp,sizeof(INPUT));}
    else if(mask==2){inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTUP;SendInput(1,&inp,sizeof(INPUT));}
    else if(mask==3){inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_RIGHTDOWN;SendInput(1,&inp,sizeof(INPUT));}
    else if(mask==4){inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_RIGHTUP;SendInput(1,&inp,sizeof(INPUT));}
}
static void InjectKey(int vk, int action) {
    INPUT inp{}; inp.type=INPUT_KEYBOARD;
    inp.ki.wVk=(WORD)vk;
    inp.ki.dwFlags=(action==1)?KEYEVENTF_KEYUP:0;
    SendInput(1,&inp,sizeof(INPUT));
}
static void InjectScroll(float rx, float ry, const string& dir) {
    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    INPUT inp{};
    inp.type=INPUT_MOUSE;
    inp.mi.dx=(LONG)(rx*sw/sw*65535); inp.mi.dy=(LONG)(ry*sh/sh*65535);
    inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_WHEEL;
    inp.mi.mouseData=(dir=="up")?(DWORD)120:(DWORD)(DWORD)(-120);
    SendInput(1,&inp,sizeof(INPUT));
}

// ── WebSocket recv (for phone input messages) ─────────────────────
static bool PcRecvAll(SOCKET s, char* buf, int n){
    int got=0; while(got<n){int r=recv(s,buf+got,n-got,0);if(r<=0)return false;got+=r;}return true;
}
static pair<int,string> PcWsRecvText(SOCKET s){
    BYTE h[2]; if(!PcRecvAll(s,(char*)h,2)) return {-1,""};
    int op=h[0]&0xF; bool masked=(h[1]&0x80)!=0;
    size_t len=h[1]&0x7F;
    if(len==126){BYTE e[2];if(!PcRecvAll(s,(char*)e,2))return{-1,""};len=((size_t)e[0]<<8)|e[1];}
    else if(len==127){BYTE e[8];if(!PcRecvAll(s,(char*)e,8))return{-1,""};
        len=0;for(int i=0;i<8;i++)len=(len<<8)|e[i];}
    BYTE mask[4]={0}; if(masked)if(!PcRecvAll(s,(char*)mask,4))return{-1,""};
    if(len>65536)return{-1,""};
    vector<BYTE> data(len);size_t got=0;
    while(got<len){int r=recv(s,(char*)data.data()+got,(int)(len-got),0);if(r<=0)return{-1,""};got+=r;}
    if(masked)for(size_t i=0;i<len;i++)data[i]^=mask[i%4];
    return {op, string(data.begin(),data.end())};
}

// ── Client handler: send info JSON, then handle input JSON ────────
static void ClientHandler(SOCKET client) {
    {
        lock_guard<mutex> lk(s_mtx);
        s_clients.push_back(client);
        g_pcStreamerClients=(int)s_clients.size();
    }
    // Send info
    char info[256];
    snprintf(info,sizeof(info),
        "{\"type\":\"info\",\"name\":\"RasFocus-PC\","
        "\"width\":%d,\"height\":%d,\"fps\":%d,\"codec\":\"h264\"}",
        GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),TARGET_FPS);
    WsSendText(client, info);

    // Recv input from phone (non-blocking check)
    DWORD tv=100; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
    while(s_active){
        auto [op,msg]=PcWsRecvText(client);
        if(op<0) break; // disconnected
        if(op==8) break; // WS close
        if(op!=1||msg.empty()) continue;
        string type=PcJget(msg,"type");
        if(type=="mouse"||type=="touch"){
            float rx=(float)atof(PcJget(msg,"x").c_str());
            float ry=(float)atof(PcJget(msg,"y").c_str());
            int mask=atoi(PcJget(msg,"mask").c_str());
            InjectPointer(mask,rx,ry);
        } else if(type=="key"){
            InjectKey(atoi(PcJget(msg,"vk").c_str()),atoi(PcJget(msg,"action").c_str()));
        } else if(type=="scroll"){
            float rx=(float)atof(PcJget(msg,"x").c_str());
            float ry=(float)atof(PcJget(msg,"y").c_str());
            InjectScroll(rx,ry,PcJget(msg,"dir"));
        }
    }
    closesocket(client);
    lock_guard<mutex> lk(s_mtx);
    s_clients.erase(remove(s_clients.begin(),s_clients.end(),client),s_clients.end());
    g_pcStreamerClients=(int)s_clients.size();
}

// ── HTTP/WS accept loop ───────────────────────────────────────────
static bool DoServerHandshake(SOCKET client){
    char buf[4096]={}; int r=recv(client,buf,sizeof(buf)-1,0);
    if(r<=0) return false;
    string req(buf,r);
    if(req.find("Upgrade: websocket")==string::npos&&
       req.find("Upgrade: WebSocket")==string::npos) return false;
    size_t kp=req.find("Sec-WebSocket-Key:");
    if(kp==string::npos) return false;
    kp+=18; while(kp<req.size()&&req[kp]==' ')kp++;
    size_t ke=req.find("\r\n",kp);
    string key=req.substr(kp,ke-kp);
    string acc=PcSha1B64(key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    string resp="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Accept: "+acc+"\r\n\r\n";
    send(client,resp.c_str(),(int)resp.size(),0);
    return true;
}

static void AcceptLoop(){
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    s_listenSock=socket(AF_INET,SOCK_STREAM,0);
    int opt=1; setsockopt(s_listenSock,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
    DWORD tv=500; setsockopt(s_listenSock,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
    sockaddr_in addr{}; addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons((u_short)WS_PORT);
    bind(s_listenSock,(sockaddr*)&addr,sizeof(addr));
    listen(s_listenSock,8);
    while(s_active){
        SOCKET cl=accept(s_listenSock,NULL,NULL);
        if(cl==INVALID_SOCKET) continue;
        if(!DoServerHandshake(cl)){closesocket(cl);continue;}
        thread(ClientHandler,cl).detach();
    }
    closesocket(s_listenSock); s_listenSock=INVALID_SOCKET;
    WSACleanup();
}

// ── Capture + encode loop ─────────────────────────────────────────
static void CaptureLoop(){
    MfH264Encoder enc;
    int w=0,h=0;
    // Initial capture to get dims
    auto firstFrame=CaptureScreen(w,h);
    if(!enc.Init(w,h)) return;

    LONGLONG pts=0;
    LONGLONG frameDur=10'000'000LL/TARGET_FPS; // 100ns units
    int fpsCount=0; DWORD fpsTimer=GetTickCount();
    DWORD frameMs=1000/TARGET_FPS;

    while(s_active){
        DWORD t0=GetTickCount();

        // Only capture if clients connected
        if(g_pcStreamerClients>0){
            int fw,fh;
            auto rgb=CaptureScreen(fw,fh);
            auto nal=enc.EncodeFrame(rgb.data(),rgb.size(),pts);
            if(!nal.empty()){
                // Packet: 1 byte flags | 4 byte pts (ms, 4 bytes) | NAL data
                bool isKeyFrame=(nal.size()>4 && nal[4]==0x67); // SPS NAL unit type=7
                bool isConfig=isKeyFrame;
                BYTE flags=(BYTE)((isKeyFrame?1:0)|(isConfig?2:0));
                vector<BYTE> pkt(5+nal.size());
                pkt[0]=flags;
                pkt[1]=(BYTE)((pts>>24)&0xFF);pkt[2]=(BYTE)((pts>>16)&0xFF);
                pkt[3]=(BYTE)((pts>> 8)&0xFF);pkt[4]=(BYTE)((pts    )&0xFF);
                memcpy(pkt.data()+5,nal.data(),nal.size());
                WsBroadcastBin(pkt.data(),pkt.size());
                RelaySendBinary(static_cast<const void*>(pkt.data()), pkt.size());  // relay
                fpsCount++;
            }
            pts+=frameDur;
        }

        DWORD elapsed=GetTickCount()-t0;
        if(elapsed<frameMs) Sleep(frameMs-elapsed);

        DWORD now=GetTickCount();
        if(now-fpsTimer>=1000){ g_pcStreamerFps=fpsCount; fpsCount=0; fpsTimer=now; }
    }
    enc.Shutdown();
}

// ── Firebase signaling: register PC ID + IP ──────────────────────
static string GetLocalIp(){
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    char host[256]={}; gethostname(host,sizeof(host));
    struct addrinfo hints={},*res=nullptr; hints.ai_family=AF_INET;
    if(getaddrinfo(host,"",&hints,&res)==0&&res){
        char ip[INET_ADDRSTRLEN]={};
        inet_ntop(AF_INET,&((sockaddr_in*)res->ai_addr)->sin_addr,ip,sizeof(ip));
        freeaddrinfo(res); return string(ip);
    }
    return "0.0.0.0";
}
static void RegisterOnFirebase(const string& deviceId, const string& ip, int port){
    // Use existing SendFirestoreRequest (defined in main.cpp)
    extern string SendFirestoreRequest(const string&,const string&,const string&);
    string path="/v1/projects/rasfocus-c746d/databases/(default)/documents/rd_devices/"+deviceId+"?updateMask.fieldPaths=id&updateMask.fieldPaths=ip&updateMask.fieldPaths=port&updateMask.fieldPaths=name&updateMask.fieldPaths=platform&updateMask.fieldPaths=ts";
    char buf[512];
    SYSTEMTIME st; GetSystemTimeAsFileTime((FILETIME*)&st);
    LONGLONG ts=(LONGLONG)GetTickCount64();
    snprintf(buf,sizeof(buf),
        "{\"fields\":{"
        "\"id\":{\"stringValue\":\"%s\"},"
        "\"ip\":{\"stringValue\":\"%s\"},"
        "\"port\":{\"integerValue\":\"%d\"},"
        "\"name\":{\"stringValue\":\"RasFocus-PC\"},"
        "\"platform\":{\"stringValue\":\"windows\"},"
        "\"ts\":{\"integerValue\":\"%lld\"}"
        "}}",
        deviceId.c_str(), ip.c_str(), port, ts);
    thread([path,payload=string(buf)](){
        SendFirestoreRequest("PATCH", path, payload);
    }).detach();
}
static void UnregisterFromFirebase(const string& deviceId){
    extern string SendFirestoreRequest(const string&,const string&,const string&);
    string path="/v1/projects/rasfocus-c746d/databases/(default)/documents/rd_devices/"+deviceId;
    thread([path](){SendFirestoreRequest("DELETE",path,"");}).detach();
}

// ── Public API ────────────────────────────────────────────────────
void PcStreamerStart(){
    if(s_active) return;
    s_active=true;
    g_pcStreamerRunning=true;
    g_pcStreamerClients=0;
    thread(AcceptLoop).detach();
    thread(CaptureLoop).detach();
}

void PcStreamerStop(){
    s_active=false;
    g_pcStreamerRunning=false;
    if(s_listenSock!=INVALID_SOCKET){closesocket(s_listenSock);s_listenSock=INVALID_SOCKET;}
    lock_guard<mutex> lk(s_mtx);
    for(SOCKET c:s_clients) closesocket(c);
    s_clients.clear(); g_pcStreamerClients=0;
}
