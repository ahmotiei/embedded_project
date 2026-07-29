#!/usr/bin/env python3
"""Collect 10 end-to-end person-detection MQTT latency samples."""

from __future__ import annotations

import csv
import json
import os
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import paho.mqtt.client as mqtt
except ImportError as error:
    raise SystemExit("Install paho-mqtt: sudo apt install python3-paho-mqtt") from error

STUDENT_ID = os.getenv("SMART_GUARD_STUDENT_ID", "401102553")
HOST = os.getenv("SMART_GUARD_MQTT_HOST", "127.0.0.1")
PORT = int(os.getenv("SMART_GUARD_MQTT_PORT", "1883"))
USERNAME = os.getenv("SMART_GUARD_MQTT_USERNAME", f"smartguard_{STUDENT_ID}")
PASSWORD = os.getenv("SMART_GUARD_MQTT_PASSWORD", "")
TARGET = int(os.getenv("SMART_GUARD_LATENCY_SAMPLES", "10"))
OUT = Path(os.getenv("SMART_GUARD_LATENCY_OUTPUT", f"test_3_5_latency_{time.strftime('%Y%m%d_%H%M%S')}"))
TOPIC = f"persons/{STUDENT_ID}/home"

if not PASSWORD:
    raise SystemExit("Set SMART_GUARD_MQTT_PASSWORD in the environment.")
OUT.mkdir(parents=True, exist_ok=True)
results: list[dict[str, object]] = []
seen_ids: set[str] = set()


def parse_timestamp(text: str) -> datetime:
    return datetime.fromisoformat(text)


def on_connect(client: mqtt.Client, _userdata: object, _flags: object, reason_code: object, _properties: object = None) -> None:
    if int(reason_code) != 0:
        print(f"MQTT connection failed: {reason_code}", file=sys.stderr)
        client.disconnect()
        return
    print(f"Connected. Waiting for {TARGET} distinct person-entry events on {TOPIC}.")
    print("For each sample, leave the frame until persons becomes zero, then enter again.")
    client.subscribe(TOPIC, qos=1)


def on_message(client: mqtt.Client, _userdata: object, message: mqtt.MQTTMessage) -> None:
    try:
        payload = json.loads(message.payload.decode("utf-8"))
        if payload.get("event") != "person_detected":
            return
        event_id = str(payload.get("event_id", ""))
        if not event_id or event_id in seen_ids:
            return
        event_time = parse_timestamp(str(payload["timestamp"]))
        received_time = datetime.now().astimezone()
        latency_ms = (received_time - event_time).total_seconds() * 1000.0
        seen_ids.add(event_id)
        row = {
            "sample": len(results) + 1,
            "event_id": event_id,
            "event_timestamp": event_time.isoformat(timespec="milliseconds"),
            "received_timestamp": received_time.isoformat(timespec="milliseconds"),
            "latency_ms": round(latency_ms, 3),
            "persons": int(payload.get("persons", 0)),
            "temperature_c": payload.get("temperature_c"),
        }
        results.append(row)
        print(json.dumps(row, ensure_ascii=False))
        if len(results) >= TARGET:
            client.disconnect()
    except Exception as error:  # keep subscriber alive for malformed unrelated messages
        print(f"Ignored message: {error}", file=sys.stderr)


try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"latency-test-{STUDENT_ID}-{os.getpid()}")
except AttributeError:  # paho-mqtt 1.x
    client = mqtt.Client(client_id=f"latency-test-{STUDENT_ID}-{os.getpid()}")
client.username_pw_set(USERNAME, PASSWORD)
client.on_connect = on_connect
client.on_message = on_message
client.connect(HOST, PORT, keepalive=30)
client.loop_forever()

if len(results) != TARGET:
    raise SystemExit(f"Only {len(results)}/{TARGET} samples were collected.")

csv_path = OUT / "latency_samples.csv"
with csv_path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(results[0]))
    writer.writeheader()
    writer.writerows(results)
latencies = [float(row["latency_ms"]) for row in results]
summary = {
    "samples": len(latencies),
    "mean_latency_ms": round(statistics.mean(latencies), 3),
    "sample_standard_deviation_ms": round(statistics.stdev(latencies), 3),
    "minimum_ms": round(min(latencies), 3),
    "maximum_ms": round(max(latencies), 3),
    "clock_note": "Host and VM clocks must be synchronized (systemd-timesyncd/NTP).",
}
(OUT / "latency_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
