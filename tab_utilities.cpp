#include "tab_utilities.h"
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <commdlg.h> // For Image Picker

using namespace Gdiplus;
using namespace std;

extern string GetSecretDir(); // From main.cpp

// --- States ---
static bool u_chkPosture = false;
static bool u_hovPosture = false;
static bool u_hovFlashcard = false;
static bool u_hovVision = false;
static bool u_hovExpense = false;

static float u_cx = 0, u_cy = 0;

// ==============================================================
// --- UTILITY 1: Quick Flashcards (With Copy-Paste) ---
// ==============================================================
struct Flashcard { string q, a; };
static vector<Flashcard> fc_items;
static HWND hListFc, hQ, hA;
static int fc_selectedIndex = -1;

void SaveFlashcards() {
    ofstream out(GetSecretDir() + "ras_flashcards.dat", ios::binary);
    out << fc_items.size() << "\n";
    for(auto& fc : fc_items) {
        out << fc.q.length() << "\n" << fc.q << "\n";
        out << fc.a.length() << "\n" << fc.a << "\n";
    } out.close();
}
void LoadFlashcards() {
    fc_items.clear(); ifstream in(GetSecretDir() + "ras_flashcards.dat", ios::binary); if(!in.is_open()) return;
    size_t count=0; in >> count; in.ignore();
    for(size_t i=0; i<count; i++) {
        Flashcard fc; size_t len=0; 
        in >> len; in.ignore(); if(len>0){ char* b=new char[len+1]; in.read(b,len); b[len]='\0'; fc.q=b; delete[] b; } in.ignore();
        in >> len; in.ignore(); if(len>0){ char* b=new char[len+1]; in.read(b,len); b[len]='\0'; fc.a=b; delete[] b; } in.ignore();
        fc_items.push_back(fc);
    }
}
void RefreshFcList() {
    SendMessage(hListFc, LB_RESETCONTENT, 0, 0);
    for(auto& fc : fc_items) {
        string disp = fc.q.substr(0, 25) + "...";
        SendMessage(hListFc, LB_ADDSTRING, 0, (LPARAM)disp.c_str());
    }
}
LRESULT CALLBACK FlashcardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            HFONT hF = CreateFont(16,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,0,0, "Segoe UI");
            hListFc = CreateWindow("LISTBOX", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY, 10,10, 180, 340, hwnd, (HMENU)1, NULL, NULL);
            CreateWindow("STATIC", "Question:", WS_CHILD|WS_VISIBLE, 200, 10, 100, 20, hwnd, NULL, NULL, NULL);
            hQ = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL, 200, 30, 360, 120, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "Answer:", WS_CHILD|WS_VISIBLE, 200, 160, 100, 20, hwnd, NULL, NULL, NULL);
            hA = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL, 200, 180, 360, 120, hwnd, NULL, NULL, NULL);
            CreateWindow("BUTTON", "Save Card", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 200, 315, 120, 35, hwnd, (HMENU)2, NULL, NULL);
            CreateWindow("BUTTON", "Delete Card", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 330, 315, 110, 35, hwnd, (HMENU)3, NULL, NULL);
            CreateWindow("BUTTON", "New Card", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 450, 315, 110, 35, hwnd, (HMENU)4, NULL, NULL);
            EnumChildWindows(hwnd, [](HWND c, LPARAM l)->BOOL{SendMessage(c,WM_SETFONT,l,TRUE);return TRUE;}, (LPARAM)hF);
            LoadFlashcards(); RefreshFcList(); return 0;
        }
        case WM_COMMAND: {
            if(LOWORD(wp)==1 && HIWORD(wp)==LBN_SELCHANGE) {
                fc_selectedIndex = SendMessage(hListFc, LB_GETCURSEL, 0, 0);
                if(fc_selectedIndex != LB_ERR && fc_selectedIndex < fc_items.size()) {
                    SetWindowText(hQ, fc_items[fc_selectedIndex].q.c_str()); SetWindowText(hA, fc_items[fc_selectedIndex].a.c_str());
                }
            } else if(LOWORD(wp)==2) { // Save
                char qB[4096], aB[4096]; GetWindowText(hQ, qB, 4096); GetWindowText(hA, aB, 4096);
                if(fc_selectedIndex != LB_ERR && fc_selectedIndex < fc_items.size()) {
                    fc_items[fc_selectedIndex].q = qB; fc_items[fc_selectedIndex].a = aB;
                } else { fc_items.push_back({qB, aB}); }
                SaveFlashcards(); RefreshFcList(); MessageBox(hwnd, "Card Saved Successfully!", "Success", MB_OK);
            } else if(LOWORD(wp)==3) { // Delete
                if(fc_selectedIndex != LB_ERR && fc_selectedIndex < fc_items.size()) {
                    fc_items.erase(fc_items.begin() + fc_selectedIndex); fc_selectedIndex = -1;
                    SetWindowText(hQ, ""); SetWindowText(hA, ""); SaveFlashcards(); RefreshFcList();
                }
            } else if(LOWORD(wp)==4) { // New
                fc_selectedIndex = -1; SendMessage(hListFc, LB_SETCURSEL, -1, 0); SetWindowText(hQ, ""); SetWindowText(hA, "");
            } break;
        }
        case WM_CLOSE: { ShowWindow(hwnd, SW_HIDE); return 0; }
    } return DefWindowProc(hwnd, msg, wp, lp);
}
void OpenFlashcards() {
    static HWND hFcWnd = NULL;
    if(!hFcWnd) {
        WNDCLASS wc={0}; wc.lpfnWndProc=FlashcardProc; wc.hInstance=GetModuleHandle(NULL); wc.lpszClassName="RasFcClass"; wc.hbrBackground=CreateSolidBrush(RGB(245,245,245)); RegisterClass(&wc);
        hFcWnd = CreateWindowEx(WS_EX_TOOLWINDOW, "RasFcClass", "Quick Flashcards (Copy & Paste Supported)", WS_SYSMENU|WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 595, 400, NULL, NULL, GetModuleHandle(NULL), NULL);
    } ShowWindow(hFcWnd, SW_SHOW); SetForegroundWindow(hFcWnd);
}

