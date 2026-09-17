#pragma once
#include <Arduino.h>

// The whole setup UI lives in flash so there is no separate "upload
// filesystem image" step -- flash the firmware and the portal just works.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Badge Setup</title>
<style>
:root{
  --bg:#f6f6f4; --card:#fff; --ink:#15171a; --muted:#6b7280;
  --line:#e3e3df; --accent:#111; --ok:#0f7b3f; --bad:#b42318;
}
@media (prefers-color-scheme:dark){
  :root{ --bg:#111214; --card:#1a1c1f; --ink:#f2f2f0; --muted:#9aa1ab;
         --line:#2c2f34; --accent:#f2f2f0; }
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
     font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
     padding-bottom:96px}
header{position:sticky;top:0;z-index:10;background:var(--bg);
       border-bottom:1px solid var(--line);padding:14px 16px 0}
h1{margin:0 0 2px;font-size:19px;letter-spacing:-.01em}
.sub{color:var(--muted);font-size:12px;margin-bottom:10px}
.tabs{display:flex;gap:2px;overflow-x:auto;scrollbar-width:none}
.tabs::-webkit-scrollbar{display:none}
.tab{flex:0 0 auto;padding:9px 14px;border:0;background:none;color:var(--muted);
     font:inherit;font-size:14px;border-bottom:2px solid transparent;cursor:pointer}
.tab.on{color:var(--ink);border-bottom-color:var(--accent);font-weight:600}
main{padding:16px}
.panel{display:none} .panel.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;
      padding:14px;margin-bottom:12px}
label{display:block;font-size:12px;font-weight:600;color:var(--muted);
      margin:12px 0 5px;letter-spacing:.02em;text-transform:uppercase}
label:first-child{margin-top:0}
input,select,textarea{width:100%;padding:10px 12px;border:1px solid var(--line);
  border-radius:9px;background:var(--bg);color:var(--ink);font:inherit}
textarea{resize:vertical;min-height:76px}
input:focus,select:focus,textarea:focus{outline:2px solid var(--accent);outline-offset:-1px}
.hint{font-size:12px;color:var(--muted);margin-top:5px}
.row{display:flex;gap:10px}.row>*{flex:1}
.check{display:flex;align-items:center;gap:10px;padding:9px 0;
       border-bottom:1px solid var(--line)}
.check:last-child{border-bottom:0}
.check input{width:20px;height:20px;flex:0 0 auto}
.check span{font-size:14px}
.check small{display:block;color:var(--muted);font-size:12px}
canvas{width:100%;max-width:280px;display:block;margin:0 auto;
       border:1px solid var(--line);border-radius:8px;background:#fff;
       image-rendering:pixelated}
.slider{display:flex;align-items:center;gap:10px}
.slider input[type=range]{flex:1}
.slider b{font-weight:600;font-size:12px;color:var(--muted);min-width:58px}
.slider i{font-style:normal;font-size:12px;color:var(--muted);min-width:34px;
          text-align:right;font-variant-numeric:tabular-nums}
button{font:inherit}
.btn{display:block;width:100%;padding:12px;border-radius:10px;border:1px solid var(--line);
     background:var(--card);color:var(--ink);font-weight:600;cursor:pointer;margin-top:10px}
.btn.pri{background:var(--accent);color:var(--bg);border-color:var(--accent)}
.btn:disabled{opacity:.45}
.bar{position:fixed;left:0;right:0;bottom:0;background:var(--card);
     border-top:1px solid var(--line);padding:10px 16px calc(10px + env(safe-area-inset-bottom));
     display:flex;gap:10px}
.bar .btn{margin:0}
#toast{position:fixed;left:50%;transform:translateX(-50%);bottom:88px;z-index:50;
  background:var(--ink);color:var(--bg);padding:10px 16px;border-radius:999px;
  font-size:13px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:90vw}
