#include "feature_browser.h"
#include <vector>

using namespace Gdiplus;
using namespace std;

void DrawFeatureBrowser(Graphics& g, float x, float y, float width) {
    FontFamily ff(L"Segoe UI");
    Font fSubtitle(&ff, 18, FontStyleRegular, UnitPixel); 
    Font fItemBold(&ff, 16, FontStyleBold, UnitPixel);    
    
    FontFamily ffi(L"Segoe MDL2 Assets");
    Font fIcon(&ffi, 16, FontStyleBold, UnitPixel);       

    SolidBrush brSubtitle(Color(255, 110, 110, 110)); 
    SolidBrush brText(Color(255, 40, 40, 40));        
    SolidBrush brCheck(Color(255, 0, 102, 204));      

    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);

    g.DrawString(L"Like your regular browser, but faster, and more secure", -1, &fSubtitle, PointF(x, y), &sf, &brSubtitle);
    y += 45.0f;

    vector<wstring> features = {
        L"Stop online ads from loading to declutter web pages",
        L"Keep your passwords secure, log in with one click",
        L"Import bookmarks from your current web browser",
        L"Block dangerous sites, downloads, and extensions",
        L"Smart AI Filter to automatically block and blur inappropriate content", 
        L"Built-in Gemini AI integration for lightning-fast answers"              
    };

    for (size_t i = 0; i < features.size(); i++) {
        g.DrawString(L"\xE73E", -1, &fIcon, PointF(x, y + 2.0f), &sf, &brCheck);
        g.DrawString(features[i].c_str(), -1, &fItemBold, PointF(x + 30.0f, y), &sf, &brText);
        y += 35.0f; 
    }
}
