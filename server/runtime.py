"""Objects shared by the whole server (one instance each).

Import them with `from runtime import store, notifier, ...`.
`stream_client` is set later by app.main(), so read it as `runtime.stream_client`.
"""

import threading

from detector import MotionDetector
from notifier import Notifier
from settings import MOTION_ENABLED, MOTION_MIN_AREA, MOTION_THRESHOLD
from stream_client import FrameStore

store = FrameStore()
notifier = Notifier(log=lambda m: print(m, flush=True))
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


def log(message):
    print(message, flush=True)


# Set by app.main() when ESP_STREAM_URL is configured
stream_client = None
