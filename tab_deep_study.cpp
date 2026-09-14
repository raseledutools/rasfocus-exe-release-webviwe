#include "tab_blocks.h"
#include <vector>
#include <fstream>
#include <string>
#include <thread>

// --- Premium Feature Gate ---
extern bool g_isPremiumUser;
extern bool g_showUpgradePopup;
#include <mmsystem.h>
#include <cmath>
#include <algorithm> 
#include <psapi.h>   
#include "resource.h" // <--- NEW: For Wallpapers
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;
using namespace std;

// --- Resolve BlockItem Struct Error ---
#ifndef BLOCKITEM_DEFINED
#define BLOCKITEM_DEFINED
struct BlockItem {
    wstring name;
    bool isHoveredCross;
    bool isSystemLocked;
};
#endif

// --- Sub Tab States for Deep Study ---
static int ds_activeSubTab = 0; // 0 = Pomodoro, 1 = Active Recall, 2 = Spaced Repetition
static bool ds_hovTab1 = false;
static bool ds_hovTab2 = false;
static bool ds_hovTab3 = false;

// --- Deep Study Global States ---
static float ds_cX = 0.0f, ds_cY = 0.0f, ds_cW = 800.0f, ds_cH = 600.0f;

// Pomodoro Settings
static int ds_focusMin = 25, ds_restMin = 5, ds_sessions = 4;
static int ds_currentSession = 1;
static ULONGLONG ds_startTime = 0; 
static bool ds_isFocusMode = false, ds_isBreak = false;
static int ds_hovMinus = 0, ds_hovPlus = 0; 
static bool ds_hovStart = false;
static bool bOneMinPopup = false; 
static bool ds_pendingRestart = false; 

// Toggles
static bool ds_chkSound = false, ds_hovSound = false;
static bool ds_chkFloat = false, ds_hovFloat = false;
static bool ds_chkNet = false, ds_hovNet = false;
static bool ds_chkTask = true, ds_hovTask = false;
static bool ds_chkSet = true, ds_hovSet = false;
static bool ds_chkBlockBreak = false, ds_hovBlockBreak = false; 
static bool ds_chkStrict = false, ds_hovStrict = false; 
static bool ds_chkHideBreakClose = false, ds_hovHideBreakClose = false; 

// Sound Dropdown (AWESOME BROWN NOISE VARIATIONS)
static int ds_soundType = 0; 
static bool ds_isSndDropOpen = false, ds_hovSndDrop = false;
static int ds_hovSndOpt = -1;
vector<wstring> ds_sndOpts = { 
    L"White Noise", L"Classic Brown", L"Deep Brown", L"Warm Brown", L"Heavy Rain", 
    L"Waterfall", L"Wind", L"Deep Focus", L"Space Drone", L"Cosmic Brown" 
};

// Allow Lists
static wstring ds_webInp = L"";
static bool ds_isWebAct = false, ds_hovWebInp = false, ds_hovWebAdd = false, ds_hovWebDrop = false, ds_isWebDropOpen = false;
static int ds_hovWebOpt = -1;
static vector<BlockItem> ds_allowWebs;

static wstring ds_appInp = L"";
static bool ds_isAppAct = false, ds_hovAppInp = false, ds_hovAppAdd = false, ds_hovAppDrop = false, ds_isAppDropOpen = false;
static int ds_hovAppOpt = -1;
static vector<BlockItem> ds_allowApps;

extern vector<wstring> commonWebsites;
extern vector<wstring> commonApps;     

// ==========================================
// --- ADVANCED DSP AUDIO SYSTEM ---
// ==========================================
static bool ds_audioPlaying = false;
static bool ds_audioThreadStarted = false;
static const int SAMPLE_RATE = 44100;
static const int BUFFER_SIZE = 8192; 
static const float PI_F = 3.14159265359f;

class NoiseGenerator {
private:
    float lastOut = 0.0f;
public:
    float getWhiteNoise() { return ((float)rand() / RAND_MAX) * 2.0f - 1.0f; }
    float getBrownNoise() {
        float white = getWhiteNoise();
        lastOut = (lastOut + (0.02f * white)) / 1.02f;
        return lastOut * 3.5f;
    }
};

class Oscillator {
private:
    float phase = 0.0f;
    float sampleRate;
public:
    Oscillator(float sr = 44100.0f) : sampleRate(sr) {}
    float processSine(float frequency) {
        float out = std::sin(2.0f * PI_F * phase);
        phase += frequency / sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
    float processTriangle(float frequency) {
        float out = 2.0f * std::abs(2.0f * phase - 1.0f) - 1.0f;
        phase += frequency / sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
};

class BiquadFilter {
private:
    float a0=1, a1=0, a2=0, b1=0, b2=0;
    float z1 = 0.0f, z2 = 0.0f;
    float sampleRate;
public:
    BiquadFilter(float sr = 44100.0f) : sampleRate(sr) {}
    void setLowPass(float freq, float q = 1.0f) {
        float w0 = 2.0f * PI_F * freq / sampleRate;
        float alpha = std::sin(w0) / (2.0f * q);
        float cosW0 = std::cos(w0);
        float a0_inv = 1.0f / (1.0f + alpha);
        a0 = ((1.0f - cosW0) / 2.0f) * a0_inv; a1 = (1.0f - cosW0) * a0_inv; a2 = ((1.0f - cosW0) / 2.0f) * a0_inv;
        b1 = (-2.0f * cosW0) * a0_inv; b2 = (1.0f - alpha) * a0_inv;
    }
    void setHighPass(float freq, float q = 1.0f) {
        float w0 = 2.0f * PI_F * freq / sampleRate;
        float alpha = std::sin(w0) / (2.0f * q);
        float cosW0 = std::cos(w0);
        float a0_inv = 1.0f / (1.0f + alpha);
        a0 = ((1.0f + cosW0) / 2.0f) * a0_inv; a1 = (-(1.0f + cosW0)) * a0_inv; a2 = ((1.0f + cosW0) / 2.0f) * a0_inv;
        b1 = (-2.0f * cosW0) * a0_inv; b2 = (1.0f - alpha) * a0_inv;
    }
    void setBandPass(float freq, float q = 1.0f) {
        float w0 = 2.0f * PI_F * freq / sampleRate;
        float alpha = std::sin(w0) / (2.0f * q);
        float cosW0 = std::cos(w0);
        float a0_inv = 1.0f / (1.0f + alpha);
        a0 = alpha * a0_inv; a1 = 0.0f; a2 = -alpha * a0_inv;
        b1 = (-2.0f * cosW0) * a0_inv; b2 = (1.0f - alpha) * a0_inv;
    }
    float process(float in) {
        float out = in * a0 + z1;
        z1 = in * a1 + z2 - b1 * out;
        z2 = in * a2 - b2 * out;
        return out;
    }
};

static NoiseGenerator dsp_noise;
static BiquadFilter dsp_mainFilter(44100.0f);
static Oscillator dsp_lfo(44100.0f), dsp_osc1(44100.0f), dsp_osc2(44100.0f);

float processSound(int soundType) {
    float output = 0.0f;
    float volume = 0.3f; 

    switch (soundType) {
        case 0: output = dsp_noise.getWhiteNoise(); break;
        case 1: output = dsp_noise.getBrownNoise(); break;
        case 2: { float white = dsp_noise.getWhiteNoise(); static float lastDeep = 0.0f; lastDeep = (lastDeep + (0.01f * white)) / 1.01f; output = lastDeep * 4.0f; } break;
        case 3: { float white = dsp_noise.getWhiteNoise(); static float lastWarm = 0.0f; lastWarm = (lastWarm + (0.05f * white)) / 1.05f; output = lastWarm * 2.0f; } break;
        case 4: dsp_mainFilter.setLowPass(800.0f); output = dsp_mainFilter.process(dsp_noise.getWhiteNoise()); break;
        case 5: dsp_mainFilter.setHighPass(100.0f); output = dsp_mainFilter.process(dsp_noise.getBrownNoise()); break;
        case 6: { float lfoVal = dsp_lfo.processSine(0.1f); dsp_mainFilter.setBandPass(400.0f + (lfoVal * 300.0f), 1.0f); output = dsp_mainFilter.process(dsp_noise.getWhiteNoise()); } break;
        case 7: { dsp_mainFilter.setLowPass(150.0f); float bgNoise = dsp_mainFilter.process(dsp_noise.getBrownNoise()); float beat = (dsp_osc1.processSine(250.0f) + dsp_osc2.processSine(264.0f)) * 0.5f; output = bgNoise + beat * 0.5f; } break;
        case 8: { float pitchMod = dsp_lfo.processSine(0.05f) * 10.0f; float t1 = dsp_osc1.processSine(55.0f); float t2 = dsp_osc2.processTriangle(110.0f + pitchMod); dsp_mainFilter.setLowPass(300.0f); output = dsp_mainFilter.process((t1 + t2) * 0.5f); } break;
        case 9: { float white = dsp_noise.getWhiteNoise(); static float lastCosmic = 0.0f; lastCosmic = (lastCosmic + (0.02f * white)) / 1.02f; float volMod = (std::sin(GetTickCount64() * 0.0002f) * 0.3f + 0.7f); output = lastCosmic * 3.5f * volMod; } break;
    }
    return output * volume;
}

void AudioGenerationThread() {
    WAVEFORMATEX wfx = {0};
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nChannels = 1;
    wfx.cbSize = 0;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = SAMPLE_RATE * 2;

    HWAVEOUT hWaveOut;
    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;

    short* buffer1 = new short[BUFFER_SIZE];
    short* buffer2 = new short[BUFFER_SIZE];
    WAVEHDR header1 = { (LPSTR)buffer1, BUFFER_SIZE * 2, 0, 0, 0, 0, 0, 0 };
    WAVEHDR header2 = { (LPSTR)buffer2, BUFFER_SIZE * 2, 0, 0, 0, 0, 0, 0 };

    waveOutPrepareHeader(hWaveOut, &header1, sizeof(WAVEHDR));
    waveOutPrepareHeader(hWaveOut, &header2, sizeof(WAVEHDR));

    for (int i = 0; i < BUFFER_SIZE; i++) { buffer1[i] = 0; buffer2[i] = 0; }
    waveOutWrite(hWaveOut, &header1, sizeof(WAVEHDR));
    waveOutWrite(hWaveOut, &header2, sizeof(WAVEHDR));

    WAVEHDR* currentHeader = &header1;

    while (true) {
        while (currentHeader->dwFlags & WHDR_INQUEUE) { Sleep(1); }

        short* currentBuffer = (short*)currentHeader->lpData;
        if (!ds_audioPlaying) {
            for (int i = 0; i < BUFFER_SIZE; i++) currentBuffer[i] = 0;
        } else {
            for (int i = 0; i < BUFFER_SIZE; i++) {
                float sample = processSound(ds_soundType);
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                currentBuffer[i] = (short)(sample * 32767.0f);
            }
        }
        
        waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));
        currentHeader = (currentHeader == &header1) ? &header2 : &header1;
    }
}

