#include "image_upload_server.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "app_tasks.h"
#include "comment_screen.h"
#include "comment_storage.h"
#include "todo_screen.h"
#include "todo_storage.h"
#include "image_screen.h"
#include "image_storage.h"

static WebServer server(80);
static File upload_file;
static bool s_server_running = false;
static bool s_routes_registered = false;
#define UPLOAD_NAME_MAX 64

static char upload_name[UPLOAD_NAME_MAX];
static bool upload_ok = false;

static void remove_if_exists(const char *path)
{
    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
    }
}

static bool safe_basename(const char *name)
{
    if (!name || name[0] == '\0' || strlen(name) >= UPLOAD_NAME_MAX) {
        return false;
    }
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
        return false;
    }
    for (const char *p = name; *p; ++p) {
        const char c = *p;
        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') {
            continue;
        }
        return false;
    }
    return true;
}

static bool name_ends_with(const char *name, const char *ext)
{
    const size_t nl = strlen(name);
    const size_t el = strlen(ext);
    if (nl < el) {
        return false;
    }
    return strcasecmp(name + nl - el, ext) == 0;
}

static bool is_allowed_upload_ext(const char *name)
{
    return name_ends_with(name, ".gif") || name_ends_with(name, ".seq") || name_ends_with(name, ".jpg") ||
           name_ends_with(name, ".jpeg") || name_ends_with(name, ".png") || name_ends_with(name, ".bmp") ||
           name_ends_with(name, ".webp");
}

static const char *mime_type_for(const char *name)
{
    if (name_ends_with(name, ".jpg") || name_ends_with(name, ".jpeg")) {
        return "image/jpeg";
    }
    if (name_ends_with(name, ".png")) {
        return "image/png";
    }
    if (name_ends_with(name, ".gif")) {
        return "image/gif";
    }
    if (name_ends_with(name, ".webp")) {
        return "image/webp";
    }
    if (name_ends_with(name, ".bmp")) {
        return "image/bmp";
    }
    if (name_ends_with(name, ".seq")) {
        return "image/jpeg";
    }
    return "application/octet-stream";
}

static void json_escape_append(String &json, const char *s)
{
    for (const char *p = s; p && *p; ++p) {
        const char c = *p;
        switch (c) {
        case '"':
        case '\\':
            json += '\\';
            json += c;
            break;
        case '\n':
            json += "\\n";
            break;
        case '\r':
            json += "\\r";
            break;
        case '\t':
            json += "\\t";
            break;
        default:
            if ((unsigned char)c < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                json += esc;
            } else {
                json += c;
            }
            break;
        }
    }
}

#define SEQ_MAGIC     "MYSEQ1"
#define SEQ_MAGIC2    "MYSEQ2"
#define SEQ_HDR_LEGACY  8
#define SEQ_HDR_EXT     12
#define SEQ_MAX_JPEG  (400 * 1024)

static uint32_t seq_data_offset(const uint8_t *hdr)
{
    if (memcmp(hdr, SEQ_MAGIC2, 6) == 0) {
        return SEQ_HDR_EXT;
    }
    return SEQ_HDR_LEGACY;
}

static uint16_t seq_rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t seq_rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** Stream one JPEG frame from a .seq file (MYSEQ1). frame_index 0 = first frame. */
static bool stream_seq_jpeg_frame(const char *fs_path, uint16_t frame_index)
{
    File f = LittleFS.open(fs_path, "r");
    if (!f) {
        return false;
    }

    uint8_t hdr[SEQ_HDR_EXT];
    if (f.read(hdr, SEQ_HDR_LEGACY) != SEQ_HDR_LEGACY) {
        f.close();
        return false;
    }
    if (memcmp(hdr, SEQ_MAGIC, 6) != 0 && memcmp(hdr, SEQ_MAGIC2, 6) != 0) {
        f.close();
        return false;
    }
    if (memcmp(hdr, SEQ_MAGIC2, 6) == 0) {
        if (f.read(hdr + SEQ_HDR_LEGACY, 4) != 4) {
            f.close();
            return false;
        }
    }

    const uint16_t nframes = seq_rd_u16(hdr + 6);
    if (nframes == 0 || nframes > 2000) {
        f.close();
        return false;
    }

    uint16_t idx = frame_index;
    if (idx >= nframes) {
        idx = (uint16_t)(nframes - 1);
    }

    uint32_t off = seq_data_offset(hdr);
    for (uint16_t i = 0; i < idx; ++i) {
        if (!f.seek(off, SeekSet)) {
            f.close();
            return false;
        }
        uint8_t skip_hdr[6];
        if (f.read(skip_hdr, 6) != 6) {
            f.close();
            return false;
        }
        const uint32_t skip_len = seq_rd_u32(skip_hdr + 2);
        if (skip_len == 0 || skip_len > SEQ_MAX_JPEG) {
            f.close();
            return false;
        }
        off += 6u + skip_len;
    }

    if (!f.seek(off, SeekSet)) {
        f.close();
        return false;
    }

    uint8_t fh[6];
    if (f.read(fh, 6) != 6) {
        f.close();
        return false;
    }

    const uint32_t jlen = seq_rd_u32(fh + 2);
    if (jlen == 0 || jlen > SEQ_MAX_JPEG) {
        f.close();
        return false;
    }

    server.sendHeader("Content-Type", "image/jpeg");
    server.sendHeader("Cache-Control", "public, max-age=300");
    server.setContentLength(jlen);
    server.send(200, "image/jpeg", "");

    WiFiClient client = server.client();
    uint8_t buf[512];
    uint32_t left = jlen;
    while (left > 0 && client.connected()) {
        const size_t chunk = (left > sizeof(buf)) ? sizeof(buf) : (size_t)left;
        const int n = f.read(buf, chunk);
        if (n <= 0) {
            break;
        }
        client.write(buf, (size_t)n);
        left -= (uint32_t)n;
        yield();
    }
    f.close();
    return left == 0;
}

