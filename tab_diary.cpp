// tab_diary.cpp
// Professional Diary — GDI+ native implementation
// Data: %APPDATA%\.rasfocus\diary_entries.json
// Features: Entry list | Add/Edit | Moods | Folders | Search | Delete | Scroll

#include "tab_gemini.h"  // extern declarations (ShowGeminiControls etc.)
#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <ctime>
#include <functional>

#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"shlwapi.lib")

using namespace Gdiplus;
using namespace std;

// ============================================================
// COLOURS
// ============================================================
static const Color Bg        (255, 248, 250, 252);
static const Color Surface   (255, 255, 255, 255);
static const Color Primary   (255,  79, 172, 254);
static const Color PrimaryDk (255,  45, 140, 220);
static const Color Accent    (255, 100, 200, 160);
static const Color Danger    (255, 220,  60,  60);
static const Color TextDark  (255,  30,  40,  60);
static const Color TextGray  (255, 130, 145, 165);
static const Color Divider   (255, 220, 225, 235);
static const Color CardHov   (255, 240, 245, 255);
static const Color TagBg     (255, 229, 242, 255);

// Mood colours
static const Color MoodHappy (255, 255, 200,  50);
static const Color MoodSad   (255,  80, 130, 255);
static const Color MoodAngry (255, 240,  80,  60);
static const Color MoodCalmC (255,  60, 200, 160);
static const Color MoodNeutl (255, 180, 180, 180);

// ============================================================
// HELPERS
// ============================================================
extern string GetSecretDir();
extern HWND   hParentWnd;
extern float  g_scaleFactor;

static wstring S2W(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
static string W2S(const wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static void FillRR(Graphics& g, Brush* b, Pen* p, float x, float y, float w, float h, float r = 8.f) {
    GraphicsPath path;
    path.AddArc(x,       y,       r*2,r*2, 180,90);
    path.AddArc(x+w-r*2,y,       r*2,r*2, 270,90);
    path.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,   0,90);
    path.AddArc(x,       y+h-r*2,r*2,r*2,  90,90);
    path.CloseFigure();
    if (b) g.FillPath(b, &path);
    if (p) g.DrawPath(p, &path);
}

static string CurrentDate() {
    time_t t = time(NULL); struct tm tm; localtime_s(&tm, &t);
    char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d", &tm); return buf;
}
static wstring CurrentDateW() { return S2W(CurrentDate()); }

// ============================================================
// SIMPLE JSON HELPERS  (no external lib)
// ============================================================
// Escape/unescape for JSON string values
static string JsonEsc(const string& s) {
    string o; o.reserve(s.size() + 8);
    for (char c : s) {
        if      (c == '"')  o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}
static string JsonUnescape(const string& s) {
    string o; bool esc = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (esc) {
            if      (s[i] == '"')  o += '"';
            else if (s[i] == '\\') o += '\\';
            else if (s[i] == 'n')  o += '\n';
            else if (s[i] == 'r')  o += '\r';
            else if (s[i] == 't')  o += '\t';
            else { o += '\\'; o += s[i]; }
            esc = false;
        } else if (s[i] == '\\') { esc = true; }
        else { o += s[i]; }
    }
    return o;
}

// Extract first string value for key from flat JSON object text
static string JGet(const string& json, const string& key) {
    // Handles: "key":"value"  or  "key": "value"
    string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == string::npos) return "";
    p += pat.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':')) p++;
    if (p >= json.size()) return "";
    if (json[p] == '"') {
        p++;
        string val; bool esc = false;
        while (p < json.size()) {
            char c = json[p++];
            if (esc) { val += '\\'; val += c; esc = false; }
            else if (c == '\\') esc = true;
            else if (c == '"') break;
            else val += c;
        }
        return JsonUnescape(val);
    }
    // number / bool
    size_t e = p;
    while (e < json.size() && json[e] != ',' && json[e] != '}' && json[e] != ']') e++;
    return json.substr(p, e - p);
}

// ============================================================
// DATA MODEL
// ============================================================
struct DiaryEntry {
    long long  id         = 0;
    string     title;
    string     body;
    string     folder     = "General";
    string     mood;           // "happy","sad","angry","calm","neutral"
    string     tags;           // comma-separated
    string     date;
    long long  timestamp  = 0;

    string Serialize() const {
        return "{\"id\":" + to_string(id)
             + ",\"title\":\"" + JsonEsc(title) + "\""
             + ",\"body\":\"" + JsonEsc(body) + "\""
             + ",\"folder\":\"" + JsonEsc(folder) + "\""
             + ",\"mood\":\"" + JsonEsc(mood) + "\""
             + ",\"tags\":\"" + JsonEsc(tags) + "\""
             + ",\"date\":\"" + JsonEsc(date) + "\""
             + ",\"timestamp\":" + to_string(timestamp) + "}";
    }
    static DiaryEntry Deserialize(const string& json) {
        DiaryEntry e;
        string idStr = JGet(json, "id");
        if (!idStr.empty()) { try { e.id = stoll(idStr); } catch(...) {} }
        e.title     = JGet(json, "title");
        e.body      = JGet(json, "body");
        e.folder    = JGet(json, "folder");    if (e.folder.empty()) e.folder = "General";
        e.mood      = JGet(json, "mood");
        e.tags      = JGet(json, "tags");
        e.date      = JGet(json, "date");
        string ts   = JGet(json, "timestamp");
        if (!ts.empty()) { try { e.timestamp = stoll(ts); } catch(...) {} }
        return e;
    }
};

// ============================================================
// STORAGE
// ============================================================
static string DiaryFilePath() { return GetSecretDir() + "diary_entries.json"; }

static vector<DiaryEntry> g_entries;
static bool g_loaded = false;

