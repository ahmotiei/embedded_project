# کدهای بخش اول پروژه Smart Guard

**نام دانشجو:** Amir Hossein Motiei  
**شماره دانشجویی:** 401102553

این بسته شامل کدهای اجرایی‌شده برای بخش اول است:

- وب‌سرور HTTPS به زبان C با GNU libmicrohttpd
- صفحه زنده با نام و شماره دانشجویی
- خواندن CPU و حافظه VM مستقیماً از `/proc`
- انتقال دمای واقعی CPU سیستم فیزیکی به VM با UDP
- انتقال تصویر وب‌کم سیستم فیزیکی به VM با TCP و V4L2
- ارائه MJPEG در `/api/v1/stream`
- ارائه تله‌متری JSON در `/api/v1/telemetry`
- Redirect از HTTP به HTTPS با کد 301
- گواهی Self-Signed با `CN=401102553`
- سرویس‌های systemd و Restart خودکار

## نصب سریع

### روی VM

```bash
chmod +x section1_all_in_one_fixed.sh
sudo bash ./section1_all_in_one_fixed.sh vm
```

### روی Ubuntu فیزیکی

```bash
chmod +x section1_all_in_one_fixed.sh
sudo bash ./section1_all_in_one_fixed.sh host
```

صفحه:

```text
https://192.168.122.186/
```

## آزمون‌ها

```bash
curl -I http://192.168.122.186/
curl -k https://192.168.122.186/api/v1/telemetry
```

```bash
sudo openssl x509   -in /etc/smart-guard/tls/server.crt   -noout -subject -issuer -dates -ext subjectAltName
```

```bash
pid=$(systemctl show -p MainPID --value smart-guard-web)
sudo kill -9 "$pid"
sleep 3
systemctl show -p MainPID --value smart-guard-web
sudo journalctl -u smart-guard-web --since "2 minutes ago" --no-pager
```

```bash
systemd-analyze
systemd-analyze blame | head -30
```

## نکته مهم درباره تعداد افراد و MQTT

رابط تعداد افراد در بخش اول آماده است و وب‌سرور مقدار فایل زیر را می‌خواند:

```text
/run/smart-guard/person_count
```

در حال حاضر مقدار آن `0` است. تشخیص واقعی افراد و برنامه MQTT، منطق بخش‌های بعدی پروژه هستند. در پوشه `systemd` نمونه سرویس و ترتیب وابستگی آن‌ها قرار داده شده، اما تا آماده‌شدن باینری‌های نهایی نباید فعال شوند.
