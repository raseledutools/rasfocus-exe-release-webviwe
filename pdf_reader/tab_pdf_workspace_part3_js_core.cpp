// PART 17 · JS: globals, utils, pdf.js init
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
pdfjsLib.GlobalWorkerOptions.workerSrc =
  'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js';

// ── Global state ─────────────────────────────────────────────
let g_tabs = [], g_activeId = null;
let g_tool = 'hand';
let g_color = '#e53935', g_fillColor = 'transparent';
let g_lineWidth = 2, g_opacity = 1.0;
let g_fontSize = 12, g_fontFamily = 'Arial';
let g_sigColor = '#1a1a1a';
let g_showRuler = false, g_showGrid = false;
let g_colorTarget = 'stroke', g_colorSwatchId = null;
let g_viewMode = 0; // 0=normal,1=night,2=sepia
let g_ctxPageIndex = -1;

// Drawing state
let g_drawing = false, g_drawCtx = null, g_drawCanvas = null;
let g_currentStroke = null;
let g_shapeStart = null;

// Pan state
let g_panning = false, g_panX0 = 0, g_panY0 = 0, g_scrollX0 = 0, g_scrollY0 = 0;

// Undo/redo
const MAX_HIST = 60;

// Current visible page tracking (for statusbar)
let g_currentVisiblePage = 1;

// Lazy render tracking
let g_pageObserver = null;   // IntersectionObserver for lazy render
let g_renderedPages = new Set(); // which pageIndex slots already rendered
)JS";

ss << LR"JS(
// ── Helpers ───────────────────────────────────────────────────
function activeTab() { return g_tabs.find(t => t.id === g_activeId); }

function showToast(msg, type='', dur=3000) {
  const box = document.getElementById('toast-box');
  const el = document.createElement('div');
  el.className = 'toast' + (type ? ' ' + type : '');
  el.textContent = msg;
  box.appendChild(el);
  setTimeout(() => el.remove(), dur);
}

function showLoading(on, txt='Processing…', pct=0) {
  document.getElementById('loading-overlay').classList.toggle('show', on);
  document.getElementById('loading-txt').textContent = txt;
  document.getElementById('loading-progress').style.width = pct + '%';
}

function setProgress(pct) {
  document.getElementById('loading-progress').style.width = pct + '%';
}

function showModal(title, html, wide=false) {
  const body = document.getElementById('modal-body');
  body.innerHTML = '<h3>' + title + '</h3>' + html;
  if (wide) body.style.maxWidth = '600px'; else body.style.maxWidth = '';
  document.getElementById('modal-overlay').classList.add('show');
}

function closeModal() {
  document.getElementById('modal-overlay').classList.remove('show');
}

document.getElementById('modal-overlay').addEventListener('mousedown', e => {
  if (e.target.id === 'modal-overlay') closeModal();
});
)JS";

ss << LR"JS(
async function saveBytesToFile(blob, name, ext='pdf', mime='application/pdf') {
  try {
    if (window.showSaveFilePicker) {
      const h = await window.showSaveFilePicker({
        suggestedName: name,
        types: [{ description: 'File', accept: { [mime]: ['.' + ext] } }]
      });
      const w = await h.createWritable();
      await w.write(blob);
      await w.close();
      showToast('Saved: ' + name, 'success');
    } else {
      saveAs(blob, name);
      showToast('Downloaded: ' + name, 'success');
    }
  } catch (e) {
    if (e.name !== 'AbortError') showToast('Save cancelled.');
  }
}

function updateStatusBar(currentPage) {
  const t = activeTab();
  if (!t) {
    const inp = document.getElementById('sb-page-input');
    if (inp) inp.value = 0;
    const tot = document.getElementById('sb-page-total');
    if (tot) tot.textContent = 'of 0';
    return;
  }
  const pg = (typeof currentPage === 'number') ? currentPage : (g_currentVisiblePage || 1);
  const inp = document.getElementById('sb-page-input');
  if (inp && document.activeElement !== inp) inp.value = pg; // focus এ থাকলে overwrite না
  const tot = document.getElementById('sb-page-total');
  if (tot) tot.textContent = 'of ' + t.pageOrder.length;
  if (inp) inp.max = t.pageOrder.length;
  const zv = Math.round(t.zoom * 100) + '%';
  document.getElementById('sb-zoom-val').textContent = zv;
  const qd = document.getElementById('qb-zoom-display'); if(qd && document.activeElement !== qd) qd.value = zv;
  document.getElementById('rp-zoom').textContent = zv;
  document.getElementById('rp-rotate').textContent = t.rotation + '°';
  document.getElementById('rp-filename').textContent = t.name;
  document.getElementById('rp-pages').textContent = t.pageOrder.length;
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 18 · JS: Tab management + file loading
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function openFileDialog(multi=false) {
  const fi = document.getElementById('fileInput');
  fi.multiple = multi;
  fi.click();
}

async function handleFiles(e) {
  const files = Array.from(e.target.files).filter(f => f.name.toLowerCase().endsWith('.pdf'));
  for (const f of files) {
    const bytes = new Uint8Array(await f.arrayBuffer());
    await createTab(f.name, bytes);
  }
  e.target.value = '';
}

async function loadPdfFromPath(path) {
  try {
    const url = 'file:///' + path.replace(/\\/g, '/');
    const res = await fetch(url);
    if (!res.ok) throw new Error('fetch failed');
    const bytes = new Uint8Array(await res.arrayBuffer());
    const name = path.split(/[\\/]/).pop();
    await createTab(name, bytes);
  } catch (e) {
    showToast('Failed to load: ' + e.message);
  }
}
)JS";

ss << LR"JS(
// ── Recent Files (localStorage simulation via in-memory) ────────
let g_recentFiles = [];

function addToRecent(name, bytes) {
  // Keep last 8 unique files
  g_recentFiles = g_recentFiles.filter(r => r.name !== name);
  g_recentFiles.unshift({ name, bytes: bytes.slice(), date: new Date().toLocaleString() });
  if (g_recentFiles.length > 8) g_recentFiles.pop();
}

function renderRecentFiles() {
  const list = document.getElementById('recent-files-list');
  if (!list) return;
  if (g_recentFiles.length === 0) {
    list.innerHTML = '<p style="font-size:11px;color:#888;padding:10px 0;text-align:center;">No recent files</p>';
    return;
  }
  list.innerHTML = g_recentFiles.map((r, i) => `
    <div onclick="reopenRecent(${i})"
      style="display:flex;align-items:center;gap:10px;padding:8px 10px;
        background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.10);
        border-radius:2px;margin-bottom:5px;cursor:pointer;"
      onmouseover="this.style.background='rgba(255,255,255,.13)'"
      onmouseout="this.style.background='rgba(255,255,255,.07)'">
      <span style="font-size:20px;opacity:.7;">&#128196;</span>
      <div style="flex:1;min-width:0;">
        <p style="font-size:12px;font-weight:600;color:#ddd;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${r.name}</p>
        <p style="font-size:10px;color:#888;margin-top:1px;">${r.date}</p>
      </div>
      <span style="font-size:10px;color:#1473E6;font-weight:700;">Open ›</span>
    </div>`).join('');
}