/** Preview thumbnail: middle frame of the sequence. */
static bool stream_seq_middle_jpeg(const char *fs_path)
{
    File f = LittleFS.open(fs_path, "r");
    if (!f) {
        return false;
    }

    uint8_t hdr[SEQ_HDR_EXT];
    if (f.read(hdr, SEQ_HDR_LEGACY) != SEQ_HDR_LEGACY) {
        f.close();
        return false;
    }
    if (memcmp(hdr, SEQ_MAGIC, 6) != 0 && memcmp(hdr, SEQ_MAGIC2, 6) != 0) {
        f.close();
        return false;
    }
    if (memcmp(hdr, SEQ_MAGIC2, 6) == 0) {
        if (f.read(hdr + SEQ_HDR_LEGACY, 4) != 4) {
            f.close();
            return false;
        }
    }

    const uint16_t nframes = seq_rd_u16(hdr + 6);
    f.close();
    if (nframes == 0) {
        return false;
    }

    const uint16_t mid = (uint16_t)((nframes - 1u) / 2u);
    return stream_seq_jpeg_frame(fs_path, mid);
}

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MyScreen</title>
<style>
body{font-family:system-ui,sans-serif;max-width:720px;margin:1.5rem auto;padding:0 1rem;background:#111;color:#eee}
h1{font-size:1.25rem}
h2{font-size:1rem;margin:0 0 .5rem}
.card{background:#1a1a22;border-radius:10px;padding:1rem;margin:1rem 0}
.muted{color:#888;font-size:.9rem}
button,input[type=file],input[type=submit]{font-size:1rem;margin:.25rem 0}
input[type=submit],button{background:#3a6;color:#fff;border:0;padding:.5rem 1rem;border-radius:6px;cursor:pointer}
button.ghost{background:#444}
button.danger{background:#633}
button:disabled{opacity:.45;cursor:not-allowed}
input[type=number]{width:4.5rem;font-size:1rem;padding:.35rem .5rem;border-radius:6px;border:1px solid #444;background:#222;color:#eee}
.gif-opts{margin:.5rem 0;padding:.5rem 0;border-top:1px solid #333}
.gif-opts label{font-size:.9rem}
#msg{margin-top:.5rem;color:#8cf}
.gal-head{display:flex;justify-content:space-between;align-items:flex-start;gap:.75rem;margin-bottom:.5rem}
.gal-head h2{margin:0}
.gal-actions{flex-shrink:0}
.gal-actions button{font-size:.9rem;padding:.45rem .85rem;margin:0}
.delete-bar{display:none;gap:.5rem;flex-wrap:wrap;margin:.5rem 0 .25rem}
.delete-bar button{font-size:.85rem;padding:.35rem .7rem;margin:0}
.gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:.75rem;margin-top:.5rem}
.gitem{position:relative;border:2px solid #333;border-radius:8px;padding:.4rem;cursor:pointer;background:#14141a;transition:border-color .15s,background .15s}
.gitem:hover{border-color:#555}
.gitem.on{border-color:#6c8;background:#1a221c}
.gitem.sel{border-color:#c66;background:#221818}
.gitem .mark{position:absolute;top:6px;right:6px;width:22px;height:22px;border-radius:4px;border:2px solid #888;background:#111;display:none;align-items:center;justify-content:center;font-size:14px;line-height:1;color:#6c8}
.gitem.delmode .mark{display:flex}
.gitem.sel .mark{border-color:#6c8;background:#1a221c}
.gitem img{width:100%;aspect-ratio:5/3;object-fit:contain;background:#000;border-radius:4px;display:block}
.gname{font-size:.75rem;margin-top:.35rem;word-break:break-all;line-height:1.2}
.gmeta{font-size:.7rem;color:#888}
.gbadge{display:inline-block;font-size:.65rem;color:#8cf;background:#223;padding:.1rem .35rem;border-radius:3px;margin-top:.2rem}
.grename{font-size:.7rem;color:#8cf;cursor:pointer;margin-top:.25rem;display:inline-block}
.grename:hover{text-decoration:underline}
#commentText{width:100%;min-height:5.5rem;font-size:1rem;padding:.6rem .75rem;border-radius:8px;border:1px solid #444;background:#222;color:#eee;resize:vertical;box-sizing:border-box;font-family:inherit;line-height:1.4}
#commentMsg{margin-top:.5rem;color:#8cf;min-height:1.2em}
.emoji-bar{display:flex;flex-wrap:wrap;gap:.35rem;margin:.5rem 0}
.emoji-bar button{font-size:1.35rem;padding:.25rem .45rem;line-height:1;min-width:2.2rem}
.emoji-bar button.emoji-heart{color:#ff4060}
.comment-opt{margin:.65rem 0}
.comment-opt label{display:block;font-size:.85rem;color:#aaa;margin-bottom:.25rem}
.comment-opt select,.comment-opt input[type=radio]{margin-right:.5rem}
#festiveOpts{margin-left:.25rem;padding:.5rem;border-left:3px solid #555}
.style-row{display:flex;flex-wrap:wrap;gap:.5rem 1rem}
.todo-row{display:flex;align-items:center;gap:.5rem;margin:.35rem 0;padding:.35rem .5rem;background:#1a1a22;border-radius:8px}
.todo-row.done .todo-text{text-decoration:line-through;color:#888}
.todo-row input[type=checkbox]{width:1.1rem;height:1.1rem;flex-shrink:0}
.todo-text{flex:1;font-size:.95rem;word-break:break-word}
.todo-edit-input{flex:1;font-size:.95rem;padding:.25rem .4rem;border-radius:6px;border:1px solid #555;background:#222;color:#eee;min-width:0}
.todo-actions{display:flex;gap:.35rem;flex-shrink:0}
.todo-del,.todo-edit{font-size:.8rem;padding:.25rem .5rem;margin:0}
#todoAddRow{display:flex;gap:.5rem;margin-top:.5rem}
#todoNew{flex:1;font-size:1rem;padding:.5rem .65rem;border-radius:8px;border:1px solid #444;background:#222;color:#eee}
#todoMsg{margin-top:.5rem;color:#8cf;min-height:1.2em}
</style>
</head>
<body>
<h1>MyScreen</h1>
<p class="muted">Images are resized in your browser (800x480). Animated GIFs become <code>.seq</code> with evenly subsampled JPEG frames; first GIF needs internet for the decoder. Ready-made <code>.seq</code> may use more frames.</p>
<div class="card">
<h2>Message board</h2>
<p class="muted">English, Chinese, or preset emoji. Pick a bubble style for the device <strong>Message</strong> screen.</p>
<div class="emoji-bar" id="emojiBar"></div>
<textarea id="commentText" maxlength="512" placeholder="Write a message..."></textarea>
<div class="comment-opt">
<label><input type="checkbox" id="commentShowTo" checked> Show &quot;To (name)&quot; on device</label>
</div>
<div class="comment-opt">
<label>Text size (all styles)</label>
<select id="commentFontSize">
<option value="20">20 — Latin / numbers</option>
<option value="24" selected>24 — Chinese + emoji</option>
<option value="30">30 — Latin / numbers</option>
<option value="36">36 — Latin / numbers</option>
</select>
</div>
<div class="comment-opt">
<label>Bubble style</label>
<div class="style-row">
<label><input type="radio" name="commentStyle" value="dialogue" checked> Dialogue</label>
<label><input type="radio" name="commentStyle" value="festive"> Festive</label>
<label><input type="radio" name="commentStyle" value="love"> Love letter</label>
<label><input type="radio" name="commentStyle" value="warning"> Warning</label>
</div>
</div>
<div id="festiveOpts" class="comment-opt" style="display:none">
<label>Festive color</label>
<select id="festiveColor">
<option value="0">Gold</option>
<option value="1">Red</option>
<option value="2">Green</option>
<option value="3">Blue</option>
<option value="4">Rainbow</option>
</select>
<label style="margin-top:.5rem">Text motion (festive)</label>
<select id="commentScroll">
<option value="still">Still (wrap)</option>
<option value="marquee">Scrolling</option>
</select>
</div>
<button type="button" id="btnPostComment">Post to Message screen</button>
<p id="commentMsg" class="muted"></p>
</div>
<div class="card">
<h2>To-Do list</h2>
<p class="muted">English tasks only (max 10). Tap Edit to change text. Check off on the screen or here — stays in sync.</p>
<div id="todoList"></div>
<div id="todoAddRow">
<input type="text" id="todoNew" maxlength="128" placeholder="New task...">
<button type="button" id="btnTodoAdd">Add</button>
</div>
<p id="todoMsg" class="muted"></p>
</div>
<div class="card">
<p><strong>Storage:</strong> <span id="free">...</span> free / <span id="total">...</span> total</p>
<form id="uploadForm" method="POST" action="/upload" enctype="multipart/form-data">
<label>Image (.gif .seq .jpg .png .webp .bmp)</label><br>
<input type="file" name="image" id="fileInput" accept=".gif,.seq,.jpg,.jpeg,.png,.webp,.bmp" required><br>
<div class="gif-opts" id="gifOpts" style="display:none">
<label for="gifMaxFrames">GIF to .seq max frames (subsample):</label>
<input type="number" id="gifMaxFrames" min="1" max="200" value="15" step="1" title="Evenly pick up to this many frames from the GIF">
</div>
<input type="submit" id="submitBtn" value="Upload">
</form>
<p id="msg"></p>
</div>
<div class="card">
<div class="gal-head">
<div>
<h2>Display on device</h2>
<p class="muted" id="galHint">Tap a preview to show it on the ESP32 screen. <span id="currentLbl"></span></p>
<div class="delete-bar" id="deleteBar">
<button type="button" id="btnSelectAll" class="ghost">Select all</button>
<button type="button" id="btnDeleteSelected" class="danger" disabled>Delete selected</button>
</div>
</div>
<div class="gal-actions">
<button type="button" id="btnTopRight" class="danger">Delete</button>
</div>
</div>
<div class="gallery" id="gallery"></div>
</div>
<script>
var deleteMode=false;
var selected=new Set();
var lastFiles=[];
var lastCurrent='';
function extBadge(name){
 if(/\.seq$/i.test(name))return 'SEQ';
 if(/\.gif$/i.test(name))return 'GIF';
 return '';
}
function allFilesSelected(){
 return lastFiles.length>0&&lastFiles.every(function(f){return selected.has(f.name);});
}
function updateDeleteUi(){
 var btn=document.getElementById('btnTopRight');
 var bar=document.getElementById('deleteBar');
 var hint=document.getElementById('galHint');
 var delBtn=document.getElementById('btnDeleteSelected');
 var selBtn=document.getElementById('btnSelectAll');
 if(deleteMode){
  btn.textContent='Cancel';
  btn.className='ghost';
  bar.style.display='flex';
  hint.style.display='none';
  var n=selected.size;
  delBtn.disabled=(n===0);
  delBtn.textContent=n?'Delete selected ('+n+')':'Delete selected';
  selBtn.textContent=allFilesSelected()?'Deselect all':'Select all';
 }else{
  btn.textContent='Delete';
  btn.className='danger';
  bar.style.display='none';
  hint.style.display='';
  delBtn.disabled=true;
  delBtn.textContent='Delete selected';
 }
}
function setDeleteMode(on){
 deleteMode=!!on;
 if(!deleteMode){selected.clear();}
 updateDeleteUi();
 renderGallery();
}
function toggleSelected(name){
 if(selected.has(name)){selected.delete(name);}else{selected.add(name);}
 updateDeleteUi();
 renderGallery();
}
document.getElementById('btnTopRight').onclick=function(){
 if(deleteMode){setDeleteMode(false);}else{setDeleteMode(true);}
};
document.getElementById('btnSelectAll').onclick=function(){
 if(allFilesSelected()){selected.clear();}
 else{lastFiles.forEach(function(f){selected.add(f.name);});}
 updateDeleteUi();
 renderGallery();
};
async function renameFile(oldName){
 if(deleteMode)return;
 var dot=oldName.lastIndexOf('.');
 var ext=(dot>0)?oldName.slice(dot):'';
 var stem=(dot>0)?oldName.slice(0,dot):oldName;
 var neu=prompt('New filename ('+ext+' will be kept):',stem);
 if(neu==null)return;
 neu=neu.trim();
 if(!neu){document.getElementById('msg').textContent='Name cannot be empty';return;}
 var to=neu+ext;
 if(to===oldName)return;
 var r=await fetch('/api/rename',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({from:oldName,to:to})});
 if(!r.ok){document.getElementById('msg').textContent='Rename failed';return;}
 if(lastCurrent===oldName){lastCurrent=to;}
 document.getElementById('msg').textContent='Renamed to '+to;
 refresh();
};
document.getElementById('btnDeleteSelected').onclick=async function(){
 if(!selected.size)return;
 if(!confirm('Delete '+selected.size+' file(s) from the device?'))return;
 var names=Array.from(selected);
 var r=await fetch('/api/delete_bulk',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({names:names})});
 if(!r.ok){document.getElementById('msg').textContent='Delete failed';return;}
 var j=await r.json();
 document.getElementById('msg').textContent='Deleted '+j.deleted+' file(s)';
 setDeleteMode(false);
 refresh();
};
async function selectImage(name){
 if(deleteMode){toggleSelected(name);return;}
 const r=await fetch('/api/select?name='+encodeURIComponent(name),{method:'POST'});
 if(!r.ok){document.getElementById('msg').textContent='Could not select '+name;return;}
 lastCurrent=name;
 document.getElementById('msg').textContent='Showing on device: '+name;
 document.getElementById('currentLbl').textContent='Now showing: '+name;
 renderGallery();
}
function renderGallery(){
 var gal=document.getElementById('gallery');
 gal.innerHTML='';
 var cur=lastCurrent;
 lastFiles.forEach(function(f){
  var card=document.createElement('div');
  var cls='gitem';
  if(deleteMode){cls+=' delmode';if(selected.has(f.name))cls+=' sel';}
  else if(f.name===cur){cls+=' on';}
  card.className=cls;
  var mark=document.createElement('div');
  mark.className='mark';
  mark.textContent=selected.has(f.name)?'\u2713':'';
  card.appendChild(mark);
  var img=document.createElement('img');
  var purl='/api/preview?name='+encodeURIComponent(f.name)+'&t='+(f.kb||0);
  img.alt=f.name;
  img.loading='lazy';
  img.onerror=function(){this.style.opacity='0.25';};
  img.src='data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7';
  card.appendChild(img);
  var nm=document.createElement('div');nm.className='gname';nm.textContent=f.name;
  card.appendChild(nm);
  var meta=document.createElement('div');meta.className='gmeta';meta.textContent=f.kb+' KB';
  card.appendChild(meta);
  var badge=extBadge(f.name);
  if(badge){var bd=document.createElement('span');bd.className='gbadge';bd.textContent=badge;card.appendChild(bd);}
  if(!deleteMode){
   var ren=document.createElement('span');
   ren.className='grename';
   ren.textContent='Rename';
   ren.onclick=function(ev){ev.stopPropagation();renameFile(f.name);};
   card.appendChild(ren);
  }
  card.onclick=function(){selectImage(f.name);};
  gal.appendChild(card);
  (function(im,u,delay){setTimeout(function(){im.src=u;},delay);})(img,purl,220*gal.childElementCount);
 });
}
async function refresh(){
  const r=await fetch('/api/storage');const j=await r.json();
  document.getElementById('free').textContent=(j.free/1024).toFixed(0)+' KB';
  document.getElementById('total').textContent=(j.total/1024).toFixed(0)+' KB';
  lastFiles=j.files||[];
  lastCurrent=j.currentName||'';
  if(deleteMode){
   var still=new Set(lastFiles.map(function(f){return f.name;}));
   selected.forEach(function(n){if(!still.has(n))selected.delete(n);});
  }
  document.getElementById('currentLbl').textContent=lastCurrent?('Now showing: '+lastCurrent):'(none selected)';
  updateDeleteUi();
  renderGallery();
}
const MAX_W=800,MAX_H=480,QUALITY=0.84;
const SEQ_JPEG_QUALITY=0.6;
const SEQ_MAX_JPEG=400*1024;
const GIF_MAX_FRAMES_DEFAULT=15;
const GIF_MAX_FRAMES_MIN=1;
const GIF_MAX_FRAMES_MAX=200;
const GIF_MAX_FRAMES_LS='myscreen_gif_max_frames';
var gifLibs=null;
function isGifFile(file){
 if(!file)return false;
 return file.type==='image/gif'||/\.gif$/i.test(file.name);
}
function readGifMaxFrames(){
 var el=document.getElementById('gifMaxFrames');
 var n=el?parseInt(el.value,10):GIF_MAX_FRAMES_DEFAULT;
 if(!Number.isFinite(n))n=GIF_MAX_FRAMES_DEFAULT;
 if(n<GIF_MAX_FRAMES_MIN)n=GIF_MAX_FRAMES_MIN;
 if(n>GIF_MAX_FRAMES_MAX)n=GIF_MAX_FRAMES_MAX;
 if(el)el.value=String(n);
 try{localStorage.setItem(GIF_MAX_FRAMES_LS,String(n));}catch(e){}
 return n;
}
function updateGifOptsVisibility(){
 var file=document.getElementById('fileInput').files[0];
 var show=isGifFile(file);
 document.getElementById('gifOpts').style.display=show?'block':'none';
}
function initGifMaxFramesInput(){
 var el=document.getElementById('gifMaxFrames');
 if(!el)return;
 try{
  var saved=localStorage.getItem(GIF_MAX_FRAMES_LS);
  if(saved){var n=parseInt(saved,10);if(n>=GIF_MAX_FRAMES_MIN&&n<=GIF_MAX_FRAMES_MAX)el.value=String(n);}
 }catch(e){}
 el.addEventListener('change',readGifMaxFrames);
 el.addEventListener('input',readGifMaxFrames);
 document.getElementById('fileInput').addEventListener('change',updateGifOptsVisibility);
}
function ensureGifLibs(){
 if(gifLibs)return gifLibs;
 gifLibs=import('https://esm.sh/omggif@1.0.10').then(function(m){
  var GR=m.GifReader||(m.default&&m.default.GifReader);
  if(!GR&&typeof m.default==='function')GR=m.default;
  if(typeof GR!=='function')throw new Error('GifReader');
  window.GifReader=GR;
 });
 return gifLibs;
}
function canvasToJpegBlob(canvas,q){
 return new Promise(function(res){
  canvas.toBlob(function(b){res(b||null);},'image/jpeg',q);
 });
}
function buildSeqBlob(frames,contentW,contentH){
 /* MYSEQ2 + u16 count + u16 content_w + u16 content_h + per frame u16 dly, u32 len, jpeg */
 var tot=12;
 for(var i=0;i<frames.length;i++)tot+=6+frames[i].u8.length;
 var out=new Uint8Array(tot);
 var p=0;
 var magic=new TextEncoder().encode('MYSEQ2');
 out.set(magic,p);p+=6;
 var dv=new DataView(out.buffer);
 dv.setUint16(p,frames.length,true);p+=2;
 dv.setUint16(p,contentW||0,true);p+=2;
 dv.setUint16(p,contentH||0,true);p+=2;
 for(var j=0;j<frames.length;j++){
  new DataView(out.buffer).setUint16(p,frames[j].dly,true);p+=2;
  new DataView(out.buffer).setUint32(p,frames[j].u8.length,true);p+=4;
  out.set(frames[j].u8,p);p+=frames[j].u8.length;
 }
 return new Blob([out],{type:'application/octet-stream'});
}
/** Source frame indices to encode (evenly spaced; at most cap). */
function gifSubsampleFrameIndices(nfull,cap){
 if(nfull<=0){return [];}
 if(cap<=1){return [0];}
 if(nfull<=cap){
  var a=[];
  for(var i=0;i<nfull;i++){a.push(i);}
  return a;
 }
 var a=[];
 var cap1=cap-1;
 for(var k=0;k<cap;k++){
  var idx=Math.round(k*(nfull-1)/cap1);
  if(a.length&&idx<=a[a.length-1]){idx=a[a.length-1]+1;}
  if(idx>=nfull){idx=nfull-1;}
  a.push(idx);
 }
 return a;
}
function gifToSeqBlob(file,maxFrames){
 var cap=Math.max(GIF_MAX_FRAMES_MIN,Math.min(GIF_MAX_FRAMES_MAX,maxFrames||GIF_MAX_FRAMES_DEFAULT));
 return ensureGifLibs().then(function(){
  return new Promise(function(resolve){
   var r=new FileReader();
   r.onload=function(){
    (async function(){
     try{
      var u8=new Uint8Array(r.result);
      var GR=window.GifReader;
      if(!GR){resolve(file);return;}
      var gr=new GR(u8);
      var w0=gr.width,h0=gr.height;
      if(!w0||!h0){resolve(file);return;}
      var s=Math.min(MAX_W/w0,MAX_H/h0,1);
      var w=Math.max(1,Math.round(w0*s)),h=Math.max(1,Math.round(h0*s));
      var full=document.createElement('canvas');
      full.width=w0;full.height=h0;
      var fcx=full.getContext('2d');
      var out=document.createElement('canvas');
      out.width=w;out.height=h;
      var ocx=out.getContext('2d');
      var rgba=new Uint8Array(w0*h0*4);
      for(var p=0;p<rgba.length;p+=4){
       rgba[p]=255;rgba[p+1]=255;rgba[p+2]=255;rgba[p+3]=255;
      }
      var nfull=gr.numFrames();
      var idxs=gifSubsampleFrameIndices(nfull,cap);
      var want={};
      for(var t=0;t<idxs.length;t++){want[idxs[t]]=true;}
      var list=[];
      for(var f=0;f<nfull;f++){
       gr.decodeAndBlitFrameRGBA(f,rgba);
       if(!want[f]){continue;}
       var id=fcx.createImageData(w0,h0);
       id.data.set(rgba);
       fcx.putImageData(id,0,0);
       ocx.clearRect(0,0,w,h);
       ocx.fillStyle='#fff';
       ocx.fillRect(0,0,w,h);
       ocx.drawImage(full,0,0,w0,h0,0,0,w,h);
       var jb=await canvasToJpegBlob(out,SEQ_JPEG_QUALITY);
       if(!jb){resolve(file);return;}
       var ju8=new Uint8Array(await jb.arrayBuffer());
       if(ju8.length>SEQ_MAX_JPEG){resolve(file);return;}
       var fi=gr.frameInfo(f);
       var cs=(fi.delay!=null)?fi.delay:6;
       var dly=Math.max(5,Math.min(65535,Math.round(cs*10)));
       list.push({dly:dly,u8:ju8});
      }
      resolve(buildSeqBlob(list,w,h));
     }catch(e){resolve(file);}
    })();
   };
   r.onerror=function(){resolve(file);};
   r.readAsArrayBuffer(file);
  });
 }).catch(function(){return file;});
}
function shrinkBlob(file){
 if(/\.seq$/i.test(file.name))return Promise.resolve(file);
 if(isGifFile(file))return gifToSeqBlob(file,readGifMaxFrames());
 return new Promise(function(resolve){
  var url=URL.createObjectURL(file);
  var img=new Image();
  img.onload=function(){
   URL.revokeObjectURL(url);
   var w=img.naturalWidth,h=img.naturalHeight;
   if (!w||!h){ resolve(file); return; }
   var s=Math.min(MAX_W/w,MAX_H/h,1);
   if (s>=1 && file.type==='image/jpeg' && file.size<180000){ resolve(file); return; }
   w=Math.max(1,Math.round(w*s)); h=Math.max(1,Math.round(h*s));
   var c=document.createElement('canvas');
   c.width=w; c.height=h;
   var cx=c.getContext('2d');
   cx.drawImage(img,0,0,w,h);
   c.toBlob(function(b){ resolve(b||file); },'image/jpeg',QUALITY);
  };
  img.onerror=function(){ URL.revokeObjectURL(url); resolve(file); };
  img.src=url;
 });
}
document.getElementById('uploadForm').addEventListener('submit',function(ev){
 ev.preventDefault();
 var input=document.getElementById('fileInput');
 var btn=document.getElementById('submitBtn');
 var file=input.files[0];
 if(!file) return;
 btn.disabled=true;
 var gif=isGifFile(file);
 document.getElementById('msg').textContent=gif?('Converting GIF to .seq ('+readGifMaxFrames()+' frames max)...'):'Optimizing...';
 shrinkBlob(file).then(function(blob){
  var name=file.name;
  if(blob!==file){
   var ext='.jpg';
   if(file.type==='image/gif'||/\.gif$/i.test(file.name))ext='.seq';
   else if(/\.seq$/i.test(file.name))ext='.seq';
   name=name.replace(/\.[^.]+$/i,'')+ext;
  }
  var fd=new FormData();
  fd.append('image',blob,name);
  return fetch('/upload',{method:'POST',body:fd,redirect:'follow'});
 }).then(function(r){
  window.location.href=r.url;
 }).catch(function(e){
  document.getElementById('msg').textContent='Upload error: '+e;
  btn.disabled=false;
 });
});
var COMMENT_EMOJIS=['\u2728','\u2764','\uD83D\uDE01','\uD83D\uDE0E','\uD83D\uDC4D','\uD83D\uDCAA'];
(function(){
 var bar=document.getElementById('emojiBar');
 COMMENT_EMOJIS.forEach(function(em){
  var b=document.createElement('button');
  b.type='button';b.textContent=em;b.title='Insert emoji';
  if(em==='\u2764')b.className='emoji-heart';
  b.onclick=function(){
   var ta=document.getElementById('commentText');
   var s=ta.selectionStart,e=ta.selectionEnd;
   ta.value=ta.value.slice(0,s)+em+ta.value.slice(e);
   ta.focus();ta.selectionStart=ta.selectionEnd=s+em.length;
  };
  bar.appendChild(b);
 });
})();
function updateFestiveOpts(){
 var fest=document.querySelector('input[name="commentStyle"][value="festive"]').checked;
 document.getElementById('festiveOpts').style.display=fest?'block':'none';
}
document.querySelectorAll('input[name="commentStyle"]').forEach(function(el){
 el.onchange=updateFestiveOpts;
});
function commentPayload(){
 var style='dialogue';
 document.querySelectorAll('input[name="commentStyle"]').forEach(function(el){
  if(el.checked)style=el.value;
 });
 return {
  text:document.getElementById('commentText').value.trim(),
  style:style,
  font_size:parseInt(document.getElementById('commentFontSize').value,10)||24,
  show_to:document.getElementById('commentShowTo').checked,
  scroll:document.getElementById('commentScroll').value,
  festive_color:parseInt(document.getElementById('festiveColor').value,10)||0
 };
}
async function loadComment(){
 const r=await fetch('/api/comment');
 if(!r.ok)return;
 const j=await r.json();
 if(j.text!=null)document.getElementById('commentText').value=j.text;
 if(j.style){
  document.querySelectorAll('input[name="commentStyle"]').forEach(function(el){
   el.checked=(el.value===j.style);
  });
 }
 if(j.font_size!=null)document.getElementById('commentFontSize').value=String(j.font_size);
 else if(j.font){
  var m={sans20:20,cjk:24,sans30:30,sans36:36};
  if(m[j.font]!=null)document.getElementById('commentFontSize').value=String(m[j.font]);
 }
 if(j.show_to!=null)document.getElementById('commentShowTo').checked=!!j.show_to;
 if(j.scroll)document.getElementById('commentScroll').value=j.scroll;
 if(j.festive_color!=null)document.getElementById('festiveColor').value=String(j.festive_color);
 updateFestiveOpts();
 document.getElementById('commentMsg').textContent=j.text?'Saved on device.':'';
}
document.getElementById('btnPostComment').onclick=async function(){
 var p=commentPayload();
 var msg=document.getElementById('commentMsg');
 if(!p.text){msg.textContent='Enter a message first';return;}
 msg.textContent='Posting...';
 var r=await fetch('/api/comment',{method:'POST',headers:{'Content-Type':'application/json; charset=utf-8'},
  body:JSON.stringify(p)});
 if(!r.ok){msg.textContent='Post failed';return;}
 msg.textContent='Posted — showing on Message screen';
};
var todoTasks=[];
function todoId(){
 var s='t';
 for(var i=0;i<7;i++){s+=Math.floor(Math.random()*36).toString(36);}
 return s;
}
function beginTodoEdit(row,t,span,editBtn){
 if(row.querySelector('.todo-edit-input'))return;
 var inp=document.createElement('input');
 inp.type='text';inp.className='todo-edit-input';inp.value=t.text;inp.maxLength=128;
 span.replaceWith(inp);editBtn.disabled=true;inp.focus();inp.select();
 function finish(){
  var v=inp.value.trim();
  if(v)t.text=v;
  if(inp.parentNode){inp.replaceWith(span);span.textContent=t.text;}
  editBtn.disabled=false;
  saveTodos();
 }
 inp.onblur=finish;
 inp.onkeydown=function(ev){
  if(ev.key==='Enter'){ev.preventDefault();inp.blur();}
  if(ev.key==='Escape'){inp.value=t.text;inp.blur();}
 };
}
function renderTodos(){
 var box=document.getElementById('todoList');
 box.innerHTML='';
 todoTasks.forEach(function(t){
  var row=document.createElement('div');
  row.className='todo-row'+(t.done?' done':'');
  var cb=document.createElement('input');
  cb.type='checkbox';cb.checked=!!t.done;
  cb.onchange=function(){t.done=cb.checked;row.classList.toggle('done',t.done);saveTodos();};
  var span=document.createElement('span');
  span.className='todo-text';span.textContent=t.text;
  span.title='Double-click to edit';
  var actions=document.createElement('div');
  actions.className='todo-actions';
  var editBtn=document.createElement('button');
  editBtn.type='button';editBtn.className='todo-edit';editBtn.textContent='Edit';
  editBtn.onclick=function(){beginTodoEdit(row,t,span,editBtn);};
  span.ondblclick=function(){beginTodoEdit(row,t,span,editBtn);};
  var del=document.createElement('button');
  del.type='button';del.className='todo-del';del.textContent='Delete';
  del.onclick=function(){todoTasks=todoTasks.filter(function(x){return x.id!==t.id;});saveTodos();};
  actions.appendChild(editBtn);actions.appendChild(del);
  row.appendChild(cb);row.appendChild(span);row.appendChild(actions);
  box.appendChild(row);
 });
}
async function loadTodos(){
 var r=await fetch('/api/todos');
 if(!r.ok)return;
 var j=await r.json();
 todoTasks=Array.isArray(j.tasks)?j.tasks:[];
 renderTodos();
 document.getElementById('todoMsg').textContent=todoTasks.length?'Synced with device.':'';
}
async function saveTodos(){
 var msg=document.getElementById('todoMsg');
 msg.textContent='Saving...';
 var r=await fetch('/api/todos',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({tasks:todoTasks})});
 if(!r.ok){
  var err={};
  try{err=await r.json();}catch(e){}
  msg.textContent=err.error||'Save failed';
  return;
 }
 msg.textContent='Saved on device';
 renderTodos();
}
document.getElementById('btnTodoAdd').onclick=function(){
 var inp=document.getElementById('todoNew');
 var text=(inp.value||'').trim();
 if(!text){document.getElementById('todoMsg').textContent='Enter a task';return;}
 if(todoTasks.length>=10){document.getElementById('todoMsg').textContent='Max 10 tasks';return;}
 todoTasks.push({id:todoId(),text:text,done:false});
 inp.value='';
 saveTodos();
};
initGifMaxFramesInput();
loadComment();
loadTodos();
refresh();
const q=new URLSearchParams(location.search);
if(q.get('ok'))document.getElementById('msg').textContent='Upload OK: '+q.get('ok');
if(q.get('err'))document.getElementById('msg').textContent='Error: '+q.get('err');
</script>
</body>
</html>
)rawliteral";

static void send_redirect(const char *query)
{
    char loc[128];
    snprintf(loc, sizeof(loc), "/?%s", query);
    server.sendHeader("Location", loc);
    server.send(303, "text/plain", "");
}

static void handle_ping(void)
{
    server.send(200, "text/plain", "ok");
}

static void handle_root(void)
{
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_storage_api(void)
{
    String json = "{\"total\":";
    json += image_storage_total_bytes();
    json += ",\"free\":";
    json += image_storage_free_bytes();
    json += ",\"current\":";
    json += image_storage_current_index();
    json += ",\"currentName\":\"";
    char cur_name[64];
    if (image_storage_get_current_basename(cur_name, sizeof(cur_name))) {
        json_escape_append(json, cur_name);
    }
    json += "\",\"files\":[";
    bool first = true;
    const int n = image_storage_count();
    for (int i = 0; i < n; ++i) {
        char base[64];
        if (!image_storage_basename_at(i, base, sizeof(base))) {
            continue;
        }
        char path[96];
        snprintf(path, sizeof(path), IMAGE_FOLDER "/%s", base);
        File entry = LittleFS.open(path, "r");
        if (!entry) {
            continue;
        }
        const size_t kb = entry.size() / 1024;
        entry.close();
        if (!first) {
            json += ',';
        }
        first = false;
        json += "{\"name\":\"";
        json_escape_append(json, base);
        json += "\",\"kb\":";
        json += kb;
        json += '}';
    }
    json += "]}";
    server.send(200, "application/json", json);
}

static void handle_select_api(void)
{
    if (!server.hasArg("name")) {
        server.send(400, "application/json", "{\"error\":\"missing name\"}");
        return;
    }
    const String name = server.arg("name");
    if (!safe_basename(name.c_str()) || !is_allowed_upload_ext(name.c_str())) {
        server.send(400, "application/json", "{\"error\":\"invalid name\"}");
        return;
    }
    if (!image_storage_set_current_by_name(name.c_str())) {
        server.send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }
    image_screen_request_show_current();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_preview_api(void)
{
    if (!server.hasArg("name")) {
        server.send(400, "text/plain", "missing name");
        return;
    }
    const String name = server.arg("name");
    if (!safe_basename(name.c_str()) || !is_allowed_upload_ext(name.c_str())) {
        server.send(400, "text/plain", "invalid name");
        return;
    }

    char path[96];
    snprintf(path, sizeof(path), IMAGE_FOLDER "/%s", name.c_str());
    if (!LittleFS.exists(path)) {
        server.send(404, "text/plain", "not found");
        return;
    }

    if (name_ends_with(name.c_str(), ".seq")) {
        image_screen_close_file_if_displayed(name.c_str());
        if (!stream_seq_middle_jpeg(path)) {
            server.send(500, "text/plain", "seq preview failed");
        }
        return;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        server.send(500, "text/plain", "open failed");
        return;
    }
    server.sendHeader("Cache-Control", "public, max-age=300");
    server.streamFile(file, mime_type_for(name.c_str()));
}

static void handle_delete_api(void)
{
    if (!server.hasArg("name")) {
        server.send(400, "application/json", "{\"error\":\"missing name\"}");
        return;
    }
    const String name = server.arg("name");
    if (!safe_basename(name.c_str()) || !is_allowed_upload_ext(name.c_str())) {
        server.send(400, "application/json", "{\"error\":\"invalid name\"}");
        return;
    }
    if (!image_storage_delete_file(name.c_str())) {
        server.send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }
    image_storage_rescan();
    image_screen_request_show_current();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_rename_api(void)
{
    const String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"empty body\"}");
        return;
    }

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    const char *from = doc["from"].as<const char *>();
    const char *to = doc["to"].as<const char *>();
    if (!from || !to) {
        server.send(400, "application/json", "{\"error\":\"missing from or to\"}");
        return;
    }
    if (!safe_basename(from) || !is_allowed_upload_ext(from) || !safe_basename(to) ||
        !is_allowed_upload_ext(to)) {
        server.send(400, "application/json", "{\"error\":\"invalid name\"}");
        return;
    }

    char cur[64];
    const bool was_current =
        image_storage_get_current_basename(cur, sizeof(cur)) && strcasecmp(cur, from) == 0;

    if (!image_storage_rename_file(from, to)) {
        server.send(409, "application/json", "{\"error\":\"rename failed\"}");
        return;
    }

    image_storage_rescan();
    if (was_current) {
        image_storage_set_current_by_name(to);
    }
    image_screen_request_show_current();

    String resp = "{\"ok\":true,\"name\":\"";
    json_escape_append(resp, to);
    resp += "\"}";
    server.send(200, "application/json", resp);
}

static void append_comment_meta_json(String &json, const comment_display_t *d)
{
    json += ",\"style\":\"";
    switch (d->style) {
    case COMMENT_STYLE_FESTIVE:
        json += "festive";
        break;
    case COMMENT_STYLE_LOVE:
        json += "love";
        break;
    case COMMENT_STYLE_WARNING:
        json += "warning";
        break;
    default:
        json += "dialogue";
        break;
    }
    json += "\",\"font_size\":";
    switch (d->font_size) {
    case COMMENT_FONT_SIZE_20:
        json += 20;
        break;
    case COMMENT_FONT_SIZE_30:
        json += 30;
        break;
    case COMMENT_FONT_SIZE_36:
        json += 36;
        break;
    default:
        json += 24;
        break;
    }
    json += ",\"show_to\":";
    json += d->show_to ? "true" : "false";
    json += ",\"scroll\":\"";
    json += d->scroll == COMMENT_SCROLL_MARQUEE ? "marquee" : "still";
    json += "\",\"festive_color\":";
    json += (int)d->festive_color;
}

static void handle_comment_get_api(void)
{
    char buf[COMMENT_STORAGE_MAX_BYTES + 1];
    time_t received_at = 0;
    comment_display_t display;
    comment_display_defaults(&display);
    comment_storage_load_display(&display);

    String json = "{\"text\":\"";
    if (comment_storage_load_ex(buf, sizeof(buf), &received_at)) {
        json_escape_append(json, buf);
    }
    json += "\",\"received\":";
    json += (long)received_at;
    append_comment_meta_json(json, &display);
    json += '}';
    server.send(200, "application/json", json);
}

static void handle_comment_post_api(void)
{
    if (server.method() != HTTP_POST) {
        server.send(405, "application/json", "{\"error\":\"POST required\"}");
        return;
    }

    const String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"empty body\"}");
        return;
    }

    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    const char *text = doc["text"] | "";
    if (text[0] == '\0') {
        server.send(400, "application/json", "{\"error\":\"empty message\"}");
        return;
    }

    comment_display_t display;
    comment_display_defaults(&display);
    const char *style_s = doc["style"] | "dialogue";
    if (strcmp(style_s, "festive") == 0) {
        display.style = COMMENT_STYLE_FESTIVE;
    } else if (strcmp(style_s, "love") == 0) {
        display.style = COMMENT_STYLE_LOVE;
    } else if (strcmp(style_s, "warning") == 0) {
        display.style = COMMENT_STYLE_WARNING;
    }
    if (doc["font_size"].is<int>()) {
        const int px = doc["font_size"];
        if (px <= 20) {
            display.font_size = COMMENT_FONT_SIZE_20;
        } else if (px >= 36) {
            display.font_size = COMMENT_FONT_SIZE_36;
        } else if (px >= 30) {
            display.font_size = COMMENT_FONT_SIZE_30;
        } else {
            display.font_size = COMMENT_FONT_SIZE_24;
        }
    } else {
        const char *font_s = doc["font"] | "cjk";
        if (strcmp(font_s, "sans20") == 0) {
            display.font_size = COMMENT_FONT_SIZE_20;
        } else if (strcmp(font_s, "sans30") == 0) {
            display.font_size = COMMENT_FONT_SIZE_30;
        } else if (strcmp(font_s, "sans36") == 0) {
            display.font_size = COMMENT_FONT_SIZE_36;
        } else {
            display.font_size = COMMENT_FONT_SIZE_24;
        }
    }
    display.show_to = doc["show_to"] | true;
    const char *scroll_s = doc["scroll"] | "still";
    if (strcmp(scroll_s, "marquee") == 0) {
        display.scroll = COMMENT_SCROLL_MARQUEE;
    }
    int fc = doc["festive_color"] | 0;
    if (fc >= 0 && fc <= COMMENT_FESTIVE_RAINBOW) {
        display.festive_color = (comment_festive_color_t)fc;
    }

    if (!comment_storage_save_all(text, &display)) {
        server.send(400, "application/json", "{\"error\":\"invalid or too long message\"}");
        return;
    }

    comment_screen_request_show(NULL);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_todos_get_api(void)
{
    String json;
    if (!todo_storage_export_json(json)) {
        server.send(500, "application/json", "{\"error\":\"export failed\"}");
        return;
    }
    server.send(200, "application/json", json);
}

static void handle_todos_post_api(void)
{
    if (server.method() != HTTP_POST) {
        server.send(405, "application/json", "{\"error\":\"POST required\"}");
        return;
    }

    const String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"empty body\"}");
        return;
    }

    if (!todo_storage_import_json(body.c_str(), body.length())) {
        server.send(400, "application/json", "{\"error\":\"invalid tasks (ascii text, max 10)\"}");
        return;
    }

    todo_screen_request_refresh();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_delete_bulk_api(void)
{
    if (server.method() != HTTP_POST) {
        server.send(405, "application/json", "{\"error\":\"POST required\"}");
        return;
    }

    const String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"empty body\"}");
        return;
    }

    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok || !doc["names"].is<JsonArray>()) {
        server.send(400, "application/json", "{\"error\":\"expected {\\\"names\\\":[...]}\"}");
        return;
    }

    int deleted = 0;
    int failed = 0;
    for (JsonVariant v : doc["names"].as<JsonArray>()) {
        const char *name = v.as<const char *>();
        if (!name || !safe_basename(name) || !is_allowed_upload_ext(name)) {
            failed++;
            continue;
        }
        if (image_storage_delete_file(name)) {
            deleted++;
        } else {
            failed++;
        }
    }

    image_storage_rescan();
    image_screen_request_show_current();

    String resp = "{\"ok\":true,\"deleted\":";
    resp += deleted;
    resp += ",\"failed\":";
    resp += failed;
    resp += '}';
    server.send(200, "application/json", resp);
}

static void handle_upload_finish(void)
{
    if (upload_ok) {
        char q[96];
        snprintf(q, sizeof(q), "ok=%s", upload_name);
        send_redirect(q);
    } else {
        send_redirect("err=upload_failed");
    }
    upload_name[0] = '\0';
    upload_ok = false;
}

static void handle_upload_body(void)
{
    HTTPUpload &up = server.upload();

    if (up.status == UPLOAD_FILE_START) {
        upload_ok = false;
        upload_name[0] = '\0';
        if (upload_file) {
            upload_file.close();
        }

        String name = up.filename;
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) {
            name = name.substring(slash + 1);
        }
        const int backslash = name.lastIndexOf('\\');
        if (backslash >= 0) {
            name = name.substring(backslash + 1);
        }
        name.toCharArray(upload_name, sizeof(upload_name));

        if (!safe_basename(upload_name) || !is_allowed_upload_ext(upload_name)) {
            Serial.printf("upload: rejected name %s\n", upload_name);
            return;
        }

        char path[80];
        snprintf(path, sizeof(path), IMAGE_FOLDER "/%s", upload_name);
        remove_if_exists(path);
        upload_file = LittleFS.open(path, "w");
        if (!upload_file) {
            Serial.printf("upload: cannot create %s\n", path);
            return;
        }
        Serial.printf("upload: start %s\n", path);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (upload_file) {
            if (image_storage_free_bytes() < up.currentSize + 1024) {
                Serial.println("upload: filesystem full");
                upload_file.close();
                char path[80];
                snprintf(path, sizeof(path), IMAGE_FOLDER "/%s", upload_name);
                remove_if_exists(path);
                upload_file = File();
                return;
            }
            upload_file.write(up.buf, up.currentSize);
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (upload_file) {
            upload_file.close();
            upload_ok = true;
            Serial.printf("upload: done %s (%u bytes)\n", upload_name, (unsigned)up.totalSize);
            image_storage_rescan();
            if (image_storage_set_current_by_name(upload_name)) {
                image_screen_request_show_current();
            } else {
                image_screen_request_refresh();
            }
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (upload_file) {
            upload_file.close();
            char path[80];
            snprintf(path, sizeof(path), IMAGE_FOLDER "/%s", upload_name);
            remove_if_exists(path);
        }
    }
}

static void handle_not_found(void)
{
    const String &uri = server.uri();
    if (uri == "/favicon.ico") {
        server.send(204);
        return;
    }
    server.send(404, "text/plain", "Not found");
}

static void register_routes(void)
{
    if (s_routes_registered) {
        return;
    }
    s_routes_registered = true;

    server.on("/", HTTP_GET, handle_root);
    server.on("/ping", HTTP_GET, handle_ping);
    server.on("/api/storage", HTTP_GET, handle_storage_api);
    server.on("/api/preview", HTTP_GET, handle_preview_api);
    server.on("/api/select", HTTP_POST, handle_select_api);
    server.on("/api/delete", HTTP_POST, handle_delete_api);
    server.on("/api/delete_bulk", HTTP_POST, handle_delete_bulk_api);
    server.on("/api/rename", HTTP_POST, handle_rename_api);
    server.on("/api/comment", HTTP_GET, handle_comment_get_api);
    server.on("/api/comment", HTTP_POST, handle_comment_post_api);
    server.on("/api/todos", HTTP_GET, handle_todos_get_api);
    server.on("/api/todos", HTTP_POST, handle_todos_post_api);
    server.on("/upload", HTTP_POST, handle_upload_finish, handle_upload_body);
    server.onNotFound(handle_not_found);
}

void image_upload_server_start(void)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("upload server: Wi-Fi not connected");
        s_server_running = false;
        return;
    }

    WiFi.setSleep(WIFI_PS_NONE);
    register_routes();

    if (!s_server_running) {
        server.begin();
        s_server_running = true;
    }
    Serial.print("Image upload UI: http://");
    Serial.println(WiFi.localIP());
}

void image_upload_server_restart(void)
{
    if (s_server_running) {
        server.stop();
        s_server_running = false;
    }
    image_upload_server_start();
}

static void http_server_task(void *arg)
{
    (void)arg;
    Serial.println("Starting HTTP task (core 0)");

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!s_server_running) {
                image_upload_server_start();
            }
            for (int i = 0; i < 24; ++i) {
                server.handleClient();
            }
            image_screen_dispatch_pending();
            comment_screen_dispatch_pending();
            todo_screen_dispatch_pending();
        } else if (s_server_running) {
            server.stop();
            s_server_running = false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void image_upload_server_init(void)
{
    static bool task_started = false;
    if (task_started) {
        return;
    }
    task_started = true;
    xTaskCreatePinnedToCore(
        http_server_task,
        "httpd",
        APP_STACK_HTTP,
        nullptr,
        APP_PRIO_HTTP,
        nullptr,
        APP_CORE_NET);
}
