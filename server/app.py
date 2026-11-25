"""ESP32-CAM security server.

* pulls the MJPEG stream of the ESP32-CAM (or receives JPEGs on /upload),
* detects motion with frame differencing,
* saves a snapshot and sends it to Telegram / Discord,
* re-streams the video to any number of browsers (dashboard on "/").

All settings come from environment variables - see README.md.
"""

import os
import socket
import threading
import time
from collections import deque
from datetime import datetime
from pathlib import Path
from urllib.parse import urlparse

import cv2
import requests
from flask import (
    Flask,
    Response,
    abort,
    jsonify,
    render_template_string,
    request,
    send_from_directory,
)

try:  # optional: load settings from a .env file next to this script
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).resolve().parent / ".env")
except ImportError:
    pass

from detector import MotionDetector, annotate
from notifier import Notifier
from stream_client import FrameStore, StreamClient

# ---------------------------------------------------------------- settings


def env_int(name, default):
    try:
        return int(os.getenv(name, default))
    except ValueError:
        return default


def env_float(name, default):
    try:
        return float(os.getenv(name, default))
    except ValueError:
        return default


def env_bool(name, default):
    value = os.getenv(name)

    if value is None:
        return default

    return value.strip().lower() in ("1", "true", "yes", "on")


ESP_STREAM_URL = os.getenv("ESP_STREAM_URL", "").strip()
ESP_CONTROL_URL = os.getenv("ESP_CONTROL_URL", "").strip()

MOTION_ENABLED = env_bool("MOTION_ENABLED", True)
MOTION_MIN_AREA = env_int("MOTION_MIN_AREA", 600)
MOTION_THRESHOLD = env_int("MOTION_THRESHOLD", 25)
MOTION_MIN_FRAMES = env_int("MOTION_MIN_FRAMES", 2)
MOTION_COOLDOWN = env_float("MOTION_COOLDOWN", 30.0)  # seconds between alerts
MAX_EVENT_FILES = env_int("MAX_EVENT_FILES", 200)
MAX_EVENT_DAYS = env_float("MAX_EVENT_DAYS", 7.0)  # 0 = keep forever
SAVE_UPLOADS = env_bool("SAVE_UPLOADS", False)
PORT = env_int("PORT", 5000)

if ESP_STREAM_URL and not ESP_CONTROL_URL:
    # http://IP:81/stream -> http://IP/control (control panel runs on port 80)
    ESP_CONTROL_URL = f"http://{urlparse(ESP_STREAM_URL).hostname}/control"

BASE_DIR = Path(__file__).resolve().parent
EVENT_DIR = BASE_DIR / "captured_images"
EVENT_DIR.mkdir(exist_ok=True)

JPEG_SOI = b"\xff\xd8"

# ------------------------------------------------------------------- state

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 5 * 1024 * 1024  # 5 MB per upload

store = FrameStore()
notifier = Notifier(log=lambda m: print(m, flush=True))
detector = MotionDetector(
    min_area=MOTION_MIN_AREA, threshold=MOTION_THRESHOLD
)

pipeline_lock = threading.Lock()
events = deque(maxlen=24)  # newest first
events_lock = threading.Lock()
state = {
    "motion_enabled": MOTION_ENABLED,
    "motion_now": False,
    "motion_frames": 0,
    "last_alert": 0.0,
    "frames": 0,
}


def log(message):
    print(message, flush=True)


def set_motion_enabled(enabled: bool) -> bool:
    """Enable or disable motion detection at runtime."""
    enabled = bool(enabled)

    with pipeline_lock:
        if state["motion_enabled"] == enabled:
            return True

        state["motion_enabled"] = enabled

        # Reset detection state when changing the mode.
        state["motion_now"] = False
        state["motion_frames"] = 0
        detector.reset()

    log(
        f"[motion] Detection {'enabled' if enabled else 'disabled'}"
    )
    return True


# --------------------------------------------------------------- processing


TIME_FORMAT = "%H:%M:%S %d/%m/%Y"
EVENT_PATTERNS = ("motion_*.jpg", "snapshot_*.jpg")  # motion alerts, manual


def _mtime(path):
    try:
        return path.stat().st_mtime
    except OSError:
        return 0.0


def event_files():
    """Saved pictures (motion + manual), oldest first."""
    files = []

    for pattern in EVENT_PATTERNS:
        files.extend(EVENT_DIR.glob(pattern))

    return sorted(files, key=_mtime)


def cleanup_events():
    """Delete pictures beyond MAX_EVENT_FILES or older than MAX_EVENT_DAYS."""
    files = event_files()
    doomed = set()

    if MAX_EVENT_FILES > 0 and len(files) > MAX_EVENT_FILES:
        doomed.update(files[:-MAX_EVENT_FILES])

    if MAX_EVENT_DAYS > 0:
        limit = time.time() - MAX_EVENT_DAYS * 86400
        doomed.update(p for p in files if _mtime(p) < limit)

    for old in doomed:
        try:
            old.unlink()
        except OSError:
            pass


