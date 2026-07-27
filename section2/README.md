# Section 2 — RESTful API and Live Monitoring

## Overview

This directory contains the complete implementation, deployment scripts, tests, Swagger documentation, and collected evidence for **Section 2: RESTful API and Live Monitoring** of the Smart Guard embedded systems project.

The main objective of this section is to expose the system's live functionality through a documented REST API. The implemented API provides access to:

- Live camera streaming in MJPEG format
- Current detected-person count with a timestamp
- CPU usage, available memory, and CPU temperature
- Extensible administrative commands such as `ping`, `history_clear`, and `reboot`
- The five most recent person-detection records
- Swagger/OpenAPI documentation for testing every endpoint

A major project requirement is that the operational logic must be implemented in **C**. FastAPI is used only as a thin API gateway and Swagger/OpenAPI documentation layer.

The C service is responsible for:

- Reading CPU statistics directly from `/proc/stat`
- Reading memory information directly from `/proc/meminfo`
- Reading CPU temperature from `/sys`
- Receiving physical-host temperature data when local VM sensors are unavailable
- Receiving JPEG frames from the physical host
- Producing the MJPEG stream
- Reading the current person count
- Maintaining the five-record detection history
- Authenticating and executing commands
- Handling camera disconnection and automatic reconnection

FastAPI does not calculate telemetry values or execute commands. It forwards requests to the internal C API and documents their input and output schemas.

---

## Implemented Requirements

The following required endpoints are implemented:

| Endpoint | Method | Description |
|---|---|---|
| `/api/v1/stream` | `GET` | Returns the live camera feed as an MJPEG stream |
| `/api/v1/persons` | `GET` | Returns the current number of detected persons and a timestamp |
| `/api/v1/telemetry` | `GET` | Returns CPU usage, memory information, CPU temperature, camera status, and related telemetry |
| `/api/v1/command` | `POST` | Executes an authenticated command handled by the C command registry |
| `/api/v1/history` | `GET` | Returns the five most recent detection records |
| `/health` | `GET` | Verifies that the FastAPI gateway and the C core are reachable |

Uppercase compatibility aliases such as `/API/V1/TELEMETRY` are also supported where required, but they are hidden from the Swagger schema to avoid duplicate endpoint entries.

---

## System Architecture

The project is executed using a physical Ubuntu host and a Linux virtual machine.

The physical host has direct access to:

- The webcam
- Physical CPU temperature sensors
- `/dev/video*`
- `/sys/class/thermal`
- `/sys/class/hwmon`

The virtual machine runs:

- The main C REST service
- The FastAPI/Swagger gateway
- The person-detection service
- The mandatory test scripts

The data flow is:

```text
Physical Host
│
├── Webcam frames
│   └── JPEG over TCP port 9100
│
├── Physical CPU temperature
│   └── Temperature packets over UDP port 9090
│
▼
Virtual Machine
│
├── smart_guard_web C service
│   ├── Internal API: http://127.0.0.1:18080
│   ├── Reads /proc/stat
│   ├── Reads /proc/meminfo
│   ├── Reads local /sys sensors when available
│   ├── Receives host temperature
│   ├── Receives JPEG camera frames
│   ├── Produces MJPEG output
│   ├── Maintains history
│   └── Executes authenticated commands
│
▼
FastAPI / Swagger Gateway
│
├── HTTPS port 8443
└── Swagger UI: https://<VM-IP>:8443/docs
```

In the current development environment, the VM address is typically:

```text
192.168.122.186
```

The Swagger page is therefore available at:

```text
https://192.168.122.186:8443/docs
```

The C core is intentionally bound to the loopback interface:

```text
http://127.0.0.1:18080
```

This prevents the internal implementation service from being exposed directly to the external network.

---

## API Details

### Live MJPEG Stream

```http
GET /api/v1/stream
```

The endpoint returns:

```text
Content-Type: multipart/x-mixed-replace; boundary=smartguardframe
```

Each multipart section contains:

- A boundary
- `Content-Type: image/jpeg`
- `Content-Length`
- `X-Frame-Sequence`
- JPEG image bytes

