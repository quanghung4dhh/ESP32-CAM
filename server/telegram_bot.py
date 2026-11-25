
"""Receive Telegram commands using long polling."""

import os
import threading
import time

import requests
import json

POLL_TIMEOUT = 25
REQUEST_TIMEOUT = 35


class TelegramBot:
    def __init__(self, command_handler=None, log=print):
        self.token = os.getenv("TELEGRAM_BOT_TOKEN", "").strip()
        self.allowed_chat_id = os.getenv("TELEGRAM_CHAT_ID", "").strip()
        self.api_base = os.getenv(
            "TELEGRAM_API_BASE", "https://api.telegram.org"
        ).rstrip("/")

        self.log = log
        self.command_handler = command_handler
        self.stop_event = threading.Event()
        self.offset = None

        if not self.token:
            raise ValueError("Missing TELEGRAM_BOT_TOKEN")

        if not self.allowed_chat_id:
            raise ValueError("Missing TELEGRAM_CHAT_ID")

        self.base_url = f"{self.api_base}/bot{self.token}"

    def send_message(self, chat_id, text):
        """Send a text message to a Telegram chat."""
        url = f"{self.base_url}/sendMessage"

        try:
            response = requests.post(
                url,
                json={
                    "chat_id": chat_id,
                    "text": text,
                },
                timeout=REQUEST_TIMEOUT,
            )
            response.raise_for_status()

            data = response.json()
            if not data.get("ok"):
                self.log("[telegram] sendMessage was rejected")
                return False

            return True

        except requests.RequestException as exc:
            self.log(
                f"[telegram] sendMessage error: {type(exc).__name__}"
            )
            return False

    def get_updates(self):
        """Fetch new updates from Telegram."""
        url = f"{self.base_url}/getUpdates"

        params = {
            "timeout": POLL_TIMEOUT,
            "allowed_updates": json.dumps(["message"]),

        }

        if self.offset is not None:
            params["offset"] = self.offset

        response = requests.get(
            url,
            params=params,
            timeout=REQUEST_TIMEOUT,
        )
        response.raise_for_status()

        data = response.json()

        if not data.get("ok"):
            raise RuntimeError("Telegram getUpdates was rejected")

        return data.get("result", [])

    @staticmethod
    def parse_command(message):
        """Extract a command such as /snapshot from a message."""
        text = message.get("text", "").strip()

        if not text.startswith("/"):
            return None

        command = text.split()[0]
        command = command.split("@")[0].lower()

        return command

    def handle_update(self, update):
        """Validate the sender and dispatch a command."""
        message = update.get("message")
        if not message:
            return

        chat = message.get("chat", {})
        chat_id = str(chat.get("id", ""))

        # Only the configured Telegram chat may control the system.
        if chat_id != self.allowed_chat_id:
            self.log("[telegram] Ignored message from unauthorized chat")
            return

        command = self.parse_command(message)
        if not command:
            return

        self.log(f"[telegram] Received command: {command}")

        if self.command_handler:
            reply = self.command_handler(command, message)
        else:
            reply = f"Da nhan lenh: {command}"

        if reply:
            self.send_message(chat_id, reply)

    def run_forever(self):
        """Continuously poll Telegram for new messages."""
        self.log("[telegram] Bot polling started")

        while not self.stop_event.is_set():
            try:
                updates = self.get_updates()

                for update in updates:
                    if self.stop_event.is_set():
                        break

                    update_id = update.get("update_id")
                    if update_id is not None:
                        self.offset = update_id + 1

                    self.handle_update(update)

            except requests.RequestException as exc:
                self.log(
                    f"[telegram] Polling network error: "
                    f"{type(exc).__name__}"
                )
                self.stop_event.wait(3)

            except (ValueError, RuntimeError) as exc:
                self.log(f"[telegram] Polling error: {exc}")
                self.stop_event.wait(3)

            except Exception as exc:
                self.log(
                    f"[telegram] Unexpected error: {type(exc).__name__}"
                )
                self.stop_event.wait(3)

        self.log("[telegram] Bot polling stopped")

    def stop(self):
        """Request the polling loop to stop."""
        self.stop_event.set()


def main():
    # Load the same .env file used by the Python Server.
    from pathlib import Path
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).resolve().parent / ".env")

    bot = TelegramBot()
    try:
        bot.run_forever()
    except KeyboardInterrupt:
        bot.stop()


if __name__ == "__main__":
    main()
