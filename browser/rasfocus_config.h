#pragma once
// rasfocus_config.h — RasFocus AI Filter Config Manager
// TODO: implement load/save and settings UI for AI filter

#include <windows.h>
#include <string>
#include <vector>

struct RasFocusConfig {
    bool isAiEngineActive      = false;
    bool cbAiImageBlur         = false;
    bool cbFemaleDetectWeb     = false;
    bool cbFemaleDetectVideo   = false;
    std::vector<std::wstring> blockedKeywords;
    std::vector<std::wstring> blockedDomains;
    std::wstring password      = L""; // admin password to turn off
};

extern RasFocusConfig g_rasFocusConfig;

// rasfocus_ai_data.txt থেকে load করো (advanced_feature.cpp এর format অনুযায়ী)
void LoadRasFocusConfig();

// Save করো
void SaveRasFocusConfig();

// RasFocus settings page HTML return করো
std::wstring GetRasFocusSettingsHTML(bool isDarkMode);

// Keyword block list তে যোগ করো
void AddBlockedKeyword(const std::wstring& keyword);
void RemoveBlockedKeyword(const std::wstring& keyword);

// Domain block list তে যোগ করো
void AddBlockedDomain(const std::wstring& domain);
void RemoveBlockedDomain(const std::wstring& domain);
