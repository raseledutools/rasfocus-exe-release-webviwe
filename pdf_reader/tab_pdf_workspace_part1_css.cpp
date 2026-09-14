// ============================================================
//  tab_pdf_workspace_part1_css.cpp
//  Acrobat DC — pixel-accurate CSS
// ============================================================

#define _CRT_SECURE_NO_WARNINGS
#include "tab_pdf_workspace.h"
#include "../browser/mini_browser.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <Shlwapi.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/event.h>

#pragma comment(lib, "Shlwapi.lib")

using namespace Gdiplus;
using namespace Microsoft::WRL;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;
extern wstring currentWorkspacePdf;

// --- WebView2 Global Variables ---
HWND g_hAcrobatWnd = NULL;
HWND g_hWebViewWnd = NULL;
ComPtr<ICoreWebView2Environment> g_webViewEnv = nullptr;
ComPtr<ICoreWebView2Controller> g_webViewController = nullptr;
ComPtr<ICoreWebView2> g_webView = nullptr;
wstring g_acrobatPdfPath = L"";
bool g_webViewInitialized = false;

// Forward Declarations
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd);
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

static std::wstring GetAcrobatHTML()
{
    std::wstringstream ss;

// ─────────────────────────────────────────────────────────────
// PART 01 · DOCTYPE + CDN scripts
// ─────────────────────────────────────────────────────────────
ss << LR"XHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Adobe Acrobat</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
</head>
<body>
)XHTML";

