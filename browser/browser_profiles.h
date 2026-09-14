#pragma once
// browser_profiles.h — Chrome-style Multi-Profile & Google Account Manager
// Features:
//   - Multiple browser profiles (each with separate WebView2 user data dir)
//   - Google Sign-in per profile (navigate to accounts.google.com/signin)
//   - Multiple Google accounts per profile (account switcher)
//   - Persistent storage in %LOCALAPPDATA%\RasBrowserData\profiles.ini
//   - Add/Remove/Switch profiles
//   - Avatar letter + color per profile

#ifndef BROWSER_PROFILES_H
#define BROWSER_PROFILES_H

#include <windows.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// GOOGLE ACCOUNT  (একটা profile এ একাধিক Gmail account থাকতে পারে)
// ─────────────────────────────────────────────────────────────────────────────
struct GoogleAccount {
    std::wstring email;        // e.g. "user@gmail.com"
    std::wstring displayName;  // e.g. "Ras Edu"
    std::wstring photoUrl;     // Google profile photo URL (https://...googleusercontent.com/...)
};

// ─────────────────────────────────────────────────────────────────────────────
// BROWSER PROFILE
// ─────────────────────────────────────────────────────────────────────────────
struct BrowserProfile {
    int                        id;            // unique numeric id (1, 2, 3...)
    std::wstring               name;          // display name, e.g. "Personal"
    wchar_t                    avatarLetter;  // first letter of name
    COLORREF                   avatarColor;   // avatar background color
    std::vector<GoogleAccount> accounts;      // Google accounts signed in
    int                        activeAccount; // index into accounts (-1 = none)

    // Convenience: primary email or empty
    std::wstring primaryEmail() const {
        if (activeAccount >= 0 && activeAccount < (int)accounts.size())
            return accounts[activeAccount].email;
        return L"";
    }

    // Convenience: primary display name or profile name
    std::wstring displayLabel() const {
        if (activeAccount >= 0 && activeAccount < (int)accounts.size())
            return accounts[activeAccount].displayName;
        return name;
    }

    // WebView2 user data folder for this profile
    std::wstring userDataFolder() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// GLOBAL STATE
// ─────────────────────────────────────────────────────────────────────────────
extern std::vector<BrowserProfile> g_profiles;
extern int                          g_activeProfileIdx; // index into g_profiles

BrowserProfile* GetActiveProfile();  // may return nullptr if empty

// ─────────────────────────────────────────────────────────────────────────────
// PERSISTENCE
// ─────────────────────────────────────────────────────────────────────────────
void LoadProfiles();   // read %LOCALAPPDATA%\RasBrowserData\profiles.ini
void SaveProfiles();   // write same file

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE OPERATIONS
// ─────────────────────────────────────────────────────────────────────────────
// Add a new profile with given name; returns its index in g_profiles
int  AddProfile(const std::wstring& name);
// Remove profile by index (cannot remove if only 1 left)
void RemoveProfile(int idx);
// Switch active profile; returns the new user data folder path
std::wstring SwitchProfile(int idx);

// ─────────────────────────────────────────────────────────────────────────────
// ACCOUNT OPERATIONS (within the active profile)
// ─────────────────────────────────────────────────────────────────────────────
// Add a Google account to active profile (called after WebView detects sign-in)
void AddGoogleAccount(const std::wstring& email, const std::wstring& displayName);
// Remove an account from active profile by index
void RemoveGoogleAccount(int accountIdx);
// Switch active account within current profile
void SwitchGoogleAccount(int accountIdx);

// ─────────────────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────────────────
// 8 distinct avatar colors (cycles by profile id)
COLORREF ProfileAvatarColor(int profileId);
// Letter from name (first char, uppercase)
wchar_t  ProfileAvatarLetter(const std::wstring& name);

#endif // BROWSER_PROFILES_H
