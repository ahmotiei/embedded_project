# Smart Guard — Section 3
## Image Processing, Email Notification, and MQTT Communication

**Student:** Amir Hossein Motiei  
**Student ID:** `401102553`

---

## 1. Overview

Section 3 adds the intelligent monitoring and notification layer to the Smart Guard project. It extends the HTTPS web server and REST API developed in the previous sections with real-time human detection, annotated video streaming, email alerts, and MQTT communication.

The implementation is divided into three main parts:

1. **Human detection and image processing** using Python and OpenCV. This is the only part of the project implemented in Python.
2. **Email notification** implemented in C using libcurl and MIME attachments.
3. **MQTT communication** implemented in C using libmosquitto and cJSON, with authentication, QoS 1, retained status messages, Last Will and Testament, and automatic reconnection.

The final system receives frames from the physical webcam, detects people, writes the student ID, live time, person count, and measured FPS on each frame, and forwards the annotated stream to the HTTPS dashboard. When a person is detected, the system creates a detection event, publishes it through MQTT, and sends an email containing the event details and a JPEG snapshot. A persistent 30-second debounce mechanism prevents repeated email alerts.

---

## 2. System Architecture

The webcam is physically connected to the Ubuntu host. Because the virtual machine does not access the webcam directly, the host agent captures the camera frames and forwards them to the VM.

```text
Physical Ubuntu Host
│
├── USB Webcam
│       │
│       ▼
├── smart_guard_host_agent (C)
│   ├── Captures webcam frames
│   ├── Encodes frames as JPEG
│   ├── Sends raw JPEG frames to the VM over TCP port 9200
│   └── Sends host CPU temperature over UDP port 9090
│
├── Mosquitto Broker
│   ├── Listens on TCP port 1883
│   ├── Requires username and password authentication
│   ├── Uses an ACL for topic access control
│   └── Rejects anonymous clients
│
└───────────────────────────────────────────────────────┐
                                                        │
                                                        ▼
Virtual Machine
│
├── smart-guard-vision (Python + OpenCV)
│   ├── Receives raw JPEG frames on TCP port 9200
│   ├── Detects people with HOG/SVM
│   ├── Removes duplicate detections with NMS
│   ├── Calculates the real processing FPS
│   ├── Draws bounding boxes and labels
│   ├── Writes student ID, timestamp, person count, and FPS
│   ├── Creates detection events and JPEG snapshots
│   └── Sends annotated JPEG frames to 127.0.0.1:9100
│
├── smart-guard-web (C)
│   ├── HTTPS dashboard on port 443
│   ├── Swagger interface on port 8443
│   ├── Internal REST API on 127.0.0.1:18080
│   ├── MJPEG live stream
│   ├── Persons API
│   └── Telemetry API
│
└── smart-guard-mqtt / notifier (C)
    ├── Reads vision detection events
    ├── Reads telemetry from the internal C web API
    ├── Sends SMTP email alerts with JPEG attachments
    ├── Applies a persistent 30-second email debounce
    ├── Publishes JSON messages through MQTT
    ├── Uses QoS 1 and receives PUBACK
    ├── Configures a retained LWT status message
    └── Automatically reconnects after broker recovery
```

The deployment used the following private libvirt network addresses:

```text
Host address from the VM: 192.168.122.1
VM address:                192.168.122.186
MQTT broker port:          1883
Raw camera input port:     9200
Annotated frame port:      9100
Host temperature UDP port: 9090
Internal REST API port:    18080
HTTPS port:                443
Swagger port:              8443
```

---

## 3. Implemented Features

### 3.1 Human Detection and Annotated Streaming

The vision module is implemented in:

```text
vision/person_detector.py
```

It receives JPEG frames from the host agent using a simple framed TCP protocol:

```text
[4-byte network-order JPEG length][JPEG payload]
```

The receiver reads the four-byte header first and then reads exactly the announced number of bytes. This is necessary because TCP is a byte stream and does not preserve message boundaries.

