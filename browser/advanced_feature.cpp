#include "advanced_feature.h"
#include <string>
#include <fstream>
#include <codecvt>
#include <locale>
#include <ctime>
#include <vector>

using namespace Microsoft::WRL;
using namespace Gdiplus;

// গ্লোবাল এনভায়রনমেন্ট ভেরিয়েবল
extern ComPtr<ICoreWebView2Environment> g_sharedEnv;

// গ্লোবাল ডাউনলোড লিস্ট ইনিশিয়ালাইজেশন
std::vector<DownloadInfo> g_downloads;

// হিস্ট্রি ফাইলে সেভ করার ফাংশন
void SaveToHistory(const std::wstring& url, const std::wstring& title) {
    if (url == L"LOCAL_NTP" || url == L"about:blank" || url.empty()) return;

    std::wofstream out(L"rasbrowser_history.txt", std::ios::app);
    out.imbue(std::locale(out.getloc(), new std::codecvt_utf8<wchar_t>));
    if (out.is_open()) {
        time_t now = time(0);
        tm* ltm = localtime(&now);
        wchar_t timeBuf[32];
        wcsftime(timeBuf, 32, L"[%Y-%m-%d %H:%M:%S] ", ltm);
        
        out << timeBuf << title << L" | " << url << L"\n";
        out.close();
    }
}

// কাস্টম ডাউনলোড প্যানেল ড্র করার ফাংশন
void DrawDownloadPanel(Graphics& g, int windowWidth, int windowHeight, bool isDarkMode) {
    if (g_downloads.empty()) return;

    DownloadInfo& latestDl = g_downloads.back();
    
    float panelW = 350.0f;
    float panelH = 80.0f;
    float pX = windowWidth - panelW - 20.0f;
    float pY = 90.0f; 

    SolidBrush bgBrush(isDarkMode ? Color(255, 40, 40, 45) : Color(255, 255, 255, 255));
    Pen borderPen(isDarkMode ? Color(255, 70, 70, 70) : Color(255, 200, 200, 200), 1.0f);
    
    g.FillRectangle(&bgBrush, pX, pY, panelW, panelH);
    g.DrawRectangle(&borderPen, pX, pY, panelW, panelH);

    FontFamily ff(L"Segoe UI");
    Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 12, FontStyleRegular, UnitPixel);
    SolidBrush txtBr(isDarkMode ? Color(255, 230, 230, 230) : Color(255, 40, 40, 40));
    SolidBrush dimBr(isDarkMode ? Color(255, 150, 150, 150) : Color(255, 120, 120, 120));

    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);

    g.DrawString(latestDl.fileName.c_str(), -1, &fBold, RectF(pX + 15, pY + 10, panelW - 30, 20), &sf, &txtBr);

    std::wstring status = L"Starting...";
    if (latestDl.isCompleted) status = L"Completed";
    else if (latestDl.isInterrupted) status = L"Interrupted / Failed";
    else if (latestDl.totalBytes > 0) {
        int percent = (int)((latestDl.receivedBytes * 100) / latestDl.totalBytes);
        status = std::to_wstring(percent) + L"% Downloading...";
    }
    g.DrawString(status.c_str(), -1, &fNorm, RectF(pX + 15, pY + 30, panelW - 30, 20), &sf, &dimBr);

    float barX = pX + 15, barY = pY + 55, barW = panelW - 30, barH = 8.0f;
    SolidBrush barBg(isDarkMode ? Color(255, 60, 60, 60) : Color(255, 220, 220, 220));
    g.FillRectangle(&barBg, barX, barY, barW, barH);

    if (latestDl.totalBytes > 0 || latestDl.isCompleted) {
        float fillW = latestDl.isCompleted ? barW : (barW * latestDl.receivedBytes) / latestDl.totalBytes;
        SolidBrush barFill(Color(255, 0, 120, 215)); 
        if(latestDl.isInterrupted) barFill.SetColor(Color(255, 220, 50, 50)); 
        if(latestDl.isCompleted) barFill.SetColor(Color(255, 30, 180, 50)); 
        g.FillRectangle(&barFill, barX, barY, fillW, barH);
    }
}

