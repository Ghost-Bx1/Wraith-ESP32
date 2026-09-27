#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "scanner.h"

/*
 * Hidden SSID Scanner
 * Envia Probe Requests direcionados para BSSIDs específicos (forçando resposta
 * com SSID) e Probe Requests broadcast com SSIDs comuns para revelar redes ocultas.
 * Também captura Probe Responses de qualquer AP que responda.
 */
class HiddenScanner {
public:
    static HiddenScanner& get() { static HiddenScanner inst; return inst; }

    bool running = false;
    std::vector<String> revealed; // SSIDs revelados

    struct HiddenAP {
        String bssid;
        String ssid; // vazio = ainda não revelado
        uint8_t channel;
        int rssi;
    };
    std::vector<HiddenAP> targets; // BSSIDs com SSID vazio detectados no scan

    void start() {
        running = true;
        revealed.clear();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&_rxCb);
        LOG.warn("[HIDDEN] Scanner iniciado — " + String(targets.size()) + " alvo(s) oculto(s)");
    }

    void stop() {
        running = false;
        esp_wifi_set_promiscuous(false);
        LOG.info("[HIDDEN] Parado | revelados: " + String(revealed.size()));
    }

    // Carrega APs com SSID vazio do WiFiScanner
    void loadFromScan() {
        targets.clear();
        int n = WiFi.scanNetworks(false, true); // includeHidden=true
        for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i).isEmpty()) {
                HiddenAP ap;
                ap.bssid   = WiFi.BSSIDstr(i);
                ap.ssid    = "";
                ap.channel = WiFi.channel(i);
                ap.rssi    = WiFi.RSSI(i);
                targets.push_back(ap);
                LOG.data("[HIDDEN] AP oculto: " + ap.bssid + " ch" + ap.channel);
            }
        }
        LOG.info("[HIDDEN] " + String(targets.size()) + " APs ocultos encontrados");
    }

    // tick: envia probes para cada alvo, ciclando por SSIDs comuns
    void tick() {
        if (!running) return;
        static size_t targetIdx = 0;
        static size_t ssidIdx   = 0;
        static unsigned long next = 0;
        if (millis() < next) return;

        // Probe direcionado para BSSID específico
        if (!targets.empty()) {
            const HiddenAP& t = targets[targetIdx % targets.size()];
            if (t.ssid.isEmpty()) {
                uint8_t bssid[6];
                _parseMac(t.bssid.c_str(), bssid);
                esp_wifi_set_channel(t.channel, WIFI_SECOND_CHAN_NONE);
                // Probe com SSID vazio (AP responde com seu SSID real)
                _sendProbeRequest(bssid, t.channel, "", false);
                // Probe com SSIDs comuns para testar
                _sendProbeRequest(bssid, t.channel, _commonSSIDs[ssidIdx % _numSSIDs], true);
                ssidIdx++;
            }
            targetIdx++;
        }

        // Probe broadcast com SSIDs comuns para revelar APs ocultos desconhecidos
        _sendProbeRequest(nullptr, 0, _commonSSIDs[ssidIdx % _numSSIDs], true);
        ssidIdx++;

        next = millis() + 150;
    }

private:
    static const char* const _commonSSIDs[];
    static const int _numSSIDs;

    static void _parseMac(const char* s, uint8_t* out) {
        sscanf(s, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &out[0],&out[1],&out[2],&out[3],&out[4],&out[5]);
    }

    void _sendProbeRequest(const uint8_t* bssid, uint8_t ch, const char* ssid, bool directed) {
        uint8_t buf[64] = {};
        int pos = 0;
        uint8_t fakeSTA[6] = {0x02,0x00,0x00,0x00,0x00,0x01};
        esp_fill_random(fakeSTA+1, 5); fakeSTA[0] = 0x02;

        buf[pos++] = 0x40; buf[pos++] = 0x00; // Probe Request
        buf[pos++] = 0x00; buf[pos++] = 0x00;

        if (directed && bssid)
            memcpy(buf+pos, bssid, 6);
        else
            memset(buf+pos, 0xFF, 6);
        pos += 6;

        memcpy(buf+pos, fakeSTA, 6); pos += 6;

        if (directed && bssid)
            memcpy(buf+pos, bssid, 6);
        else
            memset(buf+pos, 0xFF, 6);
        pos += 6;

        buf[pos++] = 0x00; buf[pos++] = 0x00;

        // SSID IE
        uint8_t slen = ssid ? strlen(ssid) : 0;
        buf[pos++] = 0x00; buf[pos++] = slen;
        if (slen) { memcpy(buf+pos, ssid, slen); pos += slen; }

        // Supported rates
        static const uint8_t rates[] = {0x01,0x08,0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(buf+pos, rates, sizeof(rates)); pos += sizeof(rates);

        if (ch > 0) esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        esp_wifi_80211_tx(WIFI_IF_AP, buf, pos, false);
    }

    struct Hdr { uint16_t fc,dur; uint8_t a1[6],a2[6],a3[6]; uint16_t seq; } __attribute__((packed));

    static void _rxCb(void* buf, wifi_promiscuous_pkt_type_t) {
        auto& self = HiddenScanner::get();
        if (!self.running) return;
        auto* pkt = (wifi_promiscuous_pkt_t*)buf;
        size_t len = pkt->rx_ctrl.sig_len;
        if (len < sizeof(Hdr)+12) return;
        auto* hdr = (Hdr*)pkt->payload;
        uint8_t ft  = (hdr->fc & 0x000C) >> 2;
        uint8_t fst = (hdr->fc & 0x00F0) >> 4;
        // Probe Response = Management(0) subtype 5
        if (ft != 0 || fst != 5) return;
        const uint8_t* body = pkt->payload + sizeof(Hdr);
        size_t blen = len - sizeof(Hdr);
        if (blen < 12) return;
        size_t pos = 12; // skip fixed fields
        while (pos + 2 < blen) {
            uint8_t tag = body[pos], tl = body[pos+1];
            if (tag == 0x00 && tl > 0 && tl <= 32) {
                String ssid = String((char*)&body[pos+2]).substring(0, tl);
                String bssid = WiFiScanner::bssidToStr(hdr->a2);
                bool dup = false;
                for (auto& r : self.revealed) if (r == ssid) { dup = true; break; }
                if (!dup) {
                    self.revealed.push_back(ssid);
                    LOG.success("[HIDDEN] Revelado: \"" + ssid + "\" [" + bssid + "]");
                    // Atualiza target se era oculto
                    for (auto& t : self.targets)
                        if (t.bssid == bssid) t.ssid = ssid;
                }
            }
            pos += 2 + tl;
        }
    }
};

inline const char* const HiddenScanner::_commonSSIDs[] = {
    "linksys","default","home","wifi","router","network","internet","casa",
    "AP","NETGEAR","TP-LINK","ASUS","Xiaomi","Huawei","admin","guest",
    "private","hidden","secure","office","work","empresa","loja","hotel"
};
inline const int HiddenScanner::_numSSIDs = 24;