void PlayGeneratedSound(bool start) {
    if (start && !ds_audioThreadStarted) {
        thread t(AudioGenerationThread); t.detach(); ds_audioThreadStarted = true;
    }
    ds_audioPlaying = start;
}

// --- Helper for Time Calculation ---
void GetTimeLeft(int& outMin, int& outSec, int& outMs) {
    if (!ds_isFocusMode) {
        outMin = ds_focusMin; outSec = 0; outMs = 0; return;
    }
    ULONGLONG totalMs = (ULONGLONG)(ds_isBreak ? ds_restMin : ds_focusMin) * 60000;
    ULONGLONG passedMs = GetTickCount64() - ds_startTime;
    
    if (passedMs >= totalMs) { outMin = 0; outSec = 0; outMs = 0; return; }
    
    ULONGLONG leftMs = totalMs - passedMs;
    outMin = (int)(leftMs / 60000);
    outSec = (int)((leftMs % 60000) / 1000);
    outMs = (int)((leftMs % 1000) / 10); 
}

void SetInternetState(bool block) {
    string cmd = block ? "ipconfig /release" : "ipconfig /renew";
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

// ==========================================
// --- NEW: Load Wallpaper Helper ---
// ==========================================
Image* LoadImageFromResource(int resourceId) {
    HRSRC hResource = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), (LPCWSTR)RT_RCDATA);
    if (!hResource) return nullptr;

    DWORD imageSize = SizeofResource(NULL, hResource);
    HGLOBAL hGlobal = LoadResource(NULL, hResource);
    const void* pResourceData = LockResource(hGlobal);

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (hBuffer) {
        void* pBuffer = GlobalLock(hBuffer);
        memcpy(pBuffer, pResourceData, imageSize);
        GlobalUnlock(hBuffer);

        IStream* pStream = NULL;
        if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
            Image* img = Image::FromStream(pStream);
            pStream->Release();
            return img;
        }
        GlobalFree(hBuffer);
    }
    return nullptr;
}
static Image* ds_currentWallpaper = nullptr;

// ==========================================
// --- Bigger & Better 1 Minute Warning Toast ---
// ==========================================
LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT r; GetClientRect(hwnd, &r);
        
        GraphicsPath path; int d = 20;
        path.AddArc(0, 0, d, d, 180, 90); path.AddArc(r.right-d, 0, d, d, 270, 90);
        path.AddArc(r.right-d, r.bottom-d, d, d, 0, 90); path.AddArc(0, r.bottom-d, d, d, 90, 90); path.CloseFigure();
        
        SolidBrush bg(Color(245, 12, 168, 176)); // Bright Teal Background for visibility
        g.FillPath(&bg, &path);
        Pen border(Color(255, 255, 255, 255), 2.0f); // White border
        g.DrawPath(&border, &path);
        
        FontFamily ff(L"Segoe UI"); Font f(&ff, 22, FontStyleBold, UnitPixel);
        SolidBrush wBr(Color(255, 255, 255, 255));
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"⏳ Just 1 Minute Remaining! Keep Going!", -1, &f, RectF(0,0,(float)r.right,(float)r.bottom), &fmt, &wBr);
        
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_TIMER) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ShowCoolToast() {
    thread([](){
        static bool reg = false;
        if (!reg) {
            WNDCLASSW wc = {0}; wc.lpfnWndProc = ToastWndProc; wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = L"RasToastClass"; RegisterClassW(&wc); reg = true;
        }
        int w = 450, h = 70; // BIGGER TOAST
        int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 
            L"RasToastClass", L"", WS_POPUP, x, 50, w, h, NULL, NULL, NULL, NULL);
        SetLayeredWindowAttributes(hwnd, 0, 240, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE); 
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        
        // Timer increased to 3.5 seconds
        SetTimer(hwnd, 1, 3500, NULL); 
        
        MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        DestroyWindow(hwnd);
    }).detach();
}

