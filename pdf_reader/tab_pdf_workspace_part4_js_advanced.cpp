// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
// ── Page jump from statusbar input or prev/next buttons ──────
function jumpToPageFromInput(pageArg) {
  const t = activeTab(); if (!t) return;
  let pg;
  if (typeof pageArg === 'number' || (typeof pageArg === 'string' && pageArg !== '')) {
    pg = parseInt(pageArg) || 1;
  } else {
    const inp = document.getElementById('sb-page-input');
    pg = parseInt(inp ? inp.value : 1) || 1;
  }
  pg = Math.min(t.pageOrder.length, Math.max(1, pg));
  const inp = document.getElementById('sb-page-input');
  if (inp) { inp.value = pg; inp.blur(); }
  // native mode: tell iframe to jump via fragment
  if (g_nativeMode) {
    const iframe = document.getElementById('native-pdf-iframe');
    if (iframe) {
      const url = getOrCreateBlobUrl(t);
      iframe.src = url + '#toolbar=0&navpanes=0&page=' + pg;
      g_currentVisiblePage = pg;
      updateStatusBar(pg);
    }
    return;
  }
  const target = document.getElementById('pw-' + (pg - 1));
  if (target) target.scrollIntoView({ block: 'start', behavior: 'smooth' });
}

async function buildThumbs() {
  const t = activeTab(); if (!t) return;
  const list = document.getElementById('lp-thumb'); list.innerHTML = '';
  for (let i=0; i<t.pageOrder.length; i++) {
    const pNum = t.pageOrder[i];
    const page = await t.doc.getPage(pNum);
    const vp = page.getViewport({ scale: 0.17 });
    const item = document.createElement('div');
    item.className = 'thumb-item' + (i===0?' selected':'');
    item.dataset.orderIdx = i;
    const c = document.createElement('canvas');
    c.width=vp.width; c.height=vp.height;
    page.render({ canvasContext:c.getContext('2d'), viewport:vp });
    const num = document.createElement('div'); num.className='thumb-pg-num'; num.textContent=i+1;
    const del = document.createElement('div'); del.className='thumb-del'; del.textContent='×';
    del.onclick = ev => { ev.stopPropagation(); deletePageByOrder(i); };
    item.appendChild(c); item.appendChild(num); item.appendChild(del);
    item.onclick = () => {
      document.querySelectorAll('.thumb-item').forEach(x=>x.classList.remove('selected'));
      item.classList.add('selected');
      document.getElementById('pw-'+i)?.scrollIntoView({block:'start',behavior:'smooth'});
    };
    setupThumbDnd(item, list, i);
    list.appendChild(item);
  }
}
)JS";

ss << LR"JS(
function deletePageByOrder(idx) {
  const t = activeTab(); if (!t) return;
  if (t.pageOrder.length<=1) { showToast('Cannot delete the only page.'); return; }
  pushHistory(t);
  t.pageOrder.splice(idx,1);
  t.annotations = t.annotations.filter(a=>a.pageIndex!==idx)
    .map(a=>({...a, pageIndex:a.pageIndex>idx?a.pageIndex-1:a.pageIndex}));
  t.textBoxes = t.textBoxes.filter(x=>x.pageIndex!==idx)
    .map(x=>({...x, pageIndex:x.pageIndex>idx?x.pageIndex-1:x.pageIndex}));
  t.pasteImages = t.pasteImages.filter(x=>x.pageIndex!==idx)
    .map(x=>({...x, pageIndex:x.pageIndex>idx?x.pageIndex-1:x.pageIndex}));
  t.redactions = t.redactions.filter(x=>x.pageIndex!==idx)
    .map(x=>({...x, pageIndex:x.pageIndex>idx?x.pageIndex-1:x.pageIndex}));
  t.modified=true; renderTabStrip(); renderViewer(); buildThumbs();
  showToast('Page deleted.');
}

function setupThumbDnd(item, list, startIdx) {
  item.draggable=true;
  item.addEventListener('dragstart', e=>{
    e.dataTransfer.setData('text/plain',startIdx);
    item.style.opacity='.4';
  });
  item.addEventListener('dragend', ()=>{ item.style.opacity='1'; });
  item.addEventListener('dragover', e=>{ e.preventDefault(); item.style.outline='2px dashed var(--c-accent)'; });
  item.addEventListener('dragleave', ()=>{ item.style.outline=''; });
  item.addEventListener('drop', e=>{
    e.preventDefault(); item.style.outline='';
    const from=parseInt(e.dataTransfer.getData('text/plain'));
    const to=startIdx;
    if (from===to) return;
    const t=activeTab(); if (!t) return;
    const moved=t.pageOrder.splice(from,1)[0];
    t.pageOrder.splice(to,0,moved);
    t.modified=true; renderViewer(); buildThumbs();
  });
}

function switchLPanel(which, btn) {
  ['thumb','bm','layers'].forEach(id => {
    document.getElementById('lp-'+id).style.display = id===which?'':'none';
  });
  document.querySelectorAll('.lp-tab').forEach(b=>b.classList.remove('active'));
  if (btn) btn.classList.add('active');
  if (which==='thumb') buildThumbs();
  if (which==='bm') renderBookmarkPanel();
}
)JS";

ss << LR"JS(
function addBookmark() {
  const t = activeTab(); if (!t) { showToast('No file open.'); return; }
  const label = prompt('Bookmark label:', 'Page 1'); if (!label) return;
  t.bookmarks.push({ pageIndex:0, label });
  renderBookmarkPanel();
  showToast('Bookmark added.', 'success');
}

function addBookmarkAtCtx() {
  const t = activeTab(); if (!t) return;
  const label = prompt('Bookmark label:', 'Bookmark'); if (!label) return;
  t.bookmarks.push({ pageIndex: Math.max(0,g_ctxPageIndex), label });
  renderBookmarkPanel(); closeCtx();
  showToast('Bookmark added.', 'success');
}