async function reopenRecent(idx) {
  const r = g_recentFiles[idx];
  if (!r) return;
  await createTab(r.name, r.bytes);
}

async function createTab(name, bytes) {
  try {
    // Hide homepage if visible
    const hint = document.getElementById('empty-hint');
    if (hint) hint.remove();
    const doc = await pdfjsLib.getDocument({ data: bytes.slice() }).promise;
    const id = 'tab_' + Date.now() + Math.random().toString(36).slice(2, 6);

    // Auto fit-width zoom: প্রথম page এর width দিয়ে viewer এর available width থেকে zoom calculate করো
    let autoZoom = 1.0;
    try {
      const firstPage = await doc.getPage(1);
      const vp = firstPage.getViewport({ scale: 1.0 });
      const viewerEl = document.getElementById('viewer-area');
      if (viewerEl && vp.width > 0) {
        const availableWidth = viewerEl.clientWidth - 40; // 40px = padding (2×20px)
        autoZoom = Math.min(3.0, Math.max(0.5, availableWidth / vp.width));
      }
    } catch (_) {}

    const tab = {
      id, name, bytes: bytes.slice(), doc,
      zoom: autoZoom, rotation: 0,
      annotations: [],    // draw strokes, notes, shapes, stamps
      pasteImages: [],    // pasted image overlays
      textBoxes: [],      // editable text boxes
      redactions: [],     // redaction rects
      bookmarks: [],      // {pageIndex, label}
      pageOrder: Array.from({ length: doc.numPages }, (_, i) => i + 1),
      history: [], histIdx: -1,
      modified: false
    };
    g_tabs.push(tab);
    addToRecent(name, bytes);
    renderTabStrip();
    await switchTab(id);
  } catch (e) {
    showToast('Invalid or corrupted PDF.');
  }
}

function renderTabStrip() {
  const strip = document.getElementById('tabbar');
  strip.innerHTML = '';
  g_tabs.forEach(t => {
    const el = document.createElement('div');
    el.className = 'pdf-tab' + (t.id === g_activeId ? ' active' : '');
    el.innerHTML =
      '<span class="tab-icon">&#128196;</span>' +
      '<span class="tab-name">' + (t.name.length > 22 ? t.name.slice(0, 20) + '…' : t.name) + '</span>' +
      (t.modified ? '<span class="tab-modified">●</span>' : '') +
      '<span class="tab-close">&#x2715;</span>';
    el.querySelector('.tab-close').onclick = ev => { ev.stopPropagation(); closeTab(t.id); };
    el.onclick = () => switchTab(t.id);
    strip.appendChild(el);
  });
  const add = document.createElement('div');
  add.className = 'tab-add'; add.textContent = '+';
  add.onclick = () => openFileDialog(true);
  strip.appendChild(add);
}
)JS";

ss << LR"JS(
async function switchTab(id) {
  // Fix for the error when opening a second file: Make sure to reset rendering state.
  if (g_renderTimer) clearTimeout(g_renderTimer);
  g_drawing = false;
  g_shapeDrawing = false;
  g_sigDrawing = false;
  g_panning = false;

  g_activeId = id;
  renderTabStrip();
  const t = activeTab(); if (!t) return;
  updateStatusBar();
  await renderViewer();
  buildThumbs();
  renderBookmarkPanel();
  refreshStats();
}

function closeActiveTab() { if (g_activeId) closeTab(g_activeId); }

function closeTab(id) {
  g_tabs = g_tabs.filter(t => t.id !== id);
  if (g_activeId === id) {
    g_activeId = g_tabs.length > 0 ? g_tabs[g_tabs.length - 1].id : null;
  }
  renderTabStrip();
  if (g_activeId) switchTab(g_activeId);
  else {
    showHomepage();
    document.getElementById('lp-thumb').innerHTML = '';
  }
}

function showHomepage() {
  const container = document.getElementById('pdf-container');
  container.innerHTML = `
    <div id="empty-hint" style="margin-top:0;text-align:center;color:#777;pointer-events:auto;width:100%;padding:30px 20px;">
      <div style="font-size:52px;opacity:.25;margin-bottom:8px;">&#128196;</div>
      <p style="font-size:16px;font-weight:700;color:#444;margin-bottom:4px;">PDF Pro — Acrobat Edition</p>
      <p style="font-size:11.5px;opacity:.65;margin-bottom:20px;">File ▸ Open, drag &amp; drop, or click a recent file below</p>
      <div style="display:flex;gap:10px;justify-content:center;margin-bottom:24px;">
        <button onclick="openFileDialog(false)" style="padding:8px 20px;background:var(--c-accent);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128196; Open PDF</button>
        <button onclick="openFileDialog(true)" style="padding:8px 20px;background:var(--c-accent2);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128214; Open Multiple</button>
      </div>
      <div id="recent-files-section" style="max-width:560px;margin:0 auto;text-align:left;">
        <p style="font-size:10px;font-weight:700;color:#999;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;padding-left:4px;">&#128337; Recent Files</p>
        <div id="recent-files-list"></div>
      </div>
    </div>`;
  renderRecentFiles();
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 19 · JS: Rendering engine
//   READ MODE  → native <iframe> — browser renders PDF as pure
//                vector (Edge/Chrome built-in). Zero blurry.
//                Text select, smooth zoom, perfect quality.
//   EDIT MODE  → pdf.js canvas — annotation tools work here.
//   Toggle:      toolbar "Read" button or Ctrl+R
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
// ── Mode flag ─────────────────────────────────────────────────
// g_nativeMode = true  → iframe native reader (sharp, vector)
// g_nativeMode = false → pdf.js canvas (annotation tools)
let g_nativeMode = true;   // start in native (read) mode

let g_renderToken = 0;
let g_renderTimer = null;

// ── Blob URL cache: one URL per tab id ───────────────────────
const g_blobUrls = {};   // { tabId: blobUrl }

function getOrCreateBlobUrl(t) {
  if (g_blobUrls[t.id]) return g_blobUrls[t.id];
  const blob = new Blob([t.bytes], { type: 'application/pdf' });
  g_blobUrls[t.id] = URL.createObjectURL(blob);
  return g_blobUrls[t.id];
}

// ── Switch between native iframe and pdf.js canvas ───────────
function setViewerMode(native) {
  g_nativeMode = native;
  const btnRead = document.getElementById('rb-readmode');
  const btnEdit = document.getElementById('rb-editmode');
  if (btnRead) btnRead.classList.toggle('active',  native);
  if (btnEdit) btnEdit.classList.toggle('active', !native);

  // Restore container style if leaving native mode
  if (!native) {
    const container = document.getElementById('pdf-container');
    const old = container.querySelector('#native-pdf-iframe');
    if (old && old._restoreContainer) old._restoreContainer();
  }

  const sbMode = document.getElementById('sb-mode');
  if (sbMode) sbMode.textContent = native ? 'View' : 'Annotate';

  renderViewer();
  updateStatusBar();
}

// ── applyZoomTransform: only used in pdf.js (edit) mode ──────
function applyZoomTransform() {
  const t = activeTab(); if (!t) return;
  const container = document.getElementById('pdf-container');
  container.style.transform = g_nativeMode ? '' : `scale(${t.zoom})`;
  // Zoom বদলালে canvas quality ঠিক রাখতে re-render দরকার
  if (!g_nativeMode) {
    g_renderedPages.clear();
    observePageWrappers(); // observer re-trigger করো
  }
  updateStatusBar();
}

function scheduleRender(delay = 60) {
  if (g_renderTimer) clearTimeout(g_renderTimer);
  g_renderTimer = setTimeout(() => renderViewer(), delay);
}

// ── Main render dispatcher ────────────────────────────────────
async function renderViewer() {
  const t = activeTab(); if (!t) return;
  if (g_nativeMode) {
    renderNative(t);
  } else {
    await renderCanvas(t);
  }
}

// ── NATIVE MODE: iframe — browser built-in PDF engine ─────────
// Pure vector, text-selectable, smooth zoom — exactly like Edge.
function renderNative(t) {
  const container = document.getElementById('pdf-container');
  container.style.cssText = 'width:100%;height:100%;padding:0;gap:0;';
  container.innerHTML = '';

  const url = getOrCreateBlobUrl(t);

  // Full-bleed iframe — no padding/gap, no browser chrome leaking in
  const iframe = document.createElement('iframe');
  iframe.id  = 'native-pdf-iframe';
  // #toolbar=0 suppresses Edge/Chrome PDF toolbar so only our UI shows
  // view=FitH,0: page width-fit করে, top থেকে শুরু করে (gray gap কমায়)
  iframe.src = url + '#toolbar=0&navpanes=0&scrollbar=1&view=FitH,0';
  iframe.style.cssText = [
    'width:100%',
    'height:100%',
    'border:none',
    'background:#606060',   // match --ac-viewer exactly
    'display:block',
    'flex:1',
  ].join(';');
  iframe.setAttribute('allowfullscreen', '');

  container.appendChild(iframe);

  // Restore container layout after native mode exits
  iframe._restoreContainer = () => {
    container.style.cssText = '';
    container.style.display = 'flex';
    container.style.flexDirection = 'column';
    container.style.gap = '12px';
    container.style.alignItems = 'center';
    container.style.padding = '16px 20px';
    container.style.width = '100%';
    container.style.transformOrigin = 'top center';
  };

  updateStatusBar();
}
</script>
)JS";