#toast.on{opacity:1}
#toast.bad{background:var(--bad);color:#fff}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:0;font-size:13px}
dt{color:var(--muted)} dd{margin:0;text-align:right;font-variant-numeric:tabular-nums}
</style>
</head>
<body>
<header>
  <h1>Conference Badge</h1>
  <div class="sub" id="hdrsub">connecting...</div>
  <div class="tabs">
    <button class="tab on" data-p="details">Details</button>
    <button class="tab" data-p="photo">Photo</button>
    <button class="tab" data-p="look">Look</button>
    <button class="tab" data-p="net">Network</button>
    <button class="tab" data-p="info">Status</button>
  </div>
</header>
<main>

<section class="panel on" id="p-details">
  <div class="card">
    <label for="f-name">Name</label>
    <input id="f-name" maxlength="40" autocomplete="name">
    <label for="f-pronouns">Pronouns <span style="text-transform:none">(optional)</span></label>
    <input id="f-pronouns" maxlength="20" placeholder="she/her">
    <label for="f-profession">Profession / title</label>
    <input id="f-profession" maxlength="60">
    <label for="f-company">Company</label>
    <input id="f-company" maxlength="40">
  </div>
  <div class="card">
    <label for="f-event">Event name <span style="text-transform:none">(top band)</span></label>
    <input id="f-event" maxlength="40">
    <label for="f-funFact">Fun fact</label>
    <textarea id="f-funFact" maxlength="220"></textarea>
    <div class="hint"><span id="fflen">0</span>/220 - roughly 130 characters fit
      at the larger size, the rest is dropped with an ellipsis.</div>
    <label for="f-footer">Footer line <span style="text-transform:none">(optional)</span></label>
    <input id="f-footer" maxlength="48" placeholder="Table 12 - Hall B">
  </div>
  <div class="card">
    <label for="f-qrMode">QR code contains</label>
    <select id="f-qrMode">
      <option value="url">A link (website, LinkedIn, ...)</option>
      <option value="vcard">My contact card (vCard)</option>
      <option value="wifi">WiFi credentials</option>
      <option value="text">Plain text</option>
    </select>
    <div id="qr-payload">
      <label for="f-qrText"><span id="qrTextLabel">Link</span></label>
      <input id="f-qrText" maxlength="180">
      <div class="hint" id="qrTextHint">Keep it short - a shorter link means
        chunkier, easier-to-scan modules.</div>
    </div>
    <label for="f-qrCaption">Caption beside the QR</label>
    <input id="f-qrCaption" maxlength="40">
    <label for="f-website">Website / handle <span style="text-transform:none">(printed under the caption)</span></label>
    <input id="f-website" maxlength="48">
    <div class="row">
      <div><label for="f-email">Email</label><input id="f-email" maxlength="60" inputmode="email"></div>
      <div><label for="f-phone">Phone</label><input id="f-phone" maxlength="24" inputmode="tel"></div>
    </div>
    <div class="hint">Email and phone are only printed if you pick them as the
      caption line; they are always included in the vCard QR.</div>
  </div>
</section>

<section class="panel" id="p-photo">
  <div class="card">
    <canvas id="pcv" width="320" height="320"></canvas>
    <div class="hint" style="text-align:center" id="pstat">No image loaded.</div>
    <label for="f-file">Choose a photo</label>
    <input id="f-file" type="file" accept="image/*">
    <button class="btn" id="b-loadcur">Load the photo already on the badge</button>
  </div>
  <div class="card">
    <div class="slider"><b>Zoom</b><input id="s-zoom" type="range" min="100" max="300" value="100"><i id="v-zoom">1.0x</i></div>
    <div class="slider"><b>Pan X</b><input id="s-ox" type="range" min="-100" max="100" value="0"><i id="v-ox">0</i></div>
    <div class="slider"><b>Pan Y</b><input id="s-oy" type="range" min="-100" max="100" value="0"><i id="v-oy">0</i></div>
    <div class="slider"><b>Bright</b><input id="s-br" type="range" min="-60" max="60" value="0"><i id="v-br">0</i></div>
    <div class="slider"><b>Contrast</b><input id="s-ct" type="range" min="50" max="220" value="115"><i id="v-ct">1.15</i></div>
    <label for="f-dither">Dithering</label>
    <select id="f-dither">
      <option value="fs">Floyd-Steinberg (best for faces)</option>
      <option value="ordered">Ordered (crisp, retro)</option>
      <option value="none">None (posterised, 16 levels)</option>
    </select>
    <div class="hint">The preview above is exactly what the panel will show -
      your phone does the greyscale conversion, the badge just stores pixels.
      The target size follows the layout, so save your details first and the
      photo will be produced at exactly the right size with no rescaling.</div>
  </div>
  <div class="card">
    <button class="btn pri" id="b-upload">Upload photo to badge</button>
    <button class="btn" id="b-uploadr">Upload and redraw</button>
    <button class="btn" id="b-delphoto">Remove photo from badge</button>
  </div>
