"""Pull the MJPEG stream of the ESP32-CAM and hand every JPEG frame to a callback."""

import threading
import time

import requests

JPEG_SOI = b"\xff\xd8"
JPEG_EOI = b"\xff\xd9"


class FrameStore:
    """Thread-safe holder of the most recent JPEG frame."""

    def __init__(self):
        self._cond = threading.Condition()
        self._frame = None
        self._counter = 0
        self.timestamp = 0.0

    def put(self, jpeg: bytes):
        with self._cond:
            self._frame = jpeg
            self._counter += 1
            self.timestamp = time.time()
            self._cond.notify_all()

    def latest(self):
        with self._cond:
            return self._frame

    def wait_next(self, last_counter: int, timeout: float = 10.0):
        """Block until a frame newer than last_counter exists.

        Returns (frame, counter) or (None, last_counter) on timeout.
        """
        with self._cond:
            if not self._cond.wait_for(
                lambda: self._counter != last_counter, timeout=timeout
            ):
                return None, last_counter
            return self._frame, self._counter

    @property
    def counter(self):
        with self._cond:
            return self._counter


class StreamClient(threading.Thread):
    """Background thread that reads a multipart MJPEG stream.

    Frames are found by their JPEG start/end markers, so it works with the
    ESP32 firmware stream (multipart/x-mixed-replace) without parsing headers.
    Reconnects automatically when the ESP32 drops the connection.
    """

    def __init__(self, url: str, on_frame, log=print):
        super().__init__(daemon=True, name="esp32-stream-client")
        self.url = url
        self.on_frame = on_frame
        self.log = log
        self.connected = False
        self.last_error = ""
        # NOTE: do not name this "_stop": threading.Thread already uses it
        self._halt = threading.Event()

    def stop(self):
        self._halt.set()

    def run(self):
        delay = 1.0

        while not self._halt.is_set():
            try:
                self._read_stream()
                delay = 1.0
            except Exception as exc:  # network errors, bad data, ...
                self.last_error = str(exc)
                self.log(f"[stream] {exc} - retrying in {delay:.0f}s")

            self.connected = False

            if self._halt.wait(delay):
                break

            delay = min(delay * 2, 10.0)

    def _read_stream(self):
        self.log(f"[stream] connecting to {self.url}")

        # (connect timeout, read timeout): a stalled ESP32 triggers a reconnect
        with requests.get(self.url, stream=True, timeout=(5, 10)) as resp:
            resp.raise_for_status()
            self.connected = True
            self.last_error = ""
            self.log("[stream] connected")

            buf = b""

            for chunk in resp.iter_content(chunk_size=4096):
                if self._halt.is_set():
                    return

                if not chunk:
                    continue

                buf += chunk

                while True:
                    start = buf.find(JPEG_SOI)

                    if start < 0:
                        # no frame start in the buffer: keep only a tail
                        buf = buf[-1:]
                        break

                    end = buf.find(JPEG_EOI, start + 2)

                    if end < 0:
                        # incomplete frame: drop garbage before the start
                        buf = buf[start:]

                        if len(buf) > 2_000_000:  # sanity limit
                            buf = b""

                        break

                    frame = buf[start : end + 2]
                    buf = buf[end + 2 :]

                    try:
                        self.on_frame(frame)
                    except Exception as exc:
                        self.log(f"[stream] frame handler error: {exc}")

        raise ConnectionError("stream closed by the camera")