// ── CANVAS (EDIT) MODE: pdf.js — annotation tools ─────────────
ss << LR"JS(
<script>
const BASE_SCALE = 1.0;

// ── Virtual/Lazy Canvas Render — Sumatra PDF এর মতো fast ─────
// Step 1: সব পেজের জন্য সঠিক size এর placeholder তৈরি করে DOM এ রাখে।
//         এতে scrollbar সঠিক থাকে এবং scroll করা যায়।
// Step 2: IntersectionObserver (setupPageObserver) দেখে কোন page
//         viewport এ এসেছে, তখনই শুধু সেটা render করে।
async function renderCanvas(t) {
  ++g_renderToken;
  g_renderedPages.clear();

  const container = document.getElementById('pdf-container');
  container.style.transform = `scale(${t.zoom})`;
  container.innerHTML = '';

  // First pass: প্রতিটি page এর আসল size বের করে placeholder বসাও।
  // getPage() cheap — শুধু metadata লোড হয়, pixel render হয় না।
  for (let i = 0; i < t.pageOrder.length; i++) {
    const pNum = t.pageOrder[i];
    const page = await t.doc.getPage(pNum);
    const vpCss = page.getViewport({ scale: BASE_SCALE, rotation: t.rotation });

    const wrapper = document.createElement('div');
    wrapper.className = 'page-wrapper';
    wrapper.id = 'pw-' + i;
    wrapper.dataset.pageIndex = i;
    wrapper.dataset.pdfPage   = pNum;
    wrapper.style.width  = vpCss.width  + 'px';
    wrapper.style.height = vpCss.height + 'px';
    // সঠিক position maintain করার জন্য position relative দরকার
    wrapper.style.position = 'relative';

    // Placeholder — gray background + page number, actual render এর আগে দেখায়
    const placeholder = document.createElement('div');
    placeholder.className = 'page-placeholder';
    placeholder.style.cssText = [
      'width:100%', 'height:100%',
      'display:flex', 'align-items:center', 'justify-content:center',
      'background:#e8e8e8', 'color:#999', 'font-size:13px',
      'position:absolute', 'top:0', 'left:0'
    ].join(';');
    placeholder.textContent = i + 1;

    // Draw canvas (annotation layer) — সবসময় থাকে
    const drawCanvas = document.createElement('canvas');
    drawCanvas.className    = 'draw-canvas';
    drawCanvas.width        = vpCss.width;
    drawCanvas.height       = vpCss.height;
    drawCanvas.style.width  = vpCss.width  + 'px';
    drawCanvas.style.height = vpCss.height + 'px';
    drawCanvas.style.position = 'absolute';
    drawCanvas.style.top = '0'; drawCanvas.style.left = '0';

    const grid = document.createElement('div');
    grid.className = 'grid-overlay' + (g_showGrid ? ' show' : '');
    const selRect = document.createElement('div');
    selRect.className = 'sel-rect';

    wrapper.appendChild(placeholder);
    wrapper.appendChild(drawCanvas);
    wrapper.appendChild(grid);
    wrapper.appendChild(selRect);
    container.appendChild(wrapper);
  }

  // Observer setup: visible page এ render trigger করবে
  setupPageObserver(t.pageOrder.length);
  observePageWrappers();

  applyZoomTransform();
  refreshStats();
  g_currentVisiblePage = 1;
  updateStatusBar(1);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 20 · JS: Zoom, Rotate, keyboard shortcuts
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function zoomBy(delta) {
  const t = activeTab(); if (!t) return;
  if (g_nativeMode) return;
  t.zoom = Math.min(5, Math.max(0.1, t.zoom + delta));
  applyZoomTransform();
}
function zoomTo(v) {
  const t = activeTab(); if (!t) return;
  if (g_nativeMode) return;
  t.zoom = v;
  applyZoomTransform();
}
function applyZoomInput(val) {
  // Accept "125%" or "1.25" or "125"
  const clean = String(val).replace('%','').trim();
  const pct   = parseFloat(clean);
  if (!isNaN(pct) && pct > 0) zoomTo(pct / 100);
}
function rotatePDFAll() {
  const t = activeTab(); if (!t) return;
  t.rotation = (t.rotation + 90) % 360;
  g_renderedPages.clear(); // rotation বদলালে সব page re-render দরকার
  updateStatusBar(); scheduleRender();
}

// Ctrl+Wheel zoom — only in canvas/edit mode (native iframe handles itself)
document.getElementById('viewer-area').addEventListener('wheel', e => {
  if (!e.ctrlKey || g_nativeMode) return;
  e.preventDefault();
  const t = activeTab(); if (!t) return;
  t.zoom = Math.min(5, Math.max(0.1, t.zoom + (e.deltaY < 0 ? 0.07 : -0.07)));
  applyZoomTransform();
}, { passive: false });
)JS";

