#!/usr/bin/env python3
"""Smart Guard Section 3 vision relay.

Receives length-prefixed JPEG frames from the physical-host camera agent,
detects people with OpenCV's built-in HOG/SVM person model, draws the required
boxes/metadata, forwards the annotated JPEG to the existing C web core, and
atomically publishes person-count and detection-event files for C services.
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
JPEG_QUALITY = int(os.getenv("SMART_GUARD_VISION_JPEG_QUALITY", "85"))
DETECTION_WIDTH = int(os.getenv("SMART_GUARD_VISION_DETECTION_WIDTH", "640"))
HOG_HIT_THRESHOLD = float(os.getenv("SMART_GUARD_VISION_HOG_THRESHOLD", "0.15"))
HOG_SCALE = float(os.getenv("SMART_GUARD_VISION_HOG_SCALE", "1.05"))
MAX_FRAME_BYTES = int(os.getenv("SMART_GUARD_VISION_MAX_FRAME_BYTES", str(8 * 1024 * 1024)))
EVENT_KEEP_COUNT = int(os.getenv("SMART_GUARD_EVENT_KEEP_COUNT", "100"))
ZERO_CONFIRM_FRAMES = int(os.getenv("SMART_GUARD_ZERO_CONFIRM_FRAMES", "2"))

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


def resize_for_detection(frame: np.ndarray) -> tuple[np.ndarray, float]:
    height, width = frame.shape[:2]
    if DETECTION_WIDTH <= 0 or width <= DETECTION_WIDTH:
        return frame, 1.0
    scale = DETECTION_WIDTH / float(width)
    resized = cv2.resize(
        frame,
        (DETECTION_WIDTH, max(1, int(round(height * scale)))),
        interpolation=cv2.INTER_AREA,
    )
    return resized, scale


def non_max_suppression(
    boxes: Iterable[tuple[int, int, int, int]],
    scores: Iterable[float],
) -> list[tuple[int, int, int, int]]:
    boxes_list = list(boxes)
    scores_list = list(scores)
    if not boxes_list:
        return []
    xywh = [[x, y, w, h] for x, y, w, h in boxes_list]
    indices = cv2.dnn.NMSBoxes(xywh, scores_list, HOG_HIT_THRESHOLD, 0.35)
    if indices is None or len(indices) == 0:
        return []
    flattened = np.array(indices).reshape(-1)
    return [boxes_list[int(index)] for index in flattened]


def detect_people(frame: np.ndarray, hog: cv2.HOGDescriptor) -> list[tuple[int, int, int, int]]:
    detection_frame, scale = resize_for_detection(frame)
    # Positional arguments keep compatibility with both distro OpenCV builds
    # and newer PyPI wheels; some builds do not expose finalThreshold as a
    # keyword even though it is present in the C++ API.
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
        # HOG boxes are often wider than the actual person. Tightening the box
        # improves both the stream visualization and duplicate suppression.
        x += int(w * 0.10)
        w = int(w * 0.80)
        y += int(h * 0.06)
        h = int(h * 0.88)
        boxes.append((x, y, w, h))
        scores.append(score)

    selected = non_max_suppression(boxes, scores)
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


def draw_overlay(
    frame: np.ndarray,
    boxes: list[tuple[int, int, int, int]],
    fps: float,
    timestamp: str,
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

    lines = [
        f"Student ID: {STUDENT_ID}",
        f"Time: {timestamp}",
        f"Persons: {len(boxes)}",
        f"FPS: {fps:.2f}",
    ]
    font = cv2.FONT_HERSHEY_SIMPLEX
    font_scale = max(0.48, min(0.75, width / 1100.0))
    thickness = 2
    line_height = int(28 * font_scale / 0.58)
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


def process_client(client: socket.socket, peer: tuple[str, int], hog: cv2.HOGDescriptor) -> None:
    log(f"raw camera connected from {peer[0]}:{peer[1]}")
    client.settimeout(2.0)
    forwarder = FrameForwarder(FORWARD_HOST, FORWARD_PORT)
    frame_times: deque[float] = deque(maxlen=30)
    previous_persons = 0
    zero_frames = ZERO_CONFIRM_FRAMES

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

            encoded = np.frombuffer(jpeg, dtype=np.uint8)
            frame = cv2.imdecode(encoded, cv2.IMREAD_COLOR)
            if frame is None:
                log("JPEG decode failed; frame ignored")
                continue

            started = time.monotonic()
            boxes = detect_people(frame, hog)
            now = time.monotonic()
            frame_times.append(now)
            if len(frame_times) > 1:
                elapsed = frame_times[-1] - frame_times[0]
                fps = (len(frame_times) - 1) / elapsed if elapsed > 0 else 0.0
            else:
                fps = 1.0 / max(now - started, 1e-6)

            wall_time = datetime.now().astimezone()
            timestamp_display = wall_time.strftime("%Y-%m-%d %H:%M:%S %z")
            timestamp_iso = wall_time.isoformat(timespec="milliseconds")
            annotated = draw_overlay(frame, boxes, fps, timestamp_display)
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
                f"frame persons={persons} fps={fps:.2f} "
                f"processing_ms={(time.monotonic() - started) * 1000.0:.1f}"
            )
    finally:
        forwarder.close()
        try:
            client.close()
        except OSError:
            pass
        atomic_write_text(PERSON_FILE, "0\n", 0o644)
        log("raw camera disconnected; waiting for automatic reconnect")


def main() -> int:
    if not (1 <= LISTEN_PORT <= 65535 and 1 <= FORWARD_PORT <= 65535):
        print("invalid TCP port configuration", file=sys.stderr)
        return 2

    PERSON_FILE.parent.mkdir(parents=True, exist_ok=True)
    EVENT_DIR.mkdir(parents=True, exist_ok=True)
    atomic_write_text(PERSON_FILE, "0\n", 0o644)

    hog = cv2.HOGDescriptor()
    hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((LISTEN_HOST, LISTEN_PORT))
    server.listen(4)
    server.settimeout(1.0)

    log(
        f"started student_id={STUDENT_ID} input={LISTEN_HOST}:{LISTEN_PORT} "
        f"output={FORWARD_HOST}:{FORWARD_PORT} backend=OpenCV-HOG-SVM"
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
            process_client(client, peer, hog)
    finally:
        server.close()
        atomic_write_text(PERSON_FILE, "0\n", 0o644)
        log("stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
