"""Send motion alerts to Telegram and/or Discord. Never raises.

Run `python notifier.py --test` to send a test picture to every configured
service without waiting for motion.
"""

import json
import os
import threading
import time

import requests

TIMEOUT = 20

# Discord webhooks are rate limited (HTTP 429 + retry_after). Wait at most
# this long before the next attempt.
DISCORD_MAX_WAIT = 10.0
DISCORD_ATTEMPTS = 3
DISCORD_HOSTS = ("https://discord.com/api/webhooks/",
                 "https://discordapp.com/api/webhooks/")


class Notifier:
    def __init__(self, log=print):
        self.log = log

        self.telegram_token = os.getenv("TELEGRAM_BOT_TOKEN", "").strip()
        self.telegram_chat = os.getenv("TELEGRAM_CHAT_ID", "").strip()
        # Overridable for tests / proxies
        self.telegram_api = os.getenv(
            "TELEGRAM_API_BASE", "https://api.telegram.org"
        ).rstrip("/")
        self.discord_webhook = os.getenv("DISCORD_WEBHOOK_URL", "").strip()
        self.discord_name = os.getenv("DISCORD_USERNAME", "ESP32-CAM").strip()

        if self.discord_webhook and not self.discord_webhook.startswith(
            DISCORD_HOSTS
        ):
            # Only a hint: custom URLs are allowed (tests, proxies).
            # The URL itself is a secret and is never printed.
            self.log(
                "[notify] Discord: DISCORD_WEBHOOK_URL does not look like "
                "https://discord.com/api/webhooks/<id>/<token>"
            )

    @property
    def telegram_enabled(self):
        return bool(self.telegram_token and self.telegram_chat)

    @property
    def discord_enabled(self):
        return bool(self.discord_webhook)

    def describe(self):
        targets = []

        if self.telegram_enabled:
            targets.append("Telegram")

        if self.discord_enabled:
            targets.append("Discord")

        return ", ".join(targets) if targets else "none (images are only saved)"

    def send_photo(self, jpeg_path: str, caption: str):
        """Send one photo to every configured service.

        Each service runs in its own thread, so a slow or failing service
        does not delay the others. Returns when all of them are done.
        """
        jobs = []

        if self.telegram_enabled:
            jobs.append(self._telegram)

        if self.discord_enabled:
            jobs.append(self._discord)

        threads = [
            threading.Thread(target=job, args=(jpeg_path, caption), daemon=True)
            for job in jobs
        ]

        for thread in threads:
            thread.start()

        for thread in threads:
            thread.join()

    # ------------------------------------------------------------ Telegram

    def _telegram(self, jpeg_path, caption):
        url = f"{self.telegram_api}/bot{self.telegram_token}/sendPhoto"

        for attempt in (1, 2):
            try:
                with open(jpeg_path, "rb") as photo:
                    resp = requests.post(
                        url,
                        data={"chat_id": self.telegram_chat, "caption": caption},
                        files={"photo": photo},
                        timeout=TIMEOUT,
                    )

                if resp.ok:
                    self.log("[notify] Telegram: sent")
                    return

                # Do not log the URL: it contains the bot token
                self.log(
                    f"[notify] Telegram HTTP {resp.status_code}: "
                    f"{resp.text[:150]}"
                )
            except requests.RequestException as exc:
                self.log(
                    f"[notify] Telegram error ({type(exc).__name__}), "
                    f"attempt {attempt}/2"
                )

    # ------------------------------------------------------------- Discord

    @staticmethod
    def _retry_after(resp):
        """Seconds Discord asks us to wait after HTTP 429."""
        try:
            wait = float(resp.json().get("retry_after"))
        except (ValueError, TypeError, AttributeError):
            try:
                wait = float(resp.headers.get("Retry-After"))
            except (ValueError, TypeError):
                wait = 2.0

        return min(max(wait, 0.0), DISCORD_MAX_WAIT)

    def _discord(self, jpeg_path, caption):
        payload = {
            "content": caption[:2000],  # Discord message limit
            "username": self.discord_name,
            "allowed_mentions": {"parse": []},  # never ping @everyone etc.
        }

        for attempt in range(1, DISCORD_ATTEMPTS + 1):
            try:
                with open(jpeg_path, "rb") as photo:
                    resp = requests.post(
                        self.discord_webhook,
                        data={"payload_json": json.dumps(payload)},
                        files={"file": (os.path.basename(jpeg_path), photo)},
                        timeout=TIMEOUT,
                    )
            except requests.RequestException as exc:
                # Do not log the exception text: it can contain the URL
                self.log(
                    f"[notify] Discord error ({type(exc).__name__}), "
                    f"attempt {attempt}/{DISCORD_ATTEMPTS}"
                )
                continue

            if resp.ok:
                self.log("[notify] Discord: sent")
                return

            if resp.status_code == 429:
                wait = self._retry_after(resp)
                self.log(
                    f"[notify] Discord rate limited, retry in {wait:.1f}s "
                    f"(attempt {attempt}/{DISCORD_ATTEMPTS})"
                )
                time.sleep(wait)
                continue

            if 400 <= resp.status_code < 500:
                # Wrong / deleted webhook, file too large, ... retrying is useless
                hint = ""

                if resp.status_code in (401, 404):
                    hint = " - webhook URL is wrong or was deleted"
                elif resp.status_code == 413:
                    hint = " - picture is too large"

                self.log(
                    f"[notify] Discord HTTP {resp.status_code}{hint}: "
                    f"{resp.text[:150]}"
                )
                return

            self.log(
                f"[notify] Discord HTTP {resp.status_code}, "
                f"attempt {attempt}/{DISCORD_ATTEMPTS}"
            )

        self.log("[notify] Discord: giving up")


# ------------------------------------------------------------------- test


def _send_test():
    import tempfile
    from datetime import datetime
    from pathlib import Path

    import cv2
    import numpy as np

    try:  # same .env loading as app.py
        from dotenv import load_dotenv

        load_dotenv(Path(__file__).resolve().parent / ".env")
    except ImportError:
        pass

    notifier = Notifier()
    print(f"Configured services: {notifier.describe()}")

    if not (notifier.telegram_enabled or notifier.discord_enabled):
        print("Nothing to test: set DISCORD_WEBHOOK_URL and/or "
              "TELEGRAM_BOT_TOKEN + TELEGRAM_CHAT_ID (see README.md).")
        return 1

    picture = np.full((240, 320, 3), 40, np.uint8)
    cv2.putText(picture, "ESP32-CAM test", (40, 125),
                cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)
    ok, encoded = cv2.imencode(".jpg", picture)

    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "test.jpg"
        path.write_bytes(encoded.tobytes())
        notifier.send_photo(
            str(path),
            f"Tin nhắn thử từ ESP32-CAM ({datetime.now():%H:%M:%S %d/%m/%Y})",
        )

    return 0


if __name__ == "__main__":
    import sys

    if "--test" in sys.argv[1:]:
        sys.exit(_send_test())

    print("Usage: python notifier.py --test")
    sys.exit(2)