ss << LR"JS(
// Mouse‑position in statusbar
document.getElementById('viewer-area').addEventListener('mousemove', e => {
  const wrapper = e.target.closest('.page-wrapper');
  if (!wrapper) { document.getElementById('sb-coords').textContent = ''; return; }
  const rect = wrapper.getBoundingClientRect();
  const x = Math.round(e.clientX - rect.left);
  const y = Math.round(e.clientY - rect.top);
  document.getElementById('sb-coords').textContent = x + ', ' + y;
});

// Global keyboard shortcuts
document.addEventListener('keydown', e => {
  if (e.target.contentEditable === 'true' || e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
  const ctrl = e.ctrlKey || e.metaKey;
  if (ctrl && e.key === 'o') { e.preventDefault(); openFileDialog(true); }
  if (ctrl && e.key === 's') { e.preventDefault(); downloadCurrentPDF(); }
  if (ctrl && e.key === 'z') { e.preventDefault(); histUndo(); }
  if (ctrl && e.key === 'y') { e.preventDefault(); histRedo(); }
  if (ctrl && e.key === 'f') { e.preventDefault(); toggleFindBar(); }
  if (ctrl && e.key === '0') { e.preventDefault(); zoomTo(1.0); }
  if (ctrl && e.key === '=') { e.preventDefault(); zoomBy(0.15); }
  if (ctrl && e.key === '-') { e.preventDefault(); zoomBy(-0.15); }
  if (ctrl && (e.key === 'r' || e.key === 'R')) { e.preventDefault(); setViewerMode(!g_nativeMode); return; }
  // F4 = toggle thumbnail panel (Acrobat shortcut)
  if (e.key === 'F4') { e.preventDefault(); toggleLeftPanel(); return; }
  if (e.key === 'Escape') {
    if (document.querySelector('.modal-overlay[style*="flex"]') ||
        document.getElementById('loading-overlay')?.style.display === 'flex') {
      closeModal(); return;
    }
    if (document.querySelector('.dropdown.open')) { closeAllMenus(); closeCtx(); return; }
    if (document.body.classList.contains('present')) { enterPresentation(); return; }
    if (document.body.classList.contains('read'))    { exitReadMode(); return; }
  }
  if (e.key === 'Delete') deleteSelectedAnnotation();
  // Acrobat-style tool keys
  if (e.key === 'h' || e.key === 'H') setTool('hand');
  if (e.key === 'v' || e.key === 'V') setTool('select');
  if (e.key === 'd' || e.key === 'D') setTool('pen');
  if (e.key === 'u' || e.key === 'U') setTool('highlight');
  if (e.key === 'e' || e.key === 'E') setTool('eraser');
  if (e.key === 'n' || e.key === 'N') setTool('note');
  if (e.key === 't' || e.key === 'T') setTool('textbox');
  // Number shortcuts kept as fallback
  if (e.key === '1') setTool('hand');
  if (e.key === '2') setTool('select');
  if (e.key === '3') setTool('pen');
  if (e.key === '4') setTool('highlight');
  if (e.key === '5') setTool('eraser');
  if (e.key === '6') setTool('note');
  if (e.key === '7') setTool('textbox');
  if (e.key === '8') setTool('stamp');
  if ((e.key === 'r' || e.key === 'R') && !ctrl) rotatePDFAll();
});

let g_selectedAnnot = null;
function deleteSelectedAnnotation() {
  if (!g_selectedAnnot) return;
  const t = activeTab(); if (!t) return;
  t.annotations = t.annotations.filter(a => a !== g_selectedAnnot);
  g_selectedAnnot = null;
  scheduleRender();
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 21 · JS: Tool selection + Hand pan (RAF-based smooth)
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const TOOL_LABELS = {
  hand:'Hand', select:'Select', pen:'Pen', highlight:'Highlight',
  eraser:'Eraser', note:'Note', textbox:'TextBox', stamp:'Stamp',
  rect:'Rectangle', ellipse:'Ellipse', line:'Line', arrow:'Arrow',
  redact:'Redact', crop:'Crop', underline:'Underline', strikethrough:'Strikethrough'
};

function setTool(tool) {
  g_tool = tool;
  // tb-tool (topbar) এবং qb-btn (old, hidden) দুটোই handle করো
  document.querySelectorAll('.tb-tool[id^="rb-"], .qb-btn[id^="rb-"]').forEach(b => b.classList.remove('active'));
  const rbtn = document.getElementById('rb-' + tool);
  if (rbtn) rbtn.classList.add('active');

  const drawTools = ['pen','highlight','eraser','rect','ellipse','line','arrow','underline','strikethrough','redact','crop'];
  document.querySelectorAll('.draw-canvas').forEach(c => {
    c.classList.toggle('can-draw', drawTools.includes(tool));
  });

  const va = document.getElementById('viewer-area');
  if (tool === 'hand') va.style.cursor = 'grab';
  else if (tool === 'crop') va.style.cursor = 'crosshair';
  else if (drawTools.includes(tool)) va.style.cursor = 'crosshair';
  else if (tool === 'note' || tool === 'stamp' || tool === 'textbox') va.style.cursor = 'cell';
  else va.style.cursor = 'default';

  document.getElementById('sb-tool').textContent = 'Tool: ' + (TOOL_LABELS[tool] || tool);
  closeAllMenus();
}
)JS";

ss << LR"JS(
// ── Page Visibility Tracker (IntersectionObserver) ───────────────────────
// Scroll করলে কোন page visible তা track করে statusbar update করে।
// Canvas mode তে lazy rendering ও করে।
const viewerArea = document.getElementById('viewer-area');

function setupPageObserver(totalPages) {
  // আগের observer থাকলে disconnect করো
  if (g_pageObserver) { g_pageObserver.disconnect(); g_pageObserver = null; }

  g_pageObserver = new IntersectionObserver((entries) => {
    // সবচেয়ে বেশি visible যে page সেটা current page
    let best = -1, bestRatio = 0;
    entries.forEach(entry => {
      if (entry.isIntersecting && entry.intersectionRatio > bestRatio) {
        bestRatio = entry.intersectionRatio;
        best = parseInt(entry.target.dataset.pageIndex, 10);
      }
    });
    if (best >= 0) {
      g_currentVisiblePage = best + 1; // 1-indexed
      updateStatusBar(g_currentVisiblePage);
    }

    // Lazy canvas render: এখন দেখা যাচ্ছে কিন্তু আগে render হয়নি এমন page গুলো render করো
    if (!g_nativeMode) {
      entries.forEach(entry => {
        if (!entry.isIntersecting) return;
        const idx = parseInt(entry.target.dataset.pageIndex, 10);
        if (!isNaN(idx) && !g_renderedPages.has(idx)) {
          renderSinglePageCanvas(idx);
        }
      });
    }
  }, {
    root: viewerArea,
    rootMargin: '200px 0px',  // 200px আগে থেকেই pre-render শুরু
    threshold: [0, 0.1, 0.5, 1.0]
  });
}

function observePageWrappers() {
  if (!g_pageObserver) return;
  document.querySelectorAll('.page-wrapper').forEach(w => g_pageObserver.observe(w));
}

// Single page canvas render (lazy mode তে ব্যবহার হয়)
async function renderSinglePageCanvas(pageIndex) {
  const t = activeTab(); if (!t) return;
  if (g_renderedPages.has(pageIndex)) return;
  g_renderedPages.add(pageIndex);

  const wrapper = document.getElementById('pw-' + pageIndex);
  if (!wrapper) return;

  const pNum = t.pageOrder[pageIndex];
  if (!pNum) return;

  try {
    const page = await t.doc.getPage(pNum);
    const DPR = window.devicePixelRatio || 1;
    const vpCss = page.getViewport({ scale: 1.0, rotation: t.rotation });
    const vp    = page.getViewport({ scale: 1.0 * DPR, rotation: t.rotation });

    // Placeholder spinner সরাও, canvas যোগ করো
    const placeholder = wrapper.querySelector('.page-placeholder');
    if (placeholder) placeholder.remove();

    let pdfCanvas = wrapper.querySelector('.pdf-canvas');
    if (!pdfCanvas) {
      pdfCanvas = document.createElement('canvas');
      pdfCanvas.className = 'pdf-canvas';
      wrapper.insertBefore(pdfCanvas, wrapper.firstChild);
    }
    pdfCanvas.width        = Math.round(vp.width);
    pdfCanvas.height       = Math.round(vp.height);
    pdfCanvas.style.width  = vpCss.width  + 'px';
    pdfCanvas.style.height = vpCss.height + 'px';

    const pdfCtx = pdfCanvas.getContext('2d');
    await page.render({ canvasContext: pdfCtx, viewport: vp }).promise;

    // Draw canvas restore
    const drawCanvas = wrapper.querySelector('.draw-canvas');
    if (drawCanvas) {
      drawCanvas.width  = vpCss.width;
      drawCanvas.height = vpCss.height;
      drawCanvas.style.width  = vpCss.width  + 'px';
      drawCanvas.style.height = vpCss.height + 'px';
      restoreDrawAnnotations(pageIndex, drawCanvas, vp.width, vp.height);
    }
    restoreShapes(t, pageIndex, wrapper);
    restoreTextBoxes(t, pageIndex, wrapper);
    restorePasteImages(t, pageIndex, wrapper);
    restoreStickyNotes(t, pageIndex, wrapper);
    restoreStamps(t, pageIndex, wrapper);
    restoreRedactions(t, pageIndex, wrapper);
  } catch(e) {
    g_renderedPages.delete(pageIndex); // fail হলে retry allow করো
  }
}

// ── Hand pan — requestAnimationFrame for buttery smooth panning ──
let g_panRAF = null;
let g_panTargetX = 0, g_panTargetY = 0;

viewerArea.addEventListener('mousedown', e => {
  if (g_tool !== 'hand' || e.button !== 0) return;
  g_panning = true;
  g_panX0 = e.clientX; g_panY0 = e.clientY;
  g_scrollX0 = viewerArea.scrollLeft;
  g_scrollY0 = viewerArea.scrollTop;
  g_panTargetX = g_scrollX0;
  g_panTargetY = g_scrollY0;
  viewerArea.style.cursor = 'grabbing';
  e.preventDefault();
});

window.addEventListener('mousemove', e => {
  if (!g_panning) return;
  g_panTargetX = g_scrollX0 - (e.clientX - g_panX0);
  g_panTargetY = g_scrollY0 - (e.clientY - g_panY0);
  if (!g_panRAF) {
    g_panRAF = requestAnimationFrame(() => {
      viewerArea.scrollLeft = g_panTargetX;
      viewerArea.scrollTop  = g_panTargetY;
      g_panRAF = null;
    });
  }
});

window.addEventListener('mouseup', () => {
  if (g_panning) {
    g_panning = false;
    if (g_panRAF) { cancelAnimationFrame(g_panRAF); g_panRAF = null; }
    if (g_tool === 'hand') viewerArea.style.cursor = 'grab';
  }
});
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 22 · JS: Canvas drawing (pen, highlight, eraser, shapes)
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function getCanvasXY(canvas, e) {
  const r = canvas.getBoundingClientRect();
  // getBoundingClientRect gives scaled screen coords.
  // Canvas pixels live at BASE_SCALE, so divide by CSS zoom to get true canvas coords.
  const tab = activeTab();
  const zoom = (tab && tab.zoom) ? tab.zoom : 1;
  return {
    x: (e.clientX - r.left) / zoom,
    y: (e.clientY - r.top)  / zoom
  };
}

document.getElementById('pdf-container').addEventListener('mousedown', e => {
  const canvas = e.target;
  if (!canvas.classList.contains('draw-canvas') || !canvas.classList.contains('can-draw')) return;
  if (g_tool === 'eraser') { eraseAt(canvas, e); return; }
  if (['rect','ellipse','line','arrow','redact'].includes(g_tool)) { startShape(canvas, e); return; }

  const drawTools = ['pen','highlight','underline','strikethrough'];
  if (!drawTools.includes(g_tool)) return;

  g_drawing = true;
  g_drawCanvas = canvas;
  g_drawCtx = canvas.getContext('2d');
  const pos = getCanvasXY(canvas, e);
  const t = activeTab(); if (!t) return;
  const wrapper = canvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);

  let col, alpha, lw;
  if (g_tool === 'highlight') { col = '#FFE000'; alpha = 0.4; lw = 14; }
  else if (g_tool === 'underline') { col = g_color; alpha = 1; lw = 2; }
  else if (g_tool === 'strikethrough') { col = g_color; alpha = 1; lw = 2; }
  else { col = g_color; alpha = g_opacity; lw = g_lineWidth; }

  g_currentStroke = {
    type: 'stroke', tool: g_tool, pageIndex: pIdx,
    color: col, lineWidth: lw, alpha,
    points: [{ rx: pos.x / canvas.width, ry: pos.y / canvas.height }],
    id: 'a_' + Date.now()
  };

  g_drawCtx.save();
  g_drawCtx.strokeStyle = col;
  g_drawCtx.lineWidth = lw;
  g_drawCtx.lineCap = 'round';
  g_drawCtx.lineJoin = 'round';
  g_drawCtx.globalAlpha = alpha;
  g_drawCtx.beginPath();
  g_drawCtx.moveTo(pos.x, pos.y);
  e.preventDefault();
});
)JS";

