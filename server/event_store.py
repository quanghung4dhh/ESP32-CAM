"""Saved pictures on disk and the list of recent events shown on the dashboard."""

import threading
import time
from collections import deque
from datetime import datetime

from settings import EVENT_DIR, MAX_EVENT_DAYS, MAX_EVENT_FILES

events = deque(maxlen=24)  # newest first
events_lock = threading.Lock()

TIME_FORMAT = "%H:%M:%S %d/%m/%Y"
EVENT_PATTERNS = ("motion_*.jpg", "snapshot_*.jpg")  # motion alerts, manual


def _mtime(path):
    try:
        return path.stat().st_mtime
    except OSError:
        return 0.0


def event_files():
    """Saved pictures (motion + manual), oldest first."""
    files = []

    for pattern in EVENT_PATTERNS:
        files.extend(EVENT_DIR.glob(pattern))

    return sorted(files, key=_mtime)


def cleanup_events():
    """Delete pictures beyond MAX_EVENT_FILES or older than MAX_EVENT_DAYS."""
    files = event_files()
    doomed = set()

    if MAX_EVENT_FILES > 0 and len(files) > MAX_EVENT_FILES:
        doomed.update(files[:-MAX_EVENT_FILES])

    if MAX_EVENT_DAYS > 0:
        limit = time.time() - MAX_EVENT_DAYS * 86400
        doomed.update(p for p in files if _mtime(p) < limit)

    for old in doomed:
        try:
            old.unlink()
        except OSError:
            pass


def add_event(filename, when, area=None, kind="motion"):
    """Remember a saved picture for the dashboard (newest first)."""
    entry = {
        "file": filename,
        "time": when.strftime(TIME_FORMAT),
        "area": area,
        "kind": kind,  # "motion" or "manual"
    }

    with events_lock:
        for old in [e for e in events if e["file"] == filename]:
            events.remove(old)  # same-second duplicate

        events.appendleft(entry)


def load_events():
    """Rebuild the dashboard list from the pictures already on disk."""
    for path in event_files()[-events.maxlen:]:
        kind = "manual" if path.name.startswith("snapshot_") else "motion"

        try:
            # motion_20261125_113045.jpg -> 20261125_113045
            when = datetime.strptime(
                path.name.split("_", 1)[1][:15], "%Y%m%d_%H%M%S"
            )
        except (IndexError, ValueError):
            when = datetime.fromtimestamp(_mtime(path))

        add_event(path.name, when, None, kind)


def cleanup_loop():
    """Age-based cleanup must also run when nothing moves in front of the camera."""
    while True:
        time.sleep(3600)
        cleanup_events()


def events_copy():
    with events_lock:
        return list(events)