// ==============================================================
// --- UTILITY 2: Vision Board ---
// ==============================================================
static string vb_path = "";
void LoadVB() { ifstream in(GetSecretDir()+"ras_vision.dat"); if(in.is_open()) getline(in, vb_path); }
void SaveVB() { ofstream out(GetSecretDir()+"ras_vision.dat"); out<<vb_path; }

LRESULT CALLBACK VisionProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("BUTTON", "Change Picture", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 10, 10, 150, 30, hwnd, (HMENU)1, NULL, NULL);
            LoadVB(); return 0;
        }
        case WM_COMMAND: {
            if(LOWORD(wp)==1) {
                char filename[MAX_PATH] = "";
                OPENFILENAME ofn; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd; ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
                ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
                if(GetOpenFileName(&ofn)) { vb_path = filename; SaveVB(); InvalidateRect(hwnd, NULL, TRUE); }
            } break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            if(!vb_path.empty()) {
                Graphics g(hdc); wstring wpath(vb_path.begin(), vb_path.end());
                Image img(wpath.c_str()); RECT r; GetClientRect(hwnd, &r);
                g.DrawImage(&img, 10, 50, r.right-20, r.bottom-60);
            }
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_CLOSE: { ShowWindow(hwnd, SW_HIDE); return 0; }
    } return DefWindowProc(hwnd, msg, wp, lp);
}
void OpenVisionBoard() {
    static HWND hVbWnd = NULL;
    if(!hVbWnd) {
        WNDCLASS wc={0}; wc.lpfnWndProc=VisionProc; wc.hInstance=GetModuleHandle(NULL); wc.lpszClassName="RasVbClass"; wc.hbrBackground=CreateSolidBrush(RGB(30,30,30)); RegisterClass(&wc);
        hVbWnd = CreateWindowEx(WS_EX_TOOLWINDOW, "RasVbClass", "Vision & Dream Board", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 700, 500, NULL, NULL, GetModuleHandle(NULL), NULL);
    } ShowWindow(hVbWnd, SW_SHOW); SetForegroundWindow(hVbWnd);
}

