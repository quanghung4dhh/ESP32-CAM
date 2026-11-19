import socket
from datetime import datetime
from pathlib import Path

from flask import Flask, jsonify, request

app = Flask(__name__)

# Reject absurdly large bodies (a QVGA JPEG is only a few KB)
app.config["MAX_CONTENT_LENGTH"] = 5 * 1024 * 1024  # 5 MB

# Directory for storing captured images (always next to this file,
# regardless of the directory the server is started from)
IMAGE_DIR = Path(__file__).resolve().parent / "captured_images"
IMAGE_DIR.mkdir(exist_ok=True)

JPEG_SOI = b"\xff\xd8"  # every JPEG starts with these two bytes


@app.route("/health", methods=["GET"])
def health():
    # Handy to test connectivity from a phone/PC browser:
    # http://<server-ip>:5000/health
    return jsonify({"status": "ok"}), 200


@app.route("/upload", methods=["POST"])
def upload_image():
    data = request.get_data()

    if not data:
        return jsonify({
            "status": "error",
            "message": "No image received"
        }), 400

    if not data.startswith(JPEG_SOI):
        return jsonify({
            "status": "error",
            "message": "Payload is not a JPEG image"
        }), 400

    # Generate a unique filename
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    filename = f"capture_{timestamp}.jpg"

    image_path = IMAGE_DIR / filename
    tmp_path = image_path.with_suffix(".tmp")

    # Write to a temp file first, then rename: a reader never sees a
    # half-written image if the connection drops mid-way.
    tmp_path.write_bytes(data)
    tmp_path.replace(image_path)

    print(f"[{request.remote_addr}] Image received: {filename} "
          f"({len(data)} bytes)", flush=True)

    return jsonify({
        "status": "success",
        "filename": filename,
        "size": len(data)
    }), 200


def get_lan_ip():
    """Best-effort detection of this machine's LAN address."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # No packet is actually sent; this only selects the outgoing interface
        sock.connect(("10.255.255.255", 1))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


if __name__ == "__main__":
    lan_ip = get_lan_ip()
    print(f"Server LAN IP : {lan_ip}")
    print(f"Set in secrets.h -> #define IP_ADDRESS \"{lan_ip}\"")
    print(f"Health check  : http://{lan_ip}:5000/health", flush=True)

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False,
        threaded=True
    )
