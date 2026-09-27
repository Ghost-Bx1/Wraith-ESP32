#pragma once

// ── AP de gerência (painel web) ───────────────────────────────────────────────
#define AP_SSID        "ESP32-Pentest"
#define AP_PASS        "pentest123"
#define AP_CHANNEL     1
#define AP_IP          "192.168.4.1"
#define MDNS_HOST      "ghostpredator"  // acessível em http://ghostpredator.local
#define WEB_PASS       "ghost2024"      // senha do painel web

// ── Segundo rádio WiFi (ESP8266 via UART2) ────────────────────────────────────
#define RADIO2_SERIAL  Serial2
#define RADIO2_BAUD    115200
#define RADIO2_TX      17
#define RADIO2_RX      16

// ── Limites ───────────────────────────────────────────────────────────────────
#define MAX_LOG_LINES     300
#define MAX_NETS          64
#define MAX_CLIENTS       128
#define DEAUTH_BURST      64      // frames por ciclo (modo agressivo)
#define BEACON_INTERVAL   50      // ms entre beacons flood

// ── Pinos status LED ──────────────────────────────────────────────────────────
#define LED_STATUS     2          // LED azul onboard