</section>

<section class="panel" id="p-look">
  <div class="card">
    <div class="check"><input type="checkbox" id="f-showPhoto">
      <span>Show photo<small>Off gives the text and fun fact more room</small></span></div>
    <div class="check"><input type="checkbox" id="f-border">
      <span>Border frame<small>Hairline rule around the whole badge</small></span></div>
    <div class="check"><input type="checkbox" id="f-invert">
      <span>Dark theme<small>White ink on black. Uses more power per refresh</small></span></div>
    <div class="check"><input type="checkbox" id="f-flip">
      <span>Flip 180&deg;<small>If your lanyard hangs the board the other way up</small></span></div>
  </div>
  <div class="card">
    <label for="f-sleepAfterMin">Sleep after (minutes idle)</label>
    <input id="f-sleepAfterMin" type="number" min="0" max="240" step="1">
    <div class="hint">The image stays on screen while asleep - e-ink needs no
      power to hold it. Press the side button to wake the portal back up.
      Set to 0 to keep WiFi on forever (handy on a desk, hungry on battery).</div>
  </div>
</section>

<section class="panel" id="p-net">
  <div class="card">
    <label for="f-staSsid">Join an existing WiFi <span style="text-transform:none">(optional)</span></label>
    <input id="f-staSsid" maxlength="32" autocomplete="off">
    <label for="f-staPass">Password</label>
    <input id="f-staPass" type="password" maxlength="63" autocomplete="off">
    <div class="hint">If this connects, reach the badge at
      <b>http://badge.local/</b>. If it fails the badge falls back to its own
      access point, so you cannot lock yourself out.</div>
  </div>
  <div class="card">
    <label for="f-apSsid">Badge access point name</label>
    <input id="f-apSsid" maxlength="31" placeholder="Badge-XXXX">
    <label for="f-apPass">Access point password</label>
    <input id="f-apPass" type="password" maxlength="63">
    <div class="hint">Minimum 8 characters, or leave empty for an open network.
      Changing this takes effect after the next restart.</div>
  </div>
</section>

<section class="panel" id="p-info">
  <div class="card"><dl id="stats"></dl></div>
  <div class="card">
    <button class="btn" id="b-render">Redraw the badge now</button>
    <button class="btn" id="b-blank">Clear screen to white</button>
    <div class="hint">Clearing to white before storing the badge for a few
      months is kind to the panel.</div>
  </div>
</section>
</main>

<div class="bar">
  <button class="btn" id="b-save">Save</button>
  <button class="btn pri" id="b-saver">Save &amp; redraw</button>
</div>
<div id="toast"></div>

<script>
const $ = id => document.getElementById(id);
const TEXT_KEYS = ["name","pronouns","profession","company","funFact","event",
                   "footer","qrMode","qrText","qrCaption","email","phone",
                   "website","apSsid","apPass","staSsid","staPass"];
const BOOL_KEYS = ["showPhoto","flip","invert","border"];
const NUM_KEYS  = ["sleepAfterMin"];

let P = {w:320,h:320};          // photo size the firmware wants
let processed = null;           // Uint8Array of 0..15, one entry per pixel
let srcImg = null;

