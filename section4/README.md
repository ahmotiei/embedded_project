# Smart Guard — Section 4: Advanced Capabilities

This directory contains **Section 4** of the Smart Guard embedded systems project.  
It extends the functionality implemented in Sections 1–3 with four advanced capabilities:

1. **Guard Mode / Anti-Theft System**
2. **SQLite Black Box**
3. **Software Watchdog**
4. **Adaptive Thermal Management**

The implementation is organized for a distributed deployment in which the **host computer** can provide the webcam stream and MQTT broker, while the **Linux VM or Orange Pi** runs the web server, person detector, notification client, watchdog, Swagger layer, and persistent event storage.

> **Important:** Configuration files in this repository are examples. Passwords, email credentials, API keys, private keys, and MQTT credentials must never be hard-coded or committed to the repository.

---

## 1. Section 4 Requirements

### 1.1 Guard Mode

Guard Mode adds an armed/disarmed security state to the Smart Guard system.

When Guard Mode is armed:

- Every valid person detection is treated as a security alarm.
- An immediate email is sent.
- The email includes the detection snapshot and event information.
- An emergency MQTT message is published to:

```text
alarm/<student_id>/home
```

- The alarm is recorded in the SQLite black box.
- The current armed state is visible on the HTML dashboard.
- Guard Mode can be enabled or disabled through the API.

When Guard Mode is disarmed, normal person detection and monitoring continue without triggering the emergency alarm workflow.

---

### 1.2 SQLite Black Box

The black box stores detection and system events in SQLite.

Its responsibilities include:

- Recording person detections.
- Recording alarm events.
- Recording watchdog events.
- Recording thermal-management events.
- Saving event timestamps and relevant metadata.
- Saving or referencing the latest available snapshot.
- Maintaining a bounded history through circular-buffer behavior.
- Reporting the total number of person detections through the API.
- Providing recent records for verification and reporting.
- Preserving database integrity across service restarts.

The maximum number of retained records and the database path are controlled through configuration.

---

### 1.3 Software Watchdog

The software watchdog checks whether the image-processing service continues to produce fresh frames.

If no new frame is received for more than **30 seconds**, the system treats the condition as possible camera tampering or camera failure.

The watchdog then:

1. Records a system event.
2. Sends a camera-tamper warning email.
3. Restarts the vision service.
4. Monitors the system until frame processing recovers.
5. Makes the event available through the API and journal logs.

The watchdog is implemented as a separate C service so that it can detect and recover the vision process even when the vision process itself is blocked.

---

### 1.4 Adaptive Thermal Management

Adaptive thermal management protects the system from sustained high CPU temperature.

When the configured temperature threshold is exceeded, the system can automatically:

- Reduce the image-processing FPS.
- Reduce the processing resolution.
- Enter a throttled thermal mode.
- Record the thermal event in the black box.
- Send an email notification.
- Expose the active thermal mode through the API and dashboard.

After the CPU temperature drops below the configured recovery threshold, the system restores normal processing settings.

Using separate activation and recovery thresholds provides hysteresis and helps prevent rapid switching between normal and throttled modes.

---

## 2. High-Level Architecture

```text
+-----------------------------+
| Host Computer               |
|                             |
|  Webcam                     |
|    |                        |
|    v                        |
|  Host Camera Agent (C)      |
|                             |
|  Mosquitto MQTT Broker      |
+-------------+---------------+
              |
              | Camera stream / MQTT / network communication
              |
+-------------v-----------------------------------------------+
| Linux VM or Orange Pi                                       |
|                                                             |
|  Vision Service (Python/OpenCV/MobileNet)                    |
|       |                                                     |
|       +--> Annotated stream, person count, FPS, heartbeat    |
|                                                             |
|  Web Server (C)                                              |
|       +--> HTTPS dashboard and live runtime state            |
|                                                             |
|  MQTT / Notification Service (C)                             |
|       +--> Person, telemetry, alarm, and email notifications |
|                                                             |
|  Watchdog Service (C)                                        |
|       +--> Detects stale frames and restarts vision          |
|                                                             |
|  SQLite Black Box                                            |
|       +--> Detection, alarm, watchdog, and thermal history   |
|                                                             |
|  FastAPI / Swagger Layer                                     |
|       +--> API documentation and test interface              |
+-------------------------------------------------------------+
```

The exact hostnames, ports, paths, credentials, thresholds, and student ID are supplied through environment configuration files.

