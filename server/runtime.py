"""Objects shared by the whole server (one instance each).

Import them with `from runtime import store, notifier, ...`.
`stream_client` is set later by app.main(), so read it as `runtime.stream_client`.
"""

import sys
import threading

from detector import MotionDetector
from notifier import Notifier
from settings import MOTION_ENABLED, MOTION_MIN_AREA, MOTION_THRESHOLD
from stream_client import FrameStore


def log(message):
    """Print one log line.

    One write() call per line: print() writes the text and the newline
    separately, so lines from different threads could be glued together.
    """
    sys.stdout.write(f"{message}\n")
    sys.stdout.flush()


store = FrameStore()
notifier = Notifier(log=log)
detector = MotionDetector(
    min_area=MOTION_MIN_AREA, threshold=MOTION_THRESHOLD
)

pipeline_lock = threading.Lock()
state = {
    "motion_enabled": MOTION_ENABLED,
    "motion_now": False,
    "motion_frames": 0,
    "last_alert": 0.0,
    "frames": 0,
}


# Set by app.main() when ESP_STREAM_URL is configured
stream_client = None
