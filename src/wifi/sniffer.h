#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../utils/rgb_led.h"
#include "scanner.h"
#include "ap_clone.h"

struct HandshakeCapture {
    String bssid, client;
    uint8_t type;
    uint8_t data[256];
    size_t  len;
    unsigned long ts;
};

// Rastreia estado completo do handshake por par AP/cliente
struct HandshakeState {
    String   bssid;
    String   client;
    String   ssid;
    bool     m1 = false, m2 = false, m3 = false, m4 = false;
    unsigned long lastTs = 0;
    // true quando tem M1+M2 ou M2+M3 — suficiente para hashcat -m 22000
    bool crackable() const { return (m1 && m2) || (m2 && m3); }
    // true quando tem os 4 mensagens — handshake completo para aircrack-ng
    bool complete()  const { return m1 && m2 && m3 && m4; }
};

struct PMKIDCapture {
    String bssid, client;
    uint8_t pmkid[16];
    unsigned long ts;
};

struct ProbeRequest {
    String mac, ssid;
    int    rssi;
    uint8_t macRaw[6];
    uint8_t channel;
    unsigned long ts;
    // Fingerprinting baseado nos IEs da probe request
    bool randomizedMAC = false; // MAC localmente administrado (privacidade)
    bool hasHT         = false; // 802.11n capable
    bool hasVHT        = false; // 802.11ac capable
    bool hasHE         = false; // 802.11ax (Wi-Fi 6) capable
    uint8_t devType    = 0;     // 0=desconhecido,1=Apple,2=Android,3=Windows,4=Linux
};

// Tipo de dispositivo detectado via probe IEs
static const char* const kDevTypeNames[] = {"?","Apple","Android","Windows","Linux"};

class Sniffer {
public:
    static Sniffer& get() { static Sniffer inst; return inst; }

    bool     running     = false;
    bool     channelHop  = true;
    uint8_t  hopChannel  = 1;
    uint32_t packetCount = 0;
    uint32_t mgmtCount   = 0;
    uint32_t dataCount   = 0;
    uint32_t eapolCount  = 0;

    std::vector<HandshakeCapture> handshakes;
    std::vector<PMKIDCapture>     pmkids;
    std::vector<ProbeRequest>     probes;
    std::vector<HandshakeState>   handshakeStates; // estado consolidado por par

    // Karma callback — setado pelo main após incluir karma.h
    using ProbeCallback = void(*)(const uint8_t* mac, const char* ssid, uint8_t ch);
    ProbeCallback onProbe = nullptr;

    void start() {
        esp_wifi_set_promiscuous_filter(nullptr);
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&_rxCb);
        running = true;
        LOG.info("[SNIFFER] Ativo — hop: " + String(channelHop ? "sim" : "nao"));
    }

    void stop() {
        esp_wifi_set_promiscuous(false);
        running = false;
        LOG.info("[SNIFFER] Parado | pkts:" + String(packetCount) +
                 " hs:" + String(handshakes.size()) +
                 " pmkid:" + String(pmkids.size()) +
                 " eapol:" + String(eapolCount));
    }

    void lockChannel(uint8_t ch) {
        channelHop = false;
        hopChannel = ch;
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        LOG.info("[SNIFFER] Fixado canal " + String(ch));
    }

    void resumeHop() {
        channelHop = true;
        LOG.info("[SNIFFER] Hop retomado");
    }

    bool  eapolActive = false;   // pausa o hop enquanto EAPOL está em andamento
    uint32_t pktPerSec = 0;      // pacotes por segundo (atualizado a cada tick)
    uint32_t channelStats[14] = {}; // pacotes por canal 1-13 (índice 1..13)

    void tick() {
        if (!running) return;
        static unsigned long lastHop = 0;
        static unsigned long lastPps = 0;
        static uint32_t lastPktCount = 0;
        // Atualiza pps a cada 1s
        if (millis() - lastPps > 1000) {
            pktPerSec  = packetCount - lastPktCount;
            lastPktCount = packetCount;
            lastPps = millis();
        }
        if (!channelHop) return;
        // Pausa hop se EAPOL ativo (capturando handshake)
        uint32_t hopMs = eapolActive ? SNIFFER_HOP_MS * 4 : SNIFFER_HOP_MS;
        if (millis() - lastHop > hopMs) {
            hopChannel = (hopChannel % 13) + 1;
            esp_wifi_set_channel(hopChannel, WIFI_SECOND_CHAN_NONE);
            eapolActive = false;
            lastHop = millis();
        }
    }