/* ---------- chrome ---------- */
document.querySelectorAll(".tab").forEach(t => t.onclick = () => {
  document.querySelectorAll(".tab").forEach(x => x.classList.remove("on"));
  document.querySelectorAll(".panel").forEach(x => x.classList.remove("on"));
  t.classList.add("on");
  $("p-" + t.dataset.p).classList.add("on");
});

let toastTimer = null;
function toast(msg, bad) {
  const t = $("toast");
  t.textContent = msg;
  t.className = "on" + (bad ? " bad" : "");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.className = "", bad ? 4200 : 2200);
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  const txt = await r.text();
  let j = {};
  try { j = JSON.parse(txt); } catch (e) { j = {ok:r.ok, msg:txt.slice(0,120)}; }
  if (!r.ok || j.ok === false) throw new Error(j.msg || ("HTTP " + r.status));
  return j;
}

/* ---------- config ---------- */
function fillForm(c) {
  TEXT_KEYS.forEach(k => { if ($("f-"+k)) $("f-"+k).value = c[k] ?? ""; });
  BOOL_KEYS.forEach(k => { if ($("f-"+k)) $("f-"+k).checked = !!c[k]; });
  NUM_KEYS.forEach(k  => { if ($("f-"+k)) $("f-"+k).value = c[k] ?? 0; });
  onQrMode();
  $("fflen").textContent = $("f-funFact").value.length;
}

function readForm() {
  const c = {};
  TEXT_KEYS.forEach(k => { if ($("f-"+k)) c[k] = $("f-"+k).value; });
  BOOL_KEYS.forEach(k => { if ($("f-"+k)) c[k] = $("f-"+k).checked; });
  NUM_KEYS.forEach(k  => { if ($("f-"+k)) c[k] = parseInt($("f-"+k).value || "0", 10); });
  return c;
}

function onQrMode() {
  const m = $("f-qrMode").value;
  const lab = {url:"Link", vcard:"", wifi:"SSID|password", text:"Text"}[m];
  const hint = {
    url:  "Keep it short - a shorter link means chunkier, easier-to-scan modules.",
    vcard:"Built automatically from your name, title, company, email, phone and website.",
    wifi: "Write it as NetworkName|password. Phones will offer to join on scan.",
    text: "Anything you like, up to about 180 characters."
  }[m];
  $("qr-payload").style.display = (m === "vcard") ? "none" : "";
  $("qrTextLabel").textContent = lab;
  $("qrTextHint").textContent = hint;
}
$("f-qrMode").onchange = onQrMode;
$("f-funFact").oninput = () => $("fflen").textContent = $("f-funFact").value.length;

async function save(render) {
  const btns = [$("b-save"), $("b-saver")];
  btns.forEach(b => b.disabled = true);
  try {
    await api("/api/config" + (render ? "?render=1" : ""), {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(readForm())
    });
    toast(render ? "Saved - the panel is refreshing" : "Saved");
    await refreshStatus();          // the photo target moves with the layout
  } catch (e) {
    toast(e.message, true);
  }
  btns.forEach(b => b.disabled = false);
}
$("b-save").onclick  = () => save(false);
$("b-saver").onclick = () => save(true);

$("b-render").onclick = async () => {
  try { await api("/api/render", {method:"POST"}); toast("Refreshing the panel"); }
  catch (e) { toast(e.message, true); }
};
$("b-blank").onclick = async () => {
  if (!confirm("Clear the screen to white?")) return;
  try { await api("/api/blank", {method:"POST"}); toast("Clearing"); }
  catch (e) { toast(e.message, true); }
};

/* ---------- photo pipeline ----------
   Everything happens here, on the phone: fit, greyscale, dither, 4bpp pack.
   The badge only ever receives finished pixels, which is why it can accept a
   12 megapixel camera shot without a JPEG decoder or a scaler. */
const SL = ["zoom","ox","oy","br","ct"];
SL.forEach(k => $("s-"+k).oninput = () => { showSliders(); draw(); });
$("f-dither").onchange = draw;

