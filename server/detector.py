"""Motion detection by frame differencing (OpenCV)."""

from dataclasses import dataclass, field

import cv2
import numpy as np


@dataclass
class MotionResult:
    motion: bool = False
    area: int = 0  # changed pixels (after threshold + dilate)
    boxes: list = field(default_factory=list)  # [(x, y, w, h), ...]


class MotionDetector:
    """Compares every frame with a slowly updated background image.

    Steps: gray -> blur -> |frame - background| -> threshold -> dilate ->
    contours. Using a running-average background (instead of only the
    previous frame) avoids missing slow movement and ignores flicker.
    """

    def __init__(
        self,
        min_area: int = 600,
        threshold: int = 25,
        blur: int = 21,
        background_alpha: float = 0.05,
        warmup_frames: int = 10,
        lighting_ratio: float = 0.6,
    ):
        self.lighting_ratio = lighting_ratio
        self.min_area = min_area
        self.threshold = threshold
        self.blur = blur if blur % 2 == 1 else blur + 1  # must be odd
        self.alpha = background_alpha
        self.warmup_frames = warmup_frames

        self._background = None
        self._seen = 0

    def reset(self):
        self._background = None
        self._seen = 0

    def process(self, jpeg: bytes):
        """Return (MotionResult, decoded BGR image or None)."""
        image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)

        if image is None:  # corrupted frame
            return MotionResult(), None

        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (self.blur, self.blur), 0)

        if self._background is None or self._background.shape != gray.shape:
            self._background = gray.astype("float32")
            self._seen = 1
            return MotionResult(), image

        self._seen += 1

        diff = cv2.absdiff(gray, cv2.convertScaleAbs(self._background))
        _, mask = cv2.threshold(diff, self.threshold, 255, cv2.THRESH_BINARY)
        # A global change (auto exposure / lights switched on) changes most of
        # the picture at once: that is not motion. Re-learn the background.
        if cv2.countNonZero(mask) > self.lighting_ratio * mask.size:
            self._background = gray.astype("float32")
            return MotionResult(), image

        mask = cv2.dilate(mask, None, iterations=2)

        contours, _ = cv2.findContours(
            mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
        )

        boxes = []
        area = 0

        for contour in contours:
            contour_area = cv2.contourArea(contour)

            if contour_area >= self.min_area:
                boxes.append(cv2.boundingRect(contour))
                area += int(contour_area)

        result = MotionResult(
            motion=bool(boxes) and self._seen > self.warmup_frames,
            area=area,
            boxes=boxes,
        )

        # Update the background. While something moves, adapt much more
        # slowly so a person standing still is not absorbed immediately.
        alpha = self.alpha / 5 if boxes else self.alpha
        cv2.accumulateWeighted(gray, self._background, alpha)

        return result, image


def annotate(image, boxes, label=""):
    """Draw the detected boxes (and an optional label) on a copy of image."""
    out = image.copy()

    for x, y, w, h in boxes:
        cv2.rectangle(out, (x, y), (x + w, y + h), (0, 0, 255), 2)

    if label:
        cv2.putText(
            out, label, (6, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.5,
            (0, 255, 255), 1, cv2.LINE_AA,
        )

    return out
