// html_tools.h
#pragma once
#include <string>

// ==========================================
// 1. PDF Split Tool (LOCAL_PDF_SPLIT)
// ==========================================
const std::wstring HTML_PDF_SPLIT = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - Split PDF</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
    <style>body { background-color: #f8fafc; font-family: 'Inter', sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }</style>
</head>
<body>
    <div class="bg-white p-8 rounded-xl shadow-lg border-t-4 border-red-500 w-full max-w-md">
        <div class="flex items-center gap-4 mb-6">
            <div class="w-12 h-12 flex items-center justify-center rounded-lg bg-red-100 text-red-600 text-2xl">✂️</div>
            <div>
                <h3 class="text-xl font-bold text-slate-800">Split PDF</h3>
                <p class="text-slate-500 text-sm">Extract specific pages from PDF.</p>
            </div>
        </div>
        <input type="file" id="split-file" accept=".pdf" class="w-full border p-2 rounded mb-4 text-sm">
        <div class="flex gap-2 mb-4">
            <input type="number" id="split-start" placeholder="Start Page" min="1" class="w-1/2 border p-2 rounded">
            <input type="number" id="split-end" placeholder="End Page" min="1" class="w-1/2 border p-2 rounded">
        </div>
        <button onclick="splitPDF()" class="w-full bg-red-600 hover:bg-red-700 text-white font-bold py-3 px-4 rounded-lg transition-all">Extract Pages</button>
    </div>
    <script>
        const { PDFDocument } = PDFLib;
        const readFileAsync = (file) => new Promise((resolve, reject) => { let r = new FileReader(); r.onload = () => resolve(r.result); r.readAsArrayBuffer(file); });
        
        async function splitPDF() {
            const fileInput = document.getElementById('split-file');
            const startPage = parseInt(document.getElementById('split-start').value);
            const endPage = parseInt(document.getElementById('split-end').value);
            if (!fileInput.files[0] || !startPage || !endPage) return alert("Please select file and page range");
            
            const pdfBytes = await readFileAsync(fileInput.files[0]);
            const pdfDoc = await PDFDocument.load(pdfBytes);
            const newPdf = await PDFDocument.create();
            const totalPages = pdfDoc.getPageCount();
            
            if(startPage < 1 || endPage > totalPages || startPage > endPage) return alert("Invalid page range");
            const range = []; for (let i = startPage - 1; i < endPage; i++) range.push(i);
            const copiedPages = await newPdf.copyPages(pdfDoc, range);
            copiedPages.forEach((page) => newPdf.addPage(page));
            
            const pdfData = await newPdf.save();
            const blob = new Blob([pdfData], { type: 'application/pdf' });
            saveAs(blob, `split_pages_${startPage}-${endPage}.pdf`);
        }
    </script>
</body>
</html>
)html";

