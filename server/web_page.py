"""HTML of the dashboard (rendered by the "/" route)."""

PAGE = """<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-CAM Security</title>
<style>
  body{margin:0;padding:20px;font-family:Arial,sans-serif;background:#111827;color:#f9fafb}
  .wrap{max-width:900px;margin:auto}
  img.live{width:100%;background:#000;border-radius:8px}
  .card{background:#1f2937;border-radius:10px;padding:16px;margin-top:16px}
  .ok{color:#4ade80}.bad{color:#f87171}.warn{color:#fbbf24}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:10px}
  .grid img{width:100%;border-radius:6px}.grid small{color:#9ca3af}
  input[type=range]{width:100%}
  button{background:#2563eb;color:#fff;border:0;border-radius:6px;padding:8px 14px;font-size:15px;cursor:pointer}
  button:disabled{opacity:.5}
</style></head><body><div class="wrap">
<h2>ESP32-CAM Security</h2>
<img class="live" id="live" alt="Live stream">
<div class="card">
  <div id="status">Đang tải trạng thái...</div>
  <p><button id="snap" onclick="snap()">📸 Chụp ngay</button>
     <span id="snapmsg"></span></p>
  {% if zoom_enabled %}
  <p>Zoom: <b id="zv">1.0×</b></p>
  <input type="range" id="zoom" min="1" max="4" step="0.1" value="1"
         oninput="zv.textContent=Number(this.value).toFixed(1)+'×'"
         onchange="fetch('/api/zoom?value='+this.value)">
  {% endif %}
</div>
<div class="card"><b>Sự kiện chuyển động gần đây</b>
  <div class="grid" id="events"></div></div>
<script>
const live=document.getElementById('live');
async function snap(){
  const b=document.getElementById('snap'),m=document.getElementById('snapmsg');
  b.disabled=true;
  try{
    const r=await fetch('/api/snapshot',{method:'POST'});
    const j=await r.json();
    m.textContent=r.ok?'Đã lưu '+j.file:(j.error||'Lỗi');
  }catch(e){m.textContent='Lỗi kết nối';}
  b.disabled=false;
  refresh();
}
function startStream(){live.src='/stream?'+Date.now();}
live.onerror=()=>setTimeout(startStream,3000);
startStream();
async function refresh(){
  try{
    const s=await (await fetch('/status')).json();
    const cam=s.camera_online?'<span class="ok">camera online</span>':'<span class="bad">camera offline</span>';
    const mo=s.motion_now?'<span class="warn">đang có chuyển động</span>':'không có chuyển động';
    document.getElementById('status').innerHTML=
      cam+' · '+mo+' · thông báo: '+s.notify+' · '+s.frames+' khung';
    document.getElementById('events').innerHTML=s.events.map(e=>
      '<a href="/events/'+e.file+'" target="_blank"><img src="/events/'+e.file+
      '"><small>'+e.time+(e.kind==='manual'?' · chụp tay':'')+'</small></a>').join('')||'<small>Chưa có.</small>';
  }catch(e){}
}
refresh();setInterval(refresh,2000);
</script></div></body></html>"""
