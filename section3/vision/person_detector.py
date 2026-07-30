#!/usr/bin/env python3
"""Smart Guard Section 3 vision relay using MobileNet SSD.

The vision process receives length-prefixed JPEG frames from the physical-host
camera agent, performs human detection, draws the required metadata, forwards
annotated JPEG frames to the C web core, and atomically publishes detection and
heartbeat files.  A C thermal controller changes processing FPS and resolution
through a small runtime JSON control file.
"""

from __future__ import annotations

import json
import os
import signal
import socket
import struct
import sys
import time
from collections import deque
from datetime import datetime
from pathlib import Path
from typing import Iterable

import cv2
import numpy as np


STUDENT_ID = os.getenv("SMART_GUARD_STUDENT_ID", "401102553")
LISTEN_HOST = os.getenv("SMART_GUARD_VISION_LISTEN_HOST", "0.0.0.0")
LISTEN_PORT = int(os.getenv("SMART_GUARD_VISION_LISTEN_PORT", "9200"))
FORWARD_HOST = os.getenv("SMART_GUARD_VISION_FORWARD_HOST", "127.0.0.1")
FORWARD_PORT = int(os.getenv("SMART_GUARD_VISION_FORWARD_PORT", "9100"))
PERSON_FILE = Path(os.getenv("SMART_GUARD_PERSON_FILE", "/run/smart-guard/person_count"))
EVENT_FILE = Path(os.getenv("SMART_GUARD_EVENT_FILE", "/run/smart-guard/detection_event.json"))
EVENT_DIR = Path(os.getenv("SMART_GUARD_EVENT_DIR", "/run/smart-guard/events"))
STATUS_FILE = Path(os.getenv("SMART_GUARD_VISION_STATUS_FILE", "/run/smart-guard/vision_status.json"))
CONTROL_FILE = Path(os.getenv("SMART_GUARD_VISION_CONTROL_FILE", "/run/smart-guard/section3_vision_control.json"))
GUARD_STATE_FILE = Path(os.getenv("SMART_GUARD_GUARD_STATE_FILE", "/var/lib/smart-guard/guard_mode"))
JPEG_QUALITY = int(os.getenv("SMART_GUARD_VISION_JPEG_QUALITY", "85"))
DEFAULT_DETECTION_WIDTH = int(os.getenv("SMART_GUARD_NORMAL_DETECTION_WIDTH", os.getenv("SMART_GUARD_VISION_DETECTION_WIDTH", "640")))
DEFAULT_MAX_FPS = int(os.getenv("SMART_GUARD_NORMAL_MAX_FPS", "0"))
DEFAULT_OUTPUT_WIDTH = int(os.getenv("SMART_GUARD_NORMAL_OUTPUT_WIDTH", "0"))
VISION_BACKEND = os.getenv("SMART_GUARD_VISION_BACKEND", "mobilenet_ssd").strip().lower()
MODEL_DIR = Path(os.getenv("SMART_GUARD_VISION_MODEL_DIR", "/opt/smart-guard/section3/vision/models"))
MOBILENET_PROTOTXT = Path(os.getenv("SMART_GUARD_MOBILENET_PROTOTXT", str(MODEL_DIR / "deploy.prototxt")))
MOBILENET_MODEL = Path(os.getenv("SMART_GUARD_MOBILENET_MODEL", str(MODEL_DIR / "mobilenet_iter_73000.caffemodel")))
DNN_CONFIDENCE = float(os.getenv("SMART_GUARD_VISION_DNN_CONFIDENCE", "0.42"))
DNN_NMS_THRESHOLD = float(os.getenv("SMART_GUARD_VISION_DNN_NMS_THRESHOLD", "0.35"))
DNN_MIN_BOX_HEIGHT = int(os.getenv("SMART_GUARD_VISION_DNN_MIN_BOX_HEIGHT", "48"))
DNN_MIN_AREA_RATIO = float(os.getenv("SMART_GUARD_VISION_DNN_MIN_AREA_RATIO", "0.006"))
DETECTION_HOLD_FRAMES = int(os.getenv("SMART_GUARD_DETECTION_HOLD_FRAMES", "2"))
LOW_LIGHT_ENHANCE = os.getenv("SMART_GUARD_VISION_LOW_LIGHT_ENHANCE", "1").strip().lower() in {"1", "true", "yes", "on"}
LOW_LIGHT_THRESHOLD = float(os.getenv("SMART_GUARD_VISION_LOW_LIGHT_THRESHOLD", "75"))
HOG_HIT_THRESHOLD = float(os.getenv("SMART_GUARD_VISION_HOG_THRESHOLD", "0.0"))
HOG_SCALE = float(os.getenv("SMART_GUARD_VISION_HOG_SCALE", "1.03"))
MAX_FRAME_BYTES = int(os.getenv("SMART_GUARD_VISION_MAX_FRAME_BYTES", str(8 * 1024 * 1024)))
EVENT_KEEP_COUNT = int(os.getenv("SMART_GUARD_EVENT_KEEP_COUNT", "100"))
ZERO_CONFIRM_FRAMES = int(os.getenv("SMART_GUARD_ZERO_CONFIRM_FRAMES", "2"))
CONTROL_REFRESH_SECONDS = float(os.getenv("SMART_GUARD_VISION_CONTROL_REFRESH_SECONDS", "1.0"))

