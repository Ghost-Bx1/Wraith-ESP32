#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "scanner.h"

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
};

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

    void tick() {
        if (!running || !channelHop) return;
        static unsigned long last = 0;
        if (millis() - last > 200) {
            hopChannel = (hopChannel % 13) + 1;
            esp_wifi_set_channel(hopChannel, WIFI_SECOND_CHAN_NONE);
            last = millis();
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
        if (elen < 100) return false;
        for (size_t i = 4; i + 18 < elen; i++) {
            if (eapol[i] == 0x4C && eapol[i+1] >= 16) {
                memcpy(out, eapol + i + 2, 16);
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

        if (ft == 0x00) {
            self.mgmtCount++;
            if (fst == 0x04) {
                const uint8_t* body = pkt->payload + 24;
                size_t blen = len > 24 ? len - 24 : 0;
                String ssid = "";
                if (blen >= 2 && body[0] == 0x00) {
                    uint8_t sl = body[1];
                    if (sl > 0 && sl <= 32 && blen >= (size_t)(2+sl))
                        ssid = String((char*)(body+2)).substring(0, sl);
                }
                ProbeRequest pr;
                pr.mac  = WiFiScanner::bssidToStr(hdr->a2);
                pr.ssid = ssid; pr.rssi = rssi;
                pr.channel = ch; pr.ts = millis();
                memcpy(pr.macRaw, hdr->a2, 6);
                if (self.probes.size() < 300) self.probes.push_back(pr);
                WiFiScanner::get().addClient(hdr->a2, hdr->a3, rssi);
                if (ssid.length() > 0) {
                    LOG.data("[PROBE] " + pr.mac + " -> \"" + ssid + "\"");
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
                uint8_t pmkid[16];
                if (_extractPMKID(eapol, elen, pmkid)) {
                    PMKIDCapture cap;
                    cap.bssid  = WiFiScanner::bssidToStr(hdr->a1);
                    cap.client = WiFiScanner::bssidToStr(hdr->a2);
                    memcpy(cap.pmkid, pmkid, 16);
                    cap.ts = millis();
                    if (self.pmkids.size() < 50) self.pmkids.push_back(cap);
                    char hex[33];
                    for (int i=0;i<16;i++) sprintf(hex+i*2,"%02X",pmkid[i]);
                    LOG.success("[PMKID] " + cap.bssid + " -> " + String(hex));
                }
                HandshakeCapture hs;
                hs.bssid  = WiFiScanner::bssidToStr(hdr->a1);
                hs.client = WiFiScanner::bssidToStr(hdr->a2);
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
                if (self.handshakes.size() < 100) self.handshakes.push_back(hs);

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
                    if (st->crackable() && hs.type <= 2)
                        LOG.success("[HS-CRACK] " + hs.bssid + " CRACKAVEL (M1+M2) — use hashcat -m 22000");
                    if (st->complete())
                        LOG.success("[HS-FULL] " + hs.bssid + " HANDSHAKE COMPLETO (M1-M4) — aircrack-ng pronto");
                }
                LOG.success("[EAPOL M" + String(hs.type) + "] " + hs.bssid + " <-> " + hs.client);
            }
        }
    }
};
