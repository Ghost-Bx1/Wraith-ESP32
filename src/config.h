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

// ── Limites — adaptativos via PSRAM (ESP32-S3 N16R8 = 8MB PSRAM) ─────────────
#ifdef BOARD_HAS_PSRAM
  // N16R8 com 8MB PSRAM: limites muito maiores
  #define MAX_LOG_LINES     2000
  #define MAX_NETS          256
  #define MAX_CLIENTS       1000
  #define MAX_PROBES        2000
  #define MAX_HANDSHAKES    500
  #define MAX_PMKIDS        400
#else
  // ESP32 padrão 520KB SRAM: limites conservadores
  #define MAX_LOG_LINES     300
  #define MAX_NETS          80
  #define MAX_CLIENTS       200
  #define MAX_PROBES        400
  #define MAX_HANDSHAKES    150
  #define MAX_PMKIDS        80
#endif

#define DEAUTH_BURST      256     // frames por ciclo (modo normal)
#define DEAUTH_BURST_SOFT 64      // frames por ciclo (modo suave)
#define DEAUTH_BURST_HARD 512     // frames por ciclo (modo agressivo)
#define BEACON_INTERVAL   50      // ms entre beacons flood
#define SNIFFER_HOP_MS    200     // ms por canal no channel hopping
#define FISHER_TIMEOUT_MS 30000   // timeout por alvo no PMKID Fisher

// ── LED RGB onboard WS2812B ────────────────────────────────────────────────────
// Se o LED não funcionar, altere RGB_LED_PIN para o outro valor:
//   GPIO 48 → ESP32-S3 DevKitC-1 Espressif padrão
//   GPIO 38 → Variantes customizadas N16R8 com 2x USB-C
// Para descobrir qual é o seu: teste 48 primeiro, depois 38 se não funcionar.
#ifdef ESP32_S3
  #define RGB_LED_PIN    48       // WS2812B onboard — tente 38 se não funcionar
#else
  #define RGB_LED_PIN    2        // fallback: LED simples em outros boards
#endif
#define RGB_LED_COUNT    1        // 1 LED RGB onboard
#define RGB_BRIGHTNESS   80       // 0–255 (80 = bom para uso indoor sem ofuscar)

// ── USB Radio — segundo rádio via porta USB OTG (conector USB-C esquerdo) ─────
// Conecte um ESP32/ESP8266 com firmware AT ao conector USB OTG via cabo USB-C.
// O adaptador aparece como porta serial (CDC ACM) e recebe comandos AT.
// Habilite APENAS se tiver o módulo conectado; caso contrário deixe comentado.
// Para ativar: descomente as 4 linhas abaixo e grave o firmware no módulo externo.
// #define USB_RADIO_SERIAL  Serial1
// #define USB_RADIO_BAUD    921600
// #define USB_RADIO_TX      17    // GPIO17 (UART1 TX — livre no N16R8)
// #define USB_RADIO_RX      18    // GPIO18 (UART1 RX — livre no N16R8)

// ── Pinos status LED (fallback para ESP32 sem WS2812) ────────────────────────
#define LED_STATUS     2          // LED azul onboard (ESP32 original)