The received JPEG is decoded with OpenCV. Human detection is performed with OpenCV's built-in HOG descriptor and default SVM people detector. This detector was selected because it is lightweight, requires no external model file, and can run on CPU-only systems.

The detector may generate multiple overlapping rectangles for the same person. Non-Maximum Suppression is therefore applied to keep the strongest rectangle and remove redundant overlapping detections. The number of remaining rectangles becomes the current person count.

To control CPU usage, large input frames are resized to a configurable detection width before HOG processing. The resulting coordinates are then mapped back to the original image size before drawing. This keeps the final stream at the original resolution while reducing detection cost.

The real FPS is calculated from the processing timestamps of the latest frames using a monotonic clock. It is not taken from the nominal camera configuration. A moving window reduces short-term FPS fluctuations.

Every output frame contains:

```text
Student ID: 401102553
Live system date and time
Detected person count
Measured vision FPS
A bounding box and Person N label for each detection
```

The latest person count and vision status are written atomically to runtime files so that the C services never read partially written content. When a new person enters the scene or the detected count changes, the vision module creates a unique event containing the event ID, timestamp, person count, vision FPS, and snapshot path.

Typical runtime files are:

```text
/run/smart-guard/person_count
/run/smart-guard/vision_status.json
/run/smart-guard/detection_event.json
/run/smart-guard/events/event_<event_id>.jpg
```

### 3.2 Email Notification in C

Email notification is implemented in:

```text
src/smart_guard_notifier.c
```

The notifier monitors the detection event file generated by the vision module. When it observes a new event ID, it reads the event information and requests the current telemetry from the internal C web API:

```text
http://127.0.0.1:18080/api/v1/telemetry
```

The loopback API is used because it is available only inside the VM and avoids disabling certificate verification for the public self-signed HTTPS endpoint.

The email body contains:

```text
Student ID
Detected person count
Detection timestamp
Current CPU temperature
Vision FPS
Event ID
```

The JPEG snapshot generated at the moment of detection is attached as an `image/jpeg` MIME part. libcurl handles the SMTP connection, TLS verification, message headers, recipients, text body, Base64 encoding, and attachment transfer.

The email credentials are not hard-coded in the C source. The notifier reads them from environment variables supplied by a protected runtime file. The repository contains only an example configuration file.

The 30-second debounce mechanism stores both the timestamp of the last successfully sent email and the last processed email event ID. Because these values are stored under `/var/lib/smart-guard/`, the debounce state survives service restarts. An event received inside the 30-second interval is recorded as processed but does not generate another email. SMTP failures are retried with a delay to avoid a busy retry loop.

### 3.3 MQTT Communication in C

The MQTT client is also implemented in:

```text
src/smart_guard_notifier.c
```

It uses libmosquitto for MQTT communication and cJSON for JSON message construction. The broker runs on the physical host and requires authentication.

The required topics are:

```text
telemetry/401102553/home
persons/401102553/home
```

An additional status topic is used for connection monitoring and LWT:

```text
status/401102553/home
```

The telemetry topic carries periodic system information such as CPU temperature, CPU usage, available memory, current person count, camera state, and timestamps. The persons topic carries periodic count messages and immediate `person_detected` events. The status topic carries `online`, `offline-clean`, and `offline-unexpected` states.

Messages are created with cJSON instead of manual string concatenation. This preserves the correct JSON data types and safely escapes string values. A real person-detection message has the following structure:

```json
{
  "student_id": "401102553",
  "persons": 1,
  "timestamp": "2026-07-28T14:08:30.710+03:30",
  "published_at": "2026-07-28T14:08:30+0330",
  "temperature_available": true,
  "temperature_c": 80,
  "event": "person_detected",
  "event_id": "1785235110713244608",
  "vision_fps": 12.962
}
```

All MQTT publications use QoS 1. After a PUBLISH packet is queued, the broker confirms receipt with PUBACK. The message ID is written to the journal when queued and again when the matching acknowledgement is received.