RUNNING = True


def log(message: str) -> None:
    print(f"[vision] {message}", flush=True)


def handle_signal(signum: int, _frame: object) -> None:
    global RUNNING
    log(f"signal {signum} received; stopping")
    RUNNING = False


signal.signal(signal.SIGINT, handle_signal)
signal.signal(signal.SIGTERM, handle_signal)


def atomic_write_bytes(path: Path, payload: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with open(temporary, "wb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    os.chmod(temporary, mode)
    os.replace(temporary, path)


def atomic_write_text(path: Path, text: str, mode: int = 0o644) -> None:
    atomic_write_bytes(path, text.encode("utf-8"), mode)


def recv_exact(sock: socket.socket, size: int) -> bytes | None:
    data = bytearray()
    while RUNNING and len(data) < size:
        try:
            chunk = sock.recv(size - len(data))
        except socket.timeout:
            continue
        except OSError:
            return None
        if not chunk:
            return None
        data.extend(chunk)
    return bytes(data) if len(data) == size else None


class FrameForwarder:
    """Persistent best-effort TCP sender to the C web server."""

    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self.sock: socket.socket | None = None
        self.next_retry = 0.0

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def _connect(self) -> bool:
        now = time.monotonic()
        if now < self.next_retry:
            return False
        self.close()
        try:
            sock = socket.create_connection((self.host, self.port), timeout=3.0)
            sock.settimeout(3.0)
            self.sock = sock
            log(f"connected annotated-frame output to {self.host}:{self.port}")
            return True
        except OSError as error:
            self.next_retry = now + 2.0
            log(f"cannot connect to C web core: {error}")
            return False

    def send(self, jpeg: bytes) -> bool:
        if self.sock is None and not self._connect():
            return False
        assert self.sock is not None
        packet = struct.pack("!I", len(jpeg)) + jpeg
        try:
            self.sock.sendall(packet)
            return True
        except OSError as error:
            log(f"annotated-frame forwarding failed: {error}")
            self.close()
            self.next_retry = time.monotonic() + 1.0
            return False


class RuntimeProfile:
    def __init__(self) -> None:
        self.mode = "normal"
        self.max_fps = max(0, DEFAULT_MAX_FPS)
        self.detection_width = max(160, DEFAULT_DETECTION_WIDTH)
        self.output_width = max(0, DEFAULT_OUTPUT_WIDTH)
        self.temperature_c: float | None = None
        self.updated_at = "startup"
        self.file_mtime_ns = -1

    def load_if_changed(self) -> bool:
        try:
            file_mtime_ns = CONTROL_FILE.stat().st_mtime_ns
        except OSError:
            return False
        if file_mtime_ns == self.file_mtime_ns:
            return False
        try:
            payload = json.loads(CONTROL_FILE.read_text(encoding="utf-8"))
            mode = str(payload.get("mode", "normal"))
            max_fps = int(payload.get("max_fps", DEFAULT_MAX_FPS))
            detection_width = int(payload.get("detection_width", DEFAULT_DETECTION_WIDTH))
            output_width = int(payload.get("output_width", DEFAULT_OUTPUT_WIDTH))
            temperature = payload.get("temperature_c")
            self.mode = "thermal" if mode == "thermal" else "normal"
            self.max_fps = max(0, min(max_fps, 120))
            self.detection_width = max(160, min(detection_width, 4096))
            self.output_width = max(0, min(output_width, 4096))
            self.temperature_c = float(temperature) if isinstance(temperature, (int, float)) else None
            self.updated_at = str(payload.get("updated_at", "unknown"))
            self.file_mtime_ns = file_mtime_ns
            log(
                "runtime profile changed "
                f"mode={self.mode} max_fps={self.max_fps} "
                f"detection_width={self.detection_width} output_width={self.output_width}"
            )
            return True
        except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
            log(f"invalid vision control file ignored: {error}")
            return False


def read_guard_armed() -> bool:
    try:
        text = GUARD_STATE_FILE.read_text(encoding="utf-8").strip().lower()
    except OSError:
        return False
    return text in {"1", "true", "yes", "on", "armed"}


def resize_output_frame(frame: np.ndarray, output_width: int) -> np.ndarray:
    if output_width <= 0 or frame.shape[1] <= output_width:
        return frame
    scale = output_width / float(frame.shape[1])
    output_height = max(1, int(round(frame.shape[0] * scale)))
    return cv2.resize(frame, (output_width, output_height), interpolation=cv2.INTER_AREA)


def resize_for_detection(frame: np.ndarray, detection_width: int) -> tuple[np.ndarray, float]:
    height, width = frame.shape[:2]
    if detection_width <= 0 or width <= detection_width:
        return frame, 1.0
    scale = detection_width / float(width)
    resized = cv2.resize(
        frame,
        (detection_width, max(1, int(round(height * scale)))),
        interpolation=cv2.INTER_AREA,
    )
    return resized, scale


def non_max_suppression(
    boxes: Iterable[tuple[int, int, int, int]],
    scores: Iterable[float],
    score_threshold: float,
    nms_threshold: float,
) -> list[tuple[int, int, int, int]]:
    boxes_list = list(boxes)
    scores_list = list(scores)
    if not boxes_list:
        return []
    xywh = [[x, y, w, h] for x, y, w, h in boxes_list]
    indices = cv2.dnn.NMSBoxes(xywh, scores_list, score_threshold, nms_threshold)
    if indices is None or len(indices) == 0:
        return []
    flattened = np.array(indices).reshape(-1)
    return [boxes_list[int(index)] for index in flattened]


def detect_people_hog(
    frame: np.ndarray,
    hog: cv2.HOGDescriptor,
    detection_width: int,
) -> list[tuple[int, int, int, int]]:
    detection_frame, scale = resize_for_detection(frame, detection_width)
    rectangles, weights = hog.detectMultiScale(
        detection_frame,
        HOG_HIT_THRESHOLD,
        (8, 8),
        (8, 8),
        HOG_SCALE,
        2.0,
        False,
    )

    boxes: list[tuple[int, int, int, int]] = []
    scores: list[float] = []
    for rectangle, weight in zip(rectangles, weights):
        x, y, w, h = [int(value) for value in rectangle]
        score = float(np.array(weight).reshape(-1)[0])
        if score < HOG_HIT_THRESHOLD:
            continue
        x += int(w * 0.10)
        w = int(w * 0.80)
        y += int(h * 0.06)
        h = int(h * 0.88)
        boxes.append((x, y, w, h))
        scores.append(score)

    selected = non_max_suppression(boxes, scores, HOG_HIT_THRESHOLD, 0.35)
    if scale == 1.0:
        return selected

    inverse = 1.0 / scale
    return [
        (
            int(round(x * inverse)),
            int(round(y * inverse)),
            int(round(w * inverse)),
            int(round(h * inverse)),
        )
        for x, y, w, h in selected
    ]



def enhance_low_light(frame: np.ndarray) -> np.ndarray:
    """Apply CLAHE only when the frame is genuinely dark."""
    if not LOW_LIGHT_ENHANCE:
        return frame
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    if float(np.mean(gray)) >= LOW_LIGHT_THRESHOLD:
        return frame
    lab = cv2.cvtColor(frame, cv2.COLOR_BGR2LAB)
    lightness, channel_a, channel_b = cv2.split(lab)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    lightness = clahe.apply(lightness)
    return cv2.cvtColor(cv2.merge((lightness, channel_a, channel_b)), cv2.COLOR_LAB2BGR)


class PersonDetector:
    backend_name = "unknown"

    def detect(self, frame: np.ndarray, detection_width: int) -> list[tuple[int, int, int, int]]:
        raise NotImplementedError


class MobileNetSSDDetector(PersonDetector):
    backend_name = "OpenCV-DNN-MobileNet-SSD"
    PERSON_CLASS_ID = 15

    def __init__(self) -> None:
        if not MOBILENET_PROTOTXT.is_file():
            raise FileNotFoundError(f"missing MobileNet-SSD prototxt: {MOBILENET_PROTOTXT}")
        if not MOBILENET_MODEL.is_file():
            raise FileNotFoundError(f"missing MobileNet-SSD model: {MOBILENET_MODEL}")
        self.net = cv2.dnn.readNetFromCaffe(str(MOBILENET_PROTOTXT), str(MOBILENET_MODEL))
        self.net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
        self.net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)

    def detect(self, frame: np.ndarray, detection_width: int) -> list[tuple[int, int, int, int]]:
        detection_frame, scale = resize_for_detection(enhance_low_light(frame), detection_width)
        height, width = detection_frame.shape[:2]
        blob = cv2.dnn.blobFromImage(
            detection_frame,
            scalefactor=0.007843,
            size=(300, 300),
            mean=(127.5, 127.5, 127.5),
            swapRB=False,
            crop=False,
        )
        self.net.setInput(blob)
        detections = self.net.forward()
        boxes: list[tuple[int, int, int, int]] = []
        scores: list[float] = []
        frame_area = float(max(1, width * height))
        for index in range(detections.shape[2]):
            confidence = float(detections[0, 0, index, 2])
            class_id = int(detections[0, 0, index, 1])
            if class_id != self.PERSON_CLASS_ID or confidence < DNN_CONFIDENCE:
                continue
            left = int(round(detections[0, 0, index, 3] * width))
            top = int(round(detections[0, 0, index, 4] * height))
            right = int(round(detections[0, 0, index, 5] * width))
            bottom = int(round(detections[0, 0, index, 6] * height))
            left = max(0, min(left, width - 1))
            top = max(0, min(top, height - 1))
            right = max(left + 1, min(right, width))
            bottom = max(top + 1, min(bottom, height))
            box_width = right - left
            box_height = bottom - top
            if box_height < DNN_MIN_BOX_HEIGHT:
                continue
            if (box_width * box_height) / frame_area < DNN_MIN_AREA_RATIO:
                continue
            boxes.append((left, top, box_width, box_height))
            scores.append(confidence)

        selected = non_max_suppression(boxes, scores, DNN_CONFIDENCE, DNN_NMS_THRESHOLD)
        if scale == 1.0:
            return selected
        inverse = 1.0 / scale
        return [
            (
                int(round(x * inverse)),
                int(round(y * inverse)),
                int(round(w * inverse)),
                int(round(h * inverse)),
            )
            for x, y, w, h in selected
        ]


class HOGPersonDetector(PersonDetector):
    backend_name = "OpenCV-HOG-SVM-fallback"

    def __init__(self) -> None:
        self.hog = cv2.HOGDescriptor()
        self.hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())

    def detect(self, frame: np.ndarray, detection_width: int) -> list[tuple[int, int, int, int]]:
        return detect_people_hog(enhance_low_light(frame), self.hog, detection_width)


