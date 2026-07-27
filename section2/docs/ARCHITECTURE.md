# Section 2 Architecture

The physical Ubuntu host keeps the existing Section 1 C agent. It reads the CPU temperature directly from `/sys`, captures the webcam, sends `SGTEMP1 <epoch> <millidegrees>` by UDP port 9090, and sends length-prefixed JPEG frames by TCP port 9100.

On the VM, the upgraded `smart_guard_web` C process preserves ports 80, 443, 9090, and 9100. It also opens a loopback-only API on `127.0.0.1:18080`. The C process performs all operational work:

- reads `/proc/stat` for CPU load;
- reads `/proc/meminfo` for free and available memory;
- reads `/sys/class/thermal` and `/sys/class/hwmon` directly when a local CPU sensor exists;
- falls back to the physical host's C/sysfs temperature packet when the VM exposes no thermal sensor;
- receives and produces the MJPEG stream;
- reads the current person count from `/run/smart-guard/person_count`;
- maintains a five-record ring history;
- parses and dispatches commands through a C command registry;
- uses the `reboot(2)` system call for the reboot command.

FastAPI listens on HTTPS port 8443 and is intentionally thin. It publishes OpenAPI/Swagger and proxies every call to the C loopback service. No telemetry, history, counting, or command logic is reimplemented in Python.

```text
Physical Host (C agent)
  /sys temperature ----UDP 9090----> VM C core
  /dev/video0 ---------TCP 9100----> VM C core
                                         |
                                         +-- HTTPS 443 dashboard/direct API
                                         +-- HTTP 80 -> 301 HTTPS
                                         +-- 127.0.0.1:18080 C REST API
                                                        |
                                                        v
                                             FastAPI Swagger :8443
```