An optional `frames` query parameter is supported.

```text
frames=0
```

keeps the stream open indefinitely.

```text
frames=1
```

returns one real JPEG frame and closes the response. This mode is useful for testing the endpoint from Swagger, because Swagger UI cannot finish an infinite MJPEG request.

Example:

```text
GET /api/v1/stream?frames=1
```

---

### Current Person Count

```http
GET /api/v1/persons
```

Example response:

```json
{
  "student_id": "401102553",
  "timestamp": "2026-07-27T21:24:08+0330",
  "persons": 1
}
```

The value is read by the C service from the runtime output of the vision service.

The expected runtime file is:

```text
/run/smart-guard/person_count
```

Invalid, missing, negative, or unreasonable values are rejected and safely handled.

---

### Telemetry

```http
GET /api/v1/telemetry
```

Example response:

```json
{
  "student_name": "Amir Hossein Motiei",
  "student_id": "401102553",
  "timestamp": "2026-07-27T21:24:14+0330",
  "cpu_usage_percent": 12.935,
  "memory_total_kb": 1494600,
  "memory_free_kb": 70076,
  "memory_available_kb": 1033484,
  "cpu_temperature_available": true,
  "cpu_temperature_stale": false,
  "cpu_temperature_c": 59,
  "temperature_source": "host_sysfs_udp",
  "persons": 1,
  "camera_connected": true,
  "last_frame_age_seconds": 0.006
}
```

CPU usage is calculated from two consecutive reads of:

```text
/proc/stat
```

Memory information is read directly from:

```text
/proc/meminfo
```

Temperature is read from:

```text
/sys/class/thermal/thermal_zone*/temp
/sys/class/hwmon/hwmon*/temp*_input
```

When the VM does not expose a physical CPU temperature sensor, the C core uses the value read from the physical host and received over UDP.

The `temperature_source` field identifies the source:

```text
local_sysfs
host_sysfs_udp
```

The `cpu_temperature_stale` field becomes `true` when no new temperature packet is received within the configured timeout.

The `camera_connected` and `last_frame_age_seconds` fields help distinguish an active camera connection from a connection that has stopped delivering fresh frames.

---

### Detection History

```http
GET /api/v1/history
```

The C service stores the latest five records in a fixed-size ring buffer.

Each record contains:

- An incremental record ID
- A timestamp
- The detected-person count

The fixed-size design ensures that history memory usage does not grow over time.

When a sixth record is inserted, it replaces the oldest record.

---

### Command Endpoint

```http
POST /api/v1/command
```

Example request:

```json
{
  "cmd": "ping"
}
```

Available commands currently include:

| Command | Description |
|---|---|
| `ping` | Verifies command endpoint and C core availability |
| `history_clear` | Clears the five-record in-memory history |
| `reboot` | Reboots the VM using the Linux reboot system call |

The command implementation uses an extensible command registry. New commands can be added by implementing a new handler and registering it in the command table.

The endpoint requires the following request header:

```text
X-Command-Token
```

The real token must not be stored in source code or committed to Git.

The token is loaded through the service environment configuration.

Typical response codes include:

| Status | Meaning |
|---|---|
| `202 Accepted` | The command was accepted |
| `400 Bad Request` | Invalid JSON or unknown command |
| `401 Unauthorized` | Missing or invalid command token |
| `422 Unprocessable Entity` | Invalid FastAPI request schema |
| `500 Internal Server Error` | Command execution failed |
| `503 Service Unavailable` | The C core is unavailable |

The reboot command uses:

```c
reboot(RB_AUTOBOOT)
```

instead of running a shell command such as:

```c
system("reboot")
```

A short delay is used so that the HTTP `202 Accepted` response can be sent before the VM restarts.

---

## Directory Structure

```text
section2/
├── config/
├── docs/
├── scripts/
├── systemd/
├── tests/
├── vm/
├── README.md
└── SHA256SUMS.txt
```

### `config/`

Contains configuration templates.

```text
config/
└── section2.env.example
```

