# Service dependency diagram

```text
Physical Ubuntu Host
network-online.target
        |
        +--> smart-guard-host-agent.service
                 |
                 +-- UDP 9090: physical CPU temperature
                 +-- TCP 9100: JPEG webcam frames
                              |
                              v
Ubuntu VM
network-online.target
        |
        +--> smart-guard-web.service
                 |
                 +-- HTTP 80 -> HTTPS 443 (301)
                 +-- /api/v1/telemetry
                 +-- /api/v1/stream
                 +-- /run/smart-guard/person_count

Final project integration:
smart-guard-vision.service
        |
        +--> updates person count / annotated frames
        |
        +--> smart-guard-mqtt.service
        |
        +--> smart-guard-web.service
```

Reason for ordering:
- Network must be ready before the host agent and web server start.
- The future vision service must start only after its camera/input is available.
- The future MQTT client consumes telemetry/person-count data, so it starts after vision.
- Every long-running service uses `Restart=always`.