function renderBookmarkPanel() {
  const t = activeTab();
  const panel = document.getElementById('lp-bm');
  if (!t || !t.bookmarks.length) {
    panel.innerHTML = '<div style="padding:10px;font-size:10.5px;color:var(--c-muted);">No bookmarks.</div>'; return;
  }
  panel.innerHTML = '';
  t.bookmarks.forEach((bm,i) => {
    const el = document.createElement('div'); el.className='bm-item';
    el.innerHTML = '&#9733; ' + bm.label +
      '<span class="bm-del" onclick="deleteBookmark('+i+')">&#128465;</span>';
    el.onclick = ()=>{ document.getElementById('pw-'+bm.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'}); };
    panel.appendChild(el);
  });
}

function deleteBookmark(i) {
  const t = activeTab(); if (!t) return;
  t.bookmarks.splice(i,1); renderBookmarkPanel();
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 29 · JS: Signature pad
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
let g_sigDrawing=false, g_sigCtx=null;

function openSignatureModal() {
  document.getElementById('sig-modal-overlay').classList.add('show');
  const c=document.getElementById('sig-modal-canvas');
  g_sigCtx=c.getContext('2d');
  g_sigCtx.fillStyle='#fff'; g_sigCtx.fillRect(0,0,c.width,c.height);
}
function closeSigModal() {
  document.getElementById('sig-modal-overlay').classList.remove('show');
}
function clearSigPad() {
  const c=document.getElementById('sig-modal-canvas');
  g_sigCtx.fillStyle='#fff'; g_sigCtx.fillRect(0,0,c.width,c.height);
}

const sigCanvas = document.getElementById('sig-modal-canvas');
sigCanvas.addEventListener('mousedown', e=>{
  g_sigDrawing=true;
  const r=sigCanvas.getBoundingClientRect();
  g_sigCtx.beginPath();
  g_sigCtx.moveTo(e.clientX-r.left, e.clientY-r.top);
  g_sigCtx.strokeStyle=g_sigColor; g_sigCtx.lineWidth=2;
  g_sigCtx.lineCap='round';
});
sigCanvas.addEventListener('mousemove', e=>{
  if (!g_sigDrawing) return;
  const r=sigCanvas.getBoundingClientRect();
  g_sigCtx.lineTo(e.clientX-r.left, e.clientY-r.top);
  g_sigCtx.stroke();
  g_sigCtx.beginPath(); g_sigCtx.moveTo(e.clientX-r.left, e.clientY-r.top);
});
window.addEventListener('mouseup', ()=>{ g_sigDrawing=false; });
)JS";

ss << LR"JS(
function applySigToPage() {
  const t=activeTab(); if (!t) { closeSigModal(); return; }
  const src=document.getElementById('sig-modal-canvas').toDataURL('image/png');
  const container=document.getElementById('pdf-container');
  const first=container.querySelector('.page-wrapper');
  if (!first) { closeSigModal(); showToast('Open a PDF first.'); return; }
  const data={
    id:'sig_'+Date.now(), pageIndex:parseInt(first.dataset.pageIndex)||0,
    rx:0.05, ry:0.7, rw:0.35, rh:0.1,
    src, mimeType:'image/png'
  };
  pushHistory(t); t.pasteImages.push(data); t.modified=true;
  createImgOverlay(data, first);
  closeSigModal(); renderTabStrip();
  showToast('Signature inserted.', 'success');
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 30 · JS: Colour picker + undo/redo + menus
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const PALETTE = [
  '#000000','#434343','#666666','#999999','#b7b7b7','#cccccc','#d9d9d9','#ffffff',
  '#ff0000','#ff9900','#ffff00','#00ff00','#00ffff','#4a86e8','#0000ff','#9900ff',
  '#ff00ff','#e06666','#f6b26b','#ffd966','#93c47d','#76a5af','#6fa8dc','#8e7cc3',
  '#c27ba0','#cc4125','#e69138','#f1c232','#6aa84f','#45818e','#3d85c6','#674ea7',
];

function showColorPicker(target, swatchId) {
  g_colorTarget = target; g_colorSwatchId = swatchId;
  const grid = document.getElementById('color-grid'); grid.innerHTML='';
  PALETTE.forEach(c => {
    const cell=document.createElement('div'); cell.className='color-cell';
    cell.style.background=c;
    cell.onclick=()=>{ applyCustomColor(c); };
    grid.appendChild(cell);
  });
  const popup=document.getElementById('color-picker-popup');
  const swatch=document.getElementById(swatchId);
  if (swatch) {
    const r=swatch.getBoundingClientRect();
    popup.style.left=r.left+'px'; popup.style.top=(r.bottom+4)+'px';
  }
  popup.classList.add('show');
}

function applyCustomColor(c) {
  if (g_colorTarget==='stroke') {
    g_color=c;
    if (g_colorSwatchId) document.getElementById(g_colorSwatchId).style.background=c;
    document.getElementById('rp-color').style.background=c;
    document.getElementById('rb-color-stroke').style.background=c;
  } else {
    g_fillColor=c;
    if (g_colorSwatchId) document.getElementById(g_colorSwatchId).style.background=c;
    document.getElementById('rb-color-fill').style.background=c;
  }
  document.getElementById('color-picker-popup').classList.remove('show');
}
)JS";

ss << LR"JS(
document.addEventListener('click', e=>{
  const pp=document.getElementById('color-picker-popup');
  if (pp.classList.contains('show') && !pp.contains(e.target) && !e.target.classList.contains('color-swatch')) {
    pp.classList.remove('show');
  }
});

// ── Undo / Redo ───────────────────────────────────────────────
function pushHistory(t) {
  const snap={
    annotations: JSON.parse(JSON.stringify(t.annotations)),
    textBoxes: JSON.parse(JSON.stringify(t.textBoxes)),
    redactions: JSON.parse(JSON.stringify(t.redactions)),
    pageOrder: [...t.pageOrder]
  };
  t.history.splice(t.histIdx+1);
  t.history.push(snap);
  if (t.history.length>MAX_HIST) t.history.shift();
  t.histIdx=t.history.length-1;
}

function histUndo() {
  const t=activeTab(); if (!t||t.histIdx<0) { showToast('Nothing to undo.'); return; }
  t.histIdx--;
  if (t.histIdx<0) {
    t.annotations=[]; t.textBoxes=[]; t.redactions=[];
  } else {
    const snap=t.history[t.histIdx];
    t.annotations=JSON.parse(JSON.stringify(snap.annotations));
    t.textBoxes=JSON.parse(JSON.stringify(snap.textBoxes));
    t.redactions=JSON.parse(JSON.stringify(snap.redactions));
    t.pageOrder=[...snap.pageOrder];
  }
  scheduleRender(); buildThumbs(); showToast('Undo.');
}

function histRedo() {
  const t=activeTab(); if (!t||t.histIdx>=t.history.length-1) { showToast('Nothing to redo.'); return; }
  t.histIdx++;
  const snap=t.history[t.histIdx];
  t.annotations=JSON.parse(JSON.stringify(snap.annotations));
  t.textBoxes=JSON.parse(JSON.stringify(snap.textBoxes));
  t.redactions=JSON.parse(JSON.stringify(snap.redactions));
  t.pageOrder=[...snap.pageOrder];
  scheduleRender(); buildThumbs(); showToast('Redo.');
}
)JS";

ss << LR"JS(
// ── Menus ─────────────────────────────────────────────────────
function toggleMenu(id, btn) {
  const m=document.getElementById(id);
  const isOpen=m.classList.contains('show');
  closeAllMenus();
  if (!isOpen) {
    const r=btn.getBoundingClientRect();
    m.style.left=r.left+'px'; m.style.top=r.bottom+'px';
    m.classList.add('show'); btn.classList.add('open');
  }
}

function closeAllMenus() {
  document.querySelectorAll('.dropdown.show').forEach(d=>d.classList.remove('show'));
  document.querySelectorAll('.top-menu.open').forEach(b=>b.classList.remove('open'));
}
document.addEventListener('click', e=>{
  if (!e.target.closest('.dropdown') && !e.target.closest('.top-menu')) closeAllMenus();
});

// ── Ribbon tabs (legacy stub — quick-bar replaces ribbon) ─────
function switchRibbon(id, btn) {
  // No-op: ribbon replaced by quick-bar in Acrobat DC design
}

// ── Right-panel section toggle ─────────────────────────────────
function toggleRPSection(hdr) {
  const body=hdr.nextElementSibling;
  const collapsed=body.style.display==='none';
  body.style.display=collapsed?'':'none';
  hdr.querySelector('.toggle').textContent=collapsed?'▼':'▶';
}

// ── Context menu ─────────────────────────────────────────────
document.getElementById('pdf-container').addEventListener('contextmenu', e=>{
  e.preventDefault();
  const wrapper=e.target.closest('.page-wrapper');
  g_ctxPageIndex=wrapper?parseInt(wrapper.dataset.pageIndex):-1;
  const m=document.getElementById('ctx-menu');
  m.style.left=e.clientX+'px'; m.style.top=e.clientY+'px';
  m.classList.add('show');
});
function closeCtx() { document.getElementById('ctx-menu').classList.remove('show'); }
document.addEventListener('click', ()=>closeCtx());
function ctxCopy() { copySelectedText(); closeCtx(); }
function ctxDeletePage() {
  if (g_ctxPageIndex>=0) deletePageByOrder(g_ctxPageIndex); closeCtx();
}
function copySelectedText() {
  const sel=window.getSelection(); if (sel) { document.execCommand('copy'); showToast('Copied.','success'); }
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 31 · JS: Find / Search
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
let g_findMatches=[], g_findIdx=0;

function toggleFindBar() {
  const fb=document.getElementById('findbar');
  fb.classList.toggle('show');
  if (fb.classList.contains('show')) document.getElementById('find-input').focus();
  else clearHighlights();
}

async function doFind() {
  const q=document.getElementById('find-input').value.trim().toLowerCase();
  g_findMatches=[]; g_findIdx=0; clearHighlights();
  if (!q) { document.getElementById('find-count').textContent=''; return; }
  const t=activeTab(); if (!t) return;
  for (let i=0;i<t.pageOrder.length;i++) {
    const page=await t.doc.getPage(t.pageOrder[i]);
    const content=await page.getTextContent();
    const text=content.items.map(x=>x.str).join(' ').toLowerCase();
    let pos=0;
    while ((pos=text.indexOf(q,pos))!==-1) {
      g_findMatches.push({pageIndex:i, charPos:pos}); pos+=q.length;
    }
  }
  document.getElementById('find-count').textContent=
    g_findMatches.length>0 ? '1 of '+g_findMatches.length : 'Not found';
  if (g_findMatches.length) {
    const m=g_findMatches[0];
    document.getElementById('pw-'+m.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'});
  }
}

function findNext() {
  if (!g_findMatches.length) return;
  g_findIdx=(g_findIdx+1)%g_findMatches.length;
  document.getElementById('find-count').textContent=(g_findIdx+1)+' of '+g_findMatches.length;
  const m=g_findMatches[g_findIdx];
  document.getElementById('pw-'+m.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'});
}
function findPrev() {
  if (!g_findMatches.length) return;
  g_findIdx=(g_findIdx-1+g_findMatches.length)%g_findMatches.length;
  document.getElementById('find-count').textContent=(g_findIdx+1)+' of '+g_findMatches.length;
  const m=g_findMatches[g_findIdx];
  document.getElementById('pw-'+m.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'});
}
function clearHighlights() {
  g_findMatches=[]; g_findIdx=0;
  document.getElementById('find-count').textContent='';
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 32 · JS: Save PDF (bake annotations)
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
async function downloadCurrentPDF(saveAs2=false) {
  const t=activeTab(); if (!t) { showToast('No file open.'); return; }
  showLoading(true,'Baking annotations…',10);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const out=await PDFLib.PDFDocument.create();
    const copied=await out.copyPages(src, t.pageOrder.map(n=>n-1));
    copied.forEach(p=>out.addPage(p));
    const pages=out.getPages();
    setProgress(30);

    for (const a of t.annotations) {
      if (a.pageIndex>=pages.length) continue;
      const page=pages[a.pageIndex];
      const {width,height}=page.getSize();

      if (a.type==='stroke' && a.points && a.points.length>=2) {
        const isHL=a.alpha<0.8;
        const col=hexToRgb(a.color);
        for (let i=1;i<a.points.length;i++) {
          page.drawLine({
            start:{x:a.points[i-1].rx*width,y:height-(a.points[i-1].ry*height)},
            end:{x:a.points[i].rx*width,y:height-(a.points[i].ry*height)},
            thickness:a.lineWidth,
            color:col,
            opacity:a.alpha||1
          });
        }
      }
      else if (a.type==='shape') {
        const x1=a.x1*width,y1=height-(a.y1*height);
        const x2=a.x2*width,y2=height-(a.y2*height);
        const col=hexToRgb(a.color);
        if (a.tool==='rect') {
          const rx=Math.min(x1,x2), ry=Math.min(y1,y2);
          page.drawRectangle({x:rx,y:ry,width:Math.abs(x2-x1),height:Math.abs(y2-y1),
            borderColor:col,borderWidth:a.lineWidth,opacity:a.alpha||1});
        } else if (a.tool==='line'||a.tool==='arrow') {
          page.drawLine({start:{x:x1,y:y1},end:{x:x2,y:y2},
            thickness:a.lineWidth,color:col,opacity:a.alpha||1});
        } else if (a.tool==='redact') {
          const rx=Math.min(x1,x2), ry=Math.min(y1,y2);
          page.drawRectangle({x:rx,y:ry,width:Math.abs(x2-x1),height:Math.abs(y2-y1),
            color:PDFLib.rgb(0,0,0)});
        }
      }
      else if (a.type==='note') {
        const nx=a.rx*width, ny=height-(a.ry*height);
        page.drawRectangle({x:nx,y:ny-32,width:160,height:32,
          color:PDFLib.rgb(1,.97,.77),borderColor:PDFLib.rgb(.8,.75,.1),borderWidth:1,opacity:.9});
        page.drawText(a.text||'',{x:nx+4,y:ny-24,size:8,
          color:PDFLib.rgb(.1,.1,.1),maxWidth:150});
      }
      else if (a.type==='stamp') {
        const sx=a.rx*width, sy=height-(a.ry*height);
        const col=hexToRgb(STAMP_COLORS[a.label]||'#555');
        page.drawText(a.label,{x:sx,y:sy,size:24,
          color:col,opacity:.5});
      }
    }
    setProgress(60);
)JS";

ss << LR"JS(
    // Bake redactions
    for (const r of t.redactions) {
      if (r.pageIndex>=pages.length) continue;
      const page=pages[r.pageIndex];
      const {width,height}=page.getSize();
      page.drawRectangle({x:r.x*width,y:height-(r.y*height)-(r.h*height),
        width:r.w*width,height:r.h*height,color:PDFLib.rgb(0,0,0)});
    }

    // Bake text boxes
    for (const tb of t.textBoxes) {
      if (tb.pageIndex>=pages.length) continue;
      const page=pages[tb.pageIndex];
      const {width,height}=page.getSize();
      page.drawText(tb.text||'',{
        x:tb.rx*width,y:height-(tb.ry*height),
        size:tb.fontSize||12,color:hexToRgb(tb.color||'#000')
      });
    }

    // Bake pasted images
    for (const img of t.pasteImages) {
      if (img.pageIndex>=pages.length) continue;
      const page=pages[img.pageIndex];
      const {width,height}=page.getSize();
      try {
        let emb;
        if (img.mimeType==='image/png') emb=await out.embedPng(img.buffer);
        else emb=await out.embedJpg(img.buffer);
        const iw=img.rw*width, ih=img.rh*height;
        page.drawImage(emb,{x:img.rx*width,y:height-(img.ry*height)-ih,width:iw,height:ih});
      } catch(e) {}
    }
    setProgress(90);

    const saved=await out.save();
    const name=saveAs2?undefined:t.name;
    await saveBytesToFile(new Blob([saved],{type:'application/pdf'}), name||t.name);
    t.modified=false; renderTabStrip();
  } catch(e) { showToast('Save failed: '+e.message); }
  showLoading(false);
}

function hexToRgb(hex) {
  hex=hex.replace('#','');
  if (hex.length===3) hex=hex.split('').map(c=>c+c).join('');
  const r=parseInt(hex.slice(0,2),16)/255;
  const g=parseInt(hex.slice(2,4),16)/255;
  const b=parseInt(hex.slice(4,6),16)/255;
  return PDFLib.rgb(r,g,b);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 33 · JS: Merge, Split, Extract, Delete, Insert Blank
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function actionMergePDFs() {
  document.getElementById('mergeInput').click();
}

async function doMerge(e) {
  const files=[...e.target.files];
  e.target.value='';
  if (files.length<2) { showToast('Select at least 2 PDF files.'); return; }
  showLoading(true,'Combining PDFs…',0);
  try {
    const out=await PDFLib.PDFDocument.create();
    for (let i=0;i<files.length;i++) {
      setProgress(Math.round(i/files.length*80));
      const bytes=new Uint8Array(await files[i].arrayBuffer());
      const src=await PDFLib.PDFDocument.load(bytes);
      const copied=await out.copyPages(src,src.getPageIndices());
      copied.forEach(p=>out.addPage(p));
    }
    setProgress(95);
    await saveBytesToFile(new Blob([await out.save()]),'Combined.pdf');
  } catch(e) { showToast('Merge failed: '+e.message); }
  showLoading(false);
}

function modalSplit() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Split PDF',`
    <label>Split after page number (1–${t.pageOrder.length-1}):</label>
    <input type="number" id="split-at" min="1" max="${t.pageOrder.length-1}" value="1">
    <label>Or split into equal parts:</label>
    <input type="number" id="split-parts" min="2" max="${t.pageOrder.length}" value="2" placeholder="Parts">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doSplit()">Split</button>
    </div>`);
}

async function doSplit() {
  const t=activeTab();
  const sp=parseInt(document.getElementById('split-at').value);
  closeModal(); showLoading(true,'Splitting…',10);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const all=src.getPageIndices();
    const d1=await PDFLib.PDFDocument.create(), d2=await PDFLib.PDFDocument.create();
    const map=t.pageOrder.map(n=>n-1);
    (await d1.copyPages(src,map.slice(0,sp))).forEach(p=>d1.addPage(p));
    (await d2.copyPages(src,map.slice(sp))).forEach(p=>d2.addPage(p));
    setProgress(70);
    await saveBytesToFile(new Blob([await d1.save()]),'Part1.pdf');
    await saveBytesToFile(new Blob([await d2.save()]),'Part2.pdf');
  } catch(e){ showToast('Split failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
function modalExtract() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Extract Pages',`
    <label>Page numbers to extract (e.g. <code>1, 3, 5-7</code>):</label>
    <input type="text" id="extract-pages" placeholder="1, 2, 3">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doExtract()">Extract</button>
    </div>`);
}

async function doExtract() {
  const t=activeTab(); if (!t) return;
  const val=document.getElementById('extract-pages').value;
  const idxs=parsePageRange(val, t.pageOrder.length);
  if (!idxs.length){ showToast('No valid pages.'); return; }
  closeModal(); showLoading(true,'Extracting…',10);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const out=await PDFLib.PDFDocument.create();
    const pdfIdxs=idxs.map(i=>t.pageOrder[i]-1);
    (await out.copyPages(src,pdfIdxs)).forEach(p=>out.addPage(p));
    setProgress(80);
    await saveBytesToFile(new Blob([await out.save()]),'Extracted.pdf');
  } catch(e){ showToast('Extract failed.'); }
  showLoading(false);
}

function parsePageRange(str, total) {
  const result=[];
  str.split(',').forEach(part=>{
    part=part.trim();
    const dash=part.match(/^(\d+)-(\d+)$/);
    if (dash) {
      for (let i=parseInt(dash[1]);i<=parseInt(dash[2]);i++) {
        const idx=i-1; if (idx>=0&&idx<total) result.push(idx);
      }
    } else {
      const n=parseInt(part)-1; if (!isNaN(n)&&n>=0&&n<total) result.push(n);
    }
  });
  return [...new Set(result)].sort((a,b)=>a-b);
}

function modalDeletePages() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Delete Pages',`
    <label>Page numbers to delete (e.g. <code>2, 4, 6-8</code>):</label>
    <input type="text" id="del-pages" placeholder="e.g. 1, 3">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" style="background:#c62828;" onclick="doDeletePages()">Delete</button>
    </div>`);
}

function doDeletePages() {
  const t=activeTab(); if (!t) return;
  const val=document.getElementById('del-pages').value;
  const toRemove=new Set(parsePageRange(val,t.pageOrder.length));
  if (!toRemove.size){ showToast('No valid pages.'); return; }
  if (t.pageOrder.length-toRemove.size<1){ showToast('Cannot delete all pages.'); return; }
  closeModal(); pushHistory(t);
  const sorted=[...toRemove].sort((a,b)=>b-a);
  sorted.forEach(idx=>{
    t.pageOrder.splice(idx,1);
    ['annotations','textBoxes','pasteImages','redactions'].forEach(key=>{
      t[key]=t[key].filter(a=>a.pageIndex!==idx)
        .map(a=>({...a,pageIndex:a.pageIndex>idx?a.pageIndex-1:a.pageIndex}));
    });
  });
  t.modified=true; closeModal(); renderViewer(); buildThumbs();
  showToast('Page(s) deleted.','success');
}
)JS";

ss << LR"JS(
function modalInsertBlank() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Insert Blank Page',`
    <label>Insert after page (0 = before first):</label>
    <input type="number" id="insert-after" min="0" max="${t.pageOrder.length}" value="${t.pageOrder.length}">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-blue" onclick="doInsertBlank()">Insert</button>
    </div>`);
}

async function doInsertBlank() {
  const t=activeTab(); if (!t) return;
  const after=parseInt(document.getElementById('insert-after').value);
  closeModal(); showLoading(true,'Inserting blank page…',20);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const out=await PDFLib.PDFDocument.create();
    const copied=await out.copyPages(src,t.pageOrder.map(n=>n-1));
    const blankPage=out.addPage([612,792]);
    // Rebuild page order: insert blank after 'after'
    const newOrder=[];
    copied.forEach((p,i)=>{ if(i===after) newOrder.push(blankPage); newOrder.push(p); });
    if (after>=copied.length) newOrder.push(blankPage);
    // Rebuild PDF with correct order
    const final=await PDFLib.PDFDocument.create();
    newOrder.forEach(p=>{ const [np]=final.addPage([p.getWidth(),p.getHeight()]); });
    // Actually easier: just save with blank inserted
    const saved=await out.save();
    const newBytes=new Uint8Array(saved);
    const newDoc=await pdfjsLib.getDocument({data:newBytes}).promise;
    t.bytes=newBytes; t.doc=newDoc;
    t.pageOrder=Array.from({length:newDoc.numPages},(_,i)=>i+1);
    t.modified=true; renderViewer(); buildThumbs(); renderTabStrip();
    showToast('Blank page inserted.','success');
  } catch(e){ showToast('Insert failed: '+e.message); }
  showLoading(false);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 34 · JS: Watermark, Header/Footer, Bates, Password, Compress
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function modalWatermark() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showModal('Add Watermark',`
    <label>Watermark text:</label>
    <input type="text" id="wm-text" value="CONFIDENTIAL">
    <label>Font size:</label>
    <input type="number" id="wm-size" value="60" min="12" max="200">
    <label>Opacity (1–100):</label>
    <input type="number" id="wm-opacity" value="18" min="1" max="100">
    <label>Colour:</label>
    <input type="color" id="wm-color" value="#cc0000" style="width:48px;height:28px;border:none;">
    <label>Angle (degrees):</label>
    <input type="number" id="wm-angle" value="40" min="0" max="360">
    <label>Apply to:</label>
    <select id="wm-pages">
      <option value="all">All pages</option>
      <option value="odd">Odd pages</option>
      <option value="even">Even pages</option>
      <option value="first">First page only</option>
    </select>
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doWatermark()">Apply</button>
    </div>`);
}

async function doWatermark() {
  const t=activeTab(); if (!t) return;
  const txt=document.getElementById('wm-text').value;
  const sz=parseInt(document.getElementById('wm-size').value);
  const op=parseInt(document.getElementById('wm-opacity').value)/100;
  const col=hexToRgb(document.getElementById('wm-color').value);
  const angle=parseInt(document.getElementById('wm-angle').value);
  const which=document.getElementById('wm-pages').value;
  closeModal(); showLoading(true,'Applying watermark…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes);
    doc.getPages().forEach((page,i)=>{
      if (which==='odd'&&i%2!==0) return;
      if (which==='even'&&i%2===0) return;
      if (which==='first'&&i>0) return;
      const {width,height}=page.getSize();
      page.drawText(txt,{
        x:width/2-sz*txt.length/4, y:height/2,
        size:sz, color:col, opacity:op,
        rotate:PDFLib.degrees(angle)
      });
    });
    setProgress(80);
    await saveBytesToFile(new Blob([await doc.save()]),'Watermarked_'+t.name);
  } catch(e){ showToast('Watermark failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
function modalHeaderFooter() {
  showModal('Header & Footer',`
    <label>Header text (use {page}, {total}, {date}):</label>
    <input type="text" id="hf-header" value="Page {page} of {total}">
    <label>Footer text:</label>
    <input type="text" id="hf-footer" value="{date}">
    <label>Font size:</label>
    <input type="number" id="hf-size" value="9" min="6" max="24">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doHeaderFooter()">Apply</button>
    </div>`);
}

async function doHeaderFooter() {
  const t=activeTab(); if (!t) return;
  const hdr=document.getElementById('hf-header').value;
  const ftr=document.getElementById('hf-footer').value;
  const sz=parseInt(document.getElementById('hf-size').value);
  closeModal(); showLoading(true,'Applying header/footer…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes);
    const pages=doc.getPages(); const total=pages.length;
    const today=new Date().toLocaleDateString();
    pages.forEach((page,i)=>{
      const {width,height}=page.getSize();
      const rep=s=>s.replace(/{page}/g,i+1).replace(/{total}/g,total).replace(/{date}/g,today);
      if (hdr) page.drawText(rep(hdr),{x:40,y:height-20,size:sz,color:PDFLib.rgb(.3,.3,.3)});
      if (ftr) page.drawText(rep(ftr),{x:40,y:12,size:sz,color:PDFLib.rgb(.3,.3,.3)});
    });
    setProgress(80);
    await saveBytesToFile(new Blob([await doc.save()]),'HF_'+t.name);
  } catch(e){ showToast('Header/Footer failed.'); }
  showLoading(false);
}

function modalBatesNumber() {
  showModal('Bates Numbering',`
    <label>Prefix (e.g. DOC-):</label>
    <input type="text" id="bates-prefix" value="DOC-">
    <label>Start number:</label>
    <input type="number" id="bates-start" value="1" min="0">
    <label>Digits (zero-padded):</label>
    <input type="number" id="bates-digits" value="6" min="1" max="10">
    <label>Position:</label>
    <select id="bates-pos">
      <option value="br">Bottom Right</option>
      <option value="bl">Bottom Left</option>
      <option value="tr">Top Right</option>
      <option value="tl">Top Left</option>
    </select>
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doBates()">Apply</button>
    </div>`);
}

async function doBates() {
  const t=activeTab(); if (!t) return;
  const prefix=document.getElementById('bates-prefix').value;
  const start=parseInt(document.getElementById('bates-start').value);
  const digits=parseInt(document.getElementById('bates-digits').value);
  const pos=document.getElementById('bates-pos').value;
  closeModal(); showLoading(true,'Adding Bates numbers…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes);
    const pages=doc.getPages();
    pages.forEach((page,i)=>{
      const {width,height}=page.getSize();
      const label=prefix+String(start+i).padStart(digits,'0');
      let x,y;
      if (pos==='br'){x=width-80;y=12;}
      else if(pos==='bl'){x=20;y=12;}
      else if(pos==='tr'){x=width-80;y=height-20;}
      else{x=20;y=height-20;}
      page.drawText(label,{x,y,size:8,color:PDFLib.rgb(.2,.2,.2)});
    });
    setProgress(80);
    await saveBytesToFile(new Blob([await doc.save()]),'Bates_'+t.name);
  } catch(e){ showToast('Bates numbering failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
function modalPassword() {
  showModal('Encrypt PDF',`
    <p style="font-size:11px;color:var(--c-muted);margin-bottom:10px;">Note: PDF-lib 1.x does not support AES-256; output will have owner/user password set at PDF level.</p>
    <label>User password (to open):</label>
    <input type="password" id="pw-user" placeholder="Leave blank = no open password">
    <label>Owner password (to edit):</label>
    <input type="password" id="pw-owner" placeholder="Required for restrictions">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doPassword()">Apply</button>
    </div>`);
}

async function doPassword() {
  closeModal(); showToast('PDF encryption requires server-side processing in this build. Saved as-is.','warn');
}

async function actionCompressPDF() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showLoading(true,'Compressing…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes,{ignoreEncryption:true});
    setProgress(60);
    const saved=await doc.save({useObjectStreams:true,addDefaultPage:false});
    const ratio=Math.round((1-(saved.length/t.bytes.length))*100);
    setProgress(90);
    await saveBytesToFile(new Blob([saved]),'Compressed_'+t.name);
    showToast('Compressed! Size reduction: '+(ratio>0?ratio+'%':'minimal — already optimal.'),'success');
  } catch(e){ showToast('Compress failed.'); }
  showLoading(false);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 35 · JS: Export images, text, OCR, Stats, Panels, DocProps
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
async function actionPDFtoImages() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showLoading(true,'Rendering pages…',5);
  try {
    const zip=new JSZip();
    for (let i=0;i<t.pageOrder.length;i++) {
      setProgress(Math.round((i/t.pageOrder.length)*90));
      const page=await t.doc.getPage(t.pageOrder[i]);
      const vp=page.getViewport({scale:2.0});
      const c=document.createElement('canvas'); c.width=vp.width; c.height=vp.height;
      await page.render({canvasContext:c.getContext('2d'),viewport:vp}).promise;
      const blob=await new Promise(r=>c.toBlob(r,'image/png'));
      zip.file('Page_'+(i+1).toString().padStart(3,'0')+'.png',blob);
    }
    setProgress(95);
    const zipBlob=await zip.generateAsync({type:'blob'});
    await saveBytesToFile(zipBlob,'Pages_'+t.name.replace('.pdf','')+'.zip','zip','application/zip');
  } catch(e){ showToast('Export failed.'); }
  showLoading(false);
}

async function actionPDFtoText() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showLoading(true,'Extracting text…',5);
  try {
    let out='';
    for (let i=0;i<t.pageOrder.length;i++) {
      setProgress(Math.round((i/t.pageOrder.length)*90));
      const page=await t.doc.getPage(t.pageOrder[i]);
      const content=await page.getTextContent();
      out+='\n========== Page '+(i+1)+' ==========\n';
      out+=content.items.map(x=>x.str).join(' ')+'\n';
    }
    await saveBytesToFile(new Blob([out],{type:'text/plain'}),'Text_'+t.name.replace('.pdf','')+'.txt','txt','text/plain');
  } catch(e){ showToast('Text export failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
async function actionPerformOCR() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  // If in read mode, temporarily exit so loading overlay is visible
  const wasReadMode = document.body.classList.contains('read');
  if (wasReadMode) exitReadMode();
  showLoading(true,'Running OCR (page 1)…',10);
  try {
    const page=await t.doc.getPage(t.pageOrder[0]);
    const vp=page.getViewport({scale:2.5});
    const c=document.createElement('canvas'); c.width=vp.width; c.height=vp.height;
    await page.render({canvasContext:c.getContext('2d'),viewport:vp}).promise;
    setProgress(40);
    const result=await Tesseract.recognize(c.toDataURL('image/png'),'eng');
    setProgress(90);
    await saveBytesToFile(new Blob([result.data.text],{type:'text/plain'}),'OCR_'+t.name.replace('.pdf','')+'.txt','txt','text/plain');
    showToast('OCR complete! Confidence: '+Math.round(result.data.confidence)+'%','success');
  } catch(e){ showToast('OCR failed: '+e.message); }
  showLoading(false);
  // Restore read mode after OCR
  if (wasReadMode) enterReadMode();
}

async function refreshStats() {
  const t=activeTab(); if (!t) return;
  let words=0, chars=0;
  try {
    for (let i=0;i<Math.min(t.pageOrder.length,5);i++) {
      const page=await t.doc.getPage(t.pageOrder[i]);
      const content=await page.getTextContent();
      const text=content.items.map(x=>x.str).join(' ');
      chars+=text.length;
      words+=text.split(/\s+/).filter(Boolean).length;
    }
    if (t.pageOrder.length>5) { words=Math.round(words*(t.pageOrder.length/5)); chars=Math.round(chars*(t.pageOrder.length/5)); }
  } catch(e){}
  document.getElementById('stat-words').textContent=words.toLocaleString();
  document.getElementById('stat-chars').textContent=chars.toLocaleString();
  document.getElementById('stat-annots').textContent=
    (t.annotations.length+t.textBoxes.length+t.pasteImages.length+t.redactions.length).toLocaleString();
  document.getElementById('rp-curpage').textContent=t.pageOrder.length>0?'1 of '+t.pageOrder.length:'—';
}

// ── Panel toggles ─────────────────────────────────────────────
function toggleLeftPanel() { document.getElementById('left-panel').classList.toggle('open'); }
function toggleRightPanel() { document.getElementById('right-panel').classList.toggle('open'); }
function toggleBothPanels() { toggleLeftPanel(); toggleRightPanel(); }
)JS";

ss << LR"JS(
// ── View modes ────────────────────────────────────────────────
function cycleViewMode() {
  g_viewMode=(g_viewMode+1)%3;
  document.body.classList.remove('night','sepia');
  const labels=['Normal','Night','Sepia'];
  if (g_viewMode===1) document.body.classList.add('night');
  if (g_viewMode===2) document.body.classList.add('sepia');
  document.getElementById('sb-mode').textContent=labels[g_viewMode];
}
function enterReadMode() {
  document.body.classList.add('read');
  document.getElementById('sb-mode').textContent = 'Read';
}
function exitReadMode() {
  document.body.classList.remove('read');
  document.getElementById('sb-mode').textContent = 'Normal';
  setTimeout(() => { const t = activeTab(); if (t) renderViewer(); }, 80);
}
function toggleReadMode() {
  if (document.body.classList.contains('read')) exitReadMode();
  else enterReadMode();
}
function enterPresentation() {
  if (document.body.classList.contains('present')) {
    document.body.classList.remove('present');
    document.getElementById('sb-mode').textContent='Normal';
    document.exitFullscreen?.();
  } else {
    document.body.classList.add('present');
    document.getElementById('sb-mode').textContent='Presentation';
    document.documentElement.requestFullscreen?.();
  }
}

// ── Ruler & Grid ──────────────────────────────────────────────
function toggleRuler() {
  g_showRuler=!g_showRuler;
  showToast('Ruler: '+(g_showRuler?'On':'Off'));
}
function toggleGrid() {
  g_showGrid=!g_showGrid;
  document.querySelectorAll('.grid-overlay').forEach(g=>g.classList.toggle('show',g_showGrid));
  showToast('Grid: '+(g_showGrid?'On':'Off'));
}

// ── Document Properties ───────────────────────────────────────
async function showDocProperties() {
  const t=activeTab(); if (!t){ showToast('No file open.'); return; }
  let title='—', author='—', subject='—', keywords='—', creator='—';
  try {
    const meta=await t.doc.getMetadata();
    if (meta&&meta.info) {
      title=meta.info.Title||'—'; author=meta.info.Author||'—';
      subject=meta.info.Subject||'—'; keywords=meta.info.Keywords||'—';
      creator=meta.info.Creator||'—';
    }
  } catch(e){}
  showModal('Document Properties',`
    <table style="width:100%;font-size:11px;border-collapse:collapse;">
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);width:90px;">File</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${t.name}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Pages</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${t.pageOrder.length}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Title</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${title}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Author</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${author}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Subject</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${subject}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Creator</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${creator}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);">Size (est.)</td><td style="padding:4px 8px;">${(t.bytes.length/1024).toFixed(1)} KB</td></tr>
    </table>
    <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Close</button></div>`);
}
)JS";

ss << LR"JS(
// ── Keyboard shortcuts reference ───────────────────────────────
function showShortcutModal() {
  showModal('Keyboard Shortcuts',`
    <table style="width:100%;font-size:11px;border-collapse:collapse;">
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>1</kbd></td><td>Hand tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>2</kbd></td><td>Select tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>3</kbd></td><td>Pen tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>4</kbd></td><td>Highlight tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>5</kbd></td><td>Eraser</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>6</kbd></td><td>Sticky Note</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>7</kbd></td><td>Text Box</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>8</kbd></td><td>Stamp</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>R</kbd></td><td>Rotate all pages</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Del</kbd></td><td>Delete selected annotation</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+O</kbd></td><td>Open file(s)</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+S</kbd></td><td>Save / bake</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+Z</kbd></td><td>Undo</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+Y</kbd></td><td>Redo</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+F</kbd></td><td>Find text</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+0</kbd></td><td>Zoom 100%</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+Wheel</kbd></td><td>Smooth zoom</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Esc</kbd></td><td>Close dialogs</td></tr>
    </table>
    <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Close</button></div>`,true);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 36 · JS: Drag-drop open + resize observer + init
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
// ── Drag-drop open PDF — whole window (Acrobat style) ─────────
let g_dragDepth = 0;

document.addEventListener('dragenter', e => {
  if (!e.dataTransfer.types.includes('Files')) return;
  g_dragDepth++;
  if (g_dragDepth === 1) showDragOverlay(true);
});
document.addEventListener('dragleave', e => {
  g_dragDepth--;
  if (g_dragDepth <= 0) { g_dragDepth = 0; showDragOverlay(false); }
});
document.addEventListener('dragover', e => { e.preventDefault(); });
document.addEventListener('drop', async e => {
  e.preventDefault();
  g_dragDepth = 0;
  showDragOverlay(false);
  for (const f of e.dataTransfer.files) {
    if (!f.name.toLowerCase().endsWith('.pdf')) continue;
    const bytes = new Uint8Array(await f.arrayBuffer());
    await createTab(f.name, bytes);
  }
});

function showDragOverlay(show) {
  let ov = document.getElementById('drag-overlay');
  if (!ov) {
    ov = document.createElement('div');
    ov.id = 'drag-overlay';
    ov.style.cssText = [
      'position:fixed','inset:0','z-index:99999',
      'background:rgba(20,115,230,.18)',
      'border:3px dashed #1473E6',
      'display:flex','align-items:center','justify-content:center',
      'pointer-events:none',
    ].join(';');
    ov.innerHTML = '<div style="background:#1473E6;color:#fff;padding:16px 32px;border-radius:3px;font-size:18px;font-weight:700;">Drop PDF to Open</div>';
    document.body.appendChild(ov);
  }
  ov.style.display = show ? 'flex' : 'none';
}

// ── Window resize → re-render ─────────────────────────────────
let g_resizeTimer = null;
window.addEventListener('resize', () => {
  if (g_resizeTimer) clearTimeout(g_resizeTimer);
  g_resizeTimer = setTimeout(() => scheduleRender(), 150);
});

// ── Expose bridge for C++ WebView2 ───────────────────────────
window.loadPdfFromPath=loadPdfFromPath;
window.g_webViewController_bridge=true;

// ── Init ──────────────────────────────────────────────────────
setTool('hand');
renderTabStrip();
updateStatusBar();
renderRecentFiles();
</script>
</body>
</html>
)JS";

    return ss.str();
}

// ==========================================
// WINDOW PROCEDURE
// ==========================================
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT r; GetClientRect(hWnd, &r);
        g_hWebViewWnd = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, r.right, r.bottom, hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
        InitializeWebView2(hWnd, g_hWebViewWnd);
        break;
    }
    case WM_SIZE: {
        if (g_hWebViewWnd && g_webViewController) {
            RECT r; GetClientRect(hWnd, &r);
            SetWindowPos(g_hWebViewWnd, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER);
            g_webViewController->put_Bounds(RECT{ 0, 0, r.right, r.bottom });
        }
        break;
    }
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    case WM_DESTROY: {
        if (g_webViewController) { g_webViewController->Close(); g_webViewController = nullptr; }
        g_webView = nullptr; g_webViewEnv = nullptr; g_webViewInitialized = false;
        if (g_hWebViewWnd) { DestroyWindow(g_hWebViewWnd); g_hWebViewWnd = NULL; }
        g_hAcrobatWnd = NULL;
        break;
    }
    default: return DefWindowProcW(hWnd, msg, wp, lp);
    }
    return 0;
}

// ==========================================
// WEBVIEW2 INITIALIZATION
// ==========================================
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd) {
    auto envHandler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hWnd, hHostWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result)) return result;
            g_webViewEnv = env;

            auto ctrlHandler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                    if (FAILED(result)) return result;
                    g_webViewController = controller;
                    g_webViewController->get_CoreWebView2(&g_webView);

                    ICoreWebView2Settings* settings;
                    g_webView->get_Settings(&settings);
                    settings->put_IsScriptEnabled(TRUE);
                    settings->put_IsWebMessageEnabled(TRUE);

                    RECT r; GetClientRect(hWnd, &r);
                    g_webViewController->put_Bounds(RECT{ 0, 0, r.right, r.bottom });

                    g_webView->NavigateToString(GetAcrobatHTML().c_str());

                    auto navHandler = Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                            BOOL success; args->get_IsSuccess(&success);
                            if (success) {
                                g_webViewInitialized = true;
                                if (!g_acrobatPdfPath.empty()) {
                                    std::wstring escaped = g_acrobatPdfPath;
                                    size_t pos = 0;
                                    while ((pos = escaped.find(L"\\", pos)) != std::wstring::npos) {
                                        escaped.replace(pos, 1, L"\\\\");
                                        pos += 2;
                                    }
                                    std::wstring script = L"loadPdfFromPath('" + escaped + L"');";
                                    sender->ExecuteScript(script.c_str(), nullptr);
                                }
                            }
                            return S_OK;
                        }
                    );
                    g_webView->add_NavigationCompleted(navHandler.Get(), nullptr);
                    return S_OK;
                }
            );
            env->CreateCoreWebView2Controller(hHostWnd, ctrlHandler.Get());
            return S_OK;
        }
    );
    return CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, envHandler.Get());
}

// ==========================================
// LAUNCH PDF VIEWER
// ==========================================
void LaunchFoxitStylePdfReader(std::wstring pdfPath) {
    g_acrobatPdfPath = pdfPath;

    if (g_hAcrobatWnd != NULL) {
        ShowWindow(g_hAcrobatWnd, SW_RESTORE);
        SetForegroundWindow(g_hAcrobatWnd);
        if (g_webViewInitialized && g_webView && !pdfPath.empty()) {
            std::wstring escaped = pdfPath;
            size_t pos = 0;
            while ((pos = escaped.find(L"\\", pos)) != std::wstring::npos) {
                escaped.replace(pos, 1, L"\\\\");
                pos += 2;
            }
            std::wstring script = L"loadPdfFromPath('" + escaped + L"');";
            g_webView->ExecuteScript(script.c_str(), nullptr);
        }
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = AcrobatViewerWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"AcrobatWorkspaceClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    g_hAcrobatWnd = CreateWindowExW(
        0, L"AcrobatWorkspaceClass", L"RasFocus — PDF Pro",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (int)(1280 * g_scaleFactor), (int)(820 * g_scaleFactor),
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
    if (hIcon) {
        SendMessage(g_hAcrobatWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(g_hAcrobatWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    ShowWindow(g_hAcrobatWnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(g_hAcrobatWnd);
    UpdateWindow(g_hAcrobatWnd);
}

// ==========================================
// LEGACY STUBS
// ==========================================
void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {
    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 18 * g_scaleFactor, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(Color(255, 120, 120, 120));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"PDF Workspace — double-click a PDF to open.", -1, &fText, RectF(cx, cy, cw, ch), &fmt, &textBrush);
}
void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