// ==========================================
// 2. Advanced PDF Merger (LOCAL_PDF_MERGE)
// ==========================================
const std::wstring HTML_PDF_MERGE = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - PDF Merger</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <script src="https://unpkg.com/pdf-lib/dist/pdf-lib.min.js"></script>
    <style>body{background:#f8fafc;display:flex;justify-content:center;align-items:center;height:100vh;} .drag-over{border:2px dashed #4b5563;background:#f3f4f6;opacity:0.7;}</style>
</head>
<body>
    <div class="bg-white p-6 rounded-xl shadow-lg border-t-4 border-gray-600 w-full max-w-md">
        <div class="mb-4 flex items-center gap-3">
            <div class="w-12 h-12 flex items-center justify-center rounded-lg bg-gray-100 text-gray-600 text-2xl shadow-inner"><i class="fa-solid fa-file-pdf"></i></div>
            <div>
                <h3 class="text-xl font-bold text-slate-800">Advanced PDF Merger</h3>
                <p class="text-slate-500 text-sm">Combine & reorder multiple PDFs.</p>
            </div>
        </div>
        <input type="file" id="merge-files" multiple accept=".pdf" class="hidden" onchange="handlePDFSelection(event)">
        <label for="merge-files" class="w-full cursor-pointer flex items-center justify-center gap-2 bg-gray-100 hover:bg-gray-200 text-gray-700 font-bold py-3 px-4 rounded-xl text-sm mb-4 border border-dashed border-gray-300">
            <i class="fa-solid fa-plus-circle text-gray-500"></i> Select PDFs
        </label>
        <ul id="pdf-list" class="mb-4 space-y-2 max-h-60 overflow-y-auto pr-1"></ul>
        <button id="merge-btn" onclick="executeMerge()" class="hidden w-full bg-gray-700 hover:bg-gray-800 text-white font-bold py-3 px-4 rounded-xl flex items-center justify-center gap-2 shadow-lg">
            <i class="fa-solid fa-layer-group"></i> Merge PDFs Now
        </button>
    </div>
    <script>
        let selectedPDFs = []; let dragStartIndex = -1;
        function handlePDFSelection(event) {
            const files = event.target.files;
            if (files.length > 0) { for (let i = 0; i < files.length; i++) selectedPDFs.push(files[i]); renderPDFList(); }
            event.target.value = '';
        }
        function renderPDFList() {
            const list = document.getElementById('pdf-list'); const btn = document.getElementById('merge-btn');
            list.innerHTML = '';
            if (selectedPDFs.length === 0) { list.innerHTML = '<p class="text-xs text-center text-gray-400 py-4 italic">No files selected.</p>'; btn.classList.add('hidden'); return; }
            if (selectedPDFs.length >= 2) btn.classList.remove('hidden'); else btn.classList.add('hidden');
            
            selectedPDFs.forEach((file, index) => {
                const li = document.createElement('li');
                li.className = 'flex items-center justify-between bg-white border rounded-lg p-3 shadow-sm cursor-grab group';
                li.draggable = true;
                li.ondragstart = (e) => dragStartIndex = index;
                li.ondrop = (e) => { e.preventDefault(); if (dragStartIndex !== index) { const item = selectedPDFs.splice(dragStartIndex, 1)[0]; selectedPDFs.splice(index, 0, item); renderPDFList(); }};
                li.ondragover = (e) => e.preventDefault();
                let fName = file.name.length > 20 ? file.name.substring(0, 20) + '...' : file.name;
                li.innerHTML = `<div class="flex items-center gap-2"><i class="fa-solid fa-grip-vertical text-gray-300"></i><span class="text-sm font-semibold">${fName}</span></div>
                <div><button onclick="removeFile(${index})" class="text-red-500 hover:text-red-700"><i class="fa-solid fa-xmark"></i></button></div>`;
                list.appendChild(li);
            });
        }
        window.removeFile = (index) => { selectedPDFs.splice(index, 1); renderPDFList(); }
        window.executeMerge = async () => {
            const btn = document.getElementById('merge-btn'); btn.disabled = true; btn.innerHTML = 'Processing...';
            try {
                const { PDFDocument } = PDFLib; const mergedPdf = await PDFDocument.create();
                for (let file of selectedPDFs) {
                    const buf = await file.arrayBuffer(); const pdfDoc = await PDFDocument.load(buf);
                    const copiedPages = await mergedPdf.copyPages(pdfDoc, pdfDoc.getPageIndices());
                    copiedPages.forEach((page) => mergedPdf.addPage(page));
                }
                const pdfBytes = await mergedPdf.save();
                const blob = new Blob([pdfBytes], { type: 'application/pdf' });
                const link = document.createElement('a'); link.href = URL.createObjectURL(blob); link.download = 'Merged_RasFocus.pdf'; link.click();
                btn.innerHTML = 'Merged Successfully!'; setTimeout(() => { btn.disabled=false; btn.innerHTML='<i class="fa-solid fa-layer-group"></i> Merge PDFs Now'; }, 2000);
            } catch (e) { alert("Error merging PDFs"); btn.disabled=false; btn.innerHTML='<i class="fa-solid fa-layer-group"></i> Merge PDFs Now'; }
        }
    </script>
</body>
</html>
)html";

