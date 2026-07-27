#!/usr/bin/env python3

import os
import signal
import sys
import time
from pathlib import Path

import cv2
import numpy as np
import requests
import urllib3


STREAM_URL = os.getenv(
    "SMART_GUARD_STREAM_URL",
    "https://127.0.0.1/api/v1/stream",
)

PERSON_COUNT_FILE = Path(
    os.getenv(
        "SMART_GUARD_PERSON_FILE",
        "/run/smart-guard/person_count",
    )
)

PROCESS_INTERVAL_SECONDS = float(
    os.getenv("SMART_GUARD_VISION_INTERVAL", "0.8")
)

MAX_FRAME_WIDTH = int(
    os.getenv("SMART_GUARD_VISION_MAX_WIDTH", "640")
)

running = True

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


def handle_signal(signum, frame):
    del signum, frame

    global running
    running = False


signal.signal(signal.SIGTERM, handle_signal)
signal.signal(signal.SIGINT, handle_signal)


def find_face_cascade() -> str:
    candidates = [
        os.path.join(
            getattr(getattr(cv2, "data", None), "haarcascades", ""),
            "haarcascade_frontalface_default.xml",
        ),
        "/usr/share/opencv4/haarcascades/"
        "haarcascade_frontalface_default.xml",
        "/usr/share/opencv/haarcascades/"
        "haarcascade_frontalface_default.xml",
    ]

    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate

    raise FileNotFoundError(
        "OpenCV frontal-face cascade was not found"
    )


def write_person_count(count: int) -> None:
    count = max(0, int(count))

    PERSON_COUNT_FILE.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    temporary_file = PERSON_COUNT_FILE.with_suffix(".tmp")

    with open(temporary_file, "w", encoding="utf-8") as file:
        file.write(f"{count}\n")
        file.flush()
        os.fsync(file.fileno())

    os.replace(
        temporary_file,
        PERSON_COUNT_FILE,
    )


def resize_frame(frame):
    height, width = frame.shape[:2]

    if width <= MAX_FRAME_WIDTH:
        return frame

    scale = MAX_FRAME_WIDTH / float(width)
    new_height = max(1, int(height * scale))

    return cv2.resize(
        frame,
        (MAX_FRAME_WIDTH, new_height),
        interpolation=cv2.INTER_AREA,
    )


def detect_people(frame, hog, face_detector) -> tuple[int, int, int]:
    frame = resize_frame(frame)

    gray = cv2.cvtColor(
        frame,
        cv2.COLOR_BGR2GRAY,
    )

    gray = cv2.equalizeHist(gray)

    faces = face_detector.detectMultiScale(
        gray,
        scaleFactor=1.10,
        minNeighbors=5,
        minSize=(50, 50),
    )

    body_rectangles, body_weights = hog.detectMultiScale(
        frame,
        winStride=(8, 8),
        padding=(8, 8),
        scale=1.05,
    )

    del body_weights

    face_count = len(faces)
    body_count = len(body_rectangles)

    # Do not add both values because a person's face and body
    # could be detected simultaneously.
    person_count = max(face_count, body_count)

    return person_count, face_count, body_count


def process_mjpeg_stream(hog, face_detector) -> None:
    session = requests.Session()

    next_processing_time = 0.0
    receive_buffer = bytearray()

    with session.get(
        STREAM_URL,
        stream=True,
        verify=False,
        timeout=(5, 20),
        headers={
            "User-Agent": "smart-guard-vision/1.0",
            "Connection": "keep-alive",
        },
    ) as response:
        response.raise_for_status()

        print(
            f"Connected to MJPEG stream: {STREAM_URL}",
            flush=True,
        )

        for chunk in response.iter_content(chunk_size=16384):
            if not running:
                break

            if not chunk:
                continue

            receive_buffer.extend(chunk)

            while running:
                start = receive_buffer.find(b"\xff\xd8")

                if start < 0:
                    if len(receive_buffer) > 2 * 1024 * 1024:
                        receive_buffer.clear()
                    break

                end = receive_buffer.find(
                    b"\xff\xd9",
                    start + 2,
                )

                if end < 0:
                    if start > 0:
                        del receive_buffer[:start]
                    break

                jpeg_data = bytes(
                    receive_buffer[start : end + 2]
                )

                del receive_buffer[: end + 2]

                current_time = time.monotonic()

                if current_time < next_processing_time:
                    continue

                encoded_frame = np.frombuffer(
                    jpeg_data,
                    dtype=np.uint8,
                )

                frame = cv2.imdecode(
                    encoded_frame,
                    cv2.IMREAD_COLOR,
                )

                if frame is None:
                    continue

                person_count, face_count, body_count = detect_people(
                    frame,
                    hog,
                    face_detector,
                )

                write_person_count(person_count)

                print(
                    "Detection result: "
                    f"persons={person_count}, "
                    f"faces={face_count}, "
                    f"bodies={body_count}",
                    flush=True,
                )

                next_processing_time = (
                    current_time + PROCESS_INTERVAL_SECONDS
                )


def main() -> int:
    print("Smart Guard vision service starting", flush=True)

    cascade_path = find_face_cascade()

    face_detector = cv2.CascadeClassifier(cascade_path)

    if face_detector.empty():
        print(
            f"Cannot load face cascade: {cascade_path}",
            file=sys.stderr,
            flush=True,
        )
        return 1

    hog = cv2.HOGDescriptor()

    hog.setSVMDetector(
        cv2.HOGDescriptor_getDefaultPeopleDetector()
    )

    write_person_count(0)

    while running:
        try:
            process_mjpeg_stream(
                hog,
                face_detector,
            )

        except requests.RequestException as error:
            print(
                f"Stream connection error: {error}",
                file=sys.stderr,
                flush=True,
            )

        except Exception as error:
            print(
                f"Vision processing error: {error}",
                file=sys.stderr,
                flush=True,
            )

        if running:
            write_person_count(0)
            time.sleep(2)

    write_person_count(0)

    print(
        "Smart Guard vision service stopped",
        flush=True,
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