ss << LR"JS(
window.addEventListener('mousemove', e => {
  if (!g_drawing || !g_drawCtx || !g_drawCanvas) return;
  const pos = getCanvasXY(g_drawCanvas, e);
  g_drawCtx.lineTo(pos.x, pos.y);
  g_drawCtx.stroke();
  g_drawCtx.beginPath();
  g_drawCtx.moveTo(pos.x, pos.y);
  if (g_currentStroke) {
    g_currentStroke.points.push({ rx: pos.x / g_drawCanvas.width, ry: pos.y / g_drawCanvas.height });
  }
});

window.addEventListener('mouseup', () => {
  if (!g_drawing) return;
  g_drawing = false;
  if (g_drawCtx) { g_drawCtx.restore(); g_drawCtx = null; }
  const t = activeTab();
  if (t && g_currentStroke && g_currentStroke.points.length > 1) {
    pushHistory(t);
    t.annotations.push(g_currentStroke);
    t.modified = true;
    renderTabStrip();
    refreshStats();
  }
  g_currentStroke = null; g_drawCanvas = null;
});

function restoreDrawAnnotations(pageIndex, canvas, w, h) {
  const t = activeTab(); if (!t) return;
  const ctx = canvas.getContext('2d');
  t.annotations.filter(a => a.type === 'stroke' && a.pageIndex === pageIndex).forEach(a => {
    if (!a.points || a.points.length < 2) return;
    ctx.save();
    ctx.strokeStyle = a.color;
    ctx.lineWidth = a.lineWidth;
    ctx.lineCap = 'round'; ctx.lineJoin = 'round';
    ctx.globalAlpha = a.alpha || 1;
    ctx.beginPath();
    a.points.forEach((p, i) => {
      const px = p.rx * w, py = p.ry * h;
      i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
    });
    ctx.stroke(); ctx.restore();
  });
}
)JS";