`section2.env.example` documents the environment variables required by the section.

Sensitive values such as the command token must only be stored in the real deployment environment file and must not be committed to the repository.

The deployed environment is typically stored outside the source tree, for example:

```text
/etc/smart-guard/section2.env
```

---

### `vm/src/`

Contains the main C implementation.

```text
vm/src/
├── CMakeLists.txt
├── smart_guard_web.c
└── smart_guard_web.c.bak-before-recv-timeout-fix
```

`smart_guard_web.c` implements:

- HTTP routing with `libmicrohttpd`
- CPU telemetry
- Memory telemetry
- Temperature sensor discovery
- Host temperature reception
- Camera TCP reception
- MJPEG generation
- Person-count reading
- Ring-buffer detection history
- Command authentication
- Command registry
- Reboot execution
- Network-disconnection recovery

`CMakeLists.txt` contains the C build configuration and required library definitions.

The `.bak` file is a development backup created before the camera receive-timeout recovery fix. It is not used by the running system.

---

### `vm/swagger/`

Contains the FastAPI and Swagger gateway.

```text
vm/swagger/
├── __init__.py
├── requirements.txt
└── swagger_api.py
```

`swagger_api.py` defines:

- OpenAPI metadata
- Pydantic request and response models
- Endpoint descriptions
- Response examples
- Authentication header documentation
- Error schemas
- Proxy requests to the C core
- Raw MJPEG response forwarding

`requirements.txt` contains the Python packages required by the Swagger service.

The `__pycache__` directory is generated automatically by Python and is not required in source control.

---

### `systemd/`

Contains systemd service definitions.

```text
systemd/
├── smart-guard-swagger.service
└── smart-guard-web.service
```

`smart-guard-web.service` starts the C core.

`smart-guard-swagger.service` starts the FastAPI/Swagger gateway.

Typical status checks:

```bash
systemctl status smart-guard-web.service
systemctl status smart-guard-swagger.service
```

Quick active-state checks:

```bash
systemctl is-active smart-guard-web.service
systemctl is-active smart-guard-swagger.service
```

The services are configured to start automatically when the VM boots.

---

### `scripts/`

Contains deployment and environment helper scripts.

```text
scripts/
├── copy_to_vm.sh
├── host_only_check.sh
├── install_vm_section2.sh
└── stage_on_host.sh
```

`stage_on_host.sh` prepares Section 2 files and dependencies on the physical host.

`copy_to_vm.sh` copies the required project files from the host to the virtual machine.

`install_vm_section2.sh` installs and configures the C service, Swagger service, systemd units, Python environment, and required runtime directories inside the VM.

`host_only_check.sh` performs checks that must be run on the physical host, such as validating host-side camera and temperature access.

The scripts should be executed from the project environment for which they were written. Review environment-specific values such as usernames, VM addresses, and destination paths before deployment.

---

### `tests/`

Contains the mandatory test scripts and analysis utilities.

```text
tests/
├── analyze_test_2_3.py
├── common.sh
├── plot_test_2_1.py
├── plot_test_2_2.py
├── test_2_1_temperature.sh
├── test_2_2_memory.sh
├── test_2_3_load.sh
├── test_2_4_network_recovery.sh
├── test_2_4_network_recovery.sh.bak
└── verify_endpoints.sh
```

`common.sh` defines shared variables such as:

```text
API_BASE
EVIDENCE_ROOT
VM_IP
```

The test scripts are designed to run inside the VM unless explicitly stated otherwise.

`verify_endpoints.sh` performs a quick API verification.

`test_2_1_temperature.sh` collects temperature data for the three mandatory operating modes.

`plot_test_2_1.py` generates the three-mode temperature plot and maximum-temperature table.

`test_2_2_memory.sh` records C-process memory usage during continuous streaming.

`plot_test_2_2.py` generates the memory curve and leak-analysis output.

`test_2_3_load.sh` starts 50 concurrent telemetry request loops for 30 seconds.

`analyze_test_2_3.py` calculates request statistics and generates latency and telemetry plots.

`test_2_4_network_recovery.sh` records API, camera, frame-age, and temperature-freshness status during a network interruption.

The `.bak` file is retained only for development comparison and is not used by the official test.

---

## Mandatory Tests

### Test 2-1 — Temperature in Three Operating Modes

The test records CPU temperature for five minutes with a 30-second sampling interval in the following modes:

1. Idle
2. Stream only
3. Stream with active detection

Commands:

```bash
bash tests/test_2_1_temperature.sh idle
bash tests/test_2_1_temperature.sh stream
bash tests/test_2_1_temperature.sh stream_detection
```

After collecting all three datasets:

```bash
python3 tests/plot_test_2_1.py \
  evidence/test_2_1_temperature
```

Official result files:

```text
docs/evidence/test_2_1/data/idle.csv
docs/evidence/test_2_1/data/stream.csv
docs/evidence/test_2_1/data/stream_detection.csv
docs/evidence/test_2_1/data/maximum_temperature.csv
```

Official report images:

```text
docs/evidence/test_2_1/S2_T21_01_idle_end.png
docs/evidence/test_2_1/S2_T21_02_stream_only_end.png
docs/evidence/test_2_1/S2_T21_03_stream_detection_end.png
docs/evidence/test_2_1/S2_T21_04_temperature_three_modes.png
docs/evidence/test_2_1/S2_T21_05_max_temperature_table.png
```

Observed maximum temperatures:

| Mode | Maximum Temperature |
|---|---:|
| Idle | 61°C |
| Stream only | 78°C |
| Stream with detection | 58°C |

The 78°C stream-only value occurred only in the first sample and returned to approximately 50°C in the next sample. The raw value was retained without modification and documented as a transient startup observation.

---

### Test 2-2 — C Process Memory During Continuous Streaming

The test records the C service memory usage for five minutes at a five-second sampling interval while one continuous MJPEG stream is active.

Command:

```bash
bash tests/test_2_2_memory.sh
```

The test reads process memory information from:

```text
/proc/<PID>/status
```

Main output:

```text
docs/evidence/test_2_2/data/c_process_memory.csv
```

Official report images:

```text
docs/evidence/test_2_2/S2_T22_01_c_memory_curve.png
docs/evidence/test_2_2/S2_T22_02_memory_leak_analysis.png
```

Observed results:

| Metric | Value |
|---|---:|
| Initial RSS | 6740 KB |
| Final RSS | 7032 KB |
| Peak RSS | 7044 KB |
| Net growth | 292 KB |
| First-half average | 7016.67 KB |
| Second-half average | 7025.73 KB |
| Average difference | 9.07 KB |

The process showed a small initial allocation increase and then stabilized near 7 MB. No sustained memory-leak pattern was detected during the five-minute test.

---

### Test 2-3 — 50 Concurrent Telemetry Request Loops

The test first sends 20 sequential baseline requests and then runs 50 concurrent `curl` loops for 30 seconds against:

```text
/api/v1/telemetry
```

Command:

```bash
bash tests/test_2_3_load.sh
```

Official data files:

```text
docs/evidence/test_2_3/data/baseline_latencies.csv
docs/evidence/test_2_3/data/latencies.csv
docs/evidence/test_2_3/data/telemetry_during_load.csv
```

Official report images:

```text
docs/evidence/test_2_3/S2_T23_01_load_summary.png
docs/evidence/test_2_3/S2_T23_02_latency_histogram.png
docs/evidence/test_2_3/S2_T23_03_telemetry_under_load.png
```

Observed request results:

| Metric | Value |
|---|---:|
| Concurrent workers | 50 |
| Total load requests | 966 |
| Successful HTTP 200 | 966 |
| Failed requests | 0 |
| Success rate | 100% |
| Approximate throughput | 32.20 requests/s |
| Baseline mean latency | 32.040 ms |
| Load mean latency | 1542.485 ms |
| Load P95 | 1598.459 ms |
| Load P99 | 1634.513 ms |
| Mean latency increase | 1510.446 ms |
| Relative increase | 4714.294% |

Observed telemetry changes:

| Metric | Initial | Final | Maximum or Minimum |
|---|---:|---:|---:|
| CPU usage | 24.623% | 58.794% | Maximum 64.356% |
| Available memory | 1084120 KB | 952848 KB | Minimum 912660 KB |
| CPU temperature | 52°C | 88°C | Maximum 94°C |

The API returned all requests successfully, but latency and system load increased significantly.

---

### Test 2-4 — Network Interruption and Automatic Recovery

The test keeps an MJPEG stream active, disconnects the VM network for two minutes, reconnects it, and records the recovery behavior.

The official timeline is:

```text
0–30 seconds: network connected
30–150 seconds: network disconnected
150–240 seconds: network restored and recovery observed
```

The VM network interface was controlled from the physical host using:

```bash
sudo virsh domif-setlink \
  embedded-base \
  52:54:00:64:6f:0c \
  down
```

After 120 seconds:

```bash
sudo virsh domif-setlink \
  embedded-base \
  52:54:00:64:6f:0c \
  up
```

The VM-side recorder is:

```bash
bash tests/test_2_4_network_recovery.sh
```

Official data:

```text
docs/evidence/test_2_4/data/recovery_status.csv
```

Official report images:

```text
docs/evidence/test_2_4/S2_T24_01_recovery_status.png
docs/evidence/test_2_4/S2_T24_02_disconnect_reconnect_logs_a.png
docs/evidence/test_2_4/S2_T24_02_disconnect_reconnect_logs_b.png
```

Two log screenshots are used because the complete output did not fit readably in one image.

The journal recorded:

```text
Host camera disconnected; waiting for automatic reconnect
Host camera connected
```

Final recovery state:

```json
{
  "camera_connected": true,
  "last_frame_age_seconds": 0.021,
  "cpu_temperature_c": 49,
  "cpu_temperature_stale": false
}
```

The web service remained active, the local API remained available, and both camera frames and temperature telemetry recovered without restarting the VM or the C service.

---

## Evidence Directory

All official screenshots, plots, raw CSV files, JSON results, and logs are stored under:

```text
docs/evidence/
```

Structure:

```text
docs/evidence/
├── swagger/
├── test_2_1/
├── test_2_2/
├── test_2_3/
└── test_2_4/
```

### Swagger Evidence

```text
docs/evidence/swagger/
```

Contains live Swagger executions for:

- MJPEG stream
- Current person count
- Telemetry
- Command execution
- Detection history

Some screenshots are split into multiple images to preserve readability.

The command-token screenshot must never expose the real token.

### Test Evidence

Each test directory contains:

```text
data/
```

for raw CSV or JSON-compatible test data,

```text
logs/
```

for test summaries, system journals, and written analysis,

and top-level PNG files for official report figures.

---

## Verification

Quick endpoint verification can be performed with:

```bash
bash tests/verify_endpoints.sh
```

Manual C-core checks:

```bash
curl -s \
  http://127.0.0.1:18080/api/v1/telemetry |
jq
```

Manual Swagger-gateway checks:

```bash
curl -sk \
  https://127.0.0.1:8443/api/v1/telemetry |
jq
```

One-frame stream verification:

```bash
curl -sk \
  "https://127.0.0.1:8443/api/v1/stream?frames=1" \
  -o /tmp/smart_guard_frame_output.bin
```

Service logs:

```bash
journalctl \
  -u smart-guard-web.service \
  --no-pager
```

```bash
journalctl \
  -u smart-guard-swagger.service \
  --no-pager
```

Recent logs:

```bash
journalctl \
  -u smart-guard-web.service \
  --since "10 minutes ago" \
  --no-pager
```

---

## Configuration and Security Notes

The real command token must never be:

- Written directly into `smart_guard_web.c`
- Written directly into `swagger_api.py`
- Added to Swagger screenshots
- Committed to Git
- Included in public reports

Use the environment-file template:

```text
config/section2.env.example
```

Create the real deployment configuration outside the repository, typically:

```text
/etc/smart-guard/section2.env
```

Protect the real file with restrictive permissions.

Example:

```bash
sudo chown root:root \
  /etc/smart-guard/section2.env

sudo chmod 600 \
  /etc/smart-guard/section2.env
```

The Swagger screenshots contain a masked token value only.

---

## Repository Hygiene

The following files and directories are development artifacts and are not required by the runtime system:

```text
vm/swagger/__pycache__/
*.pyc
*.bak
```

Backup files are currently retained for development history:

```text
tests/test_2_4_network_recovery.sh.bak
vm/src/smart_guard_web.c.bak-before-recv-timeout-fix
```

They may be excluded from release packages after the final implementation has been verified.

Some legacy Swagger evidence filenames contain a Unicode elongation character. When renaming them, update all report references consistently.

Recommended ASCII-only names include:

```text
S2_SW_03_telemetry_swagger_200a.png
S2_SW_03_telemetry_swagger_200b.png
S2_SW_04_command_ping_swagger_202_token.png
```

---

## Integrity Verification

`SHA256SUMS.txt` contains SHA-256 hashes for project or evidence files.

To verify checksums from the Section 2 directory:

```bash
sha256sum -c SHA256SUMS.txt
```

A successful verification should report each tracked file as:

```text
OK
```

Regenerate checksums only after intentionally modifying the tracked files.

---

## Main Runtime Components

The expected services are:

```text
smart-guard-web.service
smart-guard-swagger.service
smart-guard-vision.service
smart-guard-host-agent.service
```

The first two are defined in this directory.

The vision service provides the person-count runtime file.

The host-agent service runs on the physical host and provides:

- Webcam JPEG frames
- Physical CPU temperature

Typical checks inside the VM:

```bash
systemctl is-active smart-guard-web.service
systemctl is-active smart-guard-swagger.service
systemctl is-active smart-guard-vision.service
```

Typical check on the physical host:

```bash
systemctl is-active smart-guard-host-agent.service
```

---

## Troubleshooting

### Swagger is available but returns `503`

Check the C service:

```bash
systemctl status smart-guard-web.service
```

Check the internal endpoint:

```bash
curl -s \
  http://127.0.0.1:18080/health
```

### Camera is disconnected

Check host agent status on the physical host:

```bash
systemctl status \
  smart-guard-host-agent.service
```

Check C-service logs inside the VM:

```bash
journalctl \
  -u smart-guard-web.service \
  --since "5 minutes ago" \
  --no-pager
```

Look for:

```text
Host camera disconnected
Host camera connected
```

### Temperature is stale

Confirm that the host agent is running and that UDP traffic between the physical host and VM is available.

Inspect telemetry:

```bash
curl -s \
  http://127.0.0.1:18080/api/v1/telemetry |
jq '{
  cpu_temperature_c,
  cpu_temperature_stale,
  temperature_source
}'
```

### Person count always returns zero

Check the vision service:

```bash
systemctl status \
  smart-guard-vision.service
```

Inspect the runtime file:

```bash
cat /run/smart-guard/person_count
```

### Command returns `401`

Verify that the `X-Command-Token` header is present and matches the value configured in the protected environment file.

Do not print the real token in logs or screenshots.

### Stream does not finish in Swagger

Set:

```text
frames=1
```

An infinite MJPEG response is expected to remain open and cannot be completed normally by Swagger UI.

---

## Final Status

Section 2 currently includes:

- Complete C-based REST logic
- FastAPI/OpenAPI documentation
- Swagger execution evidence for all mandatory endpoints
- Direct `/proc` CPU and memory reading
- Direct `/sys` temperature reading
- Physical-host temperature forwarding
- Live MJPEG camera streaming
- Five-record ring-buffer history
- Extensible authenticated command registry
- Real reboot execution
- Automatic camera reconnection
- All four mandatory experiments
- Raw CSV data
- Logs and written analyses
- Official plots and screenshots
- SHA-256 integrity information

The implementation and collected evidence cover the required functionality and mandatory tests for the RESTful API and live-monitoring section of the project.