// ==========================================
// 3. Image to PDF (LOCAL_IMG_TO_PDF)
// ==========================================
const std::wstring HTML_IMG_TO_PDF = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - Image to PDF</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js"></script>
    <style>body{background:#f8fafc;display:flex;justify-content:center;align-items:center;height:100vh;}</style>
</head>
<body>
    <div class="bg-white p-8 rounded-xl shadow-lg border-t-4 border-green-500 w-full max-w-md">
        <div class="flex items-center gap-4 mb-6">
            <div class="w-12 h-12 flex items-center justify-center rounded-lg bg-green-100 text-green-600 text-2xl">📄</div>
            <div>
                <h3 class="text-xl font-bold text-slate-800">Photo to PDF</h3>
                <p class="text-slate-500 text-sm">Convert images to a single PDF.</p>
            </div>
        </div>
        <input type="file" id="img-to-pdf-files" multiple accept="image/*" class="w-full border p-2 rounded mb-4 text-sm">
        <button onclick="imagesToPDF()" class="w-full bg-green-600 hover:bg-green-700 text-white font-bold py-3 px-4 rounded-lg">Create PDF</button>
    </div>
    <script>
        const readImageAsync = (file) => new Promise((resolve) => { let r = new FileReader(); r.onload = () => resolve(r.result); r.readAsDataURL(file); });
        async function imagesToPDF() {
            const fileInput = document.getElementById('img-to-pdf-files');
            if (fileInput.files.length === 0) return alert("Select images");
            const { jsPDF } = window.jspdf; const doc = new jsPDF();
            for (let i = 0; i < fileInput.files.length; i++) {
                const dataUrl = await readImageAsync(fileInput.files[i]);
                const imgProps = doc.getImageProperties(dataUrl);
                const pdfWidth = doc.internal.pageSize.getWidth();
                const pdfHeight = (imgProps.height * pdfWidth) / imgProps.width;
                if (i > 0) doc.addPage();
                doc.addImage(dataUrl, 'JPEG', 0, 0, pdfWidth, pdfHeight);
            }
            doc.save("images_combined.pdf");
        }
    </script>
</body>
</html>
)html";

// ==========================================
// 4. Job Photo Maker 300x300 (LOCAL_JOB_PHOTO)
// ==========================================
const std::wstring HTML_JOB_PHOTO = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - Job Photo Maker</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
    <style>body{background:#f8fafc;display:flex;justify-content:center;align-items:center;height:100vh;}</style>
</head>
<body>
    <div class="bg-white p-8 rounded-xl shadow-lg border-t-4 border-blue-500 w-full max-w-md">
        <div class="flex items-center gap-4 mb-6">
            <div class="w-12 h-12 flex items-center justify-center rounded-lg bg-blue-100 text-blue-600 text-2xl">👤</div>
            <div>
                <h3 class="text-xl font-bold text-slate-800">Job Photo Maker</h3>
                <p class="text-slate-500 text-sm">Convert to exact 300x300 px (JPG).</p>
            </div>
        </div>
        <input type="file" id="job-photo-file" accept="image/*" class="w-full border p-2 rounded mb-4 text-sm">
        <button onclick="processImage(300, 300, 'job-photo.jpg')" class="w-full bg-blue-600 hover:bg-blue-700 text-white font-bold py-3 px-4 rounded-lg">Convert Photo</button>
        <canvas id="canvas" class="hidden"></canvas>
    </div>
    <script>
        function processImage(targetW, targetH, filename) {
            const fileInput = document.getElementById('job-photo-file');
            if (!fileInput.files[0]) return alert("Select an image");
            const reader = new FileReader();
            reader.onload = function(event) {
                const img = new Image();
                img.onload = function() {
                    const canvas = document.getElementById('canvas'); canvas.width = targetW; canvas.height = targetH;
                    const ctx = canvas.getContext('2d'); ctx.fillStyle = "#FFFFFF"; ctx.fillRect(0,0, targetW, targetH);
                    ctx.drawImage(img, 0, 0, targetW, targetH);
                    canvas.toBlob((blob) => { saveAs(blob, filename); }, 'image/jpeg', 0.9);
                }; img.src = event.target.result;
            }; reader.readAsDataURL(fileInput.files[0]);
        }
    </script>
</body>
</html>
)html";

// ==========================================
// 5. Job Signature Maker 300x80 (LOCAL_JOB_SIGN)
// ==========================================
const std::wstring HTML_JOB_SIGN = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - Job Signature Maker</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
    <style>body{background:#f8fafc;display:flex;justify-content:center;align-items:center;height:100vh;}</style>
</head>
<body>
    <div class="bg-white p-8 rounded-xl shadow-lg border-t-4 border-indigo-500 w-full max-w-md">
        <div class="flex items-center gap-4 mb-6">
            <div class="w-12 h-12 flex items-center justify-center rounded-lg bg-indigo-100 text-indigo-600 text-2xl">✍️</div>
            <div>
                <h3 class="text-xl font-bold text-slate-800">Signature Maker</h3>
                <p class="text-slate-500 text-sm">Convert to exact 300x80 px (JPG).</p>
            </div>
        </div>
        <input type="file" id="sign-photo-file" accept="image/*" class="w-full border p-2 rounded mb-4 text-sm">
        <button onclick="processImage(300, 80, 'signature.jpg')" class="w-full bg-indigo-600 hover:bg-indigo-700 text-white font-bold py-3 px-4 rounded-lg">Convert Signature</button>
        <canvas id="canvas" class="hidden"></canvas>
    </div>
    <script>
        function processImage(targetW, targetH, filename) {
            const fileInput = document.getElementById('sign-photo-file');
            if (!fileInput.files[0]) return alert("Select an image");
            const reader = new FileReader();
            reader.onload = function(event) {
                const img = new Image();
                img.onload = function() {
                    const canvas = document.getElementById('canvas'); canvas.width = targetW; canvas.height = targetH;
                    const ctx = canvas.getContext('2d'); ctx.fillStyle = "#FFFFFF"; ctx.fillRect(0,0, targetW, targetH);
                    ctx.drawImage(img, 0, 0, targetW, targetH);
                    canvas.toBlob((blob) => { saveAs(blob, filename); }, 'image/jpeg', 0.9);
                }; img.src = event.target.result;
            }; reader.readAsDataURL(fileInput.files[0]);
        }
    </script>
</body>
</html>
)html";

// ==========================================
// 6. Age Calculator (LOCAL_AGE_CALC)
// ==========================================
const std::wstring HTML_AGE_CALC = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - Age Calculator</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>body{background:#f8fafc;display:flex;justify-content:center;align-items:center;height:100vh;}</style>
</head>
<body>
    <div class="bg-white p-8 rounded-xl shadow-lg border-t-4 border-lime-500 w-full max-w-md">
        <div class="flex items-center gap-4 mb-6">
            <div class="w-12 h-12 flex items-center justify-center rounded-lg bg-lime-100 text-lime-700 text-2xl">📅</div>
            <div>
                <h3 class="text-xl font-bold text-slate-800">Age Calculator</h3>
                <p class="text-slate-500 text-sm">Calculate exact age for job forms.</p>
            </div>
        </div>
        <label class="text-xs text-slate-500 font-bold block mb-1">Date of Birth:</label>
        <input type="date" id="age-dob" class="w-full border p-2 rounded mb-3 text-sm">
        
        <label class="text-xs text-slate-500 font-bold block mb-1">Calculate as of (Optional):</label>
        <input type="date" id="age-target" class="w-full border p-2 rounded mb-5 text-sm">
        
        <button onclick="calculateAge()" class="w-full bg-lime-600 hover:bg-lime-700 text-white font-bold py-3 px-4 rounded-lg">Calculate Exact Age</button>
        <div id="age-result" class="mt-4 p-3 bg-slate-100 rounded text-center font-bold text-slate-800 hidden"></div>
    </div>
    <script>
        function calculateAge() {
            const dobInput = document.getElementById('age-dob').value;
            const targetInput = document.getElementById('age-target').value || new Date().toISOString().split('T')[0];
            if(!dobInput) return alert("Please select Date of Birth");
            const dob = new Date(dobInput); const target = new Date(targetInput);
            let years = target.getFullYear() - dob.getFullYear();
            let months = target.getMonth() - dob.getMonth();
            let days = target.getDate() - dob.getDate();
            if (days < 0) { months--; const prevMonth = new Date(target.getFullYear(), target.getMonth(), 0); days += prevMonth.getDate(); }
            if (months < 0) { years--; months += 12; }
            const res = document.getElementById('age-result');
            res.innerText = `${years} Years, ${months} Months, ${days} Days`;
            res.classList.remove('hidden');
        }
    </script>
</body>
</html>
)html";

// ==========================================
// 7. Compress PDF (LOCAL_COMPRESS_PDF)
// ==========================================
const std::wstring HTML_COMPRESS_PDF = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - Compress PDF</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
    <style>body{background:#f8fafc;display:flex;justify-content:center;align-items:center;height:100vh;}</style>
</head>
<body>
    <div class="bg-white p-8 rounded-xl shadow-lg border-t-4 border-orange-500 w-full max-w-md">
        <div class="flex items-center gap-4 mb-6">
            <div class="w-12 h-12 flex items-center justify-center rounded-lg bg-orange-100 text-orange-600 text-2xl">🗜️</div>
            <div>
                <h3 class="text-xl font-bold text-slate-800">Compress PDF</h3>
                <p class="text-slate-500 text-sm">Optimize PDF structure to save space.</p>
            </div>
        </div>
        <input type="file" id="compress-file" accept=".pdf" class="w-full border p-2 rounded mb-4 text-sm">
        <button onclick="compressPDF()" class="w-full bg-orange-600 hover:bg-orange-700 text-white font-bold py-3 px-4 rounded-lg">Compress & Save</button>
    </div>
    <script>
        const { PDFDocument } = PDFLib;
        const readFileAsync = (file) => new Promise((resolve) => { let r = new FileReader(); r.onload = () => resolve(r.result); r.readAsArrayBuffer(file); });
        async function compressPDF() {
            const fileInput = document.getElementById('compress-file');
            if (!fileInput.files[0]) return alert("Please select a PDF");
            const pdfBytes = await readFileAsync(fileInput.files[0]);
            const pdfDoc = await PDFDocument.load(pdfBytes);
            const pdfData = await pdfDoc.save({ useObjectStreams: false }); 
            const blob = new Blob([pdfData], { type: 'application/pdf' });
            saveAs(blob, 'compressed_doc.pdf');
        }
    </script>
</body>
</html>
)html";

// ==========================================
// 8. Photo Viewer (LOCAL_PHOTO_VIEWER)
// ==========================================
const std::wstring HTML_PHOTO_VIEWER = LR"html(
<!DOCTYPE html>
<html lang="bn">
<head>
    <meta charset="UTF-8">
    <title>RasBrowse - Local Image Viewer</title>
    <style>
        body { margin: 0; background-color: #1a1a1a; color: white; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; font-family: 'Segoe UI', sans-serif; overflow: hidden; }
        #viewer-container { width: 90%; height: 75vh; display: flex; justify-content: center; align-items: center; background-color: #0d0d0d; border-radius: 10px; box-shadow: inset 0 0 20px rgba(0,0,0,0.8); margin-bottom: 20px;}
        #viewer { max-width: 100%; max-height: 100%; object-fit: contain; display: none; }
        .controls { text-align: center; background: rgba(255,255,255,0.05); padding: 15px 30px; border-radius: 12px; }
        .file-input-wrapper { margin-bottom: 15px; }
        input[type="file"] { background: #333; color: white; padding: 10px; border-radius: 5px; cursor: pointer; }
        p { color: #aaa; font-size: 14px; margin: 5px 0; }
        span { font-weight: bold; color: #0cb8b0; }
        #counter { font-size: 16px; font-weight: bold; color: white; margin-top: 10px; }
    </style>
</head>
<body>
    <div id="viewer-container">
        <img id="viewer" src="" alt="Selected Image">
    </div>
    <div class="controls">
        <div class="file-input-wrapper">
            <input type="file" id="fileInput" accept="image/*" multiple>
        </div>
        <p id="info">ফাইল সিলেক্ট করে কিবোর্ডের <span>← Left</span> এবং <span>Right →</span> অ্যারো ব্যবহার করুন।</p>
        <p id="counter">কোনো ছবি নেই</p>
    </div>
    <script>
        const fileInput = document.getElementById('fileInput');
        const viewer = document.getElementById('viewer');
        const counter = document.getElementById('counter');
        let images = []; let currentIndex = 0;

        fileInput.onchange = function(e) {
            images = Array.from(e.target.files);
            if (images.length > 0) { currentIndex = 0; showImage(currentIndex); viewer.style.display = "block"; }
        };

        function showImage(index) {
            const file = images[index]; const reader = new FileReader();
            reader.onload = function(event) {
                viewer.src = event.target.result;
                counter.innerText = `ইমেজ: ${index + 1} / ${images.length}`;
            };
            reader.readAsDataURL(file);
        }

        document.addEventListener('keydown', function(event) {
            if (images.length === 0) return;
            if (event.key === "ArrowRight") { currentIndex = (currentIndex + 1) % images.length; showImage(currentIndex); } 
            else if (event.key === "ArrowLeft") { currentIndex = (currentIndex - 1 + images.length) % images.length; showImage(currentIndex); }
        });
    </script>
</body>
</html>
)html";