def load_person_detector() -> PersonDetector:
    if VISION_BACKEND in {"mobilenet", "mobilenet_ssd", "dnn", "auto"}:
        try:
            detector = MobileNetSSDDetector()
            log(
                f"loaded {detector.backend_name} confidence={DNN_CONFIDENCE:.2f} "
                f"nms={DNN_NMS_THRESHOLD:.2f} model={MOBILENET_MODEL}"
            )
            return detector
        except (OSError, cv2.error) as error:
            if VISION_BACKEND != "auto":
                raise RuntimeError(f"MobileNet-SSD initialization failed: {error}") from error
            log(f"MobileNet-SSD unavailable; falling back to HOG: {error}")
    detector = HOGPersonDetector()
    log(f"loaded {detector.backend_name}")
    return detector


class DetectionHold:
    """Hold the last valid boxes for a few frames to suppress one-frame misses."""

    def __init__(self, hold_frames: int) -> None:
        self.hold_frames = max(0, hold_frames)
        self.remaining = 0
        self.last_boxes: list[tuple[int, int, int, int]] = []

    def update(self, boxes: list[tuple[int, int, int, int]]) -> list[tuple[int, int, int, int]]:
        if boxes:
            self.last_boxes = boxes
            self.remaining = self.hold_frames
            return boxes
        if self.remaining > 0 and self.last_boxes:
            self.remaining -= 1
            return self.last_boxes
        self.last_boxes = []
        return []