ss << LR"JS(
// ── Eraser ───────────────────────────────────────────────────
function eraseAt(canvas, e) {
  const pos = getCanvasXY(canvas, e);
  const ctx = canvas.getContext('2d');
  const r = 20 * (g_lineWidth / 2);
  ctx.save(); ctx.globalCompositeOperation = 'destination-out';
  ctx.beginPath(); ctx.arc(pos.x, pos.y, r, 0, Math.PI * 2);
  ctx.fill(); ctx.restore();
  // Also erase from stored strokes (remove points near)
  const t = activeTab(); if (!t) return;
  const wrapper = canvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const w = canvas.width, h = canvas.height;
  t.annotations = t.annotations.filter(a => {
    if (a.type !== 'stroke' || a.pageIndex !== pIdx) return true;
    return !a.points.some(p => Math.hypot(p.rx * w - pos.x, p.ry * h - pos.y) < r);
  });
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 23 · JS: Shape drawing (rect, ellipse, line, arrow)
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
let g_shapeCanvas = null, g_shapeCtx = null, g_shapePdfCanvas = null;
let g_shapeDrawing = false;

function startShape(canvas, e) {
  g_shapeCanvas = canvas;
  g_shapeCtx = canvas.getContext('2d');
  g_shapePdfCanvas = canvas.previousElementSibling; // pdf canvas beneath
  const pos = getCanvasXY(canvas, e);
  g_shapeStart = pos;
  g_shapeDrawing = true;
  e.preventDefault();
}

window.addEventListener('mousemove', e => {
  if (!g_shapeDrawing || !g_shapeCanvas) return;
  const pos = getCanvasXY(g_shapeCanvas, e);
  const ctx = g_shapeCtx;
  // Redraw draw canvas: restore strokes then draw preview
  const wrapper = g_shapeCanvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const w = g_shapeCanvas.width, h = g_shapeCanvas.height;
  ctx.clearRect(0, 0, w, h);
  restoreDrawAnnotations(pIdx, g_shapeCanvas, w, h);
  // Draw shape preview
  ctx.save();
  ctx.strokeStyle = g_color;
  ctx.lineWidth = g_lineWidth;
  ctx.globalAlpha = g_opacity;
  ctx.fillStyle = g_fillColor === 'transparent' ? 'transparent' : g_fillColor;
  const x = Math.min(g_shapeStart.x, pos.x);
  const y = Math.min(g_shapeStart.y, pos.y);
  const w2 = Math.abs(pos.x - g_shapeStart.x);
  const h2 = Math.abs(pos.y - g_shapeStart.y);
  ctx.beginPath();
  if (g_tool === 'rect') {
    ctx.rect(x, y, w2, h2);
  } else if (g_tool === 'ellipse') {
    ctx.ellipse(x + w2/2, y + h2/2, w2/2, h2/2, 0, 0, Math.PI*2);
  } else if (g_tool === 'line') {
    ctx.moveTo(g_shapeStart.x, g_shapeStart.y); ctx.lineTo(pos.x, pos.y);
  } else if (g_tool === 'arrow') {
    ctx.moveTo(g_shapeStart.x, g_shapeStart.y); ctx.lineTo(pos.x, pos.y);
    const ang = Math.atan2(pos.y - g_shapeStart.y, pos.x - g_shapeStart.x);
    const al = 12;
    ctx.moveTo(pos.x, pos.y);
    ctx.lineTo(pos.x - al*Math.cos(ang-0.4), pos.y - al*Math.sin(ang-0.4));
    ctx.moveTo(pos.x, pos.y);
    ctx.lineTo(pos.x - al*Math.cos(ang+0.4), pos.y - al*Math.sin(ang+0.4));
  } else if (g_tool === 'redact') {
    ctx.fillStyle = 'rgba(0,0,0,0.5)';
    ctx.fillRect(x, y, w2, h2);
  }
  if (g_fillColor !== 'transparent' && ['rect','ellipse'].includes(g_tool)) ctx.fill();
  ctx.stroke();
  ctx.restore();
});
)JS";

ss << LR"JS(
window.addEventListener('mouseup', e => {
  if (!g_shapeDrawing) return;
  g_shapeDrawing = false;
  const t = activeTab();
  if (!t || !g_shapeCanvas) { g_shapeCanvas = null; return; }
  const pos = getCanvasXY(g_shapeCanvas, e);
  const wrapper = g_shapeCanvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const w = g_shapeCanvas.width, h = g_shapeCanvas.height;
  if (Math.abs(pos.x - g_shapeStart.x) < 3 && Math.abs(pos.y - g_shapeStart.y) < 3) {
    g_shapeCanvas = null; return;
  }
  pushHistory(t);
  t.annotations.push({
    type: 'shape', tool: g_tool, pageIndex: pIdx,
    x1: g_shapeStart.x/w, y1: g_shapeStart.y/h,
    x2: pos.x/w, y2: pos.y/h,
    color: g_color, fillColor: g_fillColor,
    lineWidth: g_lineWidth, alpha: g_opacity,
    id: 'sh_' + Date.now()
  });
  t.modified = true; renderTabStrip();
  g_shapeCanvas = null; g_shapeStart = null;
});

