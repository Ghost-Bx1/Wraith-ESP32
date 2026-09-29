#pragma once

// ── AP de gerência (painel web) ───────────────────────────────────────────────
#define AP_SSID        "GhostPredator"
#define AP_PASS        "predator123"
#define AP_CHANNEL     1
#define AP_IP          "192.168.4.1"
#define MDNS_HOST      "ghostpredator"  // acessível em http://ghostpredator.local
#define WEB_USER       "admin"
#define WEB_PASS       "predator"       // senha do painel web

// ── Segundo rádio WiFi (ESP8266 via UART2) ────────────────────────────────────
#define RADIO2_SERIAL  Serial2
#define RADIO2_BAUD    115200
#define RADIO2_TX      17
#define RADIO2_RX      16

// ── Limites ───────────────────────────────────────────────────────────────────
#define MAX_LOG_LINES     300
#define MAX_NETS          80
#define MAX_CLIENTS       200
#define MAX_PROBES        400
#define MAX_HANDSHAKES    150
#define MAX_PMKIDS        80
#define DEAUTH_BURST      256     // frames por ciclo (modo normal)
#define DEAUTH_BURST_SOFT 64      // frames por ciclo (modo suave)
#define DEAUTH_BURST_HARD 512     // frames por ciclo (modo agressivo)
#define BEACON_INTERVAL   50      // ms entre beacons flood
#define SNIFFER_HOP_MS    200     // ms por canal no channel hopping
#define FISHER_TIMEOUT_MS 30000   // timeout por alvo no PMKID Fisher

// ── Pinos status LED ──────────────────────────────────────────────────────────
#define LED_STATUS     2          // LED azul onboard
