# Smart Guard - Section 1  
## Web Server, SSL Security, System Services, Vision and MQTT Telemetry

## 1. Overview

This section implements the first stage of the **Smart Guard embedded security system**.

The main goal of this section is to design and implement an autonomous monitoring platform that can collect system information, receive camera streams, detect people, and provide a secure web-based monitoring dashboard.

Because the development environment is based on a virtual machine instead of a physical embedded board, the system architecture is divided into two main parts:

- **Host System (Laptop):**  
  Responsible for accessing physical resources such as webcam and CPU temperature, then transferring the collected data to the virtual embedded system.

- **Smart Guard Virtual Machine:**  
  Works as the embedded target device and runs the main application services including the HTTPS web server, image processing service, and MQTT telemetry client.

The communication between the host machine and the virtual machine is performed through the virtual network interface.

---

# 2. Implemented Features

## 2.1 Secure Web Server

A custom HTTPS web server was developed in C using the **libmicrohttpd** library.

The web server provides a monitoring dashboard that includes:

- Student name and student ID.
- Live camera stream.
- Real-time detected person count.
- CPU usage percentage.
- Available memory information.
- CPU temperature.

The dashboard information is updated dynamically without refreshing the page.

The web server provides the following features:

- HTTP server.
- HTTPS secure server.
- HTTP to HTTPS redirection.
- REST API for telemetry information.
- Live MJPEG camera streaming.

Main API endpoint:

```
GET /api/v1/telemetry
```

Example response:

```json
{
    "student_name": "Amir Hossein Motiei",
    "student_id": "401102553",
    "cpu_usage_percent": 22.6,
    "memory_available_kb": 1080156,
    "cpu_temperature_c": 58,
    "persons": 1
}
```

---

# 2.2 SSL Certificate and HTTPS Security

A self-signed SSL certificate was generated using OpenSSL.

The certificate configuration follows the project requirements:

- Certificate type: Self-Signed Certificate
- Common Name (CN):

```
401102553
```

The web server only provides secure HTTPS access.

All HTTP requests are redirected automatically:

```
HTTP  →  HTTPS
```

using:

```
301 Moved Permanently
```

---

# 2.3 Host Resource Transfer

Since the project runs inside a virtual machine, direct access to physical hardware resources is not available inside the VM.

Therefore, a host-side agent was implemented.

The host agent is responsible for:

- Capturing images from the laptop webcam.
- Encoding frames into JPEG format.
- Sending camera frames to the VM.
- Reading CPU temperature.
- Sending temperature information to the VM.

Communication channels:

| Data | Protocol | Port |
|---|---|---|
| Camera Stream | TCP | 9100 |
| Temperature Data | UDP | 9090 |

---

# 2.4 Person Detection Service

A Python-based computer vision service was implemented using OpenCV.

The vision service:

- Receives camera frames from the web server.
- Processes images continuously.
- Detects humans in the image.
- Stores the detected person count.

The current number of detected persons is stored in:

```
/run/smart-guard/person_count
```

This value is used by:

- Web dashboard.
- MQTT telemetry service.

---

# 2.5 MQTT Telemetry Client

A native C MQTT client was developed using:

- Eclipse Mosquitto client library.
- cJSON library.
- libcurl library.

The MQTT client publishes system status and telemetry information.

Published topics:

```
status/401102553/home
```

```
telemetry/401102553/home
```

```
persons/401102553/home
```

The MQTT broker uses authentication with username and password.

Anonymous MQTT access is disabled.

---

# 2.6 Systemd Service Management

All Smart Guard components are managed using Linux systemd services.

Implemented services:

| Service | Description |
|---|---|
| smart-guard-host-agent | Transfers camera and temperature data from host |
| smart-guard-web | HTTPS web server |
| smart-guard-vision | Person detection service |
| smart-guard-mqtt | MQTT telemetry client |

The services support:

- Automatic startup after boot.
- Automatic restart after failure.
- Dependency management.
- Security restrictions.

Important systemd options:

```ini
Restart=always

RestartSec=2

NoNewPrivileges=true

ProtectSystem=strict

PrivateTmp=true
```

---

# 3. Project Directory Structure

```
section1
│
├── config
│   ├── host-agent.env.example
│   └── vm.env.example
│
├── docs
│   └── evidence
│
├── host
│   ├── CMakeLists.txt
│   └── src
│       └── smart_guard_host_agent.c
│
├── systemd
│   ├── smart-guard-host-agent.service
│   ├── smart-guard-web.service
│   ├── smart-guard-vision.service
│   └── smart-guard-mqtt.service
│
├── vm
│   ├── CMakeLists.txt
│   ├── share
│   │   └── no-camera.jpg
│   ├── src
│   │   ├── smart_guard_web.c
│   │   └── smart_guard_mqtt.c
│   └── vision
│       └── person_detector.py
│
└── section1_all_in_one_fixed.sh
```

---

# 4. Directory Description

## config

Contains example configuration files.

Sensitive information such as passwords and private keys are not stored inside the repository.

---

## host

Contains the source code running on the physical laptop.

Main file:

```
smart_guard_host_agent.c
```

Responsibilities:

- Webcam capture.
- JPEG encoding.
- Temperature reading.
- Data transmission to VM.

---

## vm

Contains all applications running inside the Smart Guard virtual embedded system.

### src

Contains C implementations:

```
smart_guard_web.c
```

Implementation of HTTPS web server.

```
smart_guard_mqtt.c
```

Implementation of MQTT telemetry client.

---

## vision

Contains image processing software:

```
person_detector.py
```

Responsibilities:

- Receiving camera stream.
- Processing frames using OpenCV.
- Detecting people.
- Updating detected person count.

---

## share

Contains shared resources.

Example:

```
no-camera.jpg
```

Fallback image when camera stream is unavailable.

---

## systemd

Contains service configuration files.

These files define:

- Startup order.
- Service dependencies.
- Restart policy.
- Security restrictions.

---

## docs/evidence

Contains all experimental results required for evaluation.

Test categories:

```
test_1_1_boot
test_1_2_restart
test_1_3_power_cycle
test_1_4_redirect
test_1_5_certificate
test_1_6_dashboard
```

Each folder contains:

- Terminal outputs.
- Screenshots.
- Verification results.
- Videos where required.

---

# 5. Installation Script

The file:

```
section1_all_in_one_fixed.sh
```

automates the installation process.

The script performs:

- Dependency installation.
- Compilation.
- Service installation.
- Permission configuration.
- Service activation.

---

# 6. Verification

The complete Section 1 implementation was tested according to the project requirements.

Performed tests:

- System boot time measurement.
- Automatic service restart after crash.
- Automatic startup after power cycle.
- HTTP to HTTPS redirect verification.
- SSL certificate validation.
- Web dashboard verification.
- Live telemetry monitoring.
- Person detection validation.
- MQTT message publishing.

All required Section 1 tests were successfully completed.