// ==============================================================
// --- UTILITY 3: Expense Tracker ---
// ==============================================================
struct Expense { int amt; string desc; };
static vector<Expense> ex_items; static HWND hListEx, hAmt, hDesc;
void SaveExp() { ofstream out(GetSecretDir()+"ras_exp.dat"); out<<ex_items.size()<<"\n"; for(auto& e:ex_items) out<<e.amt<<"\n"<<e.desc<<"\n"; }
void LoadExp() { 
    ex_items.clear(); ifstream in(GetSecretDir()+"ras_exp.dat"); if(!in.is_open()) return;
    size_t c=0; in>>c; in.ignore(); for(size_t i=0;i<c;i++) { Expense e; in>>e.amt; in.ignore(); getline(in, e.desc); ex_items.push_back(e); }
}
void RefreshExpList(HWND hwnd) {
    SendMessage(hListEx, LB_RESETCONTENT, 0, 0); int total = 0;
    for(auto& e : ex_items) {
        string s = (e.amt > 0 ? "Income (+) " : "Expense (-) ") + to_string(abs(e.amt)) + " BDT  ::  " + e.desc;
        SendMessage(hListEx, LB_ADDSTRING, 0, (LPARAM)s.c_str()); total += e.amt;
    }
    string tStr = "Total Balance: " + to_string(total) + " BDT"; SetWindowText(GetDlgItem(hwnd, 5), tStr.c_str());
}
LRESULT CALLBACK ExpenseProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            HFONT hF = CreateFont(16,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,0,0, "Segoe UI");
            hListEx = CreateWindow("LISTBOX", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL, 10,10, 460, 200, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "Amount (BDT):", WS_CHILD|WS_VISIBLE, 10, 220, 100, 20, hwnd, NULL, NULL, NULL);
            hAmt = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 110, 220, 100, 25, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "Reason:", WS_CHILD|WS_VISIBLE, 230, 220, 60, 20, hwnd, NULL, NULL, NULL);
            hDesc = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 290, 220, 180, 25, hwnd, NULL, NULL, NULL);
            CreateWindow("BUTTON", "Add Income (+)", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 10, 260, 140, 35, hwnd, (HMENU)1, NULL, NULL);
            CreateWindow("BUTTON", "Add Expense (-)", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 160, 260, 140, 35, hwnd, (HMENU)2, NULL, NULL);
            CreateWindow("BUTTON", "Clear Selected", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 310, 260, 160, 35, hwnd, (HMENU)3, NULL, NULL);
            CreateWindow("STATIC", "Total Balance: 0 BDT", WS_CHILD|WS_VISIBLE, 10, 310, 300, 25, hwnd, (HMENU)5, NULL, NULL);
            EnumChildWindows(hwnd, [](HWND c, LPARAM l)->BOOL{SendMessage(c,WM_SETFONT,l,TRUE);return TRUE;}, (LPARAM)hF);
            LoadExp(); RefreshExpList(hwnd); return 0;
        }
        case WM_COMMAND: {
            if(LOWORD(wp)==1 || LOWORD(wp)==2) {
                char aB[20], dB[256]; GetWindowText(hAmt, aB, 20); GetWindowText(hDesc, dB, 256);
                int val = atoi(aB); if(LOWORD(wp)==2) val = -val;
                if(val != 0) { ex_items.push_back({val, dB}); SaveExp(); RefreshExpList(hwnd); SetWindowText(hAmt,""); SetWindowText(hDesc,""); }
            } else if(LOWORD(wp)==3) {
                int sel = SendMessage(hListEx, LB_GETCURSEL, 0, 0);
                if(sel != LB_ERR) { ex_items.erase(ex_items.begin() + sel); SaveExp(); RefreshExpList(hwnd); }
            } break;
        }
        case WM_CLOSE: { ShowWindow(hwnd, SW_HIDE); return 0; }
    } return DefWindowProc(hwnd, msg, wp, lp);
}
void OpenExpenseTracker() {
    static HWND hExWnd = NULL;
    if(!hExWnd) {
        WNDCLASS wc={0}; wc.lpfnWndProc=ExpenseProc; wc.hInstance=GetModuleHandle(NULL); wc.lpszClassName="RasExClass"; wc.hbrBackground=CreateSolidBrush(RGB(245,245,245)); RegisterClass(&wc);
        hExWnd = CreateWindowEx(WS_EX_TOOLWINDOW, "RasExClass", "Pocket Expense Tracker", WS_SYSMENU|WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 500, 390, NULL, NULL, GetModuleHandle(NULL), NULL);
    } ShowWindow(hExWnd, SW_SHOW); SetForegroundWindow(hExWnd);
}

