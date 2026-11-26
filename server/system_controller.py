
"""Central controller for ESP32-CAM system commands."""


class SystemController:
    def __init__(
        self,
        snapshot_callback,
        send_photo_callback,
        set_motion_callback,
        get_status_callback,
        log=print,
    ):
        """
        Callbacks are injected from app.py to avoid circular imports.

        snapshot_callback() -> image path or None
        send_photo_callback(path, caption, only=None) -> {service: delivered}
        set_motion_callback(enabled: bool) -> bool
        get_status_callback() -> dict
        """
        self.snapshot_callback = snapshot_callback
        self.send_photo_callback = send_photo_callback
        self.set_motion_callback = set_motion_callback
        self.get_status_callback = get_status_callback
        self.log = log

    # ---- plain actions, used by the Discord bot (Telegram uses handle_command)

    def take_snapshot(self):
        """Save a picture of the camera; return its path or None (offline)."""
        return self.snapshot_callback()

    def set_motion(self, enabled):
        """Turn motion detection on/off; return the reply text."""
        return self._set_motion(enabled)

    def status_text(self):
        """Return the system status as text."""
        return self._status()

    def handle_command(self, command, message=None):
        """Dispatch a Telegram command to the corresponding function."""

        if command == "/start":
            return self._help()

        if command == "/snapshot":
            return self._snapshot()

        if command == "/on":
            return self._set_motion(True)

        if command == "/off":
            return self._set_motion(False)

        if command == "/status":
            return self._status()

        return (
            "Lenh khong hop le.\n"
            "Gui /start de xem danh sach lenh."
        )

    @staticmethod
    def _help():
        return (
            "ESP32-CAM Security Camera\n\n"
            "/start - Huong dan su dung\n"
            "/snapshot - Chup anh hien tai\n"
            "/on - Bat phat hien chuyen dong\n"
            "/off - Tat phat hien chuyen dong\n"
            "/status - Xem trang thai he thong"
        )

    def _snapshot(self):
        image_path = self.snapshot_callback()

        if image_path is None:
            return "Khong the chup anh: camera offline hoac chua co frame."

        # Answer in the Telegram chat only (not also in Discord)
        results = self.send_photo_callback(
            str(image_path),
            "Anh chup thu cong tu ESP32-CAM",
            only="telegram",
        )

        if not (results or {}).get("telegram"):
            self.log("[controller] Snapshot saved but not delivered")
            return (
                "Da chup anh nhung khong gui duoc qua Telegram. "
                "Anh van duoc luu tren server."
            )

        self.log("[controller] Snapshot sent")
        return "Da chup va gui anh thanh cong."

    def _set_motion(self, enabled):
        success = self.set_motion_callback(enabled)

        if not success:
            return "Khong the thay doi trang thai Motion Detection."

        status = "BAT" if enabled else "TAT"
        self.log(f"[controller] Motion Detection: {status}")

        return f"Da {status} Motion Detection."

    def _status(self):
        status = self.get_status_callback()

        camera = "ONLINE" if status.get("camera_online") else "OFFLINE"
        stream = "CONNECTED" if status.get(
            "stream_connected") else "DISCONNECTED"
        motion = "ON" if status.get("motion_enabled") else "OFF"

        return (
            "ESP32-CAM SYSTEM STATUS\n\n"
            f"Camera: {camera}\n"
            f"Stream: {stream}\n"
            f"Motion Detection: {motion}\n"
            f"Frames received: {status.get('frames', 0)}\n"
            f"Notification: {status.get('notify', 'None')}"
        )
