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
state = {
    "motion_now": False,
    "motion_frames": 0,
    "last_alert": 0.0,
    "frames": 0,
}


def log(message):
    print(message, flush=True)


# --------------------------------------------------------------- processing


def cleanup_events():
    files = sorted(EVENT_DIR.glob("motion_*.jpg"))

    for old in files[:-MAX_EVENT_FILES] if MAX_EVENT_FILES > 0 else []:
        try:
            old.unlink()
        except OSError:
            pass


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

    events.appendleft(
        {
            "file": filename,
            "time": now.strftime("%H:%M:%S %d/%m/%Y"),
            "area": result.area,
        }
    )

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

        if not MOTION_ENABLED:
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
</style></head><body><div class="wrap">
<h2>ESP32-CAM Security</h2>
<img class="live" id="live" alt="Live stream">
<div class="card">
  <div id="status">Đang tải trạng thái...</div>
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
      '"><small>'+e.time+'</small></a>').join('')||'<small>Chưa có.</small>';
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


@app.route("/status")
def status():
    age = time.time() - store.timestamp if store.timestamp else None

    return jsonify(
        {
            "camera_online": age is not None and age < 5,
            "stream_connected": bool(
                getattr(stream_client, "connected", False)
            ),
            "motion_now": state["motion_now"],
            "frames": state["frames"],
            "notify": notifier.describe(),
            "events": list(events),
        }
    )


@app.route("/snapshot")
def snapshot():
    frame = store.latest()

    if frame is None:
        abort(404)

    return Response(frame, mimetype="image/jpeg")


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

    if ESP_STREAM_URL:
        stream_client = StreamClient(ESP_STREAM_URL, on_frame, log=log)
        stream_client.start()

    app.run(host="0.0.0.0", port=PORT, debug=False, threaded=True)


if __name__ == "__main__":
    main()