The LWT payload is configured before connecting to the broker. It contains the retained state `offline-unexpected`. If the client disappears without sending a normal MQTT DISCONNECT packet, the broker publishes this Will message. After a successful connection or reconnection, the client replaces the retained status with `online`. During a controlled shutdown, it publishes `offline-clean` before disconnecting.

The initial connection is asynchronous, and libmosquitto runs its network loop in a separate thread. Automatic reconnection uses an increasing delay from 2 seconds up to 30 seconds. This prevents a tight reconnect loop when the broker remains unavailable and allows the notifier's email and event-processing logic to continue running.

---

## 4. Repository Structure

```text
section3/
├── broker/
├── config/
├── docs/
├── host/
├── scripts/
├── src/
├── systemd/
├── tests/
├── vision/
├── web/
├── broker_rejection_journal.log
├── MANIFEST.txt
└── SHA256SUMS.txt
```

### 4.1 `broker/`

Contains the Mosquitto configuration files distributed with the project.

```text
broker/
├── acl-smart-guard
└── mosquitto.conf
```

`mosquitto.conf` defines the listener, disables anonymous access, and references the password and ACL files installed under `/etc/mosquitto/`. `acl-smart-guard` restricts publish and subscribe operations to the Smart Guard topics.

The real Mosquitto password database is intentionally not stored in this repository.

### 4.2 `config/`

Contains safe configuration templates.

```text
config/
├── alerts.env.example
├── host-agent.section3.env.example
├── mqtt.env.example
└── section3.env.example
```

These files document the required environment variables but use placeholders instead of real passwords. They may be copied to protected runtime paths and edited during installation.

### 4.3 `docs/evidence/`

Contains screenshots, CSV files, JSON summaries, and journal extracts collected for the final report.

```text
docs/evidence/
├── section3_overview/
├── test_3_1_lighting/
├── test_3_2_spoof/
├── test_3_3_resolution/
├── test_3_4_lwt_broker/
├── test_3_5_latency/
├── test_3_6_mqtt_auth/
└── test_3_7_ssh_unauthorized/
```

`section3_overview/` contains general evidence of the annotated dashboard, received email, email debounce, MQTT JSON topics, QoS 1 acknowledgements, and active services and ports.

Each `test_3_x_*` directory contains the raw measurements, logs, sample images, result files, and screenshots for one required experiment.

### 4.4 `host/`

Contains the host-side C camera agent.

```text
host/
├── CMakeLists.txt
└── smart_guard_host_agent.c
```

The host agent captures the physical webcam, encodes frames as JPEG, forwards them to the vision service in the VM, and sends host CPU temperature measurements.

### 4.5 `scripts/`

Contains installation and configuration helpers.

```text
scripts/
├── configure_host_agent.sh
├── install_broker_host.sh
├── install_host_section3.sh
└── install_vm_section3.sh
```

`install_broker_host.sh` installs and configures the Mosquitto broker on the physical host. `install_host_section3.sh` builds and installs the host camera agent. `configure_host_agent.sh` updates host-agent connection and camera settings. `install_vm_section3.sh` installs the vision, web, notifier, configuration templates, and systemd services inside the VM.

### 4.6 `src/`

Contains the C notifier and its build configuration.

```text
src/
├── CMakeLists.txt
└── smart_guard_notifier.c
```

The notifier implements detection-event processing, telemetry retrieval, SMTP email transmission, persistent debounce state, MQTT JSON publication, QoS 1 handling, LWT, and automatic reconnect.

### 4.7 `systemd/`

Contains the service unit files installed in the VM.

```text
systemd/
├── smart-guard-mqtt.service
├── smart-guard-vision.service
└── smart-guard-web.service
```

The units define service dependencies, automatic startup, crash recovery, runtime directories, environment files, and security restrictions. The intended startup order is web service, vision service, then MQTT/notifier service.

### 4.8 `tests/`

Contains the required experiment and verification scripts.

```text
tests/
├── common.sh
├── test_3_1_lighting.sh
├── test_3_2_spoof.sh
├── test_3_3_resolution.sh
├── test_3_4_lwt_broker_host.sh
├── test_3_5_latency_subscriber.py
├── test_3_6_mqtt_unauthorized.sh
├── test_3_7_ssh_unauthorized.sh
└── verify_section3.sh
```

