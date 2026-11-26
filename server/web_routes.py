"""HTTP routes of the dashboard and the JSON API."""

from datetime import datetime

import requests
from flask import (
    Blueprint,
    Response,
    abort,
    jsonify,
    render_template_string,
    request,
    send_from_directory,
)

import motion_pipeline
from camera_ops import capture_snapshot, get_system_status
from event_store import events_copy
from runtime import store
from settings import ESP_CONTROL_URL, EVENT_DIR, SAVE_UPLOADS
from web_page import PAGE

bp = Blueprint("web", __name__)

JPEG_SOI = b"\xff\xd8"


@bp.route("/")
def index():
    return render_template_string(PAGE, zoom_enabled=bool(ESP_CONTROL_URL))


@bp.route("/health")
def health():
    return jsonify({"status": "ok"}), 200


@bp.route("/status")
def status():
    result = get_system_status()
    result["events"] = events_copy()
    return jsonify(result)


@bp.route("/snapshot")
def snapshot():
    frame = store.latest()

    if frame is None:
        abort(404)

    return Response(frame, mimetype="image/jpeg")


@bp.route("/api/snapshot", methods=["POST"])
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


@bp.route("/stream")
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


@bp.route("/events/<path:name>")
def event_file(name):
    return send_from_directory(EVENT_DIR, name)


@bp.route("/api/zoom")
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


@bp.route("/upload", methods=["POST"])
def upload_image():
    """Compatibility with firmware that pushes JPEGs instead of being pulled."""
    data = request.get_data()

    if not data:
        return jsonify({"status": "error", "message": "No image received"}), 400

    if not data.startswith(JPEG_SOI):
        return jsonify(
            {"status": "error", "message": "Payload is not a JPEG image"}
        ), 400

    motion_pipeline.on_frame(data)

    filename = None

    if SAVE_UPLOADS:
        filename = f"upload_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.jpg"
        tmp = EVENT_DIR / (filename + ".tmp")
        tmp.write_bytes(data)
        tmp.replace(EVENT_DIR / filename)

    return jsonify({"status": "success", "filename": filename, "size": len(data)}), 200