// ==========================================
// --- Bigger & Better Session Complete Popup ---
// ==========================================
LRESULT CALLBACK SesCompWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int hover = 0;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT r; GetClientRect(hwnd, &r);
        
        GraphicsPath path; int d = 20;
        path.AddArc(0, 0, d, d, 180, 90); path.AddArc(r.right-d, 0, d, d, 270, 90);
        path.AddArc(r.right-d, r.bottom-d, d, d, 0, 90); path.AddArc(0, r.bottom-d, d, d, 90, 90); path.CloseFigure();
        
        SolidBrush bg(Color(255, 255, 255, 255)); // White background
        g.FillPath(&bg, &path);
        Pen border(Color(255, 12, 168, 176), 3.0f); // Thicker Teal border
        g.DrawPath(&border, &path);
        
        FontFamily ff(L"Segoe UI"); 
        Font fTitle(&ff, 28, FontStyleBold, UnitPixel); 
        Font fSub(&ff, 18, FontStyleRegular, UnitPixel);
        SolidBrush bDark(Color(255, 30, 30, 30));
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        
        g.DrawString(L"🎉 Session Completed Successfully!", -1, &fTitle, RectF(0, 30, (float)r.right, 40), &fmt, &bDark);
        g.DrawString(L"Awesome work! Are you ready to start the next one?", -1, &fSub, RectF(0, 80, (float)r.right, 30), &fmt, &bDark);

        // Yes Button
        SolidBrush bYes(hover == 1 ? Color(255, 30, 185, 195) : Color(255, 12, 168, 176));
        GraphicsPath bp1; bp1.AddArc(70, 140, 10, 10, 180, 90); bp1.AddArc(200-10, 140, 10, 10, 270, 90); bp1.AddArc(200-10, 190-10, 10, 10, 0, 90); bp1.AddArc(70, 190-10, 10, 10, 90, 90); bp1.CloseFigure();
        g.FillPath(&bYes, &bp1);
        SolidBrush bWhite(Color(255, 255, 255, 255));
        g.DrawString(L"Yes, Let's Go!", -1, &fSub, RectF(70, 140, 130, 50), &fmt, &bWhite);

        // No Button
        SolidBrush bNo(hover == 2 ? Color(255, 180, 180, 180) : Color(255, 150, 150, 150));
        GraphicsPath bp2; bp2.AddArc(250, 140, 10, 10, 180, 90); bp2.AddArc(380-10, 140, 10, 10, 270, 90); bp2.AddArc(380-10, 190-10, 10, 10, 0, 90); bp2.AddArc(250, 190-10, 10, 10, 90, 90); bp2.CloseFigure();
        g.FillPath(&bNo, &bp2);
        g.DrawString(L"Take a Rest", -1, &fSub, RectF(250, 140, 130, 50), &fmt, &bWhite);

        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        int x = LOWORD(lParam), y = HIWORD(lParam); int oldH = hover;
        if (x >= 70 && x <= 200 && y >= 140 && y <= 190) hover = 1;
        else if (x >= 250 && x <= 380 && y >= 140 && y <= 190) hover = 2;
        else hover = 0;
        if (oldH != hover) InvalidateRect(hwnd, NULL, FALSE);
    }
    if (msg == WM_LBUTTONDOWN) {
        if (hover == 1) { ds_pendingRestart = true; PostQuitMessage(0); }
        else if (hover == 2) { PostQuitMessage(0); }
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ShowSessionCompletePopup() {
    thread([](){
        static bool reg = false;
        if (!reg) {
            WNDCLASSW wc = {0}; wc.lpfnWndProc = SesCompWndProc; wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = L"RasSesCompClass"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClassW(&wc); reg = true;
        }
        int w = 450, h = 230; // BIGGER POPUP
        int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2; int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 
            L"RasSesCompClass", L"", WS_POPUP, x, y, w, h, NULL, NULL, NULL, NULL);
        SetLayeredWindowAttributes(hwnd, 0, 250, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        
        MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        DestroyWindow(hwnd);
    }).detach();
}

// --- Floating Timer Window ---
static HWND hFloatTmr = NULL;
LRESULT CALLBACK FloatTimerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool hoverClose = false;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_CREATE) { SetTimer(hwnd, 1, 30, NULL); return 0; }
    if (msg == WM_TIMER) { InvalidateRect(hwnd, NULL, FALSE); return 0; }
    if (msg == WM_NCHITTEST) {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt); RECT r; GetClientRect(hwnd, &r);
        if (pt.x >= r.right - 30 && pt.y <= 30) return HTCLIENT; 
        return HTCAPTION; 
    }
    if (msg == WM_MOUSEMOVE) {
        int x = LOWORD(lParam); int y = HIWORD(lParam); RECT r; GetClientRect(hwnd, &r);
        bool old = hoverClose; hoverClose = (x >= r.right - 30 && y <= 30);
        if (old != hoverClose) InvalidateRect(hwnd, NULL, FALSE);
    }
    if (msg == WM_LBUTTONDOWN) {
        if (hoverClose) { ShowWindow(hwnd, SW_MINIMIZE); } 
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); 
        RECT r; GetClientRect(hwnd, &r); int w = r.right, h = r.bottom;
        HDC memDC = CreateCompatibleDC(hdc); HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h); SelectObject(memDC, memBitmap);
        Graphics g(memDC); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        GraphicsPath path; int d = 12;
        path.AddArc(0, 0, d, d, 180, 90); path.AddArc(w - d, 0, d, d, 270, 90);
        path.AddArc(w - d, h - d, d, d, 0, 90); path.AddArc(0, h - d, d, d, 90, 90); path.CloseFigure();
        SolidBrush bg(ds_isBreak ? Color(240, 16, 185, 129) : Color(240, 12, 168, 176)); g.FillPath(&bg, &path); Pen border(Color(255, 255, 255, 255), 1.5f); g.DrawPath(&border, &path);

        FontFamily ff(L"Segoe UI"); Font fTime(&ff, 34, FontStyleBold, UnitPixel); Font fSes(&ff, 14, FontStyleBold, UnitPixel);
        SolidBrush wBr(Color(255,255,255,255)); StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        
        int tMin, tSec, tMs; GetTimeLeft(tMin, tSec, tMs);
        int total = ds_isBreak ? ds_restMin : ds_focusMin; int passed = total - tMin; if (passed < 0) passed = 0;
        wchar_t buf[32]; swprintf(buf, 32, L"%02d:%02d:%02d", tMin, tSec, tMs);
        g.DrawString(buf, -1, &fTime, RectF(0.0f, 5.0f, (float)w, 40.0f), &fmt, &wBr);
        
        wchar_t sesBuf[64]; 
        if (ds_isBreak) swprintf(sesBuf, 64, L"[%d/%d Min] - BREAK TIME!", passed, total);
        else swprintf(sesBuf, 64, L"[%d/%d Min] - Session %d/%d", passed, total, ds_currentSession, ds_sessions);
        g.DrawString(sesBuf, -1, &fSes, RectF(0.0f, 45.0f, (float)w, 25.0f), &fmt, &wBr);
        
        SolidBrush closeBr(hoverClose ? Color(255,255,50,50) : Color(0,0,0,0)); g.FillRectangle(&closeBr, (float)w - 30.0f, 0.0f, 30.0f, 30.0f);
        FontFamily ffIc(L"Segoe MDL2 Assets"); Font fIc(&ffIc, 12, FontStyleRegular, UnitPixel); g.DrawString(L"\xE711", -1, &fIc, RectF((float)w - 30.0f, 0.0f, 30.0f, 30.0f), &fmt, &wBr);
        
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        DeleteObject(memBitmap); DeleteDC(memDC); EndPaint(hwnd, &ps); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ToggleFloatTimer(bool show) {
    if (show) {
        if (!hFloatTmr) {
            WNDCLASS wc = {0}; wc.lpfnWndProc = FloatTimerProc; wc.hInstance = GetModuleHandle(NULL); 
            wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = "RasFloat"; RegisterClass(&wc);
            hFloatTmr = CreateWindowEx(WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TOOLWINDOW, "RasFloat", "Timer", WS_POPUP|WS_MINIMIZEBOX, GetSystemMetrics(SM_CXSCREEN)-260, 50, 240, 85, NULL, NULL, GetModuleHandle(NULL), NULL);
            SetLayeredWindowAttributes(hFloatTmr, 0, 240, LWA_ALPHA);
        }
        ShowWindow(hFloatTmr, SW_RESTORE); ShowWindow(hFloatTmr, SW_SHOW);
    } else if (hFloatTmr) { ShowWindow(hFloatTmr, SW_HIDE); }
}

