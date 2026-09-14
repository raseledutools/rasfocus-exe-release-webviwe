// browser_profiles.cpp — Chrome-style Multi-Profile & Google Account Manager
#define _CRT_SECURE_NO_WARNINGS
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00

#include "browser_profiles.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// GLOBALS
// ─────────────────────────────────────────────────────────────────────────────
std::vector<BrowserProfile> g_profiles;
int                          g_activeProfileIdx = 0;

BrowserProfile* GetActiveProfile() {
    if (g_profiles.empty()) return nullptr;
    if (g_activeProfileIdx < 0 || g_activeProfileIdx >= (int)g_profiles.size())
        g_activeProfileIdx = 0;
    return &g_profiles[g_activeProfileIdx];
}

// ─────────────────────────────────────────────────────────────────────────────
// COLORS  (Chrome profile palette)
// ─────────────────────────────────────────────────────────────────────────────
static const COLORREF kAvatarColors[] = {
    RGB(26,  115, 232),   // Google Blue
    RGB(234,  67,  53),   // Google Red
    RGB( 52, 168,  83),   // Google Green
    RGB(251, 188,   4),   // Google Yellow/Amber
    RGB(103,  58, 183),   // Purple
    RGB(  0, 172, 193),   // Teal
    RGB(233,  30,  99),   // Pink
    RGB( 63, 175, 110),   // Emerald
};
static const int kColorCount = sizeof(kAvatarColors) / sizeof(kAvatarColors[0]);

COLORREF ProfileAvatarColor(int profileId) {
    return kAvatarColors[abs(profileId) % kColorCount];
}

wchar_t ProfileAvatarLetter(const std::wstring& name) {
    if (name.empty()) return L'P';
    return towupper(name[0]);
}

// ─────────────────────────────────────────────────────────────────────────────
// USER DATA FOLDER  per profile
// ─────────────────────────────────────────────────────────────────────────────
std::wstring BrowserProfile::userDataFolder() const {
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData);
    return std::wstring(appData) + L"\\RasBrowserData\\Profile_" + std::to_wstring(id);
}

// ─────────────────────────────────────────────────────────────────────────────
// PERSISTENCE FILE  %LOCALAPPDATA%\RasBrowserData\profiles.ini
// Format:
//   [Profile:1]
//   name=Personal
//   color=1a73e8
//   activeAccount=0
//   account0.email=user@gmail.com
//   account0.name=Ras User
//   account1.email=work@gmail.com
//   account1.name=Work Account
//   [Active]
//   idx=0
// ─────────────────────────────────────────────────────────────────────────────
static std::wstring GetProfilesFilePath() {
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData);
    std::wstring dir = std::wstring(appData) + L"\\RasBrowserData";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\profiles.ini";
}

// narrow wstring helpers for INI parsing
static std::string WtoA(const std::wstring& ws) {
    if (ws.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}
static std::wstring AtoW(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(sz - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), sz);
    return ws;
}

static COLORREF ParseColor(const std::string& hex) {
    unsigned int r = 0, g = 0, b = 0;
    if (hex.size() >= 6) {
        auto h2i = [](char c) -> unsigned int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        r = h2i(hex[0]) * 16 + h2i(hex[1]);
        g = h2i(hex[2]) * 16 + h2i(hex[3]);
        b = h2i(hex[4]) * 16 + h2i(hex[5]);
    }
    return RGB(r, g, b);
}
static std::string ColorToHex(COLORREF c) {
    char buf[8];
    sprintf_s(buf, "%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

void LoadProfiles() {
    g_profiles.clear();
    g_activeProfileIdx = 0;

    std::string path_a = WtoA(GetProfilesFilePath());
    std::ifstream f(path_a);
    if (!f.is_open()) {
        // Create default "Personal" profile
        AddProfile(L"Personal");
        SaveProfiles();
        return;
    }

    std::map<int, BrowserProfile> profileMap;
    int  currentId     = -1;
    int  activeIdx     = 0;

    std::string line;
    while (std::getline(f, line)) {
        // strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line[0] == '[') {
            // Section header
            if (line.find("[Profile:") == 0) {
                currentId = std::stoi(line.substr(9, line.size() - 10));
                auto& p          = profileMap[currentId];
                p.id             = currentId;
                p.name           = L"Profile";
                p.avatarColor    = ProfileAvatarColor(currentId);
                p.avatarLetter   = L'P';
                p.activeAccount  = -1;
            } else if (line == "[Active]") {
                currentId = -999;
            }
            continue;
        }

        // key=value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (currentId == -999) {
            if (key == "idx") activeIdx = std::stoi(val);
            continue;
        }
        if (currentId < 0 || profileMap.find(currentId) == profileMap.end()) continue;

        auto& p = profileMap[currentId];
        if (key == "name") {
            p.name         = AtoW(val);
            p.avatarLetter = ProfileAvatarLetter(p.name);
        } else if (key == "color") {
            p.avatarColor = ParseColor(val);
        } else if (key == "activeAccount") {
            p.activeAccount = std::stoi(val);
        } else if (key.rfind("account", 0) == 0) {
            // accountN.email or accountN.name
            auto dot = key.find('.');
            if (dot != std::string::npos) {
                int idx = std::stoi(key.substr(7, dot - 7));
                std::string field = key.substr(dot + 1);
                while ((int)p.accounts.size() <= idx) p.accounts.push_back({});
                if (field == "email") p.accounts[idx].email       = AtoW(val);
                else if (field == "name") p.accounts[idx].displayName = AtoW(val);
            }
        }
    }

    // Build ordered list by id
    std::vector<std::pair<int,BrowserProfile>> sorted(profileMap.begin(), profileMap.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b){ return a.first < b.first; });
    for (auto& kv : sorted) g_profiles.push_back(kv.second);

    if (g_profiles.empty()) {
        AddProfile(L"Personal");
    }

    // activeIdx is an index into sorted order
    g_activeProfileIdx = (activeIdx >= 0 && activeIdx < (int)g_profiles.size()) ? activeIdx : 0;
}