// ─────────────────────────────────────────────────────────────
// PART 02 · CSS Variables + Reset  (Sumatra-style compact palette)
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
:root{
  --ac-red:#C0392B;
  --ac-red-h:#A93226;
  --ac-topbar:#1C1C1C;
  --ac-tabbar:#2A2A2A;
  --ac-tab-bg:#363636;
  --ac-tab-active:#FFFFFF;
  --ac-toolbar:#F2F2F2;
  --ac-toolbar-border:#CCCCCC;
  --ac-viewer:#525252;
  --ac-sidebar:#EBEBEB;
  --ac-sidebar-hdr:#D0D0D0;
  --ac-text:#1A1A1A;
  --ac-muted:#777;
  --ac-light:#BBB;
  --ac-border:#DEDEDE;
  --ac-blue:#2980B9;
  --ac-page:#FFF;
  --ac-shadow:0 2px 8px rgba(0,0,0,.40);
  --radius:2px;
  --topbar-h:26px;
  --tabbar-h:24px;
  --toolbar-h:28px;
  --statusbar-h:20px;
}
*{margin:0;padding:0;box-sizing:border-box;}
html,body{
  height:100vh;overflow:hidden;
  font-family:'Segoe UI',Arial,sans-serif;font-size:11px;
  background:var(--ac-viewer);color:var(--ac-text);user-select:none;
}
::-webkit-scrollbar{width:6px;height:6px;}
::-webkit-scrollbar-track{background:#3a3a3a;}
::-webkit-scrollbar-thumb{background:#686868;border-radius:0;}
::-webkit-scrollbar-thumb:hover{background:#909090;}
#app{display:flex;flex-direction:column;height:100vh;overflow:hidden;}
.workspace{display:flex;flex:1;min-height:0;overflow:hidden;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 03 · Topbar — Sumatra-style compact single strip
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
/* ── Topbar: one compact dark strip with menus + nav + zoom + tools ── */
.topbar{
  height:var(--topbar-h);background:var(--ac-topbar);
  display:flex;align-items:center;padding:0 4px 0 4px;
  gap:0;flex-shrink:0;border-bottom:1px solid #111;z-index:200;
}
/* File icon button (replaces logo) */
.ac-logo{
  width:22px;height:var(--topbar-h);display:flex;align-items:center;justify-content:center;
  background:var(--ac-red);flex-shrink:0;margin-right:2px;
  font-size:11px;font-weight:900;color:#fff;letter-spacing:-1px;
  font-style:italic;cursor:pointer;
}
/* Menu items */
.top-menu{
  color:#ccc;font-size:11px;padding:0 7px;height:var(--topbar-h);
  display:flex;align-items:center;
  cursor:pointer;white-space:nowrap;position:relative;
}
.top-menu:hover,.top-menu.open{background:rgba(255,255,255,.12);color:#fff;}
/* Separator */
.top-sep{width:1px;height:12px;background:#484848;margin:0 3px;flex-shrink:0;}
/* Page nav group (compact inline) */
.tb-nav{
  display:flex;align-items:center;gap:2px;padding:0 4px;
  height:var(--topbar-h);
}
.tb-nav-btn{
  color:#bbb;cursor:pointer;padding:0 3px;height:18px;
  display:flex;align-items:center;justify-content:center;
  font-size:12px;border-radius:1px;
}
.tb-nav-btn:hover{background:rgba(255,255,255,.12);color:#fff;}
.tb-page-input{
  width:32px;padding:1px 3px;border:1px solid #555;border-radius:1px;
  font-size:10px;text-align:center;background:#2a2a2a;color:#ddd;outline:none;
}
.tb-page-input:focus{border-color:#888;}
.tb-page-total{font-size:10px;color:#888;white-space:nowrap;}
/* Zoom group */
.tb-zoom{display:flex;align-items:center;gap:2px;padding:0 4px;}
.tb-zoom-btn{
  width:18px;height:18px;background:#333;border:1px solid #484848;
  border-radius:1px;cursor:pointer;font-size:13px;color:#ccc;
  display:flex;align-items:center;justify-content:center;flex-shrink:0;
}
.tb-zoom-btn:hover{background:#484848;color:#fff;}
.tb-zoom-input{
  width:44px;padding:1px 3px;border:1px solid #555;border-radius:1px;
  font-size:10px;text-align:center;background:#2a2a2a;color:#ddd;outline:none;
}
.tb-zoom-input:focus{border-color:#888;}
/* Tool buttons in topbar (Sumatra style small icons) */
.tb-tool{
  display:flex;align-items:center;justify-content:center;
  width:22px;height:22px;border-radius:1px;cursor:pointer;
  color:#bbb;border:1px solid transparent;flex-shrink:0;
  position:relative;
}
.tb-tool svg{width:14px;height:14px;fill:currentColor;pointer-events:none;}
.tb-tool:hover{background:rgba(255,255,255,.12);color:#fff;border-color:#555;}
.tb-tool.active{background:rgba(255,255,255,.18);color:#fff;border-color:#777;}
.tb-tool-sep{width:1px;height:14px;background:#484848;margin:0 2px;flex-shrink:0;}
/* Mode toggle buttons */
.tb-mode{
  display:flex;align-items:center;gap:1px;padding:1px;
  background:#2a2a2a;border:1px solid #484848;border-radius:2px;
}
.tb-mode-btn{
  padding:1px 7px;font-size:9.5px;border-radius:1px;cursor:pointer;
  color:#999;white-space:nowrap;
}
.tb-mode-btn:hover{color:#fff;background:rgba(255,255,255,.08);}
.tb-mode-btn.active{background:var(--ac-red);color:#fff;}
/* Right side icons */
.top-right{margin-left:auto;display:flex;align-items:center;gap:1px;height:100%;}
.top-icon{
  color:#bbb;cursor:pointer;padding:0 5px;height:100%;
  display:flex;align-items:center;font-size:13px;
}
.top-icon:hover{background:rgba(255,255,255,.10);color:#fff;}
/* Dropdown */
.dropdown{
  display:none;position:fixed;background:#2e2e2e;border:1px solid #484848;
  box-shadow:0 4px 16px rgba(0,0,0,.6);z-index:9000;
  min-width:190px;padding:2px 0;border-radius:1px;
}
.dropdown.show{display:block;}
.dd-item{
  color:#ccc;font-size:11px;padding:4px 14px;cursor:pointer;
  display:flex;align-items:center;gap:9px;white-space:nowrap;
}
.dd-item:hover{background:#3e3e3e;color:#fff;}
.dd-item.danger{color:#f08080;}
.dd-item.danger:hover{background:#5a2828;}
.dd-sep{height:1px;background:#484848;margin:2px 0;}
.dd-shortcut{margin-left:auto;color:#777;font-size:9.5px;padding-left:18px;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 04 · Tabbar  (Sumatra-style compact dark tabs)
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.tabbar{
  height:var(--tabbar-h);background:var(--ac-tabbar);
  display:flex;align-items:flex-end;padding-left:0;
  overflow-x:auto;flex-shrink:0;border-bottom:1px solid #111;
}
.tabbar::-webkit-scrollbar{height:0;}
.pdf-tab{
  height:var(--tabbar-h);background:var(--ac-tab-bg);color:#999;
  padding:0 8px 0 7px;
  display:flex;align-items:center;gap:4px;font-size:10.5px;
  border-radius:0;margin-right:1px;cursor:pointer;
  max-width:180px;white-space:nowrap;
  border-right:1px solid #222;
  flex-shrink:0;
}
.pdf-tab:hover{background:#444;color:#ddd;}
.pdf-tab.active{
  background:#E8E8E8;color:#1a1a1a;font-weight:600;
  border-top:2px solid var(--ac-red);border-right:1px solid #ccc;
}
.pdf-tab .tab-icon{font-size:10px;opacity:.5;}
.tab-name{overflow:hidden;text-overflow:ellipsis;max-width:120px;}
.tab-modified{color:var(--ac-red);font-size:8px;margin-left:1px;}
.tab-close{
  font-size:11px;cursor:pointer;opacity:0;border-radius:2px;
  width:13px;height:13px;display:flex;align-items:center;justify-content:center;
  flex-shrink:0;margin-left:2px;
}
.pdf-tab:hover .tab-close{opacity:.5;}
.tab-close:hover{opacity:1!important;background:rgba(0,0,0,.15);color:var(--ac-red);}
.tab-add{
  color:#666;cursor:pointer;padding:0 8px;font-size:15px;
  height:var(--tabbar-h);display:flex;align-items:center;flex-shrink:0;
}
.tab-add:hover{color:#ddd;background:#444;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 05 · Quick-bar stub (hidden — tools moved to topbar)
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
/* quick-bar is hidden; tools live in topbar now */
.quick-bar{display:none!important;}
/* Keep qb- class stubs so JS references don't break */
.qb-btn{} .qb-zoom-input{} .qb-zoom-btn{} .qb-group{} .qb-lbl{}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 06 · Left tool panel  (Acrobat DC vertical icon strip)
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
/* ── Left vertical tool panel ── */
.lv-panel{
  width:0;background:#EFEFEF;border-right:1px solid var(--ac-border);
  display:flex;flex-direction:column;flex-shrink:0;overflow:hidden;
  transition:width .15s;
}
.lv-panel.open{width:200px;}
.lv-tabs{
  display:flex;background:#E0E0E0;border-bottom:1px solid var(--ac-border);
  flex-shrink:0;
}
.lv-tab{
  flex:1;text-align:center;padding:5px 0;font-size:9.5px;cursor:pointer;
  border-right:1px solid var(--ac-border);color:var(--ac-muted);
}
.lv-tab:last-child{border-right:none;}
.lv-tab:hover{background:#D4D4D4;}
.lv-tab.active{background:var(--ac-toolbar);color:var(--ac-text);font-weight:700;border-top:2px solid var(--ac-red);}
.lv-body{flex:1;overflow-y:auto;overflow-x:hidden;}
/* Thumbnails */
.thumb-list{padding:6px;display:flex;flex-direction:column;gap:4px;}
.thumb-item{
  background:#fff;border:2px solid transparent;
  cursor:pointer;position:relative;box-shadow:0 1px 3px rgba(0,0,0,.18);
}
.thumb-item:hover{border-color:#aaa;}
.thumb-item.selected{border-color:var(--ac-blue);}
.thumb-item canvas{width:100%;display:block;}
.thumb-pg-num{
  position:absolute;bottom:0;left:0;right:0;text-align:center;
  font-size:8px;color:#555;background:rgba(255,255,255,.8);padding:1px 0;
}
.thumb-del{
  position:absolute;top:2px;right:2px;width:14px;height:14px;
  background:var(--ac-red);color:#fff;border-radius:50%;
  font-size:8px;display:none;align-items:center;justify-content:center;cursor:pointer;
}
.thumb-item:hover .thumb-del{display:flex;}
/* Bookmarks */
.bm-item{
  padding:5px 10px;font-size:11px;cursor:pointer;
  border-bottom:1px solid var(--ac-border);color:var(--ac-text);
  display:flex;align-items:center;gap:6px;
}
.bm-item:hover{background:#DDEEFF;}
.bm-item .bm-del{margin-left:auto;opacity:0;font-size:11px;color:var(--ac-red);}
.bm-item:hover .bm-del{opacity:1;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 07 · PDF Viewer area + page rendering
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.pdf-viewer-area{
  flex:1;overflow:auto;background:var(--ac-viewer);
  display:flex;justify-content:center;position:relative;cursor:default;
}
.pdf-container{
  display:flex;flex-direction:column;gap:8px;
  align-items:center;padding:8px 10px;width:100%;
  transform-origin:top center;will-change:transform;
}
/* Page */
.page-wrapper{
  position:relative;background:var(--ac-page);
  box-shadow:var(--ac-shadow);flex-shrink:0;display:block;
}
.page-wrapper .pdf-canvas{display:block;position:relative;z-index:1;}
.page-wrapper .draw-canvas{
  position:absolute;top:0;left:0;z-index:2;pointer-events:none;
}
.page-wrapper .draw-canvas.can-draw{pointer-events:auto;}
.page-wrapper .text-layer{
  position:absolute;top:0;left:0;z-index:3;pointer-events:none;
  overflow:hidden;opacity:.2;line-height:1;
}
/* Page placeholder */
.page-placeholder{
  position:absolute;top:0;left:0;width:100%;height:100%;
  display:flex;align-items:center;justify-content:center;
  background:#e4e4e4;color:#bbb;font-size:14px;font-weight:600;
  pointer-events:none;z-index:0;
}
/* Overlays */
.grid-overlay{
  position:absolute;inset:0;pointer-events:none;z-index:19;display:none;
  background-image:linear-gradient(rgba(0,80,200,.05) 1px,transparent 1px),
    linear-gradient(90deg,rgba(0,80,200,.05) 1px,transparent 1px);
  background-size:20px 20px;
}
.grid-overlay.show{display:block;}
.sel-rect{
  position:absolute;border:1.5px dashed var(--ac-blue);
  background:rgba(20,115,230,.07);pointer-events:none;z-index:30;display:none;
}
.redact-rect{position:absolute;background:#000;z-index:22;cursor:pointer;border:2px solid var(--ac-red);}
.stamp-el{
  position:absolute;z-index:18;pointer-events:auto;cursor:move;
  font-size:20px;font-weight:900;letter-spacing:2px;opacity:.5;
  border:3px solid;border-radius:3px;padding:2px 10px;text-transform:uppercase;
}
.textbox-el{
  position:absolute;z-index:16;background:rgba(255,255,248,.9);
  border:1.5px solid var(--ac-blue);min-width:80px;min-height:24px;
  padding:3px 6px;font-size:12px;color:#111;resize:both;overflow:auto;cursor:move;
}
.sig-el{position:absolute;z-index:17;cursor:move;}
.sig-el canvas{border:1px dashed #aaa;}
.shape-el{position:absolute;z-index:15;pointer-events:auto;cursor:move;}
.crop-overlay{position:absolute;inset:0;z-index:25;cursor:crosshair;background:rgba(0,0,0,.35);display:none;}
.crop-overlay.show{display:block;}
.crop-rect{position:absolute;border:2px solid var(--ac-blue);background:transparent;box-shadow:0 0 0 9999px rgba(0,0,0,.4);pointer-events:none;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 08 · Right Properties Panel
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.right-panel{
  width:0;background:#F5F5F5;
  border-left:1px solid #D0D0D0;
  display:flex;flex-direction:column;overflow-y:auto;flex-shrink:0;
  transition:width .15s;
}
.right-panel.open{width:220px;}
.right-panel.collapsed{width:0;border:none;overflow:hidden;}
.rp-section{border-bottom:1px solid var(--ac-border);}
.rp-header{
  padding:6px 10px;font-size:10px;font-weight:700;color:var(--ac-muted);
  text-transform:uppercase;letter-spacing:.5px;display:flex;
  align-items:center;gap:4px;cursor:pointer;
  background:var(--ac-sidebar-hdr);border-bottom:1px solid var(--ac-border);
}
.rp-header:hover{background:#CACACA;}
.rp-header .toggle{margin-left:auto;font-size:10px;}
.rp-body{padding:8px 10px;display:flex;flex-direction:column;gap:6px;}
.rp-row{display:flex;align-items:center;gap:6px;flex-wrap:wrap;}
.rp-label{font-size:10.5px;color:var(--ac-muted);min-width:52px;}
.rp-input{
  flex:1;padding:3px 5px;border:1px solid var(--ac-border);border-radius:var(--radius);
  font-size:11px;outline:none;min-width:50px;background:#fff;
}
.rp-input:focus{border-color:var(--ac-blue);box-shadow:0 0 0 2px rgba(20,115,230,.15);}
.rp-btn{
  width:100%;padding:5px 0;border:1px solid var(--ac-border);border-radius:var(--radius);
  background:#fff;cursor:pointer;font-size:11px;font-weight:600;
  color:var(--ac-text);transition:background .1s;
  display:flex;align-items:center;justify-content:center;gap:5px;
}
.rp-btn:hover{background:#E8E8E8;}
.rp-btn.danger{color:var(--ac-red);border-color:#F5B8B8;}
.rp-btn.danger:hover{background:#FDE8E8;}
.rp-btn.primary{background:var(--ac-red);color:#fff;border-color:var(--ac-red);}
.rp-btn.primary:hover{background:var(--ac-red-h);}
.badge{
  display:inline-block;background:var(--ac-blue);color:#fff;
  font-size:9px;padding:1px 5px;border-radius:8px;font-weight:700;
}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 09 · Statusbar  (Acrobat DC bottom strip)
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.statusbar{
  height:var(--statusbar-h);background:#2A2A2A;border-top:1px solid #111;
  display:flex;align-items:center;padding:0 8px;gap:8px;
  flex-shrink:0;z-index:100;
}
.sb-item{font-size:10px;color:#888;white-space:nowrap;}
.sb-sep{width:1px;height:10px;background:#484848;margin:0 2px;}
.sb-zoom-row{display:flex;align-items:center;gap:2px;}
.sb-zoom-btn{
  width:16px;height:16px;background:#383838;border:1px solid #484848;
  border-radius:1px;cursor:pointer;font-size:12px;color:#aaa;
  display:flex;align-items:center;justify-content:center;line-height:1;
}
.sb-zoom-btn:hover{background:#484848;color:#fff;}
#sb-zoom-val{
  font-size:10px;min-width:32px;text-align:center;
  color:#bbb;cursor:default;
}
.sb-right{margin-left:auto;display:flex;align-items:center;gap:6px;}
/* Find bar */
.findbar{
  display:none;position:absolute;bottom:var(--statusbar-h);right:12px;
  background:#fff;border:1px solid var(--ac-border);
  box-shadow:var(--ac-shadow);padding:6px 8px;z-index:500;
  align-items:center;gap:6px;border-radius:2px;
}
.findbar.show{display:flex;}
.findbar input{
  padding:3px 7px;border:1px solid var(--ac-border);border-radius:var(--radius);
  font-size:11.5px;outline:none;width:180px;
}
.findbar input:focus{border-color:var(--ac-blue);}
.find-btn{
  padding:3px 8px;border:1px solid var(--ac-border);border-radius:var(--radius);
  background:#f0f0f0;cursor:pointer;font-size:11px;
}
.find-btn:hover{background:#e0e0e0;}
#find-count{font-size:10.5px;color:var(--ac-muted);min-width:60px;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 10 · Toast, Loading, Modal, Night/Read/Presentation
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
/* Toast */
.toast-box{
  position:fixed;bottom:32px;left:50%;transform:translateX(-50%);
  z-index:9999;display:flex;flex-direction:column;gap:5px;pointer-events:none;
}
.toast{
  padding:7px 18px;border-radius:2px;background:#2a2a2a;color:#fff;
  font-size:11.5px;font-weight:500;box-shadow:0 3px 10px rgba(0,0,0,.3);
  animation:toastIn .18s ease;
}
.toast.success{background:#1a7a4a;}
.toast.warn{background:#b86e00;}
@keyframes toastIn{from{transform:translateY(8px);opacity:0}to{transform:none;opacity:1}}
/* Loading */
.loading-overlay{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,.5);
  z-index:9000;flex-direction:column;align-items:center;justify-content:center;color:#fff;
}
.loading-overlay.show{display:flex;}
.spinner{
  width:28px;height:28px;border:3px solid rgba(255,255,255,.2);
  border-top:3px solid var(--ac-red);border-radius:50%;
  animation:spin .6s linear infinite;margin-bottom:12px;
}
@keyframes spin{to{transform:rotate(360deg)}}
#loading-txt{font-size:12px;font-weight:600;}
.progress-wrap{background:#333;border-radius:0;height:3px;overflow:hidden;margin-top:10px;width:200px;}
.progress-bar{height:100%;background:var(--ac-red);transition:width .2s;}
/* Modal */
.modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.45);z-index:8000;align-items:center;justify-content:center;}
.modal-overlay.show{display:flex;}
.modal{background:#fff;border-radius:2px;padding:20px 22px;min-width:320px;max-width:500px;box-shadow:0 8px 32px rgba(0,0,0,.28);width:100%;}
.modal h3{font-size:14px;font-weight:700;margin-bottom:14px;color:var(--ac-text);}
.modal label{font-size:11px;color:var(--ac-muted);display:block;margin-bottom:3px;}
.modal input[type=text],.modal input[type=number],.modal input[type=password],
.modal textarea,.modal select{
  width:100%;padding:6px 8px;border:1px solid var(--ac-border);border-radius:var(--radius);
  font-size:12px;outline:none;margin-bottom:10px;font-family:inherit;background:#fff;
}
.modal input:focus,.modal textarea:focus,.modal select:focus{border-color:var(--ac-blue);}
.modal textarea{resize:vertical;min-height:60px;}
.modal-actions{display:flex;gap:8px;justify-content:flex-end;margin-top:6px;}
.btn{padding:6px 16px;border:none;border-radius:var(--radius);cursor:pointer;font-size:12px;font-weight:600;}
.btn-primary{background:var(--ac-blue);color:#fff;}
.btn-primary:hover{background:#0E5FCC;}
.btn-secondary{background:#f0f0f0;border:1px solid #ccc;color:var(--ac-text);}
.btn-secondary:hover{background:#e4e4e4;}
/* Night / Sepia / Read / Presentation */
body.night .pdf-viewer-area{background:#1a1a1a!important;}
body.night .page-wrapper{filter:invert(1) hue-rotate(180deg);}
body.night .quick-bar,.night .right-panel,.night .lv-panel{background:#252525!important;border-color:#333!important;}
body.night .statusbar{background:#1c1c1c!important;border-color:#333!important;}
body.sepia .page-wrapper{filter:sepia(.55) brightness(.96);}
body.read .lv-panel,body.read .right-panel,body.read .quick-bar,body.read .statusbar{display:none!important;}
body.read .pdf-viewer-area{background:#111!important;}
#read-edit-btn{
  display:none;position:fixed;top:8px;right:12px;z-index:9999;
  background:var(--ac-blue);color:#fff;border:none;border-radius:2px;
  padding:5px 14px;font-size:12px;font-weight:700;cursor:pointer;
}
body.read #read-edit-btn{display:block;}
body.present *{cursor:none!important;}
body.present .lv-panel,body.present .right-panel,body.present .quick-bar,
body.present .tabbar,body.present .topbar,body.present .statusbar{display:none!important;}
body.present .pdf-viewer-area{background:#000!important;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 11 · Context menu + Color picker
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.ctx-menu{
  display:none;position:fixed;background:#fff;border:1px solid #C8C8C8;
  box-shadow:0 4px 16px rgba(0,0,0,.18);z-index:7000;
  padding:3px 0;min-width:180px;border-radius:1px;
}
.ctx-menu.show{display:block;}
.ctx-item{
  padding:5px 14px;font-size:11.5px;cursor:pointer;color:var(--ac-text);
  display:flex;align-items:center;gap:8px;
}
.ctx-item:hover{background:#E8F4FF;}
.ctx-item.danger{color:var(--ac-red);}
.ctx-sep{height:1px;background:var(--ac-border);margin:3px 0;}
.color-picker-popup{
  display:none;position:fixed;background:#fff;border:1px solid var(--ac-border);
  box-shadow:0 4px 16px rgba(0,0,0,.2);padding:10px;z-index:6000;border-radius:2px;
}
.color-picker-popup.show{display:block;}
.color-grid{display:grid;grid-template-columns:repeat(8,20px);gap:3px;}
.color-cell{
  width:20px;height:20px;border-radius:1px;cursor:pointer;border:1px solid rgba(0,0,0,.12);
  transition:transform .1s;
}
.color-cell:hover{transform:scale(1.2);z-index:1;position:relative;}
#sig-modal-canvas{border:1px solid var(--ac-border);cursor:crosshair;touch-action:none;}
</style>
)CSS";

