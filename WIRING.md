# Wiring Guide - Wraith

## Recommended Hardware: ESP32-S3 N16R8

The recommended board is the **ESP32-S3-WROOM-1-N16R8** (16MB Flash + 8MB OPI PSRAM).

```
[ESP32-S3 N16R8 - 44 pins, 57.15mm x 27.94mm]

  Left side:                   Right side:
  3.3V  3.3V  RST              GND  TX  RX
  GPIO4  GPIO5  GPIO6  GPIO7   GPIO1  GPIO2  GPIO42  GPIO41
  GPIO15 GPIO16 GPIO17 GPIO18  GPIO40 GPIO39 GPIO38  GPIO37
  GPIO8  GPIO3  GPIO46         GPIO36 GPIO35 GPIO0
  GPIO9  GPIO10 GPIO11         GPIO45 GPIO48 GPIO47  GPIO21
  GPIO12 GPIO13 GPIO14         GPIO20 GPIO19
  5V                           GND  GND

  RGB LED: WS2812 on GPIO 48
  USB OTG: left USB-C (native USB, GPIO 19/20)
  USB Serial: right USB-C (CH343P)
  Buttons: RST (GPIO 38), BOOT (GPIO 0)
```

---

## USB Ports

| Port | Connector | Chip | Use |
|------|-----------|------|-----|
| Left USB-C | USB OTG | GPIO 19/20 | External USB devices + console |
| Right USB-C | CH343P | UART | Firmware flashing (recommended) |

> Always use the **right USB-C** to flash firmware. Both ports support serial monitoring.

---

## ESP32-S3 -> ESP8266 (Second WiFi Radio)

Connect an ESP8266 as a secondary radio via UART2:

| ESP32-S3 Pin | Wire | ESP8266 Pin | Description |
|---|---|---|---|
| GPIO17 (TX2) | -> | RX (GPIO3) | ESP32 TX -> ESP8266 RX |
| GPIO16 (RX2) | <- | TX (GPIO1) | ESP8266 TX -> ESP32 RX |
| GND | - | GND | Shared ground |
| 3.3V | -> | VCC (3.3V) | Power |

> The ESP8266 must have AT firmware (v1.7+). Do NOT connect to 5V.

```
ESP32-S3                    ESP8266
+----------+               +----------+
| GPIO17   |-------------->| RX       |
| GPIO16   |<--------------| TX       |
| GND      |---------------| GND      |
| 3.3V     |---------------| VCC      |
+----------+               +----------+
```

---

## RGB LED Status (GPIO 48 - WS2812 built-in)

| State | Pattern |
|-------|---------|
| Idle | Solid blue |
| Booting | Green flash |
| Sniffer active | Slow blink (150ms) |
| Attack active | Fast blink (800ms) |
| Capture found | White flash |

---

## Power Options

| Source | Voltage | Min Current | Notes |
|--------|---------|-------------|-------|
| USB-C (cable) | 5V | 500mA | Sufficient for ESP32 alone |
| USB-C + ESP8266 | 5V | 800mA | Use USB 3.0 or powered hub |
| LiPo battery | 3.7V | 1A+ | Via VIN pin + 3.3V regulator |
| Power bank | 5V | 1A+ | Recommended for portable use |

---

## First Access

After flashing:

1. Connect to WiFi: **`Wraith`** / password: **`wraith123`**
2. Open browser: **`http://192.168.4.1`**
3. Web panel password: **`wraith`**
4. mDNS (alternative): **`http://wraith.local`**
