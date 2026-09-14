#pragma once
#include <windows.h>
#include <wrl.h>
#include "WebView2.h"
#include <gdiplus.h>
#include <string>
#include <vector>

// ডাউনলোড ইনফরমেশন ট্র্যাক করার জন্য স্ট্রাকচার
struct DownloadInfo {
    std::wstring fileName;
    std::wstring fullPath;
    INT64 totalBytes = 0;
    INT64 receivedBytes = 0;
    bool isDownloading = false;
    bool isCompleted = false;
    bool isInterrupted = false;
};

// গ্লোবাল ডাউনলোড লিস্ট (external হিসেবে ডিক্লেয়ার করা হলো যাতে অন্য ফাইলে পাওয়া যায়)
extern std::vector<DownloadInfo> g_downloads;

// ব্রাউজারের অ্যাডভান্সড ফিচারগুলো সেটআপ করার ফাংশন
void SetupAdvancedFeatures(HWND hWnd, Microsoft::WRL::ComPtr<ICoreWebView2> webview);

// কাস্টম ডাউনলোড প্যানেল ড্র করার ফাংশন
void DrawDownloadPanel(Gdiplus::Graphics& g, int windowWidth, int windowHeight, bool isDarkMode);

// হিস্ট্রি সেভ করার ফাংশন
void SaveToHistory(const std::wstring& url, const std::wstring& title);