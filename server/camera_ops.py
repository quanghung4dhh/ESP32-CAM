"""Operations on the running system: snapshot, motion on/off, status.

Used by the web routes and by the Telegram commands.
"""

import time
from datetime import datetime

import runtime
from event_store import add_event, cleanup_events
from runtime import detector, log, notifier, pipeline_lock, state, store
from settings import EVENT_DIR


def set_motion_enabled(enabled: bool) -> bool:
    """Enable or disable motion detection at runtime."""
    enabled = bool(enabled)

    with pipeline_lock:
        if state["motion_enabled"] == enabled:
            return True

        state["motion_enabled"] = enabled

        # Reset detection state when changing the mode.
        state["motion_now"] = False
        state["motion_frames"] = 0
        detector.reset()

    log(
        f"[motion] Detection {'enabled' if enabled else 'disabled'}"
    )
    return True


def capture_snapshot():
    """Save the latest fresh camera frame and return its path."""
    frame = store.latest()
    age = time.time() - store.timestamp if store.timestamp else None

    if frame is None or age is None or age > 5:
        return None

    now = datetime.now()
    filename = f"snapshot_{now.strftime('%Y%m%d_%H%M%S_%f')}.jpg"
    path = EVENT_DIR / filename
    tmp = EVENT_DIR / (filename + ".tmp")

    try:
        tmp.write_bytes(frame)
        tmp.replace(path)
    except OSError as exc:
        log(f"[snapshot] Save failed: {type(exc).__name__}")
        return None

    add_event(filename, now, None, "manual")
    cleanup_events()

    log(f"[snapshot] {filename}")
    return path


def get_system_status():
    """Return a snapshot of the current system status."""
    age = time.time() - store.timestamp if store.timestamp else None

    with pipeline_lock:
        motion_enabled = state["motion_enabled"]
        motion_now = state["motion_now"]
        frames = state["frames"]

    return {
        "camera_online": age is not None and age < 5,
        "stream_connected": bool(
            getattr(runtime.stream_client, "connected", False)
        ),
        "motion_enabled": motion_enabled,
        "motion_now": motion_now,
        "frames": frames,
        "notify": notifier.describe(),
    }
