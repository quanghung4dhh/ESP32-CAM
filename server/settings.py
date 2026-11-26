"""All settings of the server, read from environment variables (and .env).

See README.md for the meaning of every variable.
"""

import os
from pathlib import Path
from urllib.parse import urlparse

try:  # optional: load settings from a .env file next to this script
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).resolve().parent / ".env")
except ImportError:
    pass

# ------------------------------------------------------------ env helpers


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


# --------------------------------------------------------------- settings

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