static void LoadEntries() {
    g_entries.clear();
    string path = DiaryFilePath();
    ifstream f(path);
    if (!f.is_open()) { g_loaded = true; return; }
    string content((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
    f.close();

    // Parse array [ {...}, {...}, ... ]
    size_t p = content.find('[');
    if (p == string::npos) { g_loaded = true; return; }
    p++;
    while (p < content.size()) {
        while (p < content.size() && content[p] != '{' && content[p] != ']') p++;
        if (p >= content.size() || content[p] == ']') break;
        // Find matching }
        int depth = 0; size_t start = p;
        for (size_t i = p; i < content.size(); i++) {
            if (content[i] == '{') depth++;
            else if (content[i] == '}') { depth--; if (depth == 0) { p = i + 1; break; } }
        }
        string obj = content.substr(start, p - start);
        if (!obj.empty()) g_entries.push_back(DiaryEntry::Deserialize(obj));
    }
    g_loaded = true;
}

static void SaveEntries() {
    string out = "[\n";
    for (size_t i = 0; i < g_entries.size(); i++) {
        out += "  " + g_entries[i].Serialize();
        if (i + 1 < g_entries.size()) out += ",";
        out += "\n";
    }
    out += "]";
    string path = DiaryFilePath();
    ofstream f(path);
    if (f.is_open()) { f << out; f.close(); }
}

static void UpsertEntry(const DiaryEntry& e) {
    for (auto& x : g_entries) {
        if (x.id == e.id) { x = e; SaveEntries(); return; }
    }
    g_entries.insert(g_entries.begin(), e);
    SaveEntries();
}

static void DeleteEntry(long long id) {
    g_entries.erase(remove_if(g_entries.begin(), g_entries.end(),
        [id](const DiaryEntry& e){ return e.id == id; }), g_entries.end());
    SaveEntries();
}

// ============================================================
// VIEW STATE
// ============================================================
enum class DiaryView { List, Edit };
static DiaryView g_view    = DiaryView::List;

// List state
static int   g_listScroll   = 0;    // pixel offset
static int   g_hovCard      = -1;   // hovered card index in filtered
static bool  g_hovNew       = false;
static bool  g_hovSearch    = false;
static bool  g_hovDelConfirm= false;
static long long g_delId    = -1;   // entry pending delete
static wchar_t g_search[256]= {};
static bool  g_searchFocus  = false;
static bool  g_hovFolderBar = false;
static int   g_selFolder    = 0;    // 0 = All

// Folder list (dynamic + fixed)
static vector<string> g_folders = { "All" };

static void RebuildFolders() {
    g_folders = { "All" };
    for (auto& e : g_entries)
        if (find(g_folders.begin(), g_folders.end(), e.folder) == g_folders.end())
            g_folders.push_back(e.folder);
}

static vector<DiaryEntry*> FilteredEntries() {
    string folderFilter = (g_selFolder > 0 && g_selFolder < (int)g_folders.size())
                           ? g_folders[g_selFolder] : "";
    string srch = W2S(g_search);
    // lowercase srch
    for (auto& c : srch) c = (char)tolower((unsigned char)c);

    vector<DiaryEntry*> out;
    for (auto& e : g_entries) {
        if (!folderFilter.empty() && e.folder != folderFilter) continue;
        if (!srch.empty()) {
            string title = e.title; for (auto& c : title) c = (char)tolower((unsigned char)c);
            string body  = e.body;  for (auto& c : body)  c = (char)tolower((unsigned char)c);
            if (title.find(srch) == string::npos && body.find(srch) == string::npos) continue;
        }
        out.push_back(&e);
    }
    return out;
}

// Edit state
static DiaryEntry g_edit;
static bool  g_editIsNew = true;
static int   g_editFocus = 0; // 0=none,1=title,2=body,3=folder,4=tags
static int   g_bodyScroll= 0;
static bool  g_hovSave   = false;
static bool  g_hovBack   = false;
static bool  g_hovDelBtn = false;
static int   g_hovMood   = -1; // 0-4
static int   g_hovFolder2= -1; // folder chip hover in edit
static bool  g_showFolderInput = false;
static wchar_t g_newFolder[64] = {};
static bool  g_hovAddFolder = false;

// Layout cache
static float g_cx, g_cy, g_cw, g_ch;
static const float TOOLBAR_H = 52.f;
static const float CARD_H    = 84.f;
static const float CARD_GAP  = 8.f;
static const float PAD       = 16.f;

// Mood labels + chars
static const wchar_t* MOOD_ICON[] = { L"😊", L"😢", L"😠", L"😌", L"😐" };
static const wchar_t* MOOD_LBL[]  = { L"Happy", L"Sad", L"Angry", L"Calm", L"Neutral" };
static const char*    MOOD_KEY[]  = { "happy", "sad", "angry", "calm", "neutral" };
static const Color    MOOD_CLR[]  = { MoodHappy, MoodSad, MoodAngry, MoodCalmC, MoodNeutl };

// ============================================================
// DRAW HELPERS
// ============================================================
static void DrawText_(Graphics& g, const wchar_t* text, const Font* font,
                      RectF rect, StringAlignment ha, StringAlignment va, const Brush* br,
                      StringTrimming trim = StringTrimmingEllipsisCharacter) {
    StringFormat sf;
    sf.SetAlignment(ha); sf.SetLineAlignment(va);
    sf.SetTrimming(trim);
    sf.SetFormatFlags(StringFormatFlagsLineLimit);
    g.DrawString(text, -1, font, rect, &sf, br);
}

static Color MoodColor(const string& mood) {
    for (int i = 0; i < 5; i++)
        if (mood == MOOD_KEY[i]) return MOOD_CLR[i];
    return TextGray;
}

// ============================================================
// TOOLBAR (common: back btn in edit, new + search in list)
// ============================================================
static void DrawToolbar(Graphics& g, const FontFamily& ff) {
    Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 13, FontStyleRegular, UnitPixel);
    Font fSm  (&ff, 12, FontStyleRegular, UnitPixel);

    // Toolbar background
    SolidBrush bSurf(Surface);
    g.FillRectangle(&bSurf, g_cx, g_cy, g_cw, TOOLBAR_H);
    Pen pBrd(Divider, 1.f);
    g.DrawLine(&pBrd, g_cx, g_cy + TOOLBAR_H, g_cx + g_cw, g_cy + TOOLBAR_H);

    SolidBrush bDark(TextDark);
    SolidBrush bGray(TextGray);
    SolidBrush bWhite(Color(255,255,255,255));
    SolidBrush bPrim(Primary);
    SolidBrush bPrimHov(PrimaryDk);
    SolidBrush bDanger(Danger);

    if (g_view == DiaryView::List) {
        // Title
        DrawText_(g, L"📔  Personal Diary", &fBold,
                  RectF(g_cx + PAD, g_cy + 2, g_cw * 0.4f, TOOLBAR_H - 4),
                  StringAlignmentNear, StringAlignmentCenter, &bDark);

        // Search box
        float sW = min(260.f, g_cw * 0.35f);
        float sX = g_cx + g_cw - PAD - 110.f - sW - 8.f;
        Pen pSrch(g_searchFocus ? Primary : Divider, g_searchFocus ? 2.f : 1.f);
        FillRR(g, &bSurf, &pSrch, sX, g_cy + 10, sW, 32, 16);
        DrawText_(g, g_search[0] ? g_search : L"🔍  Search entries…", &fNorm,
                  RectF(sX + 10, g_cy + 10, sW - 16, 32),
                  StringAlignmentNear, StringAlignmentCenter,
                  g_search[0] ? &bDark : &bGray);

        // "+ New Entry" button
        float btnX = g_cx + g_cw - PAD - 110.f;
        SolidBrush& bBtn = g_hovNew ? bPrimHov : bPrim;
        FillRR(g, &bBtn, nullptr, btnX, g_cy + 10, 108, 32, 16);
        DrawText_(g, L"+ New Entry", &fSm,
                  RectF(btnX, g_cy + 10, 108, 32),
                  StringAlignmentCenter, StringAlignmentCenter, &bWhite);
    } else {
        // Back button
        SolidBrush bBackBg(g_hovBack ? CardHov : Surface);
        FillRR(g, &bBackBg, nullptr, g_cx + PAD, g_cy + 12, 32, 28, 8);
        FontFamily ffIc(L"Segoe MDL2 Assets");
        Font fIc(&ffIc, 14, FontStyleRegular, UnitPixel);
        DrawText_(g, L"\xE80F", &fIc,
                  RectF(g_cx + PAD, g_cy + 12, 32, 28),
                  StringAlignmentCenter, StringAlignmentCenter, &bDark);

        // Title
        const wchar_t* titleText = g_editIsNew ? L"New Entry" : L"Edit Entry";
        DrawText_(g, titleText, &fBold,
                  RectF(g_cx + PAD + 40, g_cy + 2, g_cw * 0.5f, TOOLBAR_H - 4),
                  StringAlignmentNear, StringAlignmentCenter, &bDark);

        // Save button
        float sX = g_cx + g_cw - PAD - 90;
        SolidBrush& bSav = g_hovSave ? bPrimHov : bPrim;
        FillRR(g, &bSav, nullptr, sX, g_cy + 12, 88, 28, 14);
        DrawText_(g, L"💾  Save", &fSm,
                  RectF(sX, g_cy + 12, 88, 28),
                  StringAlignmentCenter, StringAlignmentCenter, &bWhite);

        // Delete button (only when editing existing)
        if (!g_editIsNew) {
            float dX = sX - 90;
            SolidBrush bDelBg(g_hovDelBtn ? Danger : Color(255,255,235,235));
            Pen pDelBrd(Danger, 1.f);
            FillRR(g, &bDelBg, &pDelBrd, dX, g_cy + 12, 82, 28, 14);
            SolidBrush bDelTxt(g_hovDelBtn ? Color(255,255,255,255) : Danger);
            DrawText_(g, L"🗑 Delete", &fSm,
                      RectF(dX, g_cy + 12, 82, 28),
                      StringAlignmentCenter, StringAlignmentCenter, &bDelTxt);
        }
    }
}

// ============================================================
// FOLDER BAR (list view)
// ============================================================
static vector<RectF> g_folderRects;
static void DrawFolderBar(Graphics& g, const FontFamily& ff) {
    Font fSm(&ff, 12, FontStyleRegular, UnitPixel);
    Font fSmB(&ff, 12, FontStyleBold, UnitPixel);
    g_folderRects.clear();

    float barY = g_cy + TOOLBAR_H;
    SolidBrush bBarBg(Surface);
    g.FillRectangle(&bBarBg, g_cx, barY, g_cw, 38.f);
    Pen pBrd(Divider, 1.f);
    g.DrawLine(&pBrd, g_cx, barY + 38, g_cx + g_cw, barY + 38);

    float x = g_cx + PAD;
    for (int i = 0; i < (int)g_folders.size(); i++) {
        wstring lbl = S2W(g_folders[i]);
        // Measure
        RectF bounds(0, 0, 0, 0);
        g.MeasureString(lbl.c_str(), -1, &fSm, PointF(0, 0), &bounds);
        float w = bounds.Width + 24.f;
        if (x + w > g_cx + g_cw - PAD) break; // too many — truncate

        RectF rect(x, barY + 5, w, 28.f);
        bool active = (g_selFolder == i);
        if (active) {
            SolidBrush bAct(Primary);
            FillRR(g, &bAct, nullptr, rect.X, rect.Y, rect.Width, rect.Height, 14);
            SolidBrush bTxt(Color(255,255,255,255));
            DrawText_(g, lbl.c_str(), &fSmB, rect,
                      StringAlignmentCenter, StringAlignmentCenter, &bTxt);
        } else {
            SolidBrush bChip(TagBg);
            FillRR(g, &bChip, nullptr, rect.X, rect.Y, rect.Width, rect.Height, 14);
            SolidBrush bTxt(Primary);
            DrawText_(g, lbl.c_str(), &fSm, rect,
                      StringAlignmentCenter, StringAlignmentCenter, &bTxt);
        }
        g_folderRects.push_back(rect);
        x += w + 8.f;
    }
}

// ============================================================
// ENTRY CARD (list view)
// ============================================================
static void DrawEntryCard(Graphics& g, const FontFamily& ff,
                          const DiaryEntry& e,
                          float cx, float cy, float cw,
                          bool hovered, bool pendingDel) {
    Font fTitle(&ff, 14, FontStyleBold,    UnitPixel);
    Font fBody (&ff, 12, FontStyleRegular, UnitPixel);
    Font fMeta (&ff, 11, FontStyleRegular, UnitPixel);

    Color bgCol = hovered ? CardHov : Surface;
    if (pendingDel) bgCol = Color(255, 255, 240, 240);
    SolidBrush bBg(bgCol);
    Pen pBrd(hovered ? Primary : Divider, hovered ? 1.5f : 1.f);
    FillRR(g, &bBg, &pBrd, cx, cy, cw, CARD_H - CARD_GAP, 10);

    SolidBrush bDark(TextDark);
    SolidBrush bGray(TextGray);

    float padX = 14.f, padY = 10.f;
    float inner = cw - padX * 2;

    // Mood dot
    if (!e.mood.empty()) {
        Color mc = MoodColor(e.mood);
        SolidBrush bMood(mc);
        g.FillEllipse(&bMood, cx + cw - padX - 10.f, cy + padY + 2, 10.f, 10.f);
    }

    // Title
    wstring title = e.title.empty() ? S2W(e.date) : S2W(e.title);
    DrawText_(g, title.c_str(), &fTitle,
              RectF(cx + padX, cy + padY, inner - 20, 20),
              StringAlignmentNear, StringAlignmentNear, &bDark);

    // Body preview (first line)
    wstring preview = S2W(e.body);
    // truncate newlines for preview
    for (auto& c : preview) if (c == L'\n') c = L' ';
    DrawText_(g, preview.c_str(), &fBody,
              RectF(cx + padX, cy + padY + 22, inner, 18),
              StringAlignmentNear, StringAlignmentNear, &bGray);

    // Date + folder
    wstring meta = S2W(e.date);
    if (!e.folder.empty() && e.folder != "General") meta += L"  •  " + S2W(e.folder);
    DrawText_(g, meta.c_str(), &fMeta,
              RectF(cx + padX, cy + padY + 43, inner, 16),
              StringAlignmentNear, StringAlignmentNear, &bGray);

    // Delete confirmation row
    if (pendingDel) {
        SolidBrush bDangerBr(Danger);
        DrawText_(g, L"🗑 Tap again to confirm delete", &fMeta,
                  RectF(cx + padX, cy + CARD_H - CARD_GAP - 18, inner, 16),
                  StringAlignmentNear, StringAlignmentNear, &bDangerBr);
    }
}

// ============================================================
// LIST VIEW DRAW
// ============================================================
static float g_listContentH = 0;
static const float FOLDER_BAR_H = 38.f;

void DrawDiaryListView(Graphics& g, const FontFamily& ff) {
    auto filtered = FilteredEntries();

    float areaY = g_cy + TOOLBAR_H + FOLDER_BAR_H;
    float areaH = g_ch - TOOLBAR_H - FOLDER_BAR_H;

    // Content height
    g_listContentH = filtered.size() * (CARD_H + CARD_GAP) + PAD * 2;

    // Clipping
    Region oldClip; g.GetClip(&oldClip);
    g.SetClip(RectF(g_cx, areaY, g_cw, areaH));

    SolidBrush bBg(Bg);
    g.FillRectangle(&bBg, g_cx, areaY, g_cw, areaH);

    if (filtered.empty()) {
        Font fMid(&ff, 15, FontStyleRegular, UnitPixel);
        SolidBrush bGray(TextGray);
        DrawText_(g, L"No diary entries yet.\nTap \"+ New Entry\" to start writing.",
                  &fMid, RectF(g_cx, areaY, g_cw, areaH),
                  StringAlignmentCenter, StringAlignmentCenter, &bGray, StringTrimmingNone);
    }

    float cardX = g_cx + PAD;
    float cardW = g_cw - PAD * 2;
    float y     = areaY + PAD - g_listScroll;

    for (int i = 0; i < (int)filtered.size(); i++) {
        if (y + CARD_H > areaY && y < areaY + areaH) {
            DrawEntryCard(g, ff, *filtered[i], cardX, y, cardW,
                          g_hovCard == i, g_delId == filtered[i]->id);
        }
        y += CARD_H + CARD_GAP;
    }

    g.SetClip(&oldClip);

    // Scroll indicator
    if (g_listContentH > areaH) {
        float trackH   = areaH;
        float thumbH   = max(30.f, trackH * areaH / g_listContentH);
        float maxScroll= g_listContentH - areaH;
        float thumbY   = areaY + (g_listScroll / maxScroll) * (trackH - thumbH);
        SolidBrush bThumb(Color(180, 180, 185, 200));
        g.FillRectangle(&bThumb, (Gdiplus::REAL)(g_cx + g_cw - 5), (Gdiplus::REAL)thumbY, (Gdiplus::REAL)4, (Gdiplus::REAL)thumbH);
    }
}

// ============================================================
// EDIT VIEW DRAW
// ============================================================
static vector<RectF> g_moodRects;
static vector<RectF> g_folderChipRects;
static RectF g_titleRect, g_bodyRect, g_folderInputRect, g_tagsRect;
static RectF g_saveRect, g_backRect, g_delRect, g_addFolderRect;

void DrawDiaryEditView(Graphics& g, const FontFamily& ff) {
    g_moodRects.clear(); g_folderChipRects.clear();

    Font fLabel(&ff, 11, FontStyleBold,    UnitPixel);
    Font fInput(&ff, 14, FontStyleRegular, UnitPixel);
    Font fInputB(&ff,14, FontStyleBold,    UnitPixel);
    Font fSm   (&ff, 12, FontStyleRegular, UnitPixel);

    SolidBrush bBg(Bg);
    g.FillRectangle(&bBg, g_cx, g_cy + TOOLBAR_H, g_cw, g_ch - TOOLBAR_H);

    SolidBrush bDark(TextDark);
    SolidBrush bGray(TextGray);
    SolidBrush bSurf(Surface);
    SolidBrush bPrim(Primary);
    SolidBrush bAccent(Accent);
    SolidBrush bWhite(Color(255,255,255,255));

    float y    = g_cy + TOOLBAR_H + PAD;
    float left = g_cx + PAD;
    float w    = g_cw - PAD * 2;

    // ── Date (auto)
    {
        Font fDate(&ff, 11, FontStyleItalic, UnitPixel);
        wstring ds = S2W(g_edit.date.empty() ? CurrentDate() : g_edit.date);
        DrawText_(g, (L"📅  " + ds).c_str(), &fDate,
                  RectF(left, y, w, 18), StringAlignmentNear, StringAlignmentNear, &bGray);
        y += 24.f;
    }

    // ── Mood selector
    DrawText_(g, L"MOOD", &fLabel, RectF(left, y, w, 16),
              StringAlignmentNear, StringAlignmentNear, &bGray);
    y += 20.f;
    float moodW = min(w / 5.f - 6.f, 64.f);
    for (int i = 0; i < 5; i++) {
        float mx = left + i * (moodW + 6.f);
        bool active = (g_edit.mood == MOOD_KEY[i]);
        bool hov    = (g_hovMood == i);
        Color bgC = active ? MOOD_CLR[i] : (hov ? CardHov : Surface);
        SolidBrush bM(bgC);
        Pen pM(active ? Color(0,0,0,0) : Divider, 1.f);
        FillRR(g, &bM, &pM, mx, y, moodW, 36, 10);
        // emoji + label
        Font fIco(&ff, 16, FontStyleRegular, UnitPixel);
        DrawText_(g, MOOD_ICON[i], &fIco, RectF(mx, y, moodW, 20),
                  StringAlignmentCenter, StringAlignmentCenter, &bDark);
        DrawText_(g, MOOD_LBL[i], &fSm, RectF(mx, y + 18, moodW, 18),
                  StringAlignmentCenter, StringAlignmentCenter, &bDark);
        g_moodRects.push_back(RectF(mx, y, moodW, 36));
    }
    y += 44.f;

    // ── Title
    DrawText_(g, L"TITLE", &fLabel, RectF(left, y, w, 16),
              StringAlignmentNear, StringAlignmentNear, &bGray);
    y += 20.f;
    bool tFocus = (g_editFocus == 1);
    Pen pTBrd(tFocus ? Primary : Divider, tFocus ? 2.f : 1.f);
    FillRR(g, &bSurf, &pTBrd, left, y, w, 40, 8);
    wstring titleTxt = S2W(g_edit.title);
    if (tFocus) titleTxt += L"│"; // cursor
    DrawText_(g, titleTxt.empty() ? L"Entry title…" : titleTxt.c_str(), &fInputB,
              RectF(left + 10, y, w - 16, 40),
              StringAlignmentNear, StringAlignmentCenter,
              titleTxt.empty() ? &bGray : &bDark);
    g_titleRect = RectF(left, y, w, 40); y += 50.f;

    // ── Body
    DrawText_(g, L"CONTENT", &fLabel, RectF(left, y, w, 16),
              StringAlignmentNear, StringAlignmentNear, &bGray);
    y += 20.f;
    float bodyH = g_ch - y - g_cy - PAD - 80.f; // leave room for folder+tags
    if (bodyH < 80.f) bodyH = 80.f;
    bool bFocus = (g_editFocus == 2);
    Pen pBBrd(bFocus ? Primary : Divider, bFocus ? 2.f : 1.f);
    FillRR(g, &bSurf, &pBBrd, left, y, w, bodyH, 8);

    // Draw body text with scroll
    Region oldClip; g.GetClip(&oldClip);
    g.SetClip(RectF(left, y, w, bodyH));
    wstring bodyTxt = S2W(g_edit.body);
    if (bFocus) bodyTxt += L"│";
    Font fBodyIn(&ff, 13, FontStyleRegular, UnitPixel);
    DrawText_(g, bodyTxt.empty() ? L"Write your thoughts…" : bodyTxt.c_str(),
              &fBodyIn, RectF(left + 10, y + 8 - g_bodyScroll, w - 20, bodyH * 10),
              StringAlignmentNear, StringAlignmentNear,
              bodyTxt.empty() ? &bGray : &bDark, StringTrimmingNone);
    g.SetClip(&oldClip);
    g_bodyRect = RectF(left, y, w, bodyH); y += bodyH + 12.f;

    // ── Folder chips
    DrawText_(g, L"FOLDER", &fLabel, RectF(left, y, 80, 16),
              StringAlignmentNear, StringAlignmentNear, &bGray);
    y += 20.f;
    float fx = left;
    // existing folders
    vector<string> knownFolders = { "General", "Work", "Personal", "Study" };
    for (auto& e : g_entries)
        if (find(knownFolders.begin(), knownFolders.end(), e.folder) == knownFolders.end())
            knownFolders.push_back(e.folder);

    for (int i = 0; i < (int)knownFolders.size(); i++) {
        wstring lbl = S2W(knownFolders[i]);
        RectF bounds(0,0,0,0);
        g.MeasureString(lbl.c_str(), -1, &fSm, PointF(0,0), &bounds);
        float cw2 = bounds.Width + 20.f;
        if (fx + cw2 > left + w - 40) break;
        bool active2 = (g_edit.folder == knownFolders[i]);
        bool hov2    = (g_hovFolder2 == i);
        Color bgF  = active2 ? Primary : (hov2 ? CardHov : TagBg);
        SolidBrush bFChip(bgF);
        FillRR(g, &bFChip, nullptr, fx, y, cw2, 28, 14);
        SolidBrush bFTxt(active2 ? Color(255,255,255,255) : Primary);
        DrawText_(g, lbl.c_str(), &fSm, RectF(fx, y, cw2, 28),
                  StringAlignmentCenter, StringAlignmentCenter, &bFTxt);
        g_folderChipRects.push_back(RectF(fx, y, cw2, 28));
        // store label in parallel
        fx += cw2 + 6.f;
    }

    // "+ New" folder chip
    bool hov3 = g_hovAddFolder;
    SolidBrush bAddF(hov3 ? Accent : Surface);
    Pen pAddF(Accent, 1.5f);
    FillRR(g, &bAddF, &pAddF, fx, y, 60, 28, 14);
    SolidBrush bAddFTxt(hov3 ? Color(255,255,255,255) : Accent);
    DrawText_(g, L"+ New", &fSm, RectF(fx, y, 60, 28),
              StringAlignmentCenter, StringAlignmentCenter, &bAddFTxt);
    g_addFolderRect = RectF(fx, y, 60, 28);
    y += 38.f;

    // New folder input (if visible)
    if (g_showFolderInput) {
        bool ffocus = (g_editFocus == 3);
        Pen pFI(ffocus ? Accent : Divider, ffocus ? 2.f : 1.f);
        FillRR(g, &bSurf, &pFI, left, y, w * 0.6f, 32, 8);
        wstring nf = g_newFolder;
        if (ffocus) nf += L"│";
        DrawText_(g, nf.empty() ? L"New folder name…" : nf.c_str(),
                  &fSm, RectF(left + 8, y, w * 0.6f - 12, 32),
                  StringAlignmentNear, StringAlignmentCenter,
                  nf.empty() ? &bGray : &bDark);
        g_folderInputRect = RectF(left, y, w * 0.6f, 32);

        // Confirm button
        float cfX = left + w * 0.6f + 8;
        SolidBrush bCf(Accent);
        FillRR(g, &bCf, nullptr, cfX, y, 60, 32, 8);
        DrawText_(g, L"Add", &fSm, RectF(cfX, y, 60, 32),
                  StringAlignmentCenter, StringAlignmentCenter, &bWhite);
        y += 42.f;
    }

    // ── Tags
    DrawText_(g, L"TAGS (comma separated)", &fLabel, RectF(left, y, w, 16),
              StringAlignmentNear, StringAlignmentNear, &bGray);
    y += 20.f;
    bool tgFocus = (g_editFocus == 4);
    Pen pTgBrd(tgFocus ? Primary : Divider, tgFocus ? 2.f : 1.f);
    FillRR(g, &bSurf, &pTgBrd, left, y, w, 32, 8);
    wstring tagsTxt = S2W(g_edit.tags);
    if (tgFocus) tagsTxt += L"│";
    DrawText_(g, tagsTxt.empty() ? L"e.g. work, ideas, family…" : tagsTxt.c_str(),
              &fSm, RectF(left + 8, y, w - 12, 32),
              StringAlignmentNear, StringAlignmentCenter,
              tagsTxt.empty() ? &bGray : &bDark);
    g_tagsRect = RectF(left, y, w, 32);
}

// ============================================================
// PUBLIC: DrawGeminiTab  (called from tab_special.cpp)
// ============================================================
void DrawGeminiTab(Graphics& g, float cx, float cy, float cw, float ch) {
    if (!g_loaded) LoadEntries();
    g_cx = cx; g_cy = cy; g_cw = cw; g_ch = ch;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff(L"Segoe UI");

    // Main bg
    SolidBrush bBg(Bg);
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    DrawToolbar(g, ff);

    if (g_view == DiaryView::List) {
        RebuildFolders();
        DrawFolderBar(g, ff);
        DrawDiaryListView(g, ff);
    } else {
        DrawDiaryEditView(g, ff);
    }
}

// ============================================================
// PUBLIC: ShowGeminiControls  (no Win32 overlays needed)
// ============================================================
void ShowGeminiControls(bool show) {
    (void)show; // Pure GDI — nothing to show/hide
}

// ============================================================
// PUBLIC: ResizeGeminiControls
// ============================================================
void ResizeGeminiControls(int cx, int cy, int cw, int ch) {
    g_cx = (float)cx; g_cy = (float)cy;
    g_cw = (float)cw; g_ch = (float)ch;
}

// ============================================================
// PUBLIC: InitGeminiControls
// ============================================================
void InitGeminiControls(HWND parent) {
    (void)parent;
    LoadEntries();
}

// ============================================================
// MOUSE MOVE
// ============================================================
void ProcessGeminiMouseMove(float x, float y) {
    if (g_view == DiaryView::List) {
        bool old_new = g_hovNew;
        // New button rect (approx — recomputed from g_cx/cw)
        float btnX = g_cx + g_cw - PAD - 110.f;
        g_hovNew = (x >= btnX && x <= btnX + 108 && y >= g_cy + 10 && y <= g_cy + 42);

        // Card hover
        auto filtered = FilteredEntries();
        int  oldCard = g_hovCard;
        g_hovCard = -1;
        float areaY = g_cy + TOOLBAR_H + FOLDER_BAR_H;
        float areaH = g_ch - TOOLBAR_H - FOLDER_BAR_H;
        float cardX = g_cx + PAD, cardW = g_cw - PAD * 2;
        float yy = areaY + PAD - g_listScroll;
        for (int i = 0; i < (int)filtered.size(); i++) {
            if (x >= cardX && x <= cardX + cardW && y >= yy && y <= yy + CARD_H - CARD_GAP)
                g_hovCard = i;
            yy += CARD_H + CARD_GAP;
        }

        if (old_new != g_hovNew || oldCard != g_hovCard)
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);

    } else {
        // Edit view
        bool oldSave = g_hovSave, oldBack = g_hovBack, oldDel = g_hovDelBtn;
        float sX = g_cx + g_cw - PAD - 90;
        g_hovSave   = (x >= sX && x <= sX + 88 && y >= g_cy + 12 && y <= g_cy + 40);
        g_hovBack   = (x >= g_cx + PAD && x <= g_cx + PAD + 32 && y >= g_cy + 12 && y <= g_cy + 40);
        if (!g_editIsNew) {
            float dX = sX - 90;
            g_hovDelBtn = (x >= dX && x <= dX + 82 && y >= g_cy + 12 && y <= g_cy + 40);
        }

        // Mood hover
        int oldMood = g_hovMood; g_hovMood = -1;
        for (int i = 0; i < (int)g_moodRects.size(); i++)
            if (g_moodRects[i].Contains(x, y)) { g_hovMood = i; break; }

        // Folder chip hover
        int oldF2 = g_hovFolder2; g_hovFolder2 = -1;
        for (int i = 0; i < (int)g_folderChipRects.size(); i++)
            if (g_folderChipRects[i].Contains(x, y)) { g_hovFolder2 = i; break; }

        bool oldAddF = g_hovAddFolder;
        g_hovAddFolder = g_addFolderRect.Contains(x, y);

        if (oldSave != g_hovSave || oldBack != g_hovBack || oldDel != g_hovDelBtn ||
            oldMood != g_hovMood || oldF2 != g_hovFolder2 || oldAddF != g_hovAddFolder)
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
    }
}