---

## 3. Directory Structure

The Section 4 directory contains 27 directories and 114 files.

```text
section4/
├── broker/
├── config/
├── docs/
├── host/
├── scripts/
├── src/
├── swagger/
├── systemd/
├── tests/
├── vision/
├── watchdog/
├── web/
├── MANIFEST.txt
├── SHA256SUMS.txt
└── broker_rejection_journal.log
```

### 3.1 `broker/`

Mosquitto broker configuration and access-control files.

```text
broker/
├── acl-smart-guard
└── mosquitto.conf
```

- `mosquitto.conf` configures the MQTT broker.
- `acl-smart-guard` restricts which authenticated clients may publish or subscribe to project topics.

Anonymous MQTT access must remain disabled.

---

### 3.2 `config/`

Example environment files used by the host and VM services.

```text
config/
├── alerts.env.example
├── host-agent.section3.env.example
├── mqtt.env.example
├── section2.env.example
├── section3.env.example
├── section4.env.example
└── vm.env.example
```

Typical configuration groups include:

- Student name and student ID.
- Host and VM addresses.
- Camera stream addresses.
- MQTT broker address, username, password, and topics.
- SMTP server and email credentials.
- Guard Mode settings.
- SQLite database path and circular-buffer limit.
- Watchdog timeout.
- Thermal activation and recovery thresholds.
- Normal and throttled FPS/resolution values.
- Snapshot and runtime-state paths.

Create deployment-specific copies and protect them:

```bash
cp config/section4.env.example config/section4.env
chmod 600 config/section4.env
```

Do not commit the populated file.

---

### 3.3 `docs/evidence/`

Screenshots, videos, logs, JSON responses, database outputs, and test artifacts used in the final report.

```text
docs/evidence/
├── section4_overview/
├── test_4_1_guard/
├── test_4_2_blackbox/
├── test_4_3_watchdog/
└── test_4_4_thermal/
```

#### `section4_overview/`

Contains general evidence for:

- Host-camera and broker pipeline.
- Active VM services.
- Base verification.
- Listening ports.
- Normal dashboard state.
- MobileNet person detection.
- Loaded detection backend.
- Section 4 API runtime status.
- SQLite schema and counters.
- Black-box records and snapshots.
- MQTT QoS 1 acknowledgement.
- Email and thermal configuration.
- 30-second watchdog configuration.
- systemd dependencies and restart policy.
- Swagger endpoints.
- Protected configuration permissions.

#### `test_4_1_guard/`

Contains Guard Mode evidence:

- Guard state before and after arming.
- Armed dashboard.
- Person detection while armed.
- Emergency MQTT message.
- Alarm email with attachment.
- Automated test output.
- Guard Mode demonstration video.

#### `test_4_2_blackbox/`

Contains black-box evidence:

- API output before and after detections.
- SQLite records.
- SQLite integrity-check output.
- Detection counters.
- Latest snapshot.
- Metadata generated by the test.

#### `test_4_3_watchdog/`

Contains watchdog evidence:

- Camera connected state.
- Camera disconnection.
- 30-second stale-frame detection.
- Watchdog journal.
- Vision service restart.
- Camera-tamper email.
- Stream recovery.
- Demonstration video.

#### `test_4_4_thermal/`

Contains adaptive thermal-management evidence:

- Normal mode before stress.
- `stress-ng` execution.
- Thermal mode activation.
- Reduced FPS/resolution.
- Thermal notification email.
- Normal-mode restoration.
- Recorded thermal samples in CSV format.

---

### 3.4 `host/`

Host-side camera agent written in C.

```text
host/
├── CMakeLists.txt
└── smart_guard_host_agent.c
```

The host agent is responsible for the host-side camera pipeline and communication with the VM/board according to the configured Section 3 and Section 4 settings.

---

### 3.5 `scripts/`

Installation, upgrade, model-download, and deployment scripts.

```text
scripts/
├── configure_host_agent.sh
├── download_vision_model.sh
├── install_broker_host.sh
├── install_host_section3.sh
├── install_host_section4.sh
├── install_vm_section3.sh
├── install_vm_section4.sh
├── upgrade_detection_vm.sh
└── upgrade_thermal_stability_vm.sh
```

Main responsibilities:

- Configure and install the host camera agent.
- Install the Mosquitto broker on the host.
- Install Section 3 prerequisites required by Section 4.
- Install Section 4 services on the host and VM.
- Download the MobileNet detection model.
- Upgrade the VM person-detection backend.
- Apply thermal-stability improvements.