def draw_overlay(
    frame: np.ndarray,
    boxes: list[tuple[int, int, int, int]],
    fps: float,
    timestamp: str,
    profile: RuntimeProfile,
    guard_armed: bool,
) -> np.ndarray:
    output = frame.copy()
    height, width = output.shape[:2]

    for index, (x, y, w, h) in enumerate(boxes, start=1):
        x = max(0, min(x, width - 1))
        y = max(0, min(y, height - 1))
        w = max(1, min(w, width - x))
        h = max(1, min(h, height - y))
        cv2.rectangle(output, (x, y), (x + w, y + h), (0, 255, 0), 2)
        cv2.putText(
            output,
            f"Person {index}",
            (x, max(22, y - 8)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.58,
            (0, 255, 0),
            2,
            cv2.LINE_AA,
        )

    target = "unlimited" if profile.max_fps <= 0 else str(profile.max_fps)
    lines = [
        f"Student ID: {STUDENT_ID}",
        f"Time: {timestamp}",
        f"Persons: {len(boxes)}",
        f"FPS: {fps:.2f} (target {target})",
        f"Detector: {VISION_BACKEND} / detect {profile.detection_width}px",
    ]
    font = cv2.FONT_HERSHEY_SIMPLEX
    font_scale = max(0.42, min(0.68, width / 1100.0))
    thickness = 2
    line_height = int(27 * font_scale / 0.58)
    max_text_width = max(cv2.getTextSize(line, font, font_scale, thickness)[0][0] for line in lines)
    panel_height = line_height * len(lines) + 18
    overlay = output.copy()
    cv2.rectangle(overlay, (8, 8), (max_text_width + 28, panel_height), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.62, output, 0.38, 0.0, output)

    y_position = line_height
    for line in lines:
        cv2.putText(
            output,
            line,
            (18, y_position),
            font,
            font_scale,
            (255, 255, 255),
            thickness,
            cv2.LINE_AA,
        )
        y_position += line_height

    return output


def cleanup_old_events() -> None:
    try:
        snapshots = sorted(
            EVENT_DIR.glob("event_*.jpg"),
            key=lambda path: path.stat().st_mtime_ns,
            reverse=True,
        )
        for old_snapshot in snapshots[max(1, EVENT_KEEP_COUNT) :]:
            old_snapshot.unlink(missing_ok=True)
    except OSError as error:
        log(f"event cleanup warning: {error}")


def publish_detection_event(
    annotated_jpeg: bytes,
    persons: int,
    fps: float,
    timestamp_iso: str,
) -> None:
    event_id = time.time_ns()
    EVENT_DIR.mkdir(parents=True, exist_ok=True)
    snapshot_path = EVENT_DIR / f"event_{event_id}.jpg"
    atomic_write_bytes(snapshot_path, annotated_jpeg, 0o640)
    payload = {
        "event_id": str(event_id),
        "student_id": STUDENT_ID,
        "event": "person_detected",
        "persons": persons,
        "timestamp": timestamp_iso,
        "snapshot_path": str(snapshot_path),
        "vision_fps": round(fps, 3),
    }
    atomic_write_text(EVENT_FILE, json.dumps(payload, separators=(",", ":")) + "\n", 0o640)
    cleanup_old_events()
    log(f"new detection event id={event_id} persons={persons} snapshot={snapshot_path}")


def process_client(client: socket.socket, peer: tuple[str, int], detector: PersonDetector) -> None:
    log(f"raw camera connected from {peer[0]}:{peer[1]}")
    client.settimeout(2.0)
    forwarder = FrameForwarder(FORWARD_HOST, FORWARD_PORT)
    frame_times: deque[float] = deque(maxlen=30)
    previous_persons = 0
    zero_frames = ZERO_CONFIRM_FRAMES
    profile = RuntimeProfile()
    profile.load_if_changed()
    next_profile_check = 0.0
    next_process_at = 0.0
    guard_armed = False
    detection_hold = DetectionHold(DETECTION_HOLD_FRAMES)

    try:
        while RUNNING:
            header = recv_exact(client, 4)
            if header is None:
                break
            frame_size = struct.unpack("!I", header)[0]
            if frame_size < 4 or frame_size > MAX_FRAME_BYTES:
                log(f"invalid frame size {frame_size}; closing input")
                break
            jpeg = recv_exact(client, frame_size)
            if jpeg is None:
                break
            if not (jpeg.startswith(b"\xff\xd8") and jpeg.endswith(b"\xff\xd9")):
                log("invalid JPEG markers; frame ignored")
                continue

            now_mono = time.monotonic()
            if now_mono >= next_profile_check:
                profile.load_if_changed()
                guard_armed = False
                next_profile_check = now_mono + max(0.2, CONTROL_REFRESH_SECONDS)
            if profile.max_fps > 0 and now_mono < next_process_at:
                continue

            encoded = np.frombuffer(jpeg, dtype=np.uint8)
            frame = cv2.imdecode(encoded, cv2.IMREAD_COLOR)
            if frame is None:
                log("JPEG decode failed; frame ignored")
                continue
            frame = resize_output_frame(frame, profile.output_width)

            started = time.monotonic()
            boxes = detection_hold.update(detector.detect(frame, profile.detection_width))
            processed_at = time.monotonic()
            frame_times.append(processed_at)
            if len(frame_times) > 1:
                elapsed = frame_times[-1] - frame_times[0]
                fps = (len(frame_times) - 1) / elapsed if elapsed > 0 else 0.0
            else:
                fps = 1.0 / max(processed_at - started, 1e-6)
            if profile.max_fps > 0:
                next_process_at = processed_at + 1.0 / float(profile.max_fps)
            else:
                next_process_at = processed_at

            wall_time = datetime.now().astimezone()
            timestamp_display = wall_time.strftime("%Y-%m-%d %H:%M:%S %z")
            timestamp_iso = wall_time.isoformat(timespec="milliseconds")
            annotated = draw_overlay(frame, boxes, fps, timestamp_display, profile, guard_armed)
            ok, output = cv2.imencode(
                ".jpg",
                annotated,
                [int(cv2.IMWRITE_JPEG_QUALITY), max(50, min(JPEG_QUALITY, 95))],
            )
            if not ok:
                log("JPEG encode failed")
                continue
            annotated_jpeg = output.tobytes()

            persons = len(boxes)
            atomic_write_text(PERSON_FILE, f"{persons}\n", 0o644)
            status_payload = {
                "student_id": STUDENT_ID,
                "timestamp": timestamp_iso,
                "persons": persons,
                "fps": round(fps, 3),
                "input_width": int(frame.shape[1]),
                "input_height": int(frame.shape[0]),
                "processing_ms": round((time.monotonic() - started) * 1000.0, 3),
                "adaptive_mode": profile.mode,
                "target_max_fps": profile.max_fps,
                "detection_width": profile.detection_width,
                "output_width": profile.output_width,
            }
            atomic_write_text(
                STATUS_FILE,
                json.dumps(status_payload, separators=(",", ":")) + "\n",
                0o644,
            )
            forwarder.send(annotated_jpeg)

            if persons == 0:
                zero_frames = min(ZERO_CONFIRM_FRAMES, zero_frames + 1)
                if zero_frames >= ZERO_CONFIRM_FRAMES:
                    previous_persons = 0
            else:
                zero_frames = 0
                if previous_persons == 0 or persons != previous_persons:
                    publish_detection_event(
                        annotated_jpeg=annotated_jpeg,
                        persons=persons,
                        fps=fps,
                        timestamp_iso=timestamp_iso,
                    )
                previous_persons = persons

            log(
                f"frame persons={persons} fps={fps:.2f} mode={profile.mode} "
                f"processing_ms={(time.monotonic() - started) * 1000.0:.1f}"
            )
    finally:
        forwarder.close()
        try:
            client.close()
        except OSError:
            pass
        atomic_write_text(PERSON_FILE, "0\n", 0o644)
        log("raw camera disconnected; waiting for camera recovery")


def main() -> int:
    if not (1 <= LISTEN_PORT <= 65535 and 1 <= FORWARD_PORT <= 65535):
        print("invalid TCP port configuration", file=sys.stderr)
        return 2

    PERSON_FILE.parent.mkdir(parents=True, exist_ok=True)
    EVENT_DIR.mkdir(parents=True, exist_ok=True)
    atomic_write_text(PERSON_FILE, "0\n", 0o644)

    try:
        detector = load_person_detector()
    except (RuntimeError, OSError, cv2.error) as error:
        print(f"vision detector initialization failed: {error}", file=sys.stderr)
        return 3

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((LISTEN_HOST, LISTEN_PORT))
    server.listen(4)
    server.settimeout(1.0)

    log(
        f"started student_id={STUDENT_ID} input={LISTEN_HOST}:{LISTEN_PORT} "
        f"output={FORWARD_HOST}:{FORWARD_PORT} backend={detector.backend_name} "
        f"control={CONTROL_FILE}"
    )

    try:
        while RUNNING:
            try:
                client, peer = server.accept()
            except socket.timeout:
                continue
            except OSError as error:
                if RUNNING:
                    log(f"accept failed: {error}")
                    time.sleep(1.0)
                continue
            process_client(client, peer, detector)
    finally:
        server.close()
        atomic_write_text(PERSON_FILE, "0\n", 0o644)
        log("stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