private:
    struct Hdr {
        uint16_t fc, dur;
        uint8_t  a1[6], a2[6], a3[6];
        uint16_t seq;
    } __attribute__((packed));

    static bool _isEAPOL(const uint8_t* p, size_t len) {
        return len >= 8 && p[0]==0xAA && p[1]==0xAA && p[2]==0x03 && p[6]==0x88 && p[7]==0x8E;
    }

    static bool _extractPMKID(const uint8_t* eapol, size_t elen, uint8_t* out) {
        // EAPOL-Key layout: 4B header + 95B fixed fields → Key Data Length at [97-98]
        // Key Data starts at offset 99; PMKID KDE = DD 14 00 0F AC 04 + 16B PMKID
        if (elen < 101) return false;
        uint16_t kdLen = ((uint16_t)eapol[97] << 8) | eapol[98];
        if (kdLen < 22 || elen < (size_t)(99u + kdLen)) return false;
        const uint8_t* kd = eapol + 99;
        for (size_t i = 0; i + 22 <= (size_t)kdLen; i++) {
            if (kd[i]   == 0xDD && kd[i+1] == 0x14 &&
                kd[i+2] == 0x00 && kd[i+3] == 0x0F &&
                kd[i+4] == 0xAC && kd[i+5] == 0x04) {
                memcpy(out, kd + i + 6, 16);
                return true;
            }
        }
        return false;
    }

    static void _rxCb(void* buf, wifi_promiscuous_pkt_type_t) {
        auto& self = Sniffer::get();
        self.packetCount++;
        auto* pkt  = (wifi_promiscuous_pkt_t*)buf;
        size_t len = pkt->rx_ctrl.sig_len;
        if (len < sizeof(Hdr)) return;
        auto*   hdr  = (Hdr*)pkt->payload;
        uint8_t ft   = (hdr->fc & 0x000C) >> 2;
        uint8_t fst  = (hdr->fc & 0x00F0) >> 4;
        int8_t  rssi = pkt->rx_ctrl.rssi;
        uint8_t ch   = pkt->rx_ctrl.channel;
        if (ch >= 1 && ch <= 13) self.channelStats[ch]++;

        if (ft == 0x00) {
            self.mgmtCount++;
            // Beacon (subtype 8) e Probe Response (subtype 5) → alimenta APClone
            if (fst == 0x08 || fst == 0x05) {
                APClone::get().onBeacon(pkt->payload, len, rssi, ch);
            }
            if (fst == 0x04) {
                const uint8_t* body = pkt->payload + 24;
                size_t blen = len > 24 ? len - 24 : 0;
                String ssid = "";
                if (blen >= 2 && body[0] == 0x00) {
                    uint8_t sl = body[1];
                    if (sl > 0 && sl <= 32 && blen >= (size_t)(2+sl))
                        ssid = String((char*)(body+2)).substring(0, sl);
                }
                // Ignora probes originados pelo próprio ESP32
                {
                    uint8_t apM[6], staM[6];
                    esp_wifi_get_mac(WIFI_IF_AP,  apM);
                    esp_wifi_get_mac(WIFI_IF_STA, staM);
                    if (memcmp(hdr->a2, apM,  6) == 0) return;
                    if (memcmp(hdr->a2, staM, 6) == 0) return;
                }
                ProbeRequest pr;
                pr.mac  = WiFiScanner::bssidToStr(hdr->a2);
                pr.ssid = ssid; pr.rssi = rssi;
                pr.channel = ch; pr.ts = millis();
                memcpy(pr.macRaw, hdr->a2, 6);
                // MAC randomizado: bit U/L (bit1 do primeiro octeto) = 1
                pr.randomizedMAC = (hdr->a2[0] & 0x02) != 0;

                // Fingerprinting via IEs da probe request
                {
                    const uint8_t* pie = body + 2 + (blen >= 2 ? body[1] : 0);
                    size_t rem = (blen >= 2 + body[1]) ? blen - 2 - body[1] : 0;
                    size_t pi = 0;
                    while (pi + 1 < rem) {
                        uint8_t tid = pie[pi], tl = pie[pi+1];
                        if (pi + 2 + tl > rem) break;
                        if (tid == 45)  pr.hasHT  = true; // HT Capabilities
                        if (tid == 191) pr.hasVHT = true; // VHT Capabilities
                        if (tid == 255 && tl >= 1 && pie[pi+2] == 35) pr.hasHE = true; // HE (Wi-Fi 6)
                        if (tid == 221 && tl >= 4) { // Vendor Specific
                            // Apple: 00:17:F2 ou 00:50:F2 com type=4 (P2P)
                            if (pie[pi+2]==0x00 && pie[pi+3]==0x17 && pie[pi+4]==0xF2)
                                pr.devType = 1; // Apple
                            // Microsoft: 00:50:F2 type=4 (WPS/WCN) - comum em Windows
                            if (pie[pi+2]==0x00 && pie[pi+3]==0x50 && pie[pi+4]==0xF2 && tl>=5 && pie[pi+5]==0x04)
                                pr.devType = 3; // Windows
                        }
                        pi += 2 + tl;
                    }
                }

                // Deduplicação por MAC+SSID
                bool dupProbe = false;
                for (auto& ep : self.probes)
                    if (ep.mac == pr.mac && ep.ssid == pr.ssid) { dupProbe = true; break; }
                if (!dupProbe && self.probes.size() < MAX_PROBES) self.probes.push_back(pr);
                WiFiScanner::get().addClient(hdr->a2, hdr->a3, rssi);
                if (ssid.length() > 0) {
                    WiFiScanner::get().addClientProbe(pr.mac, ssid);
                    String fp = "";
                    if (pr.devType > 0) fp += " [" + String(kDevTypeNames[pr.devType]) + "]";
                    if (pr.randomizedMAC) fp += " [rndMAC]";
                    if (pr.hasHE) fp += " [WiFi6]";
                    LOG.data("[PROBE] " + pr.mac + fp + " -> \"" + ssid + "\"");
                    if (self.onProbe) self.onProbe(hdr->a2, ssid.c_str(), ch);
                }
            }
        } else if (ft == 0x02) {
            self.dataCount++;
            WiFiScanner::get().addClient(hdr->a2, hdr->a1, rssi);
            const uint8_t* body = pkt->payload + sizeof(Hdr);
            size_t blen = len > sizeof(Hdr) ? len - sizeof(Hdr) : 0;
            if (_isEAPOL(body, blen) && blen > 8) {
                self.eapolCount++;
                const uint8_t* eapol = body + 8;
                size_t elen = blen - 8;
                // FC byte1: bit0=ToDS, bit1=FromDS
                // Infra: addr3 = BSSID always; client = addr1 if FromDS (AP→STA), addr2 if ToDS (STA→AP)
                bool fromDS = (hdr->fc >> 8) & 0x02;
                const uint8_t* bssidAddr  = hdr->a3;
                const uint8_t* clientAddr = fromDS ? hdr->a1 : hdr->a2;
                uint8_t pmkid[16];
                if (_extractPMKID(eapol, elen, pmkid)) {
                    PMKIDCapture cap;
                    cap.bssid  = WiFiScanner::bssidToStr(bssidAddr);
                    cap.client = WiFiScanner::bssidToStr(clientAddr);
                    memcpy(cap.pmkid, pmkid, 16);
                    cap.ts = millis();
                    if (self.pmkids.size() < 50) self.pmkids.push_back(cap);
                    char hex[33];
                    for (int i=0;i<16;i++) sprintf(hex+i*2,"%02X",pmkid[i]);
                    LOG.success("[PMKID] " + cap.bssid + " -> " + String(hex));
                }
                HandshakeCapture hs;
                hs.bssid  = WiFiScanner::bssidToStr(bssidAddr);
                hs.client = WiFiScanner::bssidToStr(clientAddr);
                size_t cl = elen < 256 ? elen : 256;
                memcpy(hs.data, eapol, cl);
                hs.len = cl; hs.ts = millis(); hs.type = 0;
                if (elen > 6) {
                    uint16_t ki = (eapol[5]<<8)|eapol[6];
                    bool ack=ki&0x0080, mic=ki&0x0100, ins=ki&0x0040;
                    if (ack&&!mic) hs.type=1;
                    else if (!ack&&mic) hs.type=2;
                    else if (ack&&mic&&ins) hs.type=3;
                    else hs.type=4;
                }
                self.eapolActive = true; // pausa channel hop durante handshake
                if (self.handshakes.size() < MAX_HANDSHAKES) self.handshakes.push_back(hs);

                // Atualiza estado consolidado do handshake
                HandshakeState* st = nullptr;
                for (auto& s : self.handshakeStates)
                    if (s.bssid == hs.bssid && s.client == hs.client) { st = &s; break; }
                if (!st && self.handshakeStates.size() < 50) {
                    HandshakeState ns;
                    ns.bssid = hs.bssid; ns.client = hs.client;
                    self.handshakeStates.push_back(ns);
                    st = &self.handshakeStates.back();
                }
                if (st) {
                    if (hs.type == 1) st->m1 = true;
                    if (hs.type == 2) st->m2 = true;
                    if (hs.type == 3) st->m3 = true;
                    if (hs.type == 4) st->m4 = true;
                    st->lastTs = millis();
                    if (st->crackable() && hs.type <= 2) {
                        RGBLed::get().flashFound();
                        LOG.success("[HS-CRACK] " + hs.bssid + " CRACKAVEL (M1+M2) — use hashcat -m 22000");
                    }
                    if (st->complete()) {
                        RGBLed::get().flashFound();
                        LOG.success("[HS-FULL] " + hs.bssid + " HANDSHAKE COMPLETO (M1-M4) — aircrack-ng pronto");
                    }
                }
                LOG.success("[EAPOL M" + String(hs.type) + "] " + hs.bssid + " <-> " + hs.client);
            }
        }
    }
};