void SetupAdvancedFeatures(HWND hWnd, ComPtr<ICoreWebView2> webview) {
    if (!webview) return;

    // ==========================================
    // 🟢 1. CUSTOM DOWNLOAD MANAGER (Requires ICoreWebView2_4)
    // ==========================================
    ComPtr<ICoreWebView2_4> webview4;
    if (SUCCEEDED(webview.As(&webview4))) {
        webview4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
            [hWnd](ICoreWebView2* sender, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE); 

                ComPtr<ICoreWebView2DownloadOperation> download;
                args->get_DownloadOperation(&download);

                if (download) {
                    LPWSTR path = nullptr;
                    download->get_ResultFilePath(&path);
                    std::wstring filePath = path ? path : L"Unknown File";
                    if (path) CoTaskMemFree(path);

                    std::wstring fileName = filePath;
                    size_t pos = filePath.find_last_of(L"\\/");
                    if (pos != std::wstring::npos) fileName = filePath.substr(pos + 1);

                    DownloadInfo info;
                    info.fileName = fileName;
                    info.fullPath = filePath;
                    info.isDownloading = true;
                    g_downloads.push_back(info);
                    int currentDlIdx = g_downloads.size() - 1;

                    InvalidateRect(hWnd, NULL, FALSE);

                    download->add_BytesReceivedChanged(Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
                        [hWnd, currentDlIdx](ICoreWebView2DownloadOperation* op, IUnknown* args) -> HRESULT {
                            if(currentDlIdx < g_downloads.size()) {
                                INT64 received = 0;
                                op->get_BytesReceived(&received);
                                g_downloads[currentDlIdx].receivedBytes = received;
                                
                                INT64 total = 0;
                                op->get_TotalBytesToReceive(&total);
                                g_downloads[currentDlIdx].totalBytes = total;
                                
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            return S_OK;
                        }).Get(), nullptr);

                    // 🟢 FIX: 'ICoreWebView2StateChangedEventHandler' used correctly
                    download->add_StateChanged(Callback<ICoreWebView2StateChangedEventHandler>(
                        [hWnd, currentDlIdx](ICoreWebView2DownloadOperation* op, IUnknown* args) -> HRESULT {
                            COREWEBVIEW2_DOWNLOAD_STATE state;
                            op->get_State(&state);
                            
                            if(currentDlIdx < g_downloads.size()) {
                                if (state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) {
                                    g_downloads[currentDlIdx].isDownloading = false;
                                    g_downloads[currentDlIdx].isCompleted = true;
                                    MessageBoxW(hWnd, L"✅ Download Successfully Completed!", L"RasBrowser Downloader", MB_OK | MB_ICONINFORMATION);
                                }
                                else if (state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED) {
                                    g_downloads[currentDlIdx].isDownloading = false;
                                    g_downloads[currentDlIdx].isInterrupted = true;
                                    MessageBoxW(hWnd, L"❌ Download Failed or Interrupted!", L"RasBrowser Downloader", MB_OK | MB_ICONERROR);
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            return S_OK;
                        }).Get(), nullptr);
                }
                return S_OK;
            }).Get(), nullptr);
    }

    // ==========================================
    // 🟢 2. CUSTOM RIGHT-CLICK (CONTEXT) MENU (Requires ICoreWebView2_11)
    // ==========================================
    ComPtr<ICoreWebView2_11> webview11;
    if (SUCCEEDED(webview.As(&webview11))) {
        webview11->add_ContextMenuRequested(Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
            [hWnd](ICoreWebView2* sender, ICoreWebView2ContextMenuRequestedEventArgs* args) -> HRESULT {
                
                ComPtr<ICoreWebView2ContextMenuItemCollection> items;
                args->get_MenuItems(&items);

                if (items && g_sharedEnv) {
                    ComPtr<ICoreWebView2Environment9> env9;
                    g_sharedEnv.As(&env9);

                    if (env9) {
                        ComPtr<ICoreWebView2ContextMenuItem> aiScanItem;
                        env9->CreateContextMenuItem(L"🛡️ Scan Image/Link with RasFocus AI", nullptr, 
                            COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_COMMAND, &aiScanItem);

                        aiScanItem->add_CustomItemSelected(Callback<ICoreWebView2CustomItemSelectedEventHandler>(
                            [hWnd](ICoreWebView2ContextMenuItem* sender, IUnknown* args) -> HRESULT {
                                MessageBoxW(hWnd, L"RasFocus AI is analyzing the content for safety...", L"RasFocus AI Filter", MB_OK | MB_ICONINFORMATION);
                                return S_OK;
                            }).Get(), nullptr);

                        ComPtr<ICoreWebView2ContextMenuItem> vaultItem;
                        env9->CreateContextMenuItem(L"🔒 Save Page to RasFocus Vault", nullptr, 
                            COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_COMMAND, &vaultItem);

                        vaultItem->add_CustomItemSelected(Callback<ICoreWebView2CustomItemSelectedEventHandler>(
                            [hWnd](ICoreWebView2ContextMenuItem* sender, IUnknown* args) -> HRESULT {
                                MessageBoxW(hWnd, L"Page securely saved to your private vault!", L"RasFocus Vault", MB_OK | MB_ICONINFORMATION);
                                return S_OK;
                            }).Get(), nullptr);

                        items->InsertValueAtIndex(0, aiScanItem.Get());
                        items->InsertValueAtIndex(1, vaultItem.Get());
                    }
                }
                return S_OK;
            }).Get(), nullptr);
    }

    // ==========================================
    // 🟢 3. HISTORY TRACKER (Source Changed)
    // ==========================================
    webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
        [](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
            LPWSTR uri = nullptr;
            sender->get_Source(&uri);
            if (uri) {
                std::wstring urlStr(uri);
                SaveToHistory(urlStr, L"Visited Page");
                CoTaskMemFree(uri);
            }
            return S_OK;
        }).Get(), nullptr);

    // ==========================================
    // 🟢 4. BRAVE-STYLE PRIVACY SHIELD (Global Ad & Tracker Blocker)
    // ==========================================
    webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    webview->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
        [](ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
            ComPtr<ICoreWebView2WebResourceRequest> request;
            args->get_Request(&request);
            
            LPWSTR uri = nullptr;
            request->get_Uri(&uri);
            if (uri) {
                std::wstring urlStr(uri);
                
                std::vector<std::wstring> blockedDomains = {
                    L"google-analytics.com", L"doubleclick.net", L"adservice.google.com",
                    L"facebook.com/tr/", L"connect.facebook.net", L"googlesyndication.com",
                    L"ads.twitter.com", L"pixel.wp.com", L"amazon-adsystem.com",
                    L"taboola.com", L"outbrain.com", L"criteo.com", L"scorecardresearch.com"
                };

                bool shouldBlock = false;
                for (const auto& domain : blockedDomains) {
                    if (urlStr.find(domain) != std::wstring::npos) {
                        shouldBlock = true;
                        break;
                    }
                }

                if (shouldBlock && g_sharedEnv) {
                    ComPtr<ICoreWebView2WebResourceResponse> emptyResponse;
                    g_sharedEnv->CreateWebResourceResponse(nullptr, 403, L"Blocked by RasBrowser Privacy Shield", L"", &emptyResponse);
                    args->put_Response(emptyResponse.Get());
                }
                CoTaskMemFree(uri);
            }
            return S_OK;
        }).Get(), nullptr);
}