Review every script and populate the required environment files before running it.

---

### 3.6 `src/`

Main C notification and MQTT component.

```text
src/
├── CMakeLists.txt
└── smart_guard_notifier.c
```

This component handles notification-oriented runtime logic such as:

- MQTT publishing.
- QoS 1 alarm delivery.
- Email notifications.
- Detection metadata.
- Guard Mode alarm handling.
- Thermal and watchdog notification events.

---

### 3.7 `swagger/`

FastAPI-based Swagger documentation layer.

```text
swagger/
├── __init__.py
├── requirements.txt
└── swagger_api.py
```

The Swagger layer provides an interactive API interface while the main project logic remains in the C services and the vision module.

Use the Swagger UI to:

- Read the current Guard Mode state.
- Arm or disarm Guard Mode.
- Read black-box counters and records.
- Read watchdog and thermal runtime state.
- Test the Section 4 endpoints with live responses.

---

### 3.8 `systemd/`

systemd unit files for automatic startup, dependency management, and recovery.

```text
systemd/
├── smart-guard-mqtt.service
├── smart-guard-swagger.service
├── smart-guard-vision.service
├── smart-guard-watchdog.service
└── smart-guard-web.service
```

The units are designed to:

- Start automatically at boot.
- Restart after a crash.
- Express service ordering with `After=`.
- Express required dependencies with `Requires=`.
- Keep the watchdog independent from the vision process.
- Start network-dependent services only after networking is ready.

---

### 3.9 `tests/`

Automated verification scripts for Sections 3 and 4.

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
├── test_4_1_guard_mode.sh
├── test_4_2_blackbox.sh
├── test_4_3_disconnect_host.sh
├── test_4_3_watchdog_vm.sh
├── test_4_4_stress_host.sh
├── test_4_4_thermal_vm.sh
├── verify_section3.sh
└── verify_section4.sh
```

Section 4 test mapping:

| Project Test | Script(s) | Purpose |
|---|---|---|
| 4-1 | `test_4_1_guard_mode.sh` | Verify arm/disarm, alarm MQTT, email, and black-box update |
| 4-2 | `test_4_2_blackbox.sh` | Verify SQLite records, counters, snapshots, and integrity |
| 4-3 | `test_4_3_disconnect_host.sh` and `test_4_3_watchdog_vm.sh` | Disconnect the camera source and verify watchdog recovery |
| 4-4 | `test_4_4_stress_host.sh` and `test_4_4_thermal_vm.sh` | Raise system load and verify thermal throttling and recovery |

`common.sh` contains shared helper functions and environment-loading logic.

---

### 3.10 `vision/`

Person-detection implementation and model files.

```text
vision/
├── models/
│   ├── deploy.prototxt
│   └── mobilenet_iter_73000.caffemodel
└── person_detector.py
```

The detector:

- Loads the MobileNet Caffe model.
- Detects and counts people.
- Draws bounding boxes.
- Writes the student ID, timestamp, and measured FPS on output frames.
- Produces runtime state used by the web, API, notifier, watchdog, and black-box components.
- Supports normal and thermally throttled processing settings.

---

### 3.11 `watchdog/`

Independent watchdog service written in C.

```text
watchdog/
├── CMakeLists.txt
└── smart_guard_watchdog.c
```

The watchdog checks the freshness of the latest processed frame and triggers the configured recovery workflow after the stale-frame timeout.

---

### 3.12 `web/`

Main C web server and HTML dashboard.

```text
web/
├── CMakeLists.txt
└── smart_guard_web.c
```

The dashboard presents:

- Live camera stream.
- Current person count.
- CPU temperature.
- CPU utilization.
- Free memory.
- Current Guard Mode state.
- Guard Mode control.
- Watchdog status.
- Thermal mode.
- Black-box counters and recent state.

HTTPS and HTTP-to-HTTPS redirection are inherited from the earlier project sections.

---

### 3.13 Root Files

#### `MANIFEST.txt`

Lists the files expected in the Section 4 submission package.

#### `SHA256SUMS.txt`

Contains SHA-256 hashes for package-integrity verification.

Verify the package with:

```bash
sha256sum -c SHA256SUMS.txt
```

#### `broker_rejection_journal.log`

Evidence showing rejected or unauthorized MQTT access attempts.

---

## 4. Main Runtime Services

| Service | Main Responsibility |
|---|---|
| `smart-guard-web.service` | HTTPS dashboard, live status, and web controls |
| `smart-guard-vision.service` | Person detection and annotated frame generation |
| `smart-guard-mqtt.service` | MQTT publishing and notification integration |
| `smart-guard-watchdog.service` | Stale-frame detection and vision-service recovery |
| `smart-guard-swagger.service` | Swagger/OpenAPI interface |

Check all services:

```bash
sudo systemctl status \
    smart-guard-web.service \
    smart-guard-vision.service \
    smart-guard-mqtt.service \
    smart-guard-watchdog.service \
    smart-guard-swagger.service
