"""Every new camera frame goes through here: motion detection -> alert."""

import threading
import time
from datetime import datetime

import cv2

from detector import annotate
from event_store import add_event, cleanup_events
from runtime import detector, log, notifier, pipeline_lock, state, store
from settings import EVENT_DIR, MOTION_COOLDOWN, MOTION_MIN_FRAMES


def handle_motion(image, result):
    """Save the annotated snapshot and notify (called with the lock held)."""
    now = datetime.now()
    filename = f"motion_{now.strftime('%Y%m%d_%H%M%S')}.jpg"
    path = EVENT_DIR / filename

    label = now.strftime("%H:%M:%S %d/%m/%Y")
    picture = annotate(image, result.boxes, label)
    ok, encoded = cv2.imencode(".jpg", picture, [cv2.IMWRITE_JPEG_QUALITY, 90])

    if not ok:
        return

    path.write_bytes(encoded.tobytes())

    add_event(filename, now, result.area, "motion")

    log(f"[motion] {filename} (changed area {result.area}px)")

    caption = (
        f"Phát hiện chuyển động lúc {now.strftime('%H:%M:%S %d/%m/%Y')} "
        f"(vùng thay đổi: {result.area}px)"
    )

    # Network calls must not block the frame pipeline
    threading.Thread(
        target=notifier.send_photo,
        args=(str(path), caption),
        daemon=True,
    ).start()

    cleanup_events()


def on_frame(jpeg: bytes):
    """Called for every new JPEG frame (from the stream thread or /upload)."""
    store.put(jpeg)

    with pipeline_lock:
        state["frames"] += 1

        if not state["motion_enabled"]:
            return

        result, image = detector.process(jpeg)

        state["motion_now"] = result.motion

        if not result.motion:
            state["motion_frames"] = 0
            return

        state["motion_frames"] += 1

        if state["motion_frames"] < MOTION_MIN_FRAMES:
            return

        if time.time() - state["last_alert"] < MOTION_COOLDOWN:
            return

        state["last_alert"] = time.time()
        handle_motion(image, result)