// --- Break Page With Wallpaper ---
static HWND hBreakScrn = NULL;
LRESULT CALLBACK BreakScrnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool hoverClose = false;
    if (msg == WM_ERASEBKGND) return 1;

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); 
        RECT r; GetClientRect(hwnd, &r); int w = r.right, h = r.bottom;
        
        HDC memDC = CreateCompatibleDC(hdc); 
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h); 
        SelectObject(memDC, memBitmap);
        
        Graphics g(memDC); g.SetSmoothingMode(SmoothingModeAntiAlias);
        
        if (ds_currentWallpaper) {
            g.DrawImage(ds_currentWallpaper, Rect(0, 0, w, h));
        } else {
            SolidBrush bg(Color(255, 30, 41, 59)); 
            g.FillRectangle(&bg, 0.0f, 0.0f, (float)w, (float)h);
        }

        if (ds_currentWallpaper) {
            SolidBrush textBg(Color(160, 0, 0, 0)); 
            g.FillRectangle(&textBg, 0.0f, (float)h/2 - 200.0f, (float)w, 360.0f);
        }
        
        FontFamily ff(L"Segoe UI"); Font f1(&ff, 110, FontStyleBold, UnitPixel); Font f2(&ff, 45, FontStyleRegular, UnitPixel);
        SolidBrush wBr(Color(255,255,255,255)); SolidBrush gBr(Color(255,16,185,129)); 
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        
        g.DrawString(L"TAKE A BREAK!", -1, &f1, RectF(0.0f, (float)h/2 - 180.0f, (float)w, 120.0f), &fmtC, &gBr);
        g.DrawString(L"Breathe deep, rest your eyes, and relax your mind.", -1, &f2, RectF(0.0f, (float)h/2 - 30.0f, (float)w, 60.0f), &fmtC, &wBr);
        
        int tMin, tSec, tMs; GetTimeLeft(tMin, tSec, tMs); 
        wchar_t buf[64]; swprintf(buf, 64, L"%02d:%02d:%02d Remaining", tMin, tSec, tMs);
        g.DrawString(buf, -1, &f2, RectF(0.0f, (float)h/2 + 80.0f, (float)w, 60.0f), &fmtC, &wBr);

        bool hideCloseBtn = (ds_chkStrict && ds_chkHideBreakClose);
        if (!hideCloseBtn) {
            Font fBtn(&ff, 24, FontStyleBold, UnitPixel);
            SolidBrush bBtn(hoverClose ? Color(255, 255, 100, 100) : Color(255, 200, 50, 50));
            float btnX = (float)w/2 - 100.0f; float btnY = (float)h/2 + 200.0f;
            g.FillRectangle(&bBtn, btnX, btnY, 200.0f, 50.0f);
            g.DrawString(L"Close Break", -1, &fBtn, RectF(btnX, btnY, 200.0f, 50.0f), &fmtC, &wBr);
        }
        
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        DeleteObject(memBitmap); DeleteDC(memDC); EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        RECT r; GetClientRect(hwnd, &r);
        float btnX = (float)r.right/2 - 100.0f; float btnY = (float)r.bottom/2 + 200.0f;
        int x = LOWORD(lParam), y = HIWORD(lParam);
        bool old = hoverClose;
        hoverClose = (x >= btnX && x <= btnX + 200.0f && y >= btnY && y <= btnY + 50.0f);
        if (old != hoverClose) InvalidateRect(hwnd, NULL, FALSE);
    }
    if (msg == WM_LBUTTONDOWN) {
        bool hideCloseBtn = (ds_chkStrict && ds_chkHideBreakClose);
        if (!hideCloseBtn && hoverClose) {
            ds_isBreak = false; ds_startTime = GetTickCount64(); ds_currentSession++; 
            ShowWindow(hwnd, SW_HIDE);
            if(ds_currentWallpaper) { delete ds_currentWallpaper; ds_currentWallpaper = nullptr; }
            HWND hMainWnd = FindWindowA("RasFocusCore", "RasFocus Pro");
            if (hMainWnd != NULL) InvalidateRect(hMainWnd, NULL, FALSE);
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ToggleBreakScreen(bool show) {
    if (show) {
        if (!hBreakScrn) {
            WNDCLASS wc = {0}; wc.lpfnWndProc = BreakScrnProc; wc.hInstance = GetModuleHandle(NULL); 
            wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); wc.lpszClassName = "RasBreakScrn"; RegisterClass(&wc);
            hBreakScrn = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "RasBreakScrn", "", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, GetModuleHandle(NULL), NULL);
        }
        
        // Load Random Wallpaper (102 or 103) based on resource.h
        if (ds_currentWallpaper) { delete ds_currentWallpaper; ds_currentWallpaper = nullptr; }
        int randID = 102 + (rand() % 2); 
        ds_currentWallpaper = LoadImageFromResource(randID);

        ShowWindow(hBreakScrn, SW_SHOWMAXIMIZED);
    } else {
        if (hBreakScrn) ShowWindow(hBreakScrn, SW_HIDE);
        if (ds_currentWallpaper) { delete ds_currentWallpaper; ds_currentWallpaper = nullptr; }
    }
}

// --- STRICT MODE BACKGROUND THREAD ---
static bool ds_threadStarted = false;
void DeepStudyBlockerThread() {
    while (true) {
        if (ds_isFocusMode && ds_chkStrict) {
            bool enforce = true;
            if (ds_isBreak && !ds_chkBlockBreak) enforce = false;
            
            if (enforce) {
                HWND hActive = GetForegroundWindow();
                if (hActive && hActive != hFloatTmr && hActive != hBreakScrn) {
                    
                    char cls[256]; GetClassNameA(hActive, cls, 256);
                    string sCls = cls;
                    if (sCls != "RasToastClass" && sCls != "RasSesCompClass" && sCls != "RasFocusCore") {
                        
                        DWORD pid; GetWindowThreadProcessId(hActive, &pid);
                        if (pid != GetCurrentProcessId()) {
                            wchar_t wTitle[512]; GetWindowTextW(hActive, wTitle, 512); wstring title = wTitle; for(auto& c: title) c = towlower(c);
                            if (title.find(L"settings") != wstring::npos || title.find(L"control panel") != wstring::npos || title == L"run") {
                                ShowWindow(hActive, SW_FORCEMINIMIZE); 
                            } else {
                                bool isAllowed = false; HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid); wstring pName = L"";
                                if (hProc) { wchar_t procName[MAX_PATH]; if (GetModuleBaseNameW(hProc, NULL, procName, sizeof(procName)/sizeof(wchar_t))) { pName = procName; for(auto& c: pName) c = towlower(c); } CloseHandle(hProc); }
                                
                                if (pName == L"explorer.exe" || pName == L"svchost.exe" || pName == L"dwm.exe" || pName == L"idle" || pName == L"taskmgr.exe") isAllowed = true;
                                if (!isAllowed) { for (const auto& app : ds_allowApps) { wstring a = app.name; for(auto& c: a) c = towlower(c); if (pName == a || title.find(a) != wstring::npos) { isAllowed = true; break; } } }
                                if (!isAllowed) { for (const auto& web : ds_allowWebs) { wstring w = web.name; for(auto& c: w) c = towlower(c); size_t dotPos = w.find(L"."); wstring core = (dotPos != wstring::npos) ? w.substr(0, dotPos) : w; if (core.length() > 2 && title.find(core) != wstring::npos) { isAllowed = true; break; } } }
                                if (!isAllowed) { ShowWindow(hActive, SW_FORCEMINIMIZE); }
                            }
                        }
                    }
                }
            }
        }
        Sleep(50); 
    }
}

void ResetDeepStudySystem() {
    ds_isFocusMode = false;
    ds_isBreak = false;
    ds_currentSession = 1;
    ds_chkStrict = false; 
    ds_chkNet = false;    
    SetInternetState(false); 
    ToggleFloatTimer(false); 
    ToggleBreakScreen(false); 
    PlayGeneratedSound(false); 
    bOneMinPopup = false; 
}

void RunDeepStudyTick() {
    if (ds_pendingRestart) {
        ds_pendingRestart = false;
        ds_isFocusMode = true;
        ds_startTime = GetTickCount64();
        ds_currentSession = 1;
        ds_isBreak = false;
        bOneMinPopup = false;
        if(ds_chkFloat) ToggleFloatTimer(true);
        if(ds_chkSound) PlayGeneratedSound(true);
        if(ds_chkNet) SetInternetState(true);
        return;
    }

    HWND hMainWnd = FindWindowA("RasFocusCore", "RasFocus Pro");
    if (hMainWnd != NULL && ds_isFocusMode) {
        InvalidateRect(hMainWnd, NULL, FALSE);
    }

    if(!ds_isFocusMode) return;

    int tMin, tSec, tMs; GetTimeLeft(tMin, tSec, tMs);
    
    if (!ds_isBreak && tMin == 1 && tSec == 0 && tMs == 0 && !bOneMinPopup) {
        bOneMinPopup = true;
        ShowCoolToast(); 
    }
    if (tMin > 1) bOneMinPopup = false;

    if (tMin == 0 && tSec == 0 && tMs == 0) {
        if (!ds_isBreak) {
            if (ds_currentSession >= ds_sessions) {
                ResetDeepStudySystem(); 
                ShowSessionCompletePopup(); 
                return;
            } else {
                ds_isBreak = true; ds_startTime = GetTickCount64(); ToggleBreakScreen(true);
            }
        } else {
            ds_isBreak = false; ds_startTime = GetTickCount64(); ds_currentSession++; ToggleBreakScreen(false);
        }
    }
    
    if (ds_isBreak && hBreakScrn) {
        InvalidateRect(hBreakScrn, NULL, FALSE);
    }
}