// ============================================================
// SCROLL (list view)
// ============================================================
void ProcessDiaryMouseWheel(int delta) {
    if (g_view != DiaryView::List) return;
    float areaH = g_ch - TOOLBAR_H - FOLDER_BAR_H;
    float maxScroll = max(0.f, g_listContentH - areaH);
    g_listScroll = (int)max(0.f, min((float)g_listScroll - delta * 0.3f, maxScroll));
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}

// ============================================================
// KEYBOARD
// ============================================================
void ProcessDiaryChar(wchar_t c) {
    if (g_view != DiaryView::Edit) return;
    auto AppendChar = [](string& s, wchar_t c) {
        char buf[4] = {};
        WideCharToMultiByte(CP_UTF8, 0, &c, 1, buf, 4, NULL, NULL);
        s += buf;
    };
    if (g_editFocus == 1) {
        if (c == L'\b') { if (!g_edit.title.empty()) { wstring ws = S2W(g_edit.title); if (!ws.empty()) ws.pop_back(); g_edit.title = W2S(ws); } }
        else if (c >= L' ') AppendChar(g_edit.title, c);
    } else if (g_editFocus == 2) {
        if (c == L'\b') { if (!g_edit.body.empty()) { wstring ws = S2W(g_edit.body); if (!ws.empty()) ws.pop_back(); g_edit.body = W2S(ws); } }
        else if (c >= L' ' || c == L'\r' || c == L'\n') { if (c == L'\r') c = L'\n'; AppendChar(g_edit.body, c); }
    } else if (g_editFocus == 3) { // new folder input
        if (c == L'\b') { int len = (int)wcslen(g_newFolder); if (len > 0) g_newFolder[len-1] = L'\0'; }
        else if (c >= L' ' && wcslen(g_newFolder) < 62) { int len = (int)wcslen(g_newFolder); g_newFolder[len] = c; g_newFolder[len+1] = L'\0'; }
    } else if (g_editFocus == 4) {
        if (c == L'\b') { if (!g_edit.tags.empty()) { wstring ws = S2W(g_edit.tags); if (!ws.empty()) ws.pop_back(); g_edit.tags = W2S(ws); } }
        else if (c >= L' ') AppendChar(g_edit.tags, c);
    } else if (g_view == DiaryView::List && g_searchFocus) {
        int len = (int)wcslen(g_search);
        if (c == L'\b') { if (len > 0) g_search[len-1] = L'\0'; }
        else if (c >= L' ' && len < 254) { g_search[len] = c; g_search[len+1] = L'\0'; }
    }
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}

void ProcessDiaryKeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE) {
        if (g_view == DiaryView::Edit) { g_view = DiaryView::List; g_editFocus = 0; }
        else if (g_searchFocus) { g_searchFocus = false; wmemset(g_search, 0, 256); }
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
    }
}

// ============================================================
// MOUSE CLICK
// ============================================================
void ProcessGeminiMouseClick(float x, float y) {
    if (!g_loaded) LoadEntries();

    if (g_view == DiaryView::List) {
        // Toolbar: New entry
        float btnX = g_cx + g_cw - PAD - 110.f;
        if (x >= btnX && x <= btnX + 108 && y >= g_cy + 10 && y <= g_cy + 42) {
            g_editIsNew = true;
            g_edit = DiaryEntry();
            g_edit.id        = (long long)time(NULL) * 1000 + rand() % 1000;
            g_edit.date      = CurrentDate();
            g_edit.timestamp = (long long)time(NULL);
            g_edit.folder    = "General";
            g_edit.mood      = "neutral";
            g_editFocus = 2; g_bodyScroll = 0;
            g_showFolderInput = false; memset(g_newFolder, 0, sizeof(g_newFolder));
            g_hovMood = -1; g_hovFolder2 = -1;
            g_view = DiaryView::Edit;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }

        // Folder bar
        for (int i = 0; i < (int)g_folderRects.size(); i++) {
            if (g_folderRects[i].Contains(x, y)) {
                g_selFolder = i; g_listScroll = 0;
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }

        // Search box
        float sW = min(260.f, g_cw * 0.35f);
        float sX = g_cx + g_cw - PAD - 110.f - sW - 8.f;
        if (x >= sX && x <= sX + sW && y >= g_cy + 10 && y <= g_cy + 42) {
            g_searchFocus = true;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        g_searchFocus = false;

        // Card click
        auto filtered = FilteredEntries();
        float areaY = g_cy + TOOLBAR_H + FOLDER_BAR_H;
        float cardX = g_cx + PAD, cardW = g_cw - PAD * 2;
        float yy = areaY + PAD - g_listScroll;
        for (int i = 0; i < (int)filtered.size(); i++) {
            if (x >= cardX && x <= cardX + cardW && y >= yy && y <= yy + CARD_H - CARD_GAP) {
                // Delete confirm
                if (g_delId == filtered[i]->id) {
                    DeleteEntry(g_delId);
                    g_delId = -1;
                } else if (x >= cardX + cardW - 40 && x <= cardX + cardW && y >= yy + 4 && y <= yy + 24) {
                    // right-side delete zone (only if no card edit needed)
                    g_delId = filtered[i]->id;
                } else {
                    // Open for edit
                    g_delId = -1;
                    g_editIsNew = false;
                    g_edit = *filtered[i];
                    g_editFocus = 2; g_bodyScroll = 0;
                    g_showFolderInput = false; memset(g_newFolder, 0, sizeof(g_newFolder));
                    g_hovMood = -1; g_hovFolder2 = -1;
                    g_view = DiaryView::Edit;
                }
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            yy += CARD_H + CARD_GAP;
        }
        // click elsewhere — cancel delete
        g_delId = -1;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);

    } else { // Edit view
        // Back
        if (x >= g_cx + PAD && x <= g_cx + PAD + 32 && y >= g_cy + 12 && y <= g_cy + 40) {
            g_view = DiaryView::List; g_editFocus = 0;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }

        // Save
        float sX = g_cx + g_cw - PAD - 90;
        if (x >= sX && x <= sX + 88 && y >= g_cy + 12 && y <= g_cy + 40) {
            if (g_edit.title.empty()) g_edit.title = g_edit.date;
            if (g_edit.date.empty())  g_edit.date  = CurrentDate();
            g_edit.timestamp = (long long)time(NULL);
            UpsertEntry(g_edit);
            g_view = DiaryView::List;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }

        // Delete (edit existing)
        if (!g_editIsNew) {
            float dX = sX - 90;
            if (x >= dX && x <= dX + 82 && y >= g_cy + 12 && y <= g_cy + 40) {
                DeleteEntry(g_edit.id);
                g_view = DiaryView::List;
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }

        // Mood
        for (int i = 0; i < (int)g_moodRects.size(); i++) {
            if (g_moodRects[i].Contains(x, y)) {
                g_edit.mood = MOOD_KEY[i];
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }

        // Title field
        if (g_titleRect.Contains(x, y)) { g_editFocus = 1; if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }
        // Body field
        if (g_bodyRect.Contains(x, y))  { g_editFocus = 2; if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }
        // Tags field
        if (g_tagsRect.Contains(x, y))  { g_editFocus = 4; if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }

        // Folder chips
        {
            vector<string> knownFolders = { "General", "Work", "Personal", "Study" };
            for (auto& e : g_entries)
                if (find(knownFolders.begin(), knownFolders.end(), e.folder) == knownFolders.end())
                    knownFolders.push_back(e.folder);
            for (int i = 0; i < (int)g_folderChipRects.size() && i < (int)knownFolders.size(); i++) {
                if (g_folderChipRects[i].Contains(x, y)) {
                    g_edit.folder = knownFolders[i];
                    g_showFolderInput = false;
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                    return;
                }
            }
        }

        // Add folder chip
        if (g_addFolderRect.Contains(x, y)) {
            g_showFolderInput = !g_showFolderInput;
            if (g_showFolderInput) { g_editFocus = 3; memset(g_newFolder, 0, sizeof(g_newFolder)); }
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }

        // New folder confirm button
        if (g_showFolderInput) {
            float cfX = g_folderInputRect.X + g_folderInputRect.Width + 8;
            if (x >= cfX && x <= cfX + 60 && y >= g_folderInputRect.Y && y <= g_folderInputRect.Y + 32) {
                if (wcslen(g_newFolder) > 0) {
                    g_edit.folder = W2S(g_newFolder);
                    g_showFolderInput = false; g_editFocus = 2;
                }
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            // Folder input field focus
            if (g_folderInputRect.Contains(x, y)) { g_editFocus = 3; if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }
        }

        g_editFocus = 0;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
    }
}

void ProcessGeminiCommand(int id, int code) { (void)id; (void)code; }