function restoreShapes(tab, pageIndex, wrapper) {
  const canvas = wrapper.querySelector('.draw-canvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  tab.annotations.filter(a => a.type === 'shape' && a.pageIndex === pageIndex).forEach(a => {
    const x1 = a.x1*w, y1 = a.y1*h, x2 = a.x2*w, y2 = a.y2*h;
    const rx = Math.min(x1,x2), ry = Math.min(y1,y2);
    const rw = Math.abs(x2-x1), rh = Math.abs(y2-y1);
    ctx.save();
    ctx.strokeStyle = a.color; ctx.lineWidth = a.lineWidth;
    ctx.globalAlpha = a.alpha || 1;
    if (a.fillColor && a.fillColor !== 'transparent') ctx.fillStyle = a.fillColor;
    ctx.beginPath();
    if (a.tool === 'rect') { ctx.rect(rx, ry, rw, rh); }
    else if (a.tool === 'ellipse') { ctx.ellipse(rx+rw/2, ry+rh/2, rw/2, rh/2, 0, 0, Math.PI*2); }
    else if (a.tool === 'line') { ctx.moveTo(x1,y1); ctx.lineTo(x2,y2); }
    else if (a.tool === 'arrow') {
      ctx.moveTo(x1,y1); ctx.lineTo(x2,y2);
      const ang = Math.atan2(y2-y1, x2-x1), al = 12;
      ctx.moveTo(x2,y2); ctx.lineTo(x2-al*Math.cos(ang-0.4), y2-al*Math.sin(ang-0.4));
      ctx.moveTo(x2,y2); ctx.lineTo(x2-al*Math.cos(ang+0.4), y2-al*Math.sin(ang+0.4));
    } else if (a.tool === 'redact') { ctx.fillStyle = '#000'; ctx.fillRect(rx,ry,rw,rh); }
    if (a.fillColor && a.fillColor !== 'transparent' && ['rect','ellipse'].includes(a.tool)) ctx.fill();
    ctx.stroke(); ctx.restore();
  });
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 24 · JS: Sticky Notes
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const NOTE_COLORS = ['#fff9c4','#c8e6c9','#bbdefb','#fce4ec','#ffe0b2'];

function restoreStickyNotes(tab, pageIndex, wrapper) {
  tab.annotations.filter(a => a.type === 'note' && a.pageIndex === pageIndex)
    .forEach(a => createNoteEl(a, wrapper));
}

function createNoteEl(data, wrapper) {
  const note = document.createElement('div');
  note.style.cssText = `
    position:absolute;z-index:10;background:${data.color||'#fff9c4'};
    border:1px solid #d4b800;border-radius:3px;padding:5px 6px;
    font-size:11px;min-width:90px;min-height:44px;max-width:200px;
    box-shadow:2px 3px 8px rgba(0,0,0,.2);cursor:move;resize:both;
    overflow:auto;color:#333;white-space:pre-wrap;word-break:break-word;
    left:${data.rx*100}%;top:${data.ry*100}%;
  `;
  note.contentEditable = 'true';
  note.textContent = data.text || '';
  const bar = document.createElement('div');
  bar.style.cssText = 'display:flex;gap:3px;margin-bottom:3px;';
  NOTE_COLORS.forEach(c => {
    const sw = document.createElement('span');
    sw.style.cssText = `width:10px;height:10px;border-radius:50%;background:${c};cursor:pointer;border:1px solid rgba(0,0,0,.12);`;
    sw.onclick = () => { note.style.background = c; data.color = c; };
    bar.appendChild(sw);
  });
  const close = document.createElement('span');
  close.textContent = '×';
  close.style.cssText = 'margin-left:auto;cursor:pointer;color:#999;font-size:12px;line-height:1;';
  close.onclick = () => {
    const t = activeTab(); if (t) t.annotations = t.annotations.filter(a => a.id !== data.id);
    note.remove(); refreshStats();
  };
  bar.appendChild(close);
  note.prepend(bar);
  note.addEventListener('input', () => { data.text = note.textContent; });
  makeDraggable(note, wrapper, data);
  wrapper.appendChild(note);
}
)JS";

ss << LR"JS(
document.getElementById('pdf-container').addEventListener('click', e => {
  if (g_tool !== 'note') return;
  const wrapper = e.target.closest('.page-wrapper'); if (!wrapper) return;
  if (e.target.contentEditable === 'true') return;
  const rect = wrapper.getBoundingClientRect();
  const rx = (e.clientX - rect.left) / rect.width;
  const ry = (e.clientY - rect.top) / rect.height;
  const t = activeTab(); if (!t) return;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const data = { type:'note', id:'n_'+Date.now(), pageIndex:pIdx, rx, ry, text:'', color:'#fff9c4' };
  pushHistory(t); t.annotations.push(data); t.modified = true;
  createNoteEl(data, wrapper); renderTabStrip(); refreshStats();
});

function makeDraggable(el, container, data) {
  let sx=0, sy=0, ex=0, ey=0, drag=false;
  const onDown = e => {
    if (e.target.contentEditable === 'true' && e.target !== el) return;
    if (e.target.tagName === 'SPAN' || e.target.tagName === 'SELECT' || e.target.tagName === 'INPUT') return;
    drag=true; sx=e.clientX; sy=e.clientY; ex=el.offsetLeft; ey=el.offsetTop;
    e.preventDefault();
  };
  const onMove = e => {
    if (!drag) return;
    const nx = ex+(e.clientX-sx), ny = ey+(e.clientY-sy);
    el.style.left = nx+'px'; el.style.top = ny+'px';
    if (data) { data.rx = nx/container.offsetWidth; data.ry = ny/container.offsetHeight; }
  };
  const onUp = () => { drag = false; };
  el.addEventListener('mousedown', onDown);
  window.addEventListener('mousemove', onMove);
  window.addEventListener('mouseup', onUp);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 25 · JS: Text Boxes
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function restoreTextBoxes(tab, pageIndex, wrapper) {
  tab.textBoxes.filter(tb => tb.pageIndex === pageIndex)
    .forEach(tb => createTextBoxEl(tb, wrapper));
}

function createTextBoxEl(data, wrapper) {
  const tb = document.createElement('div');
  tb.className = 'textbox-el';
  tb.style.left = (data.rx*100)+'%';
  tb.style.top = (data.ry*100)+'%';
  tb.style.fontSize = (data.fontSize||12)+'px';
  tb.style.fontFamily = data.fontFamily||'Arial';
  tb.style.color = data.color||'#111';
  tb.contentEditable = 'true';
  tb.textContent = data.text||'';
  const close = document.createElement('span');
  close.textContent='×';
  close.style.cssText='position:absolute;top:1px;right:3px;cursor:pointer;font-size:11px;color:#aaa;';
  close.onclick=()=>{
    const t=activeTab(); if(t) t.textBoxes=t.textBoxes.filter(x=>x!==data);
    tb.remove();
  };
  tb.appendChild(close);
  tb.addEventListener('input',()=>{data.text=tb.textContent;});
  makeDraggable(tb, wrapper, data);
  wrapper.appendChild(tb);
}
)JS";

ss << LR"JS(
document.getElementById('pdf-container').addEventListener('click', e => {
  if (g_tool !== 'textbox') return;
  const wrapper = e.target.closest('.page-wrapper'); if (!wrapper) return;
  if (e.target.classList.contains('textbox-el') || e.target.closest('.textbox-el')) return;
  const rect = wrapper.getBoundingClientRect();
  const data = {
    pageIndex: parseInt(wrapper.dataset.pageIndex),
    rx: (e.clientX-rect.left)/rect.width,
    ry: (e.clientY-rect.top)/rect.height,
    text:'', fontSize:g_fontSize, fontFamily:g_fontFamily, color:g_color
  };
  const t = activeTab(); if (!t) return;
  pushHistory(t); t.textBoxes.push(data); t.modified=true;
  createTextBoxEl(data, wrapper); renderTabStrip();
});
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 26 · JS: Stamps + Paste Images
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const STAMP_COLORS = {
  DRAFT:'#1565c0', APPROVED:'#2e7d32', REJECTED:'#c62828',
  CONFIDENTIAL:'#6a1b9a', FINAL:'#00695c', VOID:'#4e342e',
  COPY:'#616161', RECEIVED:'#1b5e20'
};