VOID CALLBACK DeepStudyTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) { RunDeepStudyTick(); }
static UINT_PTR ds_timerId = 0; 

void SaveDeepStudySettings() {
    string dir = "C:\\ProgramData\\SysCache_Ras\\"; CreateDirectoryA(dir.c_str(), NULL); SetFileAttributesA(dir.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    ofstream f(dir + "deep_study.dat");
    if(f.is_open()) {
        f << ds_focusMin << "\n" << ds_restMin << "\n" << ds_sessions << "\n" << ds_soundType << "\n";
        f << ds_chkSound << "\n" << ds_chkFloat << "\n" << ds_chkNet << "\n" << ds_chkTask << "\n" << ds_chkSet << "\n" << ds_chkBlockBreak << "\n" << ds_chkStrict << "\n";
        f << ds_chkHideBreakClose << "\n"; 
        f << ds_allowWebs.size() << "\n"; for(auto& w : ds_allowWebs) { f << string(w.name.begin(), w.name.end()) << "\n"; }
        f << ds_allowApps.size() << "\n"; for(auto& a : ds_allowApps) { f << string(a.name.begin(), a.name.end()) << "\n"; }
        f.close();
    }
}

void LoadDeepStudySettings() {
    string path = "C:\\ProgramData\\SysCache_Ras\\deep_study.dat"; ifstream f(path);
    if(f.is_open()) {
        f >> ds_focusMin >> ds_restMin >> ds_sessions >> ds_soundType;
        f >> ds_chkSound >> ds_chkFloat >> ds_chkNet >> ds_chkTask >> ds_chkSet >> ds_chkBlockBreak >> ds_chkStrict;
        if (!(f >> ds_chkHideBreakClose)) ds_chkHideBreakClose = false; 
        int wSize=0, aSize=0; string temp;
        if (f >> wSize) { ds_allowWebs.clear(); getline(f, temp); for(int i=0; i<wSize; i++) { getline(f, temp); ds_allowWebs.push_back({wstring(temp.begin(), temp.end()), false, false}); } }
        if (f >> aSize) { ds_allowApps.clear(); for(int i=0; i<aSize; i++) { getline(f, temp); ds_allowApps.push_back({wstring(temp.begin(), temp.end()), false, false}); } }
        f.close();
    }
}

static GraphicsPath* GetRRect(RectF rect, int radius) {
    GraphicsPath* p = new GraphicsPath(); float d = radius * 2.0f;
    p->AddArc(rect.X, rect.Y, d, d, 180, 90); p->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
    p->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90); p->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
    p->CloseFigure(); return p;
}

