"""Send motion alerts to Telegram and/or Discord. Never raises."""

import json
import os

import requests

TIMEOUT = 20


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
        """Send one photo to every configured service."""
        if self.telegram_enabled:
            self._telegram(jpeg_path, caption)

        if self.discord_enabled:
            self._discord(jpeg_path, caption)

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

    def _discord(self, jpeg_path, caption):
        for attempt in (1, 2):
            try:
                with open(jpeg_path, "rb") as photo:
                    resp = requests.post(
                        self.discord_webhook,
                        data={"payload_json": json.dumps({"content": caption})},
                        files={"file": (os.path.basename(jpeg_path), photo)},
                        timeout=TIMEOUT,
                    )

                if resp.ok:
                    self.log("[notify] Discord: sent")
                    return

                self.log(f"[notify] Discord HTTP {resp.status_code}")
            except requests.RequestException as exc:
                self.log(
                    f"[notify] Discord error ({type(exc).__name__}), "
                    f"attempt {attempt}/2"
                )
