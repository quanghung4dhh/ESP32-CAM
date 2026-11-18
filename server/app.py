from flask import Flask, request, jsonify
from pathlib import Path
from datetime import datetime

app = Flask(__name__)

# Directory for storing captured images
IMAGE_DIR = Path("captured_images")
IMAGE_DIR.mkdir(exist_ok=True)


@app.route("/upload", methods=["POST"])
def upload_image():

    if not request.data:
        return jsonify({
            "status": "error",
            "message": "No image received"
        }), 400

    # Generate a unique filename
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    filename = f"capture_{timestamp}.jpg"

    image_path = IMAGE_DIR / filename

    # Save JPEG data
    image_path.write_bytes(request.data)

    print(f"Image received: {filename}")
    print(f"Image size: {len(request.data)} bytes")

    return jsonify({
        "status": "success",
        "filename": filename,
        "size": len(request.data)
    }), 200


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False
    )