function restoreStamps(tab, pageIndex, wrapper) {
  tab.annotations.filter(a => a.type==='stamp' && a.pageIndex===pageIndex)
    .forEach(a => createStampEl(a, wrapper));
}

function createStampEl(data, wrapper) {
  const el = document.createElement('div');
  el.className = 'stamp-el';
  const col = STAMP_COLORS[data.label]||'#555';
  el.style.cssText = `left:${data.rx*100}%;top:${data.ry*100}%;color:${col};border-color:${col};`;
  el.textContent = data.label;
  const del = document.createElement('span');
  del.textContent='×'; del.style.cssText='position:absolute;top:-6px;right:-6px;background:red;color:#fff;border-radius:50%;width:14px;height:14px;font-size:10px;display:flex;align-items:center;justify-content:center;cursor:pointer;';
  del.onclick=()=>{
    const t=activeTab(); if(t) t.annotations=t.annotations.filter(a=>a!==data);
    el.remove();
  };
  el.appendChild(del);
  makeDraggable(el, wrapper, data);
  wrapper.appendChild(el);
}
)JS";

ss << LR"JS(
document.getElementById('pdf-container').addEventListener('click', e => {
  if (g_tool !== 'stamp') return;
  const wrapper = e.target.closest('.page-wrapper'); if (!wrapper) return;
  const rect = wrapper.getBoundingClientRect();
  const label = document.getElementById('rp-stamp-type').value;
  const data = {
    type:'stamp', id:'st_'+Date.now(),
    pageIndex:parseInt(wrapper.dataset.pageIndex),
    rx:(e.clientX-rect.left)/rect.width,
    ry:(e.clientY-rect.top)/rect.height,
    label
  };
  const t = activeTab(); if (!t) return;
  pushHistory(t); t.annotations.push(data); t.modified=true;
  createStampEl(data, wrapper); renderTabStrip();
});

// ── Paste images ──────────────────────────────────────────────
function restorePasteImages(tab, pageIndex, wrapper) {
  tab.pasteImages.filter(img => img.pageIndex===pageIndex)
    .forEach(img => createImgOverlay(img, wrapper));
}

function createImgOverlay(data, wrapper) {
  const div = document.createElement('div');
  div.style.cssText = `position:absolute;z-index:14;left:${data.rx*100}%;top:${data.ry*100}%;width:${data.rw*100}%;height:${data.rh*100}%;border:2px solid var(--c-accent2);cursor:move;`;
  const img = document.createElement('img');
  img.src = data.src; img.style.cssText='width:100%;height:100%;display:block;pointer-events:none;';
  const del = document.createElement('span');
  del.textContent='×';
  del.style.cssText='position:absolute;top:-6px;right:-6px;background:red;color:#fff;border-radius:50%;width:14px;height:14px;font-size:10px;display:flex;align-items:center;justify-content:center;cursor:pointer;z-index:15;';
  del.onclick=()=>{
    const t=activeTab(); if(t) t.pasteImages=t.pasteImages.filter(i=>i!==data);
    div.remove();
  };
  div.appendChild(img); div.appendChild(del);
  // SE resize
  const rh = document.createElement('div');
  rh.style.cssText='position:absolute;bottom:-5px;right:-5px;width:10px;height:10px;background:var(--c-accent);border-radius:1px;cursor:se-resize;z-index:15;';
  rh.addEventListener('mousedown', ev => startImgResize(ev, div, data, wrapper));
  div.appendChild(rh);
  makeDraggable(div, wrapper, null);
  div.addEventListener('mouseup', () => {
    data.rx=div.offsetLeft/wrapper.offsetWidth;
    data.ry=div.offsetTop/wrapper.offsetHeight;
  });
  wrapper.appendChild(div);
}
)JS";

ss << LR"JS(
function startImgResize(e, el, data, wrapper) {
  e.stopPropagation(); e.preventDefault();
  const sx=e.clientX, sy=e.clientY, sw=el.offsetWidth, sh=el.offsetHeight;
  const onM=ev=>{
    el.style.width=Math.max(40,sw+(ev.clientX-sx))+'px';
    el.style.height=Math.max(30,sh+(ev.clientY-sy))+'px';
  };
  const onU=()=>{
    data.rw=el.offsetWidth/wrapper.offsetWidth;
    data.rh=el.offsetHeight/wrapper.offsetHeight;
    window.removeEventListener('mousemove',onM);
    window.removeEventListener('mouseup',onU);
  };
  window.addEventListener('mousemove',onM);
  window.addEventListener('mouseup',onU);
}

document.addEventListener('paste', async e => {
  const t = activeTab(); if (!t) return;
  let file=null;
  for (let i=0;i<e.clipboardData.items.length;i++) {
    if (e.clipboardData.items[i].type.startsWith('image')) {
      file=e.clipboardData.items[i].getAsFile(); break;
    }
  }
  if (!file) return;
  const buf = new Uint8Array(await file.arrayBuffer());
  const src = URL.createObjectURL(file);
  const container = document.getElementById('pdf-container');
  const first = container.querySelector('.page-wrapper');
  if (!first) { showToast('Open a PDF first.'); return; }
  const data = {
    id:'img_'+Date.now(), pageIndex:parseInt(first.dataset.pageIndex)||0,
    rx:0.05, ry:0.05, rw:0.4, rh:0.3,
    src, buffer:buf, mimeType:file.type
  };
  pushHistory(t); t.pasteImages.push(data); t.modified=true;
  createImgOverlay(data, first); renderTabStrip();
  showToast('Image pasted — drag to reposition.', 'success');
});
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 27 · JS: Redactions
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function restoreRedactions(tab, pageIndex, wrapper) {
  tab.redactions.filter(r => r.pageIndex === pageIndex)
    .forEach(r => createRedactEl(r, wrapper));
}

function createRedactEl(data, wrapper) {
  const el = document.createElement('div');
  el.className = 'redact-rect';
  el.style.left = (data.x*100)+'%';
  el.style.top = (data.y*100)+'%';
  el.style.width = (data.w*100)+'%';
  el.style.height = (data.h*100)+'%';
  el.title = 'Click to remove redaction';
  el.onclick = () => {
    const t = activeTab(); if (!t) return;
    t.redactions = t.redactions.filter(r => r !== data);
    el.remove();
  };
  wrapper.appendChild(el);
}

// Redact tool draws like rect, stores in tab.redactions
// (handled by shape drawing — type 'redact')

function applyRedactions() {
  const t = activeTab(); if (!t) { showToast('No file open.'); return; }
  // Move redact annotations into t.redactions so they get baked on save
  const redactAnnots = t.annotations.filter(a => a.tool === 'redact');
  redactAnnots.forEach(a => {
    t.redactions.push({ pageIndex:a.pageIndex, x:a.x1, y:a.y1, w:Math.abs(a.x2-a.x1), h:Math.abs(a.y2-a.y1) });
    t.annotations = t.annotations.filter(x => x !== a);
  });
  scheduleRender();
  showToast('Redactions applied.', 'success');
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 28 · JS: Page Thumbnails + Bookmarks