```

Restart all Section 4 services:

```bash
sudo systemctl restart \
    smart-guard-vision.service \
    smart-guard-mqtt.service \
    smart-guard-watchdog.service \
    smart-guard-web.service \
    smart-guard-swagger.service
```

View recent logs:

```bash
sudo journalctl \
    -u smart-guard-web.service \
    -u smart-guard-vision.service \
    -u smart-guard-mqtt.service \
    -u smart-guard-watchdog.service \
    -u smart-guard-swagger.service \
    --since "10 minutes ago" \
    --no-pager
```

Follow logs in real time:

```bash
sudo journalctl -f \
    -u smart-guard-vision.service \
    -u smart-guard-watchdog.service \
    -u smart-guard-mqtt.service
```

---

## 5. Installation Outline

The scripts are split between the host computer and the VM/Orange Pi.

### 5.1 Host Computer

Typical host-side installation order:

```bash
cd ~/embedded/embedded_project/section4

sudo bash scripts/install_broker_host.sh
sudo bash scripts/install_host_section3.sh
sudo bash scripts/install_host_section4.sh
```

Configure the host agent when required:

```bash
sudo bash scripts/configure_host_agent.sh
```

---

### 5.2 VM or Orange Pi

Typical VM-side installation order:

```bash
cd ~/embedded/embedded_project/section4

sudo bash scripts/install_vm_section3.sh
sudo bash scripts/install_vm_section4.sh
```

Install or refresh the vision model:

```bash
bash scripts/download_vision_model.sh
sudo bash scripts/upgrade_detection_vm.sh
```

Apply the thermal-stability upgrade when required:

```bash
sudo bash scripts/upgrade_thermal_stability_vm.sh
```

After installation, reload systemd and restart the services:

```bash
sudo systemctl daemon-reload

sudo systemctl restart \
    smart-guard-vision.service \
    smart-guard-mqtt.service \
    smart-guard-watchdog.service \
    smart-guard-web.service \
    smart-guard-swagger.service
```

> The scripts may install files under system directories. Read each script before execution and confirm all configured paths, usernames, IP addresses, and service names.

---

## 6. Manual Build

The installer scripts are the preferred deployment method. Individual C components can also be built manually.

### Host Agent

```bash
cmake -S host -B build/host
cmake --build build/host -j"$(nproc)"
```

### Notifier / MQTT Component

```bash
cmake -S src -B build/notifier
cmake --build build/notifier -j"$(nproc)"
```

### Watchdog

```bash
cmake -S watchdog -B build/watchdog
cmake --build build/watchdog -j"$(nproc)"
```

### Web Server

```bash
cmake -S web -B build/web
cmake --build build/web -j"$(nproc)"
```

### Swagger Environment

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r swagger/requirements.txt
```

### Vision Syntax Check

```bash
python3 -m py_compile vision/person_detector.py
```

---

## 7. MQTT Topics

The project uses authenticated MQTT communication.

Main topic families include:

```text
persons/<student_id>/home
telemetry/<student_id>/home
alarm/<student_id>/home
```

The emergency alarm topic is:

```text
alarm/<student_id>/home
```

Alarm messages should be JSON and include enough information to identify the event, for example:

```json
{
  "event": "guard_alarm",
  "student_id": "<student_id>",
  "persons": 1,
  "timestamp": "2026-07-29T20:57:42+03:30",
  "guard_armed": true
}
```

The actual payload fields are defined by the running C service.

MQTT security requirements:

- Anonymous access disabled.
- Username/password authentication enabled.
- ACL rules enabled.
- No credentials hard-coded in source files.
- QoS 1 used for important alarm delivery.
- LWT retained from the Section 3 implementation.

---

## 8. API and Swagger

