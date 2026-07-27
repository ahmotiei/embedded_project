# Swagger Screenshot Checklist

Open `https://<VM_IP>:8443/docs` and capture one screenshot for each item below.

1. `GET /api/v1/persons`: press **Try it out** then **Execute** and include the real `timestamp` and `persons` response.
2. `GET /api/v1/telemetry`: include temperature, CPU usage, memory, and response code 200.
3. `GET /api/v1/history`: stand in front of the camera first so records are non-empty, then capture the five-record response.
4. `GET /api/v1/stream`: set `frames=1`, execute, and capture status 200 plus `multipart/x-mixed-replace` response headers. For the visible live image, also open the request URL in a browser tab.
5. `POST /api/v1/command`: click **Authorize**, enter the value from `/etc/smart-guard/section2.env`, execute `{"cmd":"ping"}`, and capture the accepted response. Capture the real reboot command only at the end of your test session.
