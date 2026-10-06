#pragma once
#include <pgmspace.h>

// /diag dashboard. Every button calls one /api endpoint and prints the JSON result.
static const char DIAG_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmallTV diag</title>
<style>
:root{--bg:#fff;--fg:#1a1a1a;--mut:#666;--card:#f4f4f5;--acc:#2563eb;--bd:#ddd}
@media (prefers-color-scheme:dark){:root{--bg:#111;--fg:#eee;--mut:#999;--card:#1c1c1f;--acc:#60a5fa;--bd:#333}}
body{background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,sans-serif;margin:0 auto;max-width:900px;padding:16px}
h1{font-size:20px;margin:0 0 4px}h2{font-size:15px;margin:0 0 8px}
section{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:12px;margin:12px 0}
button{background:var(--acc);color:#fff;border:0;border-radius:6px;padding:6px 10px;margin:2px;cursor:pointer;font-size:13px}
button:disabled{opacity:.5}
pre{background:var(--bg);border:1px solid var(--bd);border-radius:6px;padding:8px;overflow:auto;max-height:320px;font-size:12px;margin:8px 0 0}
p.m{color:var(--mut);margin:4px 0}
</style></head><body>
<h1>SmallTV diag</h1><p class="m">Each button runs one test on the device; the result appears in the box below.</p>

<section><h2>System</h2>
<button onclick="j('/api/info')">Info</button>
<button onclick="j('/api/bench/cpu')">CPU bench</button>
<button onclick="j('/api/bench/mem')">Memory allocation</button>
<button onclick="j('/api/bench/flash')">Flash read speed</button>
<button onclick="j('/api/log',1)">Log</button>
</section>

<section><h2>Network</h2>
<button onclick="dl()">Download 1MB (device→PC)</button>
<button onclick="ul()">Upload 256KB (PC→device)</button>
<button onclick="j('/api/wifi/scan')">Wi-Fi scan</button>
</section>

<section><h2>Display</h2>
<button onclick="j('/api/display/init')">Init</button>
<button onclick="j('/api/display/pattern?n=status')">Status</button>
<button onclick="j('/api/display/pattern?n=bars')">Colour bars</button>
<button onclick="j('/api/display/pattern?n=grid')">Grid / corners</button>
<button onclick="j('/api/display/pattern?n=gradient')">Gradient</button>
<button onclick="j('/api/display/pattern?n=text')">Fonts</button>
<button onclick="j('/api/display/pattern?n=jpeg')">JPEG</button>
<button onclick="j('/api/display/bench')">Display benchmark</button><br>
<button onclick="j('/api/display/cfg?invert=0')">Inversion off</button>
<button onclick="j('/api/display/cfg?invert=1')">Inversion on</button>
<button onclick="j('/api/display/cfg?madctl=0x08')">BGR order</button>
<button onclick="j('/api/display/cfg?madctl=0x00')">RGB order</button>
<button onclick="j('/api/display/cfg?spi=80')">SPI 80MHz</button>
<button onclick="j('/api/display/cfg?spi=40')">SPI 40MHz</button>
<button onclick="j('/api/display/cfg?bl=10')">Brightness 10%</button>
<button onclick="j('/api/display/cfg?bl=100')">Brightness 100%</button>
<p class="m">Check: each bar's letter (R/G/B...) matches its colour · the red grid border is visible on all four edges · "UP ^" points up · gradients are smooth</p>
</section>

<section><h2>PC→display streaming (thin-display mode)</h2>
<button onclick="pushRaw()">RAW RGB565 frame x3</button>
<button onclick="pushJpg()">JPEG frame x3</button>
</section>

<section><h2>Inputs (find a button)</h2>
<button onclick="j('/api/inputs?reset=1')">Reset counters</button>
<button onclick="j('/api/inputs')">Read</button>
<p class="m">Reset counters → press the device's button (if any) a few times → Read. The pin whose "changes" went up is the button.</p>
</section>

<pre id="out">...</pre>
<script>
const out=document.getElementById('out');
const show=(t,v)=>out.textContent=t+'\n'+(typeof v=='string'?v:JSON.stringify(v,null,2));
async function j(u,txt){show(u,'running...');try{const r=await fetch(u);show(u,txt?await r.text():await r.json())}catch(e){show(u,'error: '+e)}}
async function dl(){show('download','running...');const t=performance.now();const b=await (await fetch('/api/bench/zero?len=1048576')).arrayBuffer();const s=(performance.now()-t)/1000;show('download',{bytes:b.byteLength,sec:+s.toFixed(2),KBps:Math.round(b.byteLength/1024/s)})}
async function post(u,blob,name){const f=new FormData();f.append('f',blob,name);const t=performance.now();const r=await (await fetch(u,{method:'POST',body:f})).json();r.wall_ms=Math.round(performance.now()-t);return r}
async function ul(){show('upload','running...');show('upload',await post('/api/bench/sink',new Blob([new Uint8Array(262144)]),'z.bin'))}
function frame(k){const c=document.createElement('canvas');c.width=c.height=240;const g=c.getContext('2d');const gr=g.createLinearGradient(0,0,240,240);gr.addColorStop(0,`hsl(${k*120},80%,50%)`);gr.addColorStop(1,`hsl(${k*120+180},80%,40%)`);g.fillStyle=gr;g.fillRect(0,0,240,240);g.fillStyle='#fff';g.font='bold 36px sans-serif';g.fillText('PC '+(k+1),70,130);return c}
async function pushRaw(){show('raw','running...');const res=[];for(let k=0;k<3;k++){const d=frame(k).getContext('2d').getImageData(0,0,240,240).data;const b=new Uint8Array(240*240*2);for(let i=0,o=0;i<d.length;i+=4){const v=((d[i]&0xf8)<<8)|((d[i+1]&0xfc)<<3)|(d[i+2]>>3);b[o++]=v>>8;b[o++]=v&255}res.push(await post('/api/display/raw',new Blob([b]),'f.raw'))}const ms=res.reduce((a,r)=>a+r.wall_ms,0)/3;show('raw',{frames:res,avg_ms:Math.round(ms),fps:+(1000/ms).toFixed(1)})}
async function pushJpg(){show('jpeg','running...');const res=[];for(let k=0;k<3;k++){const bl=await new Promise(r=>frame(k).toBlob(r,'image/jpeg',0.8));res.push(await post('/api/display/jpeg',bl,'f.jpg'))}const ms=res.reduce((a,r)=>a+r.wall_ms,0)/3;show('jpeg',{frames:res,avg_ms:Math.round(ms),fps:+(1000/ms).toFixed(1)})}
j('/api/info');
</script></body></html>)HTML";