function showSliders() {
  $("v-zoom").textContent = ($("s-zoom").value/100).toFixed(1) + "x";
  $("v-ox").textContent = $("s-ox").value;
  $("v-oy").textContent = $("s-oy").value;
  $("v-br").textContent = $("s-br").value;
  $("v-ct").textContent = ($("s-ct").value/100).toFixed(2);
}

$("f-file").onchange = e => {
  const f = e.target.files[0];
  if (!f) return;
  const img = new Image();
  img.onload = () => {
    srcImg = img;
    $("s-zoom").value = 100; $("s-ox").value = 0; $("s-oy").value = 0;
    showSliders(); draw();
    URL.revokeObjectURL(img.src);
  };
  img.onerror = () => toast("Could not read that image", true);
  img.src = URL.createObjectURL(f);
};

function draw() {
  if (!srcImg) return;
  const c = $("pcv"), w = P.w, h = P.h;
  c.width = w; c.height = h;
  const g = c.getContext("2d", {willReadFrequently:true});
  g.fillStyle = "#fff"; g.fillRect(0, 0, w, h);

  // cover-fit, then zoom and pan within the overflow
  const s = Math.max(w/srcImg.width, h/srcImg.height) * ($("s-zoom").value/100);
  const dw = srcImg.width*s, dh = srcImg.height*s;
  const mx = Math.max(0, (dw-w)/2), my = Math.max(0, (dh-h)/2);
  g.drawImage(srcImg,
              (w-dw)/2 + ($("s-ox").value/100)*mx,
              (h-dh)/2 + ($("s-oy").value/100)*my, dw, dh);

  const id = g.getImageData(0, 0, w, h);
  processed = quantise(id, w, h);
  g.putImageData(id, 0, 0);
  $("pstat").textContent = w + " x " + h + ", 16 grey levels, " +
                           (8 + ((w+1)>>1)*h) + " bytes";
}

const BAYER = [[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]];

function quantise(id, w, h) {
  const d = id.data;
  const br = +$("s-br").value * 2.55;
  const ct = +$("s-ct").value / 100;
  const mode = $("f-dither").value;

  // Float working buffer so Floyd-Steinberg error can go out of range.
  const buf = new Float32Array(w*h);
  for (let i = 0; i < w*h; i++) {
    const v = 0.299*d[i*4] + 0.587*d[i*4+1] + 0.114*d[i*4+2];
    buf[i] = (v - 128) * ct + 128 + br;
  }

  const out = new Uint8Array(w*h);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y*w + x;
      let v = buf[i];
      if (mode === "ordered") v += (BAYER[y&3][x&3]/16 - 0.5) * 17;
      const n = Math.max(0, Math.min(15, Math.round(v/17)));
      out[i] = n;

      if (mode === "fs") {
        const err = buf[i] - n*17;
        if (x+1 < w)            buf[i+1]   += err*7/16;
        if (y+1 < h) {
          if (x > 0)            buf[i+w-1] += err*3/16;
                                buf[i+w]   += err*5/16;
          if (x+1 < w)          buf[i+w+1] += err/16;
        }
      }
      const g8 = n*17;
      d[i*4] = d[i*4+1] = d[i*4+2] = g8; d[i*4+3] = 255;
    }
  }
  return out;
}

function packEPB1(w, h, px) {
  const rb = (w+1) >> 1;
  const b = new Uint8Array(8 + rb*h);
  b[0]=0x45; b[1]=0x50; b[2]=0x42; b[3]=0x31;          // "EPB1"
  b[4]=w & 255; b[5]=w >> 8; b[6]=h & 255; b[7]=h >> 8;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const n = px[y*w+x] & 15;
      const o = 8 + y*rb + (x >> 1);
      if (x & 1) b[o] = (b[o] & 0x0F) | (n << 4);   // odd x  -> high nibble
      else       b[o] = (b[o] & 0xF0) | n;          // even x -> low nibble
    }
  }
  return b;
}