void SaveProfiles() {
    std::string path_a = WtoA(GetProfilesFilePath());
    std::ofstream f(path_a, std::ios::trunc);
    if (!f.is_open()) return;

    for (auto& p : g_profiles) {
        f << "[Profile:" << p.id << "]\n";
        f << "name=" << WtoA(p.name) << "\n";
        f << "color=" << ColorToHex(p.avatarColor) << "\n";
        f << "activeAccount=" << p.activeAccount << "\n";
        for (int i = 0; i < (int)p.accounts.size(); i++) {
            f << "account" << i << ".email=" << WtoA(p.accounts[i].email) << "\n";
            f << "account" << i << ".name="  << WtoA(p.accounts[i].displayName) << "\n";
        }
        f << "\n";
    }
    f << "[Active]\n";
    f << "idx=" << g_activeProfileIdx << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE OPERATIONS
// ─────────────────────────────────────────────────────────────────────────────
static int NextProfileId() {
    int maxId = 0;
    for (auto& p : g_profiles) if (p.id > maxId) maxId = p.id;
    return maxId + 1;
}

int AddProfile(const std::wstring& name) {
    BrowserProfile p;
    p.id            = NextProfileId();
    p.name          = name;
    p.avatarLetter  = ProfileAvatarLetter(name);
    p.avatarColor   = ProfileAvatarColor(p.id);
    p.activeAccount = -1;
    g_profiles.push_back(p);
    SaveProfiles();
    return (int)g_profiles.size() - 1;
}

void RemoveProfile(int idx) {
    if ((int)g_profiles.size() <= 1) return; // cannot remove last
    if (idx < 0 || idx >= (int)g_profiles.size()) return;
    g_profiles.erase(g_profiles.begin() + idx);
    if (g_activeProfileIdx >= (int)g_profiles.size())
        g_activeProfileIdx = (int)g_profiles.size() - 1;
    SaveProfiles();
}

std::wstring SwitchProfile(int idx) {
    if (idx < 0 || idx >= (int)g_profiles.size()) return L"";
    g_activeProfileIdx = idx;
    SaveProfiles();
    return g_profiles[idx].userDataFolder();
}

// ─────────────────────────────────────────────────────────────────────────────
// ACCOUNT OPERATIONS
// ─────────────────────────────────────────────────────────────────────────────
void AddGoogleAccount(const std::wstring& email, const std::wstring& displayName) {
    auto* p = GetActiveProfile();
    if (!p) return;
    // Don't add duplicate
    for (auto& a : p->accounts)
        if (a.email == email) return;
    GoogleAccount acc;
    acc.email       = email;
    acc.displayName = displayName.empty() ? email : displayName;
    p->accounts.push_back(acc);
    p->activeAccount = (int)p->accounts.size() - 1;
    SaveProfiles();
}

void RemoveGoogleAccount(int accountIdx) {
    auto* p = GetActiveProfile();
    if (!p) return;
    if (accountIdx < 0 || accountIdx >= (int)p->accounts.size()) return;
    p->accounts.erase(p->accounts.begin() + accountIdx);
    if (p->activeAccount >= (int)p->accounts.size())
        p->activeAccount = (int)p->accounts.size() - 1;
    SaveProfiles();
}

void SwitchGoogleAccount(int accountIdx) {
    auto* p = GetActiveProfile();
    if (!p) return;
    if (accountIdx < 0 || accountIdx >= (int)p->accounts.size()) return;
    p->activeAccount = accountIdx;
    SaveProfiles();
}
