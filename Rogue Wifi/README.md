# ESP32 Evil Twin - Captive Portal Harvester

> **⚠️ DISCLAIMER: EDUCATIONAL PURPOSES ONLY.**  
> This project was developed strictly for authorized penetration testing, security research, and educational purposes. Do not use this tool on networks or devices you do not own or have explicit permission to test. The author is not responsible for any misuse.

A highly optimized, fully self-contained Evil Twin framework built natively on the **ESP-IDF (C)** for the ESP32. It creates a rogue Wi-Fi Access Point, executes a DNS sinkhole, and forces victim devices into a flawless Captive Portal to harvest credentials.

## 🚀 Features

* **DNS Sinkholing:** Intercepts all DNS requests and forces the device's Captive Network Assistant (CNA) to open automatically on iOS, Android, and Windows.
* **Persistent Storage (NVS):** Captured credentials survive reboots and are stored securely in the ESP32's flash memory.
* **Live Admin Dashboard:** Auto-refreshing `/admin` panel showing uptime, real-time logs, connected devices, and harvest rates.
* **Client-Side Timestamps:** Injects JS to steal the victim's exact local time for precise logging without an atomic clock.

## ⚙️ Installation & Flashing

Built natively for **ESP-IDF v5.x**.

```bash
# Set your target (if not already set)
idf.py set-target esp32

# Build, flash, and open serial monitor
idf.py build flash monitor
```

## 🛠️ Usage

1. Power on the ESP32. It will immediately begin broadcasting the default network.
2. When a victim connects, their device will automatically bring up the fake login portal.
3. **To access the Admin Panel:**
   * Navigate to `http://192.168.1.1/admin` on a connected device.
   * Enter the default PIN: **`0000`**
4. Use the dashboard to view live logs, copy captured credentials, change the broadcasted SSID on the fly, or safely wipe the NVS memory.