void DrawDeepStudyTab(Graphics& g, float cx, float cy, float cw, float ch) {
    if (!ds_audioThreadStarted) { thread t(AudioGenerationThread); t.detach(); ds_audioThreadStarted = true; }
    if (!ds_timerId) { ds_timerId = SetTimer(NULL, 0, 30, DeepStudyTimerProc); }
    if (!ds_threadStarted) { thread t(DeepStudyBlockerThread); t.detach(); ds_threadStarted = true; }
    
    ds_cX = cx; ds_cY = cy; ds_cW = cw; ds_cH = ch;

    FontFamily ff(L"Segoe UI");
    Font fTabBtn(&ff, 15, FontStyleBold, UnitPixel);
    Font fH2(&ff, 16, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 14, FontStyleRegular, UnitPixel); Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fTimer(&ff, 50, FontStyleBold, UnitPixel);
    FontFamily ffIc(L"Segoe MDL2 Assets"); Font fIc(&ffIc, 14, FontStyleRegular, UnitPixel);
    
    SolidBrush bWhite(Color(255,255,255,255)); SolidBrush bBg(Color(255,248,250,252));
    SolidBrush bTeal(Color(255,12,168,176)); SolidBrush bDark(Color(255,50,50,50)); SolidBrush bGray(Color(255,120,120,120));
    SolidBrush bHovDrop(Color(255,235,248,250)); SolidBrush bHovAdd(Color(255,30,185,195)); SolidBrush bCrossHov(Color(255,239,68,68));
    SolidBrush bBreakTeal(Color(255, 16, 185, 129)); SolidBrush bStartHovFocus(Color(255, 239, 68, 68)); SolidBrush bLockRed(Color(255, 200, 30, 30));
    Pen pBrd(Color(255,220,225,230), 1.5f); Pen pTeal(Color(255,12,168,176), 2.0f);
    
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);

    // ==========================================
    // --- NEW HEADER: Sub-Tab Navigation ---
    // ==========================================
    g.FillRectangle(&bWhite, cx, cy, cw, 60.0f);
    
    float tabW = 200.0f, tabH = 40.0f;
    float tab1X = cx + 20.0f, tab2X = tab1X + tabW + 10.0f, tab3X = tab2X + tabW + 10.0f, tabY = cy + 10.0f;

    // Active colors are Teal, inactive are light grey. 
    SolidBrush bTab1(ds_activeSubTab == 0 ? Color(255, 12, 168, 176) : (ds_hovTab1 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab2(ds_activeSubTab == 1 ? Color(255, 12, 168, 176) : (ds_hovTab2 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab3(ds_activeSubTab == 2 ? Color(255, 12, 168, 176) : (ds_hovTab3 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    
    SolidBrush bT1(ds_activeSubTab == 0 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));
    SolidBrush bT2(ds_activeSubTab == 1 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));
    SolidBrush bT3(ds_activeSubTab == 2 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));

    g.FillRectangle(&bTab1, tab1X, tabY, tabW, tabH); g.DrawString(L"Pomodoro", -1, &fTabBtn, RectF(tab1X, tabY, tabW, tabH), &fC, &bT1);
    g.FillRectangle(&bTab2, tab2X, tabY, tabW, tabH); g.DrawString(L"Active Recall", -1, &fTabBtn, RectF(tab2X, tabY, tabW, tabH), &fC, &bT2);
    g.FillRectangle(&bTab3, tab3X, tabY, tabW, tabH); g.DrawString(L"Spaced Repetition", -1, &fTabBtn, RectF(tab3X, tabY, tabW, tabH), &fC, &bT3);

    // Main Background offset to start right below the new header
    float bY = cy + 60.0f;
    g.FillRectangle(&bBg, cx, bY, cw, ch - 60.0f);

    // ==========================================
    // --- POMODORO CONTENT ---
    // ==========================================
    if (ds_activeSubTab == 0) {
        float bX = cx + 30.0f, boxW = cw - 60.0f;
        RectF tmrR(bX, bY + 15.0f, 300.0f, 95.0f); GraphicsPath* tp = GetRRect(tmrR, 6); g.FillPath(ds_isBreak ? &bBreakTeal : &bTeal, tp); delete tp;
        
        int tMin, tSec, tMs; GetTimeLeft(tMin, tSec, tMs);
        wchar_t tBuf[32]; swprintf(tBuf, 32, L"%02d:%02d:%02d", tMin, tSec, tMs);
        g.DrawString(tBuf, -1, &fTimer, RectF(bX, bY + 15.0f, 300.0f, 60.0f), &fC, &bWhite);
        
        wchar_t sBuf[40]; swprintf(sBuf, 40, ds_isBreak ? L"Break Time!" : L"Session %d of %d", ds_currentSession, ds_sessions);
        g.DrawString(sBuf, -1, &fBold, RectF(bX, bY + 70.0f, 300.0f, 20.0f), &fC, &bWhite);

        RectF startBtn(bX + 50.0f, bY + 120.0f, 200.0f, 35.0f); GraphicsPath* sp = GetRRect(startBtn, 17);
        if (ds_isFocusMode && ds_chkStrict) {
            g.FillPath(&bLockRed, sp); delete sp; g.DrawString(L"STRICT MODE: LOCKED", -1, &fBold, startBtn, &fC, &bWhite);
        } else {
            SolidBrush* pStartBr = ds_isFocusMode ? &bStartHovFocus : (ds_hovStart ? &bHovAdd : &bTeal);
            g.FillPath(pStartBr, sp); delete sp; g.DrawString(ds_isFocusMode ? L"STOP POMODORO" : L"START POMODORO", -1, &fBold, startBtn, &fC, &bWhite);
        }

        float setX = bX; float setY = bY + 175.0f;
        g.DrawString(L"Custom Session Setup", -1, &fH2, RectF(setX, setY, 300.0f, 25.0f), &fL, &bDark);
        auto DrawSet = [&](float y, wstring lbl, int val, int minId, int plusId) {
            g.DrawString(lbl.c_str(), -1, &fNorm, RectF(setX, y, 120.0f, 25.0f), &fL, &bDark);
            RectF mR(setX+120.0f, y, 25.0f, 25.0f), vR(setX+145.0f, y, 35.0f, 25.0f), pR(setX+180.0f, y, 25.0f, 25.0f);
            g.FillRectangle((ds_hovMinus==minId) ? &bHovDrop : &bWhite, mR.X, mR.Y, mR.Width, mR.Height); g.DrawRectangle(&pBrd, mR.X, mR.Y, mR.Width, mR.Height); g.DrawString(L"\xE738", -1, &fIc, mR, &fC, &bDark);
            g.FillRectangle(&bWhite, vR.X, vR.Y, vR.Width, vR.Height); g.DrawRectangle(&pBrd, vR.X, vR.Y, vR.Width, vR.Height); g.DrawString(to_wstring(val).c_str(), -1, &fBold, vR, &fC, &bDark);
            g.FillRectangle((ds_hovPlus==plusId) ? &bHovDrop : &bWhite, pR.X, pR.Y, pR.Width, pR.Height); g.DrawRectangle(&pBrd, pR.X, pR.Y, pR.Width, pR.Height); g.DrawString(L"\xE710", -1, &fIc, pR, &fC, &bDark);
        };
        DrawSet(setY + 30.0f, L"Focus (Minutes):", ds_focusMin, 1, 1);
        DrawSet(setY + 60.0f, L"Rest (Minutes):", ds_restMin, 2, 2);
        DrawSet(setY + 90.0f, L"Total Sessions:", ds_sessions, 3, 3);

        float togX = bX + 420.0f; float togY = bY + 15.0f;
        auto DrawTog = [&](float x, float y, wstring lbl, bool chk, bool hov, bool isStrictBtn = false) {
            SolidBrush* cbBr = chk ? (isStrictBtn ? &bLockRed : &bTeal) : (hov ? &bHovDrop : &bWhite);
            g.FillRectangle(cbBr, x, y, 18.0f, 18.0f); g.DrawRectangle(&pBrd, x, y, 18.0f, 18.0f);
            if(chk) g.DrawString(L"\xE73E", -1, &fIc, RectF(x, y, 18.0f, 18.0f), &fC, &bWhite);
            SolidBrush* txtBr = (isStrictBtn && chk) ? &bLockRed : &bDark; g.DrawString(lbl.c_str(), -1, &fNorm, RectF(x + 25.0f, y-3.0f, 250.0f, 24.0f), &fL, txtBr);
        };
        DrawTog(togX, togY, L"Ambient Sound", ds_chkSound, ds_hovSound);
        RectF sndDrop(togX + 130.0f, togY-4.0f, 130.0f, 24.0f); g.FillRectangle(&bWhite, sndDrop.X, sndDrop.Y, sndDrop.Width, sndDrop.Height); g.DrawRectangle(ds_chkSound ? &pTeal : &pBrd, sndDrop.X, sndDrop.Y, sndDrop.Width, sndDrop.Height);
        g.DrawString(ds_sndOpts[ds_soundType].c_str(), -1, &fNorm, RectF(sndDrop.X+5.0f, sndDrop.Y, 100.0f, 24.0f), &fL, ds_chkSound ? &bDark : &bGray);
        g.DrawString(L"\xE70D", -1, &fIc, RectF(sndDrop.X+105.0f, sndDrop.Y, 20.0f, 24.0f), &fC, ds_chkSound ? &bDark : &bGray);
        DrawTog(togX, togY + 35.0f, L"Floating Stopwatch", ds_chkFloat, ds_hovFloat);
        DrawTog(togX, togY + 70.0f, L"Block Internet", ds_chkNet, ds_hovNet);
        DrawTog(togX, togY + 105.0f, L"Block Settings / AppData", ds_chkSet, ds_hovSet);
        DrawTog(togX, togY + 140.0f, L"Block Task Manager", ds_chkTask, ds_hovTask);
        DrawTog(togX, togY + 175.0f, L"Keep Blocking During Break", ds_chkBlockBreak, ds_hovBlockBreak);
        DrawTog(togX, togY + 210.0f, L"STRICT MODE (Allow List Only)", ds_chkStrict, ds_hovStrict, true);
        
        DrawTog(togX, togY + 245.0f, L"Hide Close Btn in Break", ds_chkHideBreakClose, ds_hovHideBreakClose);

        float listY = bY + 310.0f; g.DrawLine(&pBrd, bX, listY-10.0f, bX + boxW, listY-10.0f); 
        float colW = (boxW - 30.0f) / 2.0f; float rColX = bX + colW + 30.0f;

        auto DrawListSec = [&](float x, wstring title, wstring& inp, bool act, bool hDrop, bool hAdd, vector<BlockItem>& list) {
            g.DrawString(title.c_str(), -1, &fH2, RectF(x, listY, colW, 25.0f), &fL, &bDark);
            RectF iR(x, listY+30.0f, colW-100.0f, 30.0f); GraphicsPath* ip = GetRRect(iR, 4); g.FillPath(&bWhite, ip); g.DrawPath(act?&pTeal:&pBrd, ip); delete ip;
            if(inp.empty() && !act) g.DrawString(L"Type here...", -1, &fNorm, RectF(iR.X+5.0f, iR.Y, iR.Width, iR.Height), &fL, &bGray);
            else {
                g.DrawString(inp.c_str(), -1, &fNorm, RectF(iR.X+5.0f, iR.Y, iR.Width, iR.Height), &fL, &bDark);
                if(act && (GetTickCount()/500)%2==0) { RectF bR; g.MeasureString(inp.c_str(), -1, &fNorm, PointF(0,0), &bR); float cX = inp.empty() ? iR.X+5.0f : iR.X+5.0f+bR.Width; g.FillRectangle(&bDark, cX, iR.Y+6.0f, 1.5f, 18.0f); }
            }
            RectF dR(x+colW-95.0f, listY+30.0f, 25.0f, 30.0f); GraphicsPath* dp = GetRRect(dR, 4); g.FillPath(hDrop ? &bHovDrop : &bWhite, dp); g.DrawPath(&pBrd, dp); delete dp; g.DrawString(L"\xE70D", -1, &fIc, dR, &fC, &bDark);
            RectF aR(x+colW-65.0f, listY+30.0f, 65.0f, 30.0f); GraphicsPath* ap = GetRRect(aR, 4); g.FillPath(hAdd ? &bHovAdd : &bTeal, ap); delete ap; g.DrawString(L"Add", -1, &fBold, aR, &fC, &bWhite);

            RectF lR(x, listY+70.0f, colW, 90.0f); g.FillRectangle(&bWhite, lR.X, lR.Y, lR.Width, lR.Height); g.DrawRectangle(&pBrd, lR.X, lR.Y, lR.Width, lR.Height);
            float iY = lR.Y + 5.0f; int limit = min(3, (int)list.size()); 
            for(int i=0; i<limit; i++) {
                g.DrawString(list[i].name.c_str(), -1, &fNorm, RectF(x+5.0f, iY, colW-30.0f, 22.0f), &fL, &bDark);
                g.DrawString(L"\xE711", -1, &fIc, RectF(x+colW-25.0f, iY, 20.0f, 22.0f), &fC, list[i].isHoveredCross ? &bCrossHov : &bGray); iY += 24.0f;
            }
        };

        DrawListSec(bX, L"Allowed Websites", ds_webInp, ds_isWebAct, ds_hovWebDrop, ds_hovWebAdd, ds_allowWebs);
        DrawListSec(rColX, L"Allowed Apps", ds_appInp, ds_isAppAct, ds_hovAppDrop, ds_hovAppAdd, ds_allowApps);

        if(ds_isSndDropOpen) {
            RectF sD(togX + 130.0f, togY+20.0f, 130.0f, 230.0f); g.FillRectangle(&bWhite, sD.X, sD.Y, sD.Width, sD.Height); g.DrawRectangle(&pBrd, sD.X, sD.Y, sD.Width, sD.Height);
            for(int i=0; i<10; i++) {
                SolidBrush* br = (ds_hovSndOpt == i) ? &bHovDrop : &bWhite; g.FillRectangle(br, sD.X+1.0f, sD.Y+1.0f+(i*23.0f), sD.Width-2.0f, 23.0f); g.DrawString(ds_sndOpts[i].c_str(), -1, &fNorm, RectF(sD.X+5.0f, sD.Y+(i*23.0f), sD.Width, 24.0f), &fL, &bDark);
            }
        }
        if(ds_isWebDropOpen) {
            RectF wD(bX, listY+62.0f, colW-70.0f, 120.0f); g.FillRectangle(&bWhite, wD.X, wD.Y, wD.Width, wD.Height); g.DrawRectangle(&pBrd, wD.X, wD.Y, wD.Width, wD.Height);
            int limit = min(5, (int)commonWebsites.size());
            for(int i=0; i<limit; i++) {
                SolidBrush* br = (ds_hovWebOpt == i) ? &bHovDrop : &bWhite; g.FillRectangle(br, wD.X+1.0f, wD.Y+1.0f+(i*23.0f), wD.Width-2.0f, 23.0f); g.DrawString(commonWebsites[i].c_str(), -1, &fNorm, RectF(wD.X+5.0f, wD.Y+(i*23.0f), wD.Width, 24.0f), &fL, &bDark);
            }
        }
        if(ds_isAppDropOpen) {
            RectF aD(rColX, listY+62.0f, colW-70.0f, 120.0f); g.FillRectangle(&bWhite, aD.X, aD.Y, aD.Width, aD.Height); g.DrawRectangle(&pBrd, aD.X, aD.Y, aD.Width, aD.Height);
            int limit = min(5, (int)commonApps.size());
            for(int i=0; i<limit; i++) {
                SolidBrush* br = (ds_hovAppOpt == i) ? &bHovDrop : &bWhite; g.FillRectangle(br, aD.X+1.0f, aD.Y+1.0f+(i*23.0f), aD.Width-2.0f, 23.0f); g.DrawString(commonApps[i].c_str(), -1, &fNorm, RectF(aD.X+5.0f, aD.Y+(i*23.0f), aD.Width, 24.0f), &fL, &bDark);
            }
        }
    }
    else if (ds_activeSubTab == 1) {
        // ==========================================
        // --- ACTIVE RECALL CONTENT (Blank for now) ---
        // ==========================================
        g.DrawString(L"Active Recall tools will be added here...", -1, &fH2, RectF(cx, bY + 50.0f, cw, 40.0f), &fC, &bGray);
    }
    else if (ds_activeSubTab == 2) {
        // ==========================================
        // --- SPACED REPETITION CONTENT (Blank for now) ---
        // ==========================================
        g.DrawString(L"Spaced Repetition tools will be added here...", -1, &fH2, RectF(cx, bY + 50.0f, cw, 40.0f), &fC, &bGray);
    }
}

void ProcessDeepStudyMouseMove(float x, float y) {
    ds_hovTab1 = false; ds_hovTab2 = false; ds_hovTab3 = false;
    
    // --- Sub-Tab Hitboxes ---
    float tab1X = ds_cX + 20.0f, tab2X = tab1X + 210.0f, tab3X = tab2X + 210.0f, tabY = ds_cY + 10.0f;
    if(x>=tab1X && x<=tab1X+200.0f && y>=tabY && y<=tabY+40.0f) ds_hovTab1 = true;
    if(x>=tab2X && x<=tab2X+200.0f && y>=tabY && y<=tabY+40.0f) ds_hovTab2 = true;
    if(x>=tab3X && x<=tab3X+200.0f && y>=tabY && y<=tabY+40.0f) ds_hovTab3 = true;

    if (ds_activeSubTab == 0) {
        ds_hovStart = false; ds_hovMinus = 0; ds_hovPlus = 0;
        ds_hovSound = false; ds_hovSndDrop = false; ds_hovFloat = false; ds_hovNet = false; ds_hovSet = false; ds_hovTask = false; ds_hovSndOpt = -1;
        ds_hovBlockBreak = false; ds_hovStrict = false; ds_hovHideBreakClose = false;
        ds_hovWebInp = false; ds_hovWebAdd = false; ds_hovWebDrop = false; ds_hovWebOpt = -1;
        ds_hovAppInp = false; ds_hovAppAdd = false; ds_hovAppDrop = false; ds_hovAppOpt = -1;
        
        for(auto& w : ds_allowWebs) w.isHoveredCross = false;
        for(auto& a : ds_allowApps) a.isHoveredCross = false;

        float bX = ds_cX + 30.0f, bY = ds_cY + 60.0f, boxW = ds_cW - 60.0f;
        if (RectF(bX + 50.0f, bY + 120.0f, 200.0f, 35.0f).Contains(x, y)) ds_hovStart = true;
        
        if (ds_isFocusMode && ds_chkStrict && !ds_isBreak) {
            float togX = bX + 420.0f; float togY = bY + 15.0f;
            if(ds_isSndDropOpen) { for(int i=0; i<10; i++) { if(RectF(togX+130.0f, togY+20.0f+(i*23.0f), 130.0f, 23.0f).Contains(x,y)) { ds_hovSndOpt=i; return; } } }
            if(RectF(togX, togY, 120.0f, 20.0f).Contains(x,y)) ds_hovSound=true;
            if(RectF(togX+130.0f, togY-4.0f, 130.0f, 24.0f).Contains(x,y)) ds_hovSndDrop=true;
            if(RectF(togX, togY+35.0f, 180.0f, 20.0f).Contains(x,y)) ds_hovFloat=true;
            return;
        }

        float setX = bX; float setY = bY + 175.0f;
        if(RectF(setX+120.0f, setY+30.0f, 25.0f, 25.0f).Contains(x,y)) ds_hovMinus=1; if(RectF(setX+180.0f, setY+30.0f, 25.0f, 25.0f).Contains(x,y)) ds_hovPlus=1;
        if(RectF(setX+120.0f, setY+60.0f, 25.0f, 25.0f).Contains(x,y)) ds_hovMinus=2; if(RectF(setX+180.0f, setY+60.0f, 25.0f, 25.0f).Contains(x,y)) ds_hovPlus=2;
        if(RectF(setX+120.0f, setY+90.0f, 25.0f, 25.0f).Contains(x,y)) ds_hovMinus=3; if(RectF(setX+180.0f, setY+90.0f, 25.0f, 25.0f).Contains(x,y)) ds_hovPlus=3;

        float togX = bX + 420.0f; float togY = bY + 15.0f;
        if(ds_isSndDropOpen) { for(int i=0; i<10; i++) { if(RectF(togX+130.0f, togY+20.0f+(i*23.0f), 130.0f, 23.0f).Contains(x,y)) { ds_hovSndOpt=i; return; } } }
        if(RectF(togX, togY, 120.0f, 20.0f).Contains(x,y)) ds_hovSound=true;
        if(RectF(togX+130.0f, togY-4.0f, 130.0f, 24.0f).Contains(x,y)) ds_hovSndDrop=true;
        if(RectF(togX, togY+35.0f, 180.0f, 20.0f).Contains(x,y)) ds_hovFloat=true;
        if(RectF(togX, togY+70.0f, 180.0f, 20.0f).Contains(x,y)) ds_hovNet=true;
        if(RectF(togX, togY+105.0f, 180.0f, 20.0f).Contains(x,y)) ds_hovSet=true;
        if(RectF(togX, togY+140.0f, 180.0f, 20.0f).Contains(x,y)) ds_hovTask=true;
        if(RectF(togX, togY+175.0f, 200.0f, 20.0f).Contains(x,y)) ds_hovBlockBreak=true;
        if(RectF(togX, togY+210.0f, 250.0f, 20.0f).Contains(x,y)) ds_hovStrict=true;
        if(RectF(togX, togY+245.0f, 250.0f, 20.0f).Contains(x,y)) ds_hovHideBreakClose=true;

        float listY = bY + 310.0f; float colW = (boxW - 30.0f) / 2.0f; float rColX = bX + colW + 30.0f;
        if(ds_isWebDropOpen) { for(int i=0; i<5; i++) { if(RectF(bX, listY+62.0f+(i*23.0f), colW-70.0f, 23.0f).Contains(x,y)) { ds_hovWebOpt=i; return; } } }
        if(ds_isAppDropOpen) { for(int i=0; i<5; i++) { if(RectF(rColX, listY+62.0f+(i*23.0f), colW-70.0f, 23.0f).Contains(x,y)) { ds_hovAppOpt=i; return; } } }

        if(RectF(bX, listY+30.0f, colW-100.0f, 30.0f).Contains(x,y)) ds_hovWebInp=true;
        if(RectF(bX+colW-95.0f, listY+30.0f, 25.0f, 30.0f).Contains(x,y)) ds_hovWebDrop=true;
        if(RectF(bX+colW-65.0f, listY+30.0f, 65.0f, 30.0f).Contains(x,y)) ds_hovWebAdd=true;

        if(RectF(rColX, listY+30.0f, colW-100.0f, 30.0f).Contains(x,y)) ds_hovAppInp=true;
        if(RectF(rColX+colW-95.0f, listY+30.0f, 25.0f, 30.0f).Contains(x,y)) ds_hovAppDrop=true;
        if(RectF(rColX+colW-65.0f, listY+30.0f, 65.0f, 30.0f).Contains(x,y)) ds_hovAppAdd=true;

        float iY = listY+75.0f;
        for(int i=0; i<min(3,(int)ds_allowWebs.size()); i++) { if(RectF(bX+colW-25.0f, iY, 20.0f, 20.0f).Contains(x,y)) ds_allowWebs[i].isHoveredCross=true; iY+=24.0f; }
        iY = listY+75.0f;
        for(int i=0; i<min(3,(int)ds_allowApps.size()); i++) { if(RectF(rColX+colW-25.0f, iY, 20.0f, 20.0f).Contains(x,y)) ds_allowApps[i].isHoveredCross=true; iY+=24.0f; }
    }
}

void ProcessDeepStudyMouseClick(float x, float y) {
    // Check Tab Navigation Clicks
    if(ds_hovTab1) { ds_activeSubTab = 0; return; }
    if(ds_hovTab2) { ds_activeSubTab = 1; return; }
    if(ds_hovTab3) { ds_activeSubTab = 2; return; }

    if (ds_activeSubTab == 0) {
        if (ds_isFocusMode && ds_chkStrict && !ds_isBreak) {
            if (ds_hovStart) return;
            if (ds_hovWebAdd || ds_hovAppAdd || ds_hovWebDrop || ds_hovAppDrop) return;
        }
        
        if(ds_isSndDropOpen) { if(ds_hovSndOpt!=-1) { ds_soundType=ds_hovSndOpt; if(ds_isFocusMode && ds_chkSound) PlayGeneratedSound(true); } ds_isSndDropOpen=false; return; }
        if(ds_isWebDropOpen) { if(ds_hovWebOpt!=-1) { ds_allowWebs.push_back({commonWebsites[ds_hovWebOpt],false,false}); } ds_isWebDropOpen=false; return; }
        if(ds_isAppDropOpen) { if(ds_hovAppOpt!=-1) { ds_allowApps.push_back({commonApps[ds_hovAppOpt],false,false}); } ds_isAppDropOpen=false; return; }

        ds_isWebAct = ds_hovWebInp; ds_isAppAct = ds_hovAppInp;

        if (ds_hovStart) {
            if (!g_isPremiumUser) { g_showUpgradePopup = true; return; }
            ds_isFocusMode = !ds_isFocusMode;
            if (ds_isFocusMode) {
                ds_startTime = GetTickCount64(); ds_currentSession = 1; ds_isBreak = false;
                bOneMinPopup = false; 
                if(ds_chkFloat) ToggleFloatTimer(true);
                if(ds_chkSound) PlayGeneratedSound(true);
                if(ds_chkNet) SetInternetState(true);
            } else {
                ResetDeepStudySystem();
            }
        }

        if(!ds_isFocusMode) {
            if(ds_hovMinus==1 && ds_focusMin>5) ds_focusMin-=5; if(ds_hovPlus==1 && ds_focusMin<120) ds_focusMin+=5;
            if(ds_hovMinus==2 && ds_restMin>1) ds_restMin-=1; if(ds_hovPlus==2 && ds_restMin<30) ds_restMin+=1;
            if(ds_hovMinus==3 && ds_sessions>1) ds_sessions-=1; if(ds_hovPlus==3 && ds_sessions<10) ds_sessions+=1;
            
            if (!(ds_isFocusMode && ds_chkStrict && !ds_isBreak)) {
                if(ds_hovNet) ds_chkNet = !ds_chkNet;
                if(ds_hovSet) ds_chkSet = !ds_chkSet;
                if(ds_hovTask) ds_chkTask = !ds_chkTask;
                if(ds_hovBlockBreak) ds_chkBlockBreak = !ds_chkBlockBreak;
                if(ds_hovStrict) ds_chkStrict = !ds_chkStrict; 
                if(ds_hovHideBreakClose) ds_chkHideBreakClose = !ds_chkHideBreakClose;
            }
        }

        if(ds_hovSound) { ds_chkSound = !ds_chkSound; if(ds_isFocusMode){ if(ds_chkSound) PlayGeneratedSound(true); else PlayGeneratedSound(false); } }
        if(ds_hovSndDrop && ds_chkSound) ds_isSndDropOpen=true;
        if(ds_hovFloat) { ds_chkFloat = !ds_chkFloat; if(ds_isFocusMode) ToggleFloatTimer(ds_chkFloat); }

        if(ds_hovWebDrop) ds_isWebDropOpen = true; if(ds_hovAppDrop) ds_isAppDropOpen = true;
        if(ds_hovWebAdd && !ds_webInp.empty()) { ds_allowWebs.push_back({ds_webInp, false, false}); ds_webInp = L""; }
        if(ds_hovAppAdd && !ds_appInp.empty()) { ds_allowApps.push_back({ds_appInp, false, false}); ds_appInp = L""; }

        ds_allowWebs.erase(std::remove_if(ds_allowWebs.begin(), ds_allowWebs.end(), [](const BlockItem& w){ return w.isHoveredCross; }), ds_allowWebs.end());
        ds_allowApps.erase(std::remove_if(ds_allowApps.begin(), ds_allowApps.end(), [](const BlockItem& a){ return a.isHoveredCross; }), ds_allowApps.end());
    }
}

void ProcessDeepStudyKeyPress(wchar_t c) {
    if (ds_activeSubTab != 0) return; // Only apply to Pomodoro
    if (ds_isFocusMode && ds_chkStrict) return; 
    if (c>=32 && c<=126) {
        if(ds_isWebAct && ds_webInp.length()<30) ds_webInp += c;
        if(ds_isAppAct && ds_appInp.length()<30) ds_appInp += c;
    }
}

void ProcessDeepStudyKeyDown(WPARAM key) {
    if (ds_activeSubTab != 0) return; // Only apply to Pomodoro
    if (ds_isFocusMode && ds_chkStrict) return; 
    if (key==VK_BACK) {
        if(ds_isWebAct && !ds_webInp.empty()) ds_webInp.pop_back();
        if(ds_isAppAct && !ds_appInp.empty()) ds_appInp.pop_back();
    } else if (key==VK_RETURN) {
        if(ds_isWebAct && !ds_webInp.empty()) { ds_allowWebs.push_back({ds_webInp, false, false}); ds_webInp=L""; }
        if(ds_isAppAct && !ds_appInp.empty()) { ds_allowApps.push_back({ds_appInp, false, false}); ds_appInp=L""; }
    }
}