// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// webfm_html.h — die eingebettete Web-Oberfläche der SD-Dateiverwaltung.
//
// Eine einzelne Seite (PROGMEM), die per fetch() gegen die JSON-API aus
// webfm.cpp arbeitet (/api/list, /api/download, /api/upload, /api/delete,
// /api/mkdir). Bewusst ohne externe Abhängigkeiten — funktioniert offline.
// =============================================================================
#pragma once

#include <pgmspace.h>

const char kWebFmHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jarvis &mdash; SD-Card</title>
<style>
 body{font-family:system-ui,sans-serif;margin:0;background:#f4f1ea;color:#222}
 header{background:#2b2620;color:#f4f1ea;padding:10px 16px}
 header h1{margin:0;font-size:18px}
 header small{opacity:.7;font-weight:normal}
 main{max-width:760px;margin:0 auto;padding:12px}
 #crumbs{margin:8px 0;font-size:14px}
 #crumbs a{color:#7a4a12;text-decoration:none}
 #crumbs a:hover{text-decoration:underline}
 .bar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}
 button{background:#7a4a12;color:#fff;border:0;border-radius:6px;padding:6px 12px;cursor:pointer}
 button:hover{background:#935a1a}
 button.warn{background:#8a2a2a}
 table{width:100%;border-collapse:collapse;background:#fff;border-radius:8px;overflow:hidden}
 th,td{text-align:left;padding:7px 10px;border-bottom:1px solid #e4ded2;font-size:14px}
 th{background:#e9e2d4}
 td.sz{text-align:right;white-space:nowrap;color:#666}
 td.act{text-align:right;white-space:nowrap}
 a.dir{font-weight:bold;color:#7a4a12;text-decoration:none}
 a.file{color:#222;text-decoration:none}
 a.file:hover,a.dir:hover{text-decoration:underline}
 #msg{margin:8px 0;font-size:14px;color:#555;min-height:18px}
 progress{width:160px}
 input[type="text"]{border:1px solid #e4ded2;border-radius:6px;padding:6px 10px;font-size:14px;background:#fff}
 section.ota{margin-top:22px;background:#fff;border-radius:8px;padding:12px 14px;border:1px solid #e4ded2}
 section.ota h2{margin:0 0 6px;font-size:16px;color:#2b2620}
 #otamsg{font-size:14px;color:#555;min-height:18px;margin:6px 0}
</style>
</head>
<body>
<header><h1>Jarvis <small>&mdash; SD-File Management</small></h1></header>
<main>
<div id="crumbs"></div>
<div class="bar">
  <input type="file" id="up" multiple>
  <button onclick="upload()">Upload</button>
  <button onclick="mkdir()">New folder</button>
  <progress id="prog" value="0" max="100" hidden></progress>
 </div>
 <div class="bar">
  <input type="text" id="dlurl" placeholder="https://example.com/file.bin" style="flex:1;min-width:200px">
  <button onclick="fetchUrl()">Download URL</button>
 </div>
<div id="msg"></div>
<table>
 <thead><tr><th>Name</th><th style="text-align:right">Size</th><th></th></tr></thead>
 <tbody id="tbl"></tbody>
</table>
<section class="ota">
 <h2>Firmware-Update</h2>
 <div id="otamsg">Current version: &hellip;</div>
 <div class="bar">
  <button onclick="otaCheck()">Updates check</button>
  <button id="otabtn" onclick="otaUpdate()" hidden>Update now</button>
 </div>
</section>
</main>
<script>
let cur = '/';
const enc = encodeURIComponent;
const $   = id => document.getElementById(id);

function fmtSize(n){
  if (n >= 1048576) return (n/1048576).toFixed(1) + ' MB';
  if (n >= 1024)    return (n/1024).toFixed(1) + ' KB';
  return n + ' B';
}

function crumbs(){
  const parts = cur.split('/').filter(Boolean);
  let html = '<a href="#" onclick="go(\'/\');return false">SD</a>';
  let p = '';
  for (const part of parts){
    p += '/' + part;
    html += ' / <a href="#" onclick="go(\'' + p.replace(/'/g,"\\'") + '\');return false">' + part + '</a>';
  }
  $('crumbs').innerHTML = html;
}

async function load(){
  $('msg').textContent = 'Load ...';
  try {
    const r = await fetch('/api/list?path=' + enc(cur));
    const j = await r.json();
    if (!r.ok) throw new Error(j.err || r.status);
    j.entries.sort((a,b) => (b.d - a.d) || a.n.localeCompare(b.n));
    let rows = '';
    for (const e of j.entries){
      const full = (cur === '/' ? '' : cur) + '/' + e.n;
      const fq = full.replace(/'/g,"\\'");
      if (e.d){
        rows += '<tr><td><a class="dir" href="#" onclick="go(\'' + fq + '\');return false">&#128193; '
              + e.n + '</a></td><td class="sz"></td><td class="act">'
              + '<button onclick="ren(\'' + fq + '\',\'' + e.n.replace(/'/g,"\\'") + '\')" style="margin-right:4px">Rename</button>'
              + '<button class="warn" onclick="del(\'' + fq + '\')">Delete</button></td></tr>';
      } else {
        rows += '<tr><td><a class="file" href="/api/download?path=' + enc(full) + '">' + e.n
              + '</a></td><td class="sz">' + fmtSize(e.s) + '</td><td class="act">'
              + '<button onclick="ren(\'' + fq + '\',\'' + e.n.replace(/'/g,"\\'") + '\')" style="margin-right:4px">Rename</button>'
              + '<button class="warn" onclick="del(\'' + fq + '\')">Delete</button></td></tr>';
      }
    }
    $('tbl').innerHTML = rows || '<tr><td colspan="3">(leer)</td></tr>';
    $('msg').textContent = j.entries.length + ' Entry/Entries';
  } catch (e){
    $('msg').textContent = 'Error: ' + e.message;
  }
  crumbs();
}

function go(p){ cur = p || '/'; load(); }

async function post(url){
  const r = await fetch(url, {method:'POST'});
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.err || r.status);
}

async function del(p){
  try {
    // Try this first without prompting — files and flat folders will be deleted immediately.
    const r = await fetch('/api/delete?path=' + enc(p), {method:'POST'});
    const j = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(j.err || r.status);
    // The server requires confirmation only for folders containing subfolders (recursively).
    if (j.confirm){
      if (!confirm('Delete a folder and its subfolders recursively?\n' + p)) return;
      await post('/api/delete?path=' + enc(p) + '&force=1');
    }
  } catch (e){ alert('Error: ' + e.message); }
  load();
}

async function mkdir(){
  const name = prompt('Name of the new folder:');
  if (!name) return;
  const full = (cur === '/' ? '' : cur) + '/' + name;
  try { await post('/api/mkdir?path=' + enc(full)); } catch (e){ alert('Error: ' + e.message); }
  load();
}

async function upload(){
  const files = $('up').files;
  if (!files.length){ alert('Select the file(s) first.'); return; }
  const prog = $('prog');
  prog.hidden = false;
  for (let i = 0; i < files.length; i++){
    $('msg').textContent = 'Upload (' + (i+1) + '/' + files.length + '): ' + files[i].name;
    prog.value = 0;
    // XHR instead of fetch: provides upload progress.
    await new Promise((res, rej) => {
      const fd = new FormData();
      fd.append('file', files[i]);
      const x = new XMLHttpRequest();
      x.open('POST', '/api/upload?path=' + enc(cur));
      x.upload.onprogress = e => { if (e.lengthComputable) prog.value = 100 * e.loaded / e.total; };
      x.onload  = () => {
        if (x.status === 200) return res();
        let why = 'HTTP ' + x.status;
        try { why = JSON.parse(x.responseText).err || why; } catch (_) {}
        rej(new Error(why));
      };
      x.onerror = () => rej(new Error('Network error'));
      x.send(fd);
    }).catch(e => alert('Upload error when ' + files[i].name + ': ' + e.message));
  }
  prog.hidden = true;
  $('up').value = '';
  load();
}

async function ren(p, name) {
  const newName = prompt('Enter new name:', name);
  if (!newName || newName === name) return;
  const slashIdx = p.lastIndexOf('/');
  const parent = slashIdx >= 0 ? p.substring(0, slashIdx) : '';
  const newPath = (parent === '' ? '' : parent) + '/' + newName;
  try {
    const r = await fetch('/api/rename?path=' + enc(p) + '&newpath=' + enc(newPath), {method:'POST'});
    const j = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(j.err || r.status);
  } catch (e) {
    alert('Rename error: ' + e.message);
  }
  load();
}

async function fetchUrl() {
  const urlEl = $('dlurl');
  const urlVal = urlEl.value.trim();
  if (!urlVal) { alert('Please enter a valid URL.'); return; }
  $('msg').textContent = 'Downloading from URL...';
  urlEl.disabled = true;
  try {
    const r = await fetch('/api/fetch?url=' + enc(urlVal) + '&path=' + enc(cur), {method:'POST'});
    const j = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(j.err || r.status);
    urlEl.value = '';
    $('msg').textContent = 'Download completed successfully.';
  } catch (e) {
    $('msg').textContent = 'Download error: ' + e.message;
    alert('Download failed: ' + e.message);
  } finally {
    urlEl.disabled = false;
    load();
  }
}

let otaUrl = '';   // firmware.bin URL from the last check (not required for ‘force’)

async function otaCheck(){
  $('otamsg').textContent = 'Check …';
  $('otabtn').hidden = true;
  try {
    const r = await fetch('/api/ota/check');
    const j = await r.json();
    if (!j.ok) throw new Error(j.err || 'Test failed');
    if (j.update){
      $('otamsg').textContent = 'Update available: ' + j.current + ' → ' + j.latest;
      $('otabtn').hidden = false;
    } else {
      $('otamsg').textContent = 'Latest (' + j.current + ') — no update available.';
    }
  } catch (e){ $('otamsg').textContent = 'Error: ' + e.message; }
}

async function otaUpdate(){
  if (!confirm(''Update the firmware now? The device will restart afterwards and will be unavailable for about 1 minute.')) return;
  $('otabtn').hidden = true;
  $('otamsg').textContent = 'Update in progress — firmware is being loaded and written. Do NOT switch off the device' …';
  try {
    const r = await fetch('/api/ota/update', {method:'POST'});
    const j = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(j.err || ('HTTP ' + r.status));
    $('otamsg').textContent = ''Update written — Device is restarting. Please refresh the page in ~1 min.';
  } catch (e){
    // If successful, the connection will be lost due to the reboot — this is NOT an error.
    $('otamsg').textContent = 'Connection lost (probably a reboot). Please reload in ~1 min. (' + e.message + ')';
  }
}



// Display current version on load (offline — no GitHub call, fast).
fetch('/api/ota/version').then(r => r.json()).then(j => {
  if (j && j.current) $('otamsg').textContent = 'Current version: ' + j.current;
}).catch(() => {});

load();
</script>
</body>
</html>
)HTML";