// ==============================================================
// --- Posture Reminder Thread ---
// ==============================================================
static bool postureThreadRunning = false;
void PostureThread() {
    while (true) {
        Sleep(1000); 
        if (u_chkPosture) {
            static DWORD lastTickP = GetTickCount();
            if (GetTickCount() - lastTickP >= 2700000) { // 45 minutes
                lastTickP = GetTickCount();
                MessageBoxW(NULL, L"Time for a break! Drink 1 glass of water and sit up straight.", L"Health Reminder", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            }
        }
    }
}

void InitUtilities() {
    if (!postureThreadRunning) { thread t(PostureThread); t.detach(); postureThreadRunning = true; }
}

// ==============================================================
// --- Tab Drawing & Events ---
// ==============================================================
static void FillSimpleRect(Graphics& g, SolidBrush* br, Pen* pen, float x, float y, float rw, float rh) {
    if (br) g.FillRectangle(br, x, y, rw, rh);
    if (pen) g.DrawRectangle(pen, x, y, rw, rh);
}

void DrawUtilitiesTab(Graphics& g, float cx, float cy, float cw, float ch) {
    u_cx = cx; u_cy = cy;
    
    FontFamily ff(L"Segoe UI");
    Font fH2(&ff, 15, FontStyleBold, UnitPixel); Font fSub(&ff, 13, FontStyleRegular, UnitPixel);
    Font fBtn(&ff, 13, FontStyleBold, UnitPixel);
    
    SolidBrush bBg(Color(255, 248, 250, 252)); SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bDark(Color(255, 50, 50, 50)); SolidBrush bGray(Color(255, 120, 120, 120));
    SolidBrush bPurple(Color(255, 155, 89, 182)); SolidBrush bPurpleHov(Color(255, 142, 68, 173));
    Pen pBrd(Color(255, 220, 225, 230), 1.5f);
    
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    g.FillRectangle(&bBg, cx, cy, cw, ch);

    float cardX = cx + 30.0f; float cardW = cw - 60.0f; float cardH = 65.0f; float gapY = 10.0f;

    auto DrawRow = [&](float y, wstring t, wstring d, wstring btnT, bool isHov, bool isAct) {
        g.DrawLine(&pBrd, cardX, y + cardH, cardX + cardW, y + cardH);
        g.DrawString(t.c_str(), -1, &fH2, RectF(cardX, y + 8.0f, cardW - 200.0f, 20.0f), &fL, &bDark);
        g.DrawString(d.c_str(), -1, &fSub, RectF(cardX, y + 28.0f, cardW - 200.0f, 20.0f), &fL, &bGray);
        
        float aW = 140.0f, aH = 35.0f; float aX = cardX + cardW - aW, aY = y + (cardH - aH) / 2.0f;
        SolidBrush* cBg = isAct ? (isHov ? &bPurpleHov : &bPurple) : (isHov ? &bPurpleHov : &bPurple);
        
        FillSimpleRect(g, cBg, NULL, aX, aY, aW, aH);
        g.DrawString(btnT.c_str(), -1, &fBtn, RectF(aX, aY, aW, aH), &fC, &bWhite);
    };

    DrawRow(cy + 10.0f, L"Quick Flashcards", L"Save Q&A. Text is completely copy-pasteable.", L"Open Cards", u_hovFlashcard, false);
    DrawRow(cy + 10.0f + (cardH+gapY)*1, L"Vision & Dream Board", L"Pin a picture of your ultimate goal.", L"Open Board", u_hovVision, false);
    DrawRow(cy + 10.0f + (cardH+gapY)*2, L"Pocket Expense Tracker", L"Track allowances and study expenses.", L"Open Tracker", u_hovExpense, false);
    
    wstring psTxt = u_chkPosture ? L"Disable Reminder" : L"Enable Reminder";
    DrawRow(cy + 10.0f + (cardH+gapY)*3, L"Posture & Hydration", L"Reminds you to sit straight every 45 mins.", psTxt, u_hovPosture, u_chkPosture);
}

void ProcessUtilitiesMouseMove(float x, float y) {
    u_hovFlashcard = false; u_hovVision = false; u_hovExpense = false; u_hovPosture = false;

    float cardX = u_cx + 30.0f, cardW = 804.0f - 60.0f; // assuming approx cw=804
    float aW = 140.0f, aH = 35.0f, aX = cardX + cardW - aW;
    float cH = 65.0f; float gap = 10.0f;

    auto Hit = [&](int idx) { float rY = u_cy+10.0f + (cH+gap)*idx; return (x>=aX && x<=aX+aW && y>=rY+(cH-aH)/2.0f && y<=rY+(cH-aH)/2.0f+aH); };

    if(Hit(0)) u_hovFlashcard = true;
    if(Hit(1)) u_hovVision = true;
    if(Hit(2)) u_hovExpense = true;
    if(Hit(3)) u_hovPosture = true;
}

void ProcessUtilitiesMouseClick(float x, float y) {
    if(u_hovFlashcard) OpenFlashcards();
    if(u_hovVision) OpenVisionBoard();
    if(u_hovExpense) OpenExpenseTracker();
    if(u_hovPosture) {
        u_chkPosture = !u_chkPosture; 
        if(u_chkPosture) MessageBoxW(NULL, L"Posture Reminder enabled! Alerts every 45 mins.", L"Health", MB_OK | MB_ICONINFORMATION);
    }
}