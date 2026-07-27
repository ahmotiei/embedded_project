# اجرای آزمایش‌های اجباری بخش دوم

تمام آزمایش‌ها را روی VM اجرا کنید تا دسترسی مستقیم به `/proc/<PID>/status` و `journalctl` وجود داشته باشد. مسیر خروجی پیش‌فرض `~/embedded/embedded_project/section2/evidence` است.

## آزمایش 2-1 — دما در سه حالت

هر حالت باید 5 دقیقه و با فاصله نمونه‌برداری 30 ثانیه اجرا شود.

### حالت Idle

```bash
sudo systemctl stop smart-guard-vision.service
# تمام تب‌های استریم را ببندید.
bash tests/test_2_1_temperature.sh idle
```

### حالت فقط Stream

```bash
sudo systemctl stop smart-guard-vision.service
# در یک ترمینال استریم را باز نگه دارید:
curl -k https://192.168.122.186:8443/api/v1/stream -o /dev/null
# در ترمینال دوم:
bash tests/test_2_1_temperature.sh stream
```

### حالت Stream + Detection

```bash
sudo systemctl start smart-guard-vision.service
# در یک ترمینال استریم را باز نگه دارید:
curl -k https://192.168.122.186:8443/api/v1/stream -o /dev/null
# در ترمینال دوم:
bash tests/test_2_1_temperature.sh stream_detection
```

پس از پایان هر سه حالت:

```bash
python3 tests/plot_test_2_1.py evidence/test_2_1_temperature
```

خروجی شامل `temperature_three_modes.png` و `maximum_temperature.csv` خواهد بود.

## آزمایش 2-2 — مصرف حافظه C در استریم پیوسته

```bash
bash tests/test_2_2_memory.sh
```

اسکریپت یک اتصال استریم واقعی را 5 دقیقه باز نگه می‌دارد، مقدار `VmRSS` پروسه C را مستقیم از `/proc/<PID>/status` هر 5 ثانیه می‌خواند و نمودار و تحلیل اولیه نشتی حافظه را تولید می‌کند.

## آزمایش 2-3 — پنجاه درخواست همزمان با curl

```bash
bash tests/test_2_3_load.sh
```

این تست 50 حلقه همزمان `curl` را به مدت 30 ثانیه اجرا می‌کند. علاوه بر latency هر درخواست، دما، CPU و حافظه را در طول بار ثبت می‌کند. خروجی شامل p50، p95، p99، افزایش میانگین تأخیر و نمودارها است.

## آزمایش 2-4 — قطع و وصل شبکه

این تست را از کنسول VM یا ترمینالی اجرا کنید که با قطع شبکه Host از بین نرود:

```bash
bash tests/test_2_4_network_recovery.sh
```

پس از شروع تست، استریم را باز کنید، شبکه Host را قطع کنید، 2 دقیقه صبر کنید و دوباره وصل کنید. در لاگ باید ابتدا `Host camera disconnected` و سپس `Host camera connected` ثبت شود. Host agent بخش اول به صورت خودکار reconnect می‌کند.