The project uses a thin FastAPI layer for OpenAPI documentation and interactive testing. Core system logic remains in C, except for image processing.

The Section 4 API covers:

- Reading Guard Mode state.
- Arming Guard Mode.
- Disarming Guard Mode.
- Reading black-box counters.
- Reading recent black-box records.
- Reading watchdog state.
- Reading thermal-management state.
- Reading combined runtime status.

Open the Swagger UI using the address and port defined by the installed configuration.

Because deployment ports may differ between environments, verify the active listening ports with:

```bash
sudo ss -lntup
```

---

## 9. Running the Section 4 Verification

Run the complete verification script:

```bash
cd ~/embedded/embedded_project/section4
sudo bash tests/verify_section4.sh
```

Run individual tests:

```bash
sudo bash tests/test_4_1_guard_mode.sh
sudo bash tests/test_4_2_blackbox.sh
sudo bash tests/test_4_3_watchdog_vm.sh
sudo bash tests/test_4_4_thermal_vm.sh
```

Some tests are coordinated between the host and VM.

### Watchdog Test

On the VM:

```bash
sudo bash tests/test_4_3_watchdog_vm.sh
```

On the host, disconnect or stop the camera source as required by the test:

```bash
sudo bash tests/test_4_3_disconnect_host.sh
```

The expected result is a stale-frame event after approximately 30 seconds, a tamper email, a vision-service restart, and eventual stream recovery.

### Thermal Test

On the VM:

```bash
sudo bash tests/test_4_4_thermal_vm.sh
```

On the machine where stress is generated:

```bash
sudo bash tests/test_4_4_stress_host.sh
```

The expected result is thermal-mode activation, lower FPS or resolution, a thermal email, recorded samples, and restoration of normal settings after cooling.

---

## 10. Required Section 4 Evidence

### Test 4-1 — Guard Mode

Required evidence:

- Guard Mode disarmed before the test.
- Guard Mode armed through the API or dashboard.
- A person detected while armed.
- Emergency MQTT message received.
- Alarm email with attached image.
- Guard Mode disarmed after the test.
- Demonstration video.
- Automated test result.

### Test 4-2 — Black Box

Required evidence:

- SQLite schema.
- Records before and after detections.
- Total detection counter.
- Recent-event API response.
- SQLite integrity check.
- Latest detection snapshot.

### Test 4-3 — Software Watchdog

Required evidence:

- Camera working before disconnection.
- Camera source disconnected.
- No-frame timeout reached.
- Watchdog event recorded.
- Vision service restarted.
- Camera-tamper email received.
- Stream recovered.
- Demonstration video.

### Test 4-4 — Adaptive Thermal Management

Required evidence:

- Normal operating mode.
- Stress tool running.
- CPU temperature crossing the threshold.
- Thermal mode activated.
- FPS or resolution reduced.
- Thermal warning email.
- Normal mode restored after cooling.
- Temperature samples saved to CSV.

---

## 11. Security Notes

The complete project must follow these rules:

- Do not store passwords or API keys in source code.
- Protect populated environment files with restrictive permissions.
- Disable anonymous MQTT access.
- Use MQTT authentication and ACL rules.
- Disable direct root SSH login.
- Use the configured SSH authentication policy.
- Keep HTTPS enabled.
- Keep HTTP-to-HTTPS redirection enabled.
- Protect SSL private keys.
- Restrict database, snapshot, and runtime-state file permissions.
- Do not publish real credentials in screenshots, logs, videos, or the final report.

Check configuration permissions:

```bash
find config -maxdepth 1 -type f -printf '%M %u:%g %p\n'
```

Check service hardening and dependencies:

```bash
systemctl cat smart-guard-web.service
systemctl cat smart-guard-vision.service
systemctl cat smart-guard-mqtt.service
systemctl cat smart-guard-watchdog.service
systemctl cat smart-guard-swagger.service
```

---

## 12. Useful Diagnostic Commands

### Check Active Services

```bash
systemctl --type=service --state=running | grep smart-guard
```

### Check Failed Services

```bash
systemctl --failed
```

### Check Listening Ports

```bash
sudo ss -lntup
```

### Check the Camera Device on the Host

```bash
fuser -v /dev/video0
```

### Stop the Host Camera Agent Temporarily

```bash
sudo systemctl stop smart-guard-host-agent.service
```

Use the installed host service name if it differs.

### Check MQTT Connectivity

```bash
mosquitto_sub \
    -h <broker_host> \
    -p <broker_port> \
    -u <mqtt_username> \
    -P '<mqtt_password>' \
    -t 'alarm/<student_id>/home' \
    -q 1 \
    -v
```

### Check SQLite Integrity

```bash
sqlite3 <blackbox_database_path> 'PRAGMA integrity_check;'
```

### Inspect Recent Black-Box Records

```bash
sqlite3 -header -column <blackbox_database_path> \
    'SELECT * FROM events ORDER BY id DESC LIMIT 10;'
```

Adjust the table name if the deployed schema uses a different name.

---

## 13. Troubleshooting

### Vision Service Does Not Start

Check:

```bash
sudo systemctl status smart-guard-vision.service
sudo journalctl -u smart-guard-vision.service -n 100 --no-pager
```

Confirm that both model files exist:

```bash
ls -lh vision/models/deploy.prototxt
ls -lh vision/models/mobilenet_iter_73000.caffemodel
```

Validate the Python file:

```bash
python3 -m py_compile vision/person_detector.py
```

---

### No Alarm MQTT Message

Verify:

- Guard Mode is armed.
- A person is actually detected.
- The student ID is correct.
- MQTT credentials are correct.
- Broker ACL permits publishing to the alarm topic.
- The broker is reachable.
- The subscriber uses the exact topic.
- The notifier service is active.

```bash
sudo journalctl -u smart-guard-mqtt.service -n 100 --no-pager
```

---

### Alarm Email Is Not Sent

Verify:

- SMTP host and port.
- Sender and recipient addresses.
- Email password or application password.
- TLS settings.
- Snapshot path and permissions.
- Network connectivity.
- Notifier journal.

Never print the email password in logs.

---

### Watchdog Does Not Trigger

Verify:

- The watchdog service is active.
- The configured timeout is 30 seconds.
- The vision heartbeat/frame timestamp file is correct.
- The camera source was actually stopped.
- The watchdog user can restart the vision service.
- systemd permissions allow the configured restart action.

```bash
sudo journalctl -u smart-guard-watchdog.service -f
```

---

### Thermal Mode Does Not Activate

Verify:

- The CPU-temperature source is readable.
- The activation threshold is reachable.
- The recovery threshold is lower than the activation threshold.
- The stress process is running long enough.
- Thermal state files are writable.
- The vision service is reading the updated thermal mode.
- The email debounce policy is not hiding repeated notifications.

---

### SQLite Database Is Locked

Possible causes:

- Multiple writers without a busy timeout.
- A transaction that is not committed.
- Incorrect database permissions.
- Database stored on an unsuitable shared filesystem.

Check the database and service logs, then run:

```bash
sqlite3 <blackbox_database_path> 'PRAGMA integrity_check;'
```

---

## 14. Submission Checklist

Before submission, confirm that:

- [ ] Guard Mode can be armed and disarmed through the API.
- [ ] Guard Mode state is visible on the HTML dashboard.
- [ ] Armed person detection sends an immediate email with a snapshot.
- [ ] Armed person detection publishes to `alarm/<student_id>/home`.
- [ ] Alarm MQTT delivery uses QoS 1.
- [ ] Detection history is stored in SQLite.
- [ ] The black box behaves as a bounded circular buffer.
- [ ] The API reports the total number of person detections.
- [ ] The watchdog detects more than 30 seconds without a fresh frame.
- [ ] The watchdog sends a camera-tamper email.
- [ ] The watchdog restarts the vision service.
- [ ] Adaptive thermal management reduces FPS or resolution.
- [ ] Thermal activation sends an email.
- [ ] Normal settings return after cooling.
- [ ] All systemd services start at boot.
- [ ] All services restart after failure.
- [ ] MQTT authentication and ACL rules are enabled.
- [ ] Secrets are not committed.
- [ ] `verify_section4.sh` passes.
- [ ] All required screenshots, logs, JSON files, CSV files, and videos are present.
- [ ] `sha256sum -c SHA256SUMS.txt` passes.

---

## 15. Project Integrity

List the submitted files:

```bash
cat MANIFEST.txt
```

Verify all hashes:

```bash
sha256sum -c SHA256SUMS.txt
```

A clean verification result should report `OK` for every listed file.

---

## 16. License and Academic Use

This repository is an academic implementation of the Smart Guard final project for the Real-Time Embedded Systems course.

The project is intended for individual academic submission. Reuse must follow the course rules and the university's academic-integrity policy.