`common.sh` contains shared helper functions. `verify_section3.sh` performs general installation and runtime checks. The remaining scripts correspond directly to the required tests numbered 3-1 through 3-7.

### 4.9 `vision/`

Contains the Python computer-vision service.

```text
vision/
└── person_detector.py
```

This module receives frames, runs HOG/SVM human detection, performs NMS, calculates FPS, produces annotated output, updates runtime state, and creates detection events and snapshots.

### 4.10 `web/`

Contains the C web server used by Section 3.

```text
web/
├── CMakeLists.txt
└── smart_guard_web.c
```

The web component receives the annotated frame stream from the vision service and provides the HTTPS dashboard, MJPEG stream, persons endpoint, telemetry endpoint, and internal loopback API used by the notifier.

### 4.11 Manifest and Integrity Files

```text
MANIFEST.txt
SHA256SUMS.txt
```

`MANIFEST.txt` lists the packaged project files. `SHA256SUMS.txt` contains SHA-256 hashes used to verify that the packaged files were not modified or corrupted.

`broker_rejection_journal.log` is an additional broker-side security log collected during the unauthorized MQTT test.

---

## 5. Dependencies

The project uses the following main libraries and tools:

```text
CMake
GCC or Clang
OpenCV
Python 3
NumPy
libcurl
libmosquitto
cJSON
Mosquitto broker and clients
systemd
OpenSSL
v4l2 tools
```

The exact package names depend on the Linux distribution. On Ubuntu, the development packages include `libcurl4-openssl-dev`, `libmosquitto-dev`, `libcjson-dev`, and the required OpenCV and Python packages.

---

## 6. Configuration

The repository includes only example environment files. Copy the required templates to protected runtime locations and replace the placeholders locally.

Typical runtime files are:

```text
/etc/smart-guard/section3.env
/etc/smart-guard/alerts.env
/etc/smart-guard/mqtt.env
/etc/smart-guard/host-agent.env
```

The actual names may be created or updated by the installation scripts.

Never commit the runtime files. In particular, the following values must remain secret:

```text
SMART_GUARD_SMTP_PASSWORD
SMART_GUARD_MQTT_PASSWORD
Private TLS keys
SSH private keys
Mosquitto password databases
```

The example files use placeholders such as:

```text
SMART_GUARD_SMTP_PASSWORD=REPLACE_WITH_APP_PASSWORD
SMART_GUARD_MQTT_PASSWORD=REPLACE_WITH_MQTT_PASSWORD
```

---

## 7. Build and Installation

### 7.1 Install the Broker on the Physical Host

From the `section3` directory on the host:

```bash
sudo bash scripts/install_broker_host.sh
```

After installation, confirm that the broker is active and listening on port 1883:

```bash
sudo systemctl status mosquitto
sudo ss -ltnp '( sport = :1883 )'
```

### 7.2 Build and Install the Host Camera Agent

Run on the physical host:

```bash
sudo bash scripts/install_host_section3.sh
```

Configure the VM address and camera options if required:

```bash
sudo bash scripts/configure_host_agent.sh
```

### 7.3 Install Section 3 in the VM

Copy the Section 3 directory to the VM and run:

```bash
sudo bash scripts/install_vm_section3.sh
```

The script builds the C components, installs the Python vision module, copies the systemd units, creates the runtime directories, and enables the services.

---

## 8. Service Management

Check the VM services:

```bash
systemctl status smart-guard-web.service
systemctl status smart-guard-vision.service
systemctl status smart-guard-mqtt.service
```

Check whether they are active:

```bash
systemctl is-active smart-guard-web.service
systemctl is-active smart-guard-vision.service
systemctl is-active smart-guard-mqtt.service
```

Restart the complete VM pipeline:

```bash
sudo systemctl restart smart-guard-web.service
sudo systemctl restart smart-guard-vision.service
sudo systemctl restart smart-guard-mqtt.service
```