async function upload(render) {
  if (!processed) { toast("Pick an image first", true); return; }
  const btns = [$("b-upload"), $("b-uploadr")];
  btns.forEach(b => b.disabled = true);
  try {
    const fd = new FormData();
    fd.append("photo", new Blob([packEPB1(P.w, P.h, processed)],
              {type:"application/octet-stream"}), "photo.bin");
    await api("/api/photo" + (render ? "?render=1" : ""), {method:"POST", body:fd});
    toast(render ? "Uploaded - the panel is refreshing" : "Photo uploaded");
  } catch (e) {
    toast(e.message, true);
  }
  btns.forEach(b => b.disabled = false);
}
$("b-upload").onclick  = () => upload(false);
$("b-uploadr").onclick = () => upload(true);

$("b-delphoto").onclick = async () => {
  if (!confirm("Remove the stored photo?")) return;
  try { await api("/api/photo", {method:"DELETE"}); toast("Photo removed"); }
  catch (e) { toast(e.message, true); }
};

$("b-loadcur").onclick = async () => {
  try {
    const r = await fetch("/api/photo");
    if (!r.ok) throw new Error("nothing stored yet");
    const b = new Uint8Array(await r.arrayBuffer());
    if (b.length < 8 || b[0]!==0x45 || b[1]!==0x50 || b[2]!==0x42 || b[3]!==0x31)
      throw new Error("stored file is not a badge photo");
    const w = b[4] | (b[5]<<8), h = b[6] | (b[7]<<8), rb = (w+1)>>1;
    const c = $("pcv"); c.width = w; c.height = h;
    const g = c.getContext("2d");
    const id = g.createImageData(w, h);
    processed = new Uint8Array(w*h);
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const by = b[8 + y*rb + (x>>1)];
        const n = (x & 1) ? (by >> 4) : (by & 15);
        const i = y*w + x;
        processed[i] = n;
        id.data[i*4] = id.data[i*4+1] = id.data[i*4+2] = n*17;
        id.data[i*4+3] = 255;
      }
    }
    g.putImageData(id, 0, 0);
    srcImg = null;   // sliders would have nothing to re-derive from
    $("pstat").textContent = "Loaded from the badge (" + w + " x " + h + ")";
    toast("Loaded the badge photo");
  } catch (e) {
    toast(e.message, true);
  }
};

/* ---------- status ---------- */
function fmtBytes(n) {
  return n > 1048576 ? (n/1048576).toFixed(1) + " MB"
       : n > 1024    ? (n/1024).toFixed(0) + " kB" : n + " B";
}

async function refreshStatus() {
  try {
    const s = await api("/api/status");
    const moved = (P.w !== s.photoW || P.h !== s.photoH);
    P = {w: s.photoW, h: s.photoH};
    if (moved && srcImg) draw();    // re-fit at the badge's new photo size
    $("hdrsub").textContent =
      (s.mode === "ap" ? "Access point " : "On ") + s.ssid + "  -  " + s.ip;
    const up = s.uptime;
    const rows = [
      ["Mode",        s.mode === "ap" ? "Access point" : "Station (" + s.rssi + " dBm)"],
      ["Address",     s.ip],
      ["Badge canvas",s.canvasW + " x " + s.canvasH],
      ["Photo slot",  s.photoW + " x " + s.photoH + (s.hasPhoto ? " (stored)" : " (empty)")],
      ["Battery",     s.battery > 0.5 ? s.battery.toFixed(2) + " V" : "USB / unknown"],
      ["Free heap",   fmtBytes(s.heap)],
      ["Free PSRAM",  fmtBytes(s.psram)],
      ["Uptime",      Math.floor(up/60) + "m " + (up%60) + "s"]
    ];
    $("stats").innerHTML = rows.map(r =>
      "<dt>" + r[0] + "</dt><dd>" + r[1] + "</dd>").join("");
  } catch (e) {
    $("hdrsub").textContent = "offline - " + e.message;
  }
}

/* ---------- boot ---------- */
(async () => {
  showSliders();
  try {
    fillForm(await api("/api/config"));
  } catch (e) {
    toast("Could not load settings: " + e.message, true);
  }
  await refreshStatus();
  setInterval(refreshStatus, 20000);   // also keeps the sleep timer at bay
})();
</script>
</body>
</html>
)HTML";