def add_event(filename, when, area=None, kind="motion"):
    """Remember a saved picture for the dashboard (newest first)."""
    entry = {
        "file": filename,
        "time": when.strftime(TIME_FORMAT),
        "area": area,
        "kind": kind,  # "motion" or "manual"
    }

    with events_lock:
        for old in [e for e in events if e["file"] == filename]:
            events.remove(old)  # same-second duplicate

        events.appendleft(entry)


def capture_snapshot():
    """Save the latest fresh camera frame and return its path."""
    frame = store.latest()
    age = time.time() - store.timestamp if store.timestamp else None

    if frame is None or age is None or age > 5:
        return None

    now = datetime.now()
    filename = f"snapshot_{now.strftime('%Y%m%d_%H%M%S_%f')}.jpg"
    path = EVENT_DIR / filename
    tmp = EVENT_DIR / (filename + ".tmp")

    try:
        tmp.write_bytes(frame)
        tmp.replace(path)
    except OSError as exc:
        log(f"[snapshot] Save failed: {type(exc).__name__}")
        return None

    add_event(filename, now, None, "manual")
    cleanup_events()

    log(f"[snapshot] {filename}")
    return path


def get_system_status():
    """Return a snapshot of the current system status."""
    age = time.time() - store.timestamp if store.timestamp else None

    with pipeline_lock:
        motion_enabled = state["motion_enabled"]
        motion_now = state["motion_now"]
        frames = state["frames"]

    return {
        "camera_online": age is not None and age < 5,
        "stream_connected": bool(
            getattr(stream_client, "connected", False)
        ),
        "motion_enabled": motion_enabled,
        "motion_now": motion_now,
        "frames": frames,
        "notify": notifier.describe(),
    }


def load_events():
    """Rebuild the dashboard list from the pictures already on disk."""
    for path in event_files()[-events.maxlen:]:
        kind = "manual" if path.name.startswith("snapshot_") else "motion"

        try:
            # motion_20261125_113045.jpg -> 20261125_113045
            when = datetime.strptime(
                path.name.split("_", 1)[1][:15], "%Y%m%d_%H%M%S"
            )
        except (IndexError, ValueError):
            when = datetime.fromtimestamp(_mtime(path))

        add_event(path.name, when, None, kind)


def cleanup_loop():
    """Age-based cleanup must also run when nothing moves in front of the camera."""
    while True:
        time.sleep(3600)
        cleanup_events()


def handle_motion(image, result):
    """Save the annotated snapshot and notify (called with the lock held)."""
    now = datetime.now()
    filename = f"motion_{now.strftime('%Y%m%d_%H%M%S')}.jpg"
    path = EVENT_DIR / filename

    label = now.strftime("%H:%M:%S %d/%m/%Y")
    picture = annotate(image, result.boxes, label)
    ok, encoded = cv2.imencode(".jpg", picture, [cv2.IMWRITE_JPEG_QUALITY, 90])

    if not ok:
        return

    path.write_bytes(encoded.tobytes())

    add_event(filename, now, result.area, "motion")

    log(f"[motion] {filename} (changed area {result.area}px)")

    caption = (
        f"Phát hiện chuyển động lúc {now.strftime('%H:%M:%S %d/%m/%Y')} "
        f"(vùng thay đổi: {result.area}px)"
    )

    # Network calls must not block the frame pipeline
    threading.Thread(
        target=notifier.send_photo,
        args=(str(path), caption),
        daemon=True,
    ).start()

    cleanup_events()


def on_frame(jpeg: bytes):
    """Called for every new JPEG frame (from the stream thread or /upload)."""
    store.put(jpeg)

    with pipeline_lock:
        state["frames"] += 1

        if not state["motion_enabled"]:
            return

        result, image = detector.process(jpeg)

        state["motion_now"] = result.motion

        if not result.motion:
            state["motion_frames"] = 0
            return

        state["motion_frames"] += 1

        if state["motion_frames"] < MOTION_MIN_FRAMES:
            return

        if time.time() - state["last_alert"] < MOTION_COOLDOWN:
            return

        state["last_alert"] = time.time()
        handle_motion(image, result)


# ------------------------------------------------------------------- routes

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


@app.route("/")
def index():
    return render_template_string(PAGE, zoom_enabled=bool(ESP_CONTROL_URL))


@app.route("/health")
def health():
    return jsonify({"status": "ok"}), 200


def _events_copy():
    with events_lock:
        return list(events)


@app.route("/status")
def status():
    result = get_system_status()
    result["events"] = _events_copy()
    return jsonify(result)


@app.route("/snapshot")
def snapshot():
    frame = store.latest()

    if frame is None:
        abort(404)

    return Response(frame, mimetype="image/jpeg")