Follow the logs:

```bash
sudo journalctl -u smart-guard-web.service -f
sudo journalctl -u smart-guard-vision.service -f
sudo journalctl -u smart-guard-mqtt.service -f
```

Check the host agent and broker on the physical host using their installed systemd service names and Mosquitto service:

```bash
sudo systemctl status mosquitto
sudo journalctl -u mosquitto -f
```

---

## 9. MQTT Topics

Subscribe from the host with valid credentials:

```bash
read -rsp "MQTT password: " MQTT_PASS
echo

mosquitto_sub \
  -h 127.0.0.1 \
  -p 1883 \
  -u smart_guard \
  -P "$MQTT_PASS" \
  -t 'telemetry/401102553/home' \
  -t 'persons/401102553/home' \
  -t 'status/401102553/home' \
  -q 1 \
  -v

unset MQTT_PASS
```

The password is entered without terminal echo and is removed from the shell variable after the command.

---

## 10. Required Experiments

### 10.1 Test 3-1 — Lighting Accuracy

Evaluates human-detection accuracy in daylight, artificial light, low light, and backlight.

```bash
sudo bash tests/test_3_1_lighting.sh
```

Recorded result:

```text
Daylight:        13/20 = 65.00%
Artificial:      19/20 = 95.00%
Low light:        7/20 = 35.00%
Backlight:        9/20 = 45.00%
Overall:         48/80 = 60.00%
```

Evidence:

```text
docs/evidence/test_3_1_lighting/
```

### 10.2 Test 3-2 — Photo or Mobile Spoofing

Places a displayed human image in front of the camera and measures false human detections.

```bash
sudo bash tests/test_3_2_spoof.sh
```

Recorded result:

```text
Spoof detections: 2/30
Spoof success rate: 6.67%
Correct rejection rate: 93.33%
```

Evidence:

```text
docs/evidence/test_3_2_spoof/
```

### 10.3 Test 3-3 — Resolution Comparison

Compares detection accuracy, real FPS, CPU temperature, and vision-process RSS at three input resolutions.

```bash
sudo bash tests/test_3_3_resolution.sh
```

Recorded summary:

| Resolution | Accuracy | Average FPS | Final Temperature | Maximum Temperature | Average RSS |
|---|---:|---:|---:|---:|---:|
| 320x240 | 36.67% | 30.061 | 62°C | 83°C | 148608.0 KB |
| 640x480 | 61.67% | 12.920 | 68°C | 85°C | 154407.2 KB |
| 1280x720 | 66.67% | 16.438 | 68°C | 78°C | 158716.8 KB |

Evidence:

```text
docs/evidence/test_3_3_resolution/
```

### 10.4 Test 3-4 — LWT and Broker Recovery

Verifies unexpected-client LWT publication and automatic client reconnection after the broker is stopped for three minutes and started again.

```bash
sudo bash tests/test_3_4_lwt_broker_host.sh
```

The LWT portion must be tested while the broker remains active and the MQTT client is terminated unexpectedly. A stopped broker cannot publish LWT while it is offline. Broker shutdown and recovery are therefore evaluated separately.

Evidence:

```text
docs/evidence/test_3_4_lwt_broker/
```

### 10.5 Test 3-5 — End-to-End MQTT Latency

Measures the difference between the detection timestamp generated in the VM and the MQTT receive timestamp on the host.

```bash
python3 tests/test_3_5_latency_subscriber.py
```

Recorded result for 10 measurements:

```text
Mean latency:              124.522 ms
Sample standard deviation:  56.191 ms
Minimum:                    53.299 ms
Maximum:                   234.249 ms
```

Both systems must be NTP-synchronized before the test.

Evidence:

```text
docs/evidence/test_3_5_latency/
```

### 10.6 Test 3-6 — Unauthorized MQTT Access

Attempts one anonymous connection and one connection using invalid credentials.

```bash
bash tests/test_3_6_mqtt_unauthorized.sh
```

Recorded result:

```text
anonymous_exit_code=5
wrong_credentials_exit_code=5
PASS: anonymous and wrong-password MQTT attempts failed.
```

Evidence:

```text
docs/evidence/test_3_6_mqtt_auth/
```

### 10.7 Test 3-7 — Unauthorized SSH Login

Creates a temporary unknown SSH key and attempts non-interactive access to the VM root account.

```bash
SMART_GUARD_VM_IP=192.168.122.186 \
  bash tests/test_3_7_ssh_unauthorized.sh
```

Recorded result:

```text
Permission denied (publickey,password).
exit_code=255
PASS: unauthorized SSH login failed.
```

The script removes the temporary private and public keys after execution.

Evidence:

```text
docs/evidence/test_3_7_ssh_unauthorized/
```

---

## 11. General Verification

Run the general verification script after installation:

```bash
bash tests/verify_section3.sh
```

The script checks the installed services and the expected runtime behavior. Detailed logs and screenshots are stored under `docs/evidence/`.

---

## 12. Recorded Evidence

The evidence directory contains 79 collected files across the general overview and tests 3-1 through 3-7. These include:

```text
Annotated dashboard screenshots
Received email and JPEG attachment
Email debounce journal entries
MQTT topic JSON output
QoS 1 PUBACK output
Lighting and resolution CSV measurements
Spoofing measurements
Broker stop and reconnect logs
Clock synchronization evidence
Latency samples and statistical summary
Rejected MQTT authentication attempts
Rejected SSH login attempt
```

No additional runtime screenshots are required for the completed Section 3 report unless the deployment configuration changes.

---

## 13. Security Notes

Do not commit any real credential or private key. Before pushing the project, check the repository:

```bash
grep -RniE \
  'SMTP_PASSWORD|MQTT_PASSWORD|APP_PASSWORD|BEGIN .*PRIVATE KEY|password[[:space:]]*=' \
  . \
  --exclude-dir=.git \
  --exclude='*.png' \
  --exclude='*.jpg' \
  --exclude='*.jpeg'
```

Also check tracked sensitive-looking files:

```bash
git ls-files | grep -Ei \
  '(\.env$|\.key$|\.pem$|passwd|password|id_rsa|id_ed25519)'
```

Safe repository values look like:

```text
SMART_GUARD_SMTP_PASSWORD=REPLACE_WITH_APP_PASSWORD
SMART_GUARD_MQTT_PASSWORD=REPLACE_WITH_MQTT_PASSWORD
```

Unsafe values include real Gmail App Passwords, real MQTT passwords, Mosquitto password databases, TLS private keys, and SSH private keys.

Recommended `.gitignore` rules:

```gitignore
# Runtime environment and credentials
*.env
.env
.env.*
!*.env.example

# Private keys
*.key
*.pem
id_rsa
id_ed25519
unauthorized_test_key
unauthorized_test_key.pub

# Authentication databases
passwd-smart-guard
mosquitto.passwd

# Generated build directories
build/
cmake-build-*/

# Python cache
__pycache__/
*.pyc
```

---

## 14. Integrity Verification

To verify the project files using the provided checksum list:

```bash
sha256sum -c SHA256SUMS.txt
```

The command should report `OK` for files that have not been changed since the checksum list was generated. If source or evidence files are intentionally modified, regenerate the checksum file before packaging the final submission.

---

## 15. Final Status

All mandatory Section 3 requirements were implemented and tested:

```text
Human detection and person counting                  PASS
Bounding boxes and annotated live stream             PASS
Student ID, live timestamp, and measured FPS overlay PASS
C-based email notification with JPEG attachment      PASS
Persistent 30-second email debounce                  PASS
C-based MQTT client                                   PASS
Required telemetry and persons topics                PASS
JSON payloads                                         PASS
QoS 1 and PUBACK                                      PASS
Retained LWT status                                   PASS
Automatic broker reconnection                        PASS
Tests 3-1 through 3-7                                PASS
```

The complete implementation, deployment scripts, runtime services, test scripts, raw measurements, logs, and screenshots are contained in this directory.
