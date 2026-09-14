#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <iostream>

using namespace std;

// ফাংশন: মেইন অ্যাপটি রানিং আছে কি না চেক করবে
bool IsMainAppRunning(const std::wstring& processName) {
    bool exists = false;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(hSnap, &entry)) {
            do {
                if (processName == entry.szExeFile) {
                    exists = true;
                    break;
                }
            } while (Process32NextW(hSnap, &entry));
        }
        CloseHandle(hSnap);
    }
    return exists;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    // এই অ্যাপটির কোনো উইন্ডো থাকবে না, এটি পুরোপুরি হিডেন থাকবে
    
    wstring mainAppName = L"RasFocus.exe"; // আপনার মেইন অ্যাপের সঠিক নাম এখানে দিন

    while (true) {
        // মেইন অ্যাপ চলছে কি না চেক করো
        if (!IsMainAppRunning(mainAppName)) {
            // যদি বন্ধ থাকে, তবে অ্যাডমিন পারমিশন নিয়ে সাইলেন্ট মোডে চালু করো
            // lpVerb "runas" ব্যবহার করা হয়েছে অ্যাডমিন পারমিশনের জন্য
            ShellExecuteW(NULL, L"runas", mainAppName.c_str(), L"-silent", NULL, SW_HIDE);
        }
        
        // ২-৩ সেকেন্ড পরপর চেক করবে যাতে পিসির ওপর চাপ না পড়ে
        Sleep(3000); 
    }
    return 0;
}