@app.route("/api/snapshot", methods=["POST"])
def api_snapshot():
    """Save the current camera picture."""
    path = capture_snapshot()

    if path is None:
        return jsonify(
            {"error": "camera offline - no fresh frame"}
        ), 503

    return jsonify({
        "status": "saved",
        "file": path.name,
    }), 200


@app.route("/stream")
def stream():
    def generate():
        counter = store.counter

        while True:
            frame, counter = store.wait_next(counter, timeout=10)

            if frame is None:  # camera offline: close, the page will retry
                return

            yield (
                b"--frame\r\nContent-Type: image/jpeg\r\n"
                b"Content-Length: " + str(len(frame)).encode() + b"\r\n\r\n"
                + frame + b"\r\n"
            )

    return Response(
        generate(), mimetype="multipart/x-mixed-replace; boundary=frame"
    )


@app.route("/events/<path:name>")
def event_file(name):
    return send_from_directory(EVENT_DIR, name)


@app.route("/api/zoom")
def api_zoom():
    if not ESP_CONTROL_URL:
        return jsonify({"error": "ESP_STREAM_URL / ESP_CONTROL_URL not set"}), 400

    try:
        value = float(request.args.get("value", ""))
    except ValueError:
        return jsonify({"error": "invalid zoom"}), 400

    if not 1.0 <= value <= 4.0:
        return jsonify({"error": "zoom must be between 1.0 and 4.0"}), 400

    try:
        resp = requests.get(
            ESP_CONTROL_URL, params={"zoom": f"{value:.1f}"}, timeout=5
        )
    except requests.RequestException as exc:
        return jsonify({"error": f"camera unreachable ({type(exc).__name__})"}), 502

    return jsonify({"status": resp.status_code, "message": resp.text}), 200


@app.route("/upload", methods=["POST"])
def upload_image():
    """Compatibility with firmware that pushes JPEGs instead of being pulled."""
    data = request.get_data()

    if not data:
        return jsonify({"status": "error", "message": "No image received"}), 400

    if not data.startswith(JPEG_SOI):
        return jsonify(
            {"status": "error", "message": "Payload is not a JPEG image"}
        ), 400

    on_frame(data)

    filename = None

    if SAVE_UPLOADS:
        filename = f"upload_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.jpg"
        tmp = EVENT_DIR / (filename + ".tmp")
        tmp.write_bytes(data)
        tmp.replace(EVENT_DIR / filename)

    return jsonify({"status": "success", "filename": filename, "size": len(data)}), 200


# -------------------------------------------------------------------- main


def get_lan_ip():
    """Best-effort detection of this machine's LAN address."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    try:
        # selects the interface, sends nothing
        sock.connect(("10.255.255.255", 1))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


stream_client = None


def main():
    global stream_client

    lan_ip = get_lan_ip()

    print(f"Dashboard     : http://{lan_ip}:{PORT}/")
    print(
        f"Camera stream : {ESP_STREAM_URL or '(not set - waiting for /upload)'}")
    print(f"Motion        : {'on' if MOTION_ENABLED else 'off'} "
          f"(min area {MOTION_MIN_AREA}px, cooldown {MOTION_COOLDOWN:.0f}s)")
    print(f"Notifications : {notifier.describe()}", flush=True)

    cleanup_events()
    load_events()
    print(f"Saved pictures: {len(event_files())} in {EVENT_DIR} "
          f"(keep {MAX_EVENT_FILES} files / "
          f"{MAX_EVENT_DAYS:g} days)", flush=True)
    threading.Thread(target=cleanup_loop, daemon=True).start()

    if ESP_STREAM_URL:
        stream_client = StreamClient(ESP_STREAM_URL, on_frame, log=log)
        stream_client.start()

    # ------------------------------------------------ Telegram Bot

    telegram_token = os.getenv("TELEGRAM_BOT_TOKEN", "").strip()
    telegram_chat_id = os.getenv("TELEGRAM_CHAT_ID", "").strip()

    if telegram_token and telegram_chat_id:
        from telegram_bot import TelegramBot
        from system_controller import SystemController

        controller = SystemController(
            snapshot_callback=capture_snapshot,
            send_photo_callback=notifier.send_photo,
            set_motion_callback=set_motion_enabled,
            get_status_callback=get_system_status,
            log=log,
        )

        telegram_bot = TelegramBot(
            command_handler=controller.handle_command,
            log=log,
        )

        telegram_thread = threading.Thread(
            target=telegram_bot.run_forever,
            daemon=True,
            name="telegram-bot",
        )
        telegram_thread.start()

        log("[telegram] Bot started successfully")
    else:
        log("[telegram] Bot disabled: missing token or chat ID")

    # ------------------------------------------------ Flask Server

    app.run(host="0.0.0.0", port=PORT, debug=False, threaded=True)


if __name__ == "__main__":
    main()
