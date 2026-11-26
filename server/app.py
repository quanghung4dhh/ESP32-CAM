"""ESP32-CAM security server (entry point: python app.py).

* pulls the MJPEG stream of the ESP32-CAM (or receives JPEGs on /upload),
* detects motion with frame differencing,
* saves a snapshot and sends it to Telegram / Discord,
* re-streams the video to any number of browsers (dashboard on "/").

All settings come from environment variables - see README.md.

Modules:
  settings.py         environment variables / .env
  runtime.py          objects shared by everything (frame store, notifier, ...)
  event_store.py      saved pictures + recent events list
  motion_pipeline.py  frame -> motion detection -> alert
  camera_ops.py       snapshot, motion on/off, status
  web_routes.py       HTTP routes   (HTML page in web_page.py)
  telegram_bot.py     Telegram long polling, system_controller.py: commands
  notifier.py         send pictures to Telegram / Discord
  detector.py         motion detection (OpenCV)
  stream_client.py    pulls the ESP32-CAM stream
"""

import os
import socket
import threading

from flask import Flask

import runtime
from camera_ops import (
    capture_snapshot,
    get_system_status,
    set_motion_enabled,
)
from event_store import cleanup_events, cleanup_loop, event_files, load_events
from motion_pipeline import on_frame
from runtime import log, notifier
from settings import (
    ESP_STREAM_URL,
    EVENT_DIR,
    MAX_EVENT_DAYS,
    MAX_EVENT_FILES,
    MOTION_COOLDOWN,
    MOTION_ENABLED,
    MOTION_MIN_AREA,
    PORT,
)
from stream_client import StreamClient
from web_routes import bp

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 5 * 1024 * 1024  # 5 MB per upload
app.register_blueprint(bp)


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


def start_telegram_bot():
    """Start the Telegram command listener if token and chat id are set."""
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


def main():
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
        runtime.stream_client = StreamClient(ESP_STREAM_URL, on_frame, log=log)
        runtime.stream_client.start()

    start_telegram_bot()

    app.run(host="0.0.0.0", port=PORT, debug=False, threaded=True)


if __name__ == "__main__":
    main()
