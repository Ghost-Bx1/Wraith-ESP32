#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"

/*
 * WPS Scanner — detecta APs com WPS ativo via beacons/probe responses.
 * WPS vulnerável a: Pixie Dust (offline attack na EAP exchange) e PIN bruteforce.
 */
class WPSScanner {
public:
    static WPSScanner& get() { static WPSScanner inst; return inst; }

    struct WPSResult {
        String bssid, ssid;
        uint8_t channel;
        String version;      // "1.0" / "2.0"
        String configMethods; // "Push Button", "PIN", etc.
        bool   locked;        // AP Setup Locked (WPS bloqueado)
        bool   pushButton;
        bool   pinEnabled;
        int    rssi;
    };

    bool running = false;
    std::vector<WPSResult> results;

    void start() {
        results.clear();
        running = true;
        esp_wifi_set_promiscuous_filter(nullptr);
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&_rxCb);
        LOG.warn("[WPS SCAN] Iniciado — capturando beacons para IE WPS");
    }

    void stop() {
        esp_wifi_set_promiscuous(false);
        running = false;
        LOG.info("[WPS SCAN] Parado | APs com WPS: " + String(results.size()));
        for (auto& r : results) {
            String info = "[WPS] " + r.ssid + " " + r.bssid +
                          " v" + r.version +
                          " [" + r.configMethods + "]";
            if (r.locked) info += " !!LOCKED!!";
            if (r.pinEnabled) info += " PIN-VULN";
            LOG.data(info);
        }
    }

private:
    struct Hdr {
        uint16_t fc, dur;
        uint8_t  a1[6], a2[6], a3[6];
        uint16_t seq;
    } __attribute__((packed));

    static const char* _cfgMethod(uint16_t m) {
        if (m & 0x0010) return "PIN+PBC";
        if (m & 0x0080) return "PIN";
        if (m & 0x0200) return "PBC";
        return "Unknown";
    }

    static void _rxCb(void* buf, wifi_promiscuous_pkt_type_t type) {
        auto& self = WPSScanner::get();
        if (!self.running) return;
        auto* pkt  = (wifi_promiscuous_pkt_t*)buf;
        size_t len = pkt->rx_ctrl.sig_len;
        if (len < sizeof(Hdr) + 12) return;
        auto* hdr = (Hdr*)pkt->payload;
        uint8_t ftype = (hdr->fc & 0x000C) >> 2;
        uint8_t fsub  = (hdr->fc & 0x00F0) >> 4;
        // Beacon=8 or Probe Response=5
        if (ftype != 0 || (fsub != 8 && fsub != 5)) return;

        const uint8_t* body = pkt->payload + sizeof(Hdr);
        size_t blen = len - sizeof(Hdr);
        if (blen < 12) return;

        // Skip fixed fields: Timestamp(8) + Interval(2) + Capability(2)
        size_t pos = 12;
        String ssid = "";

        // First pass: get SSID
        size_t p2 = pos;
        while (p2 + 2 < blen) {
            uint8_t tag = body[p2], tlen = body[p2+1];
            if (tag == 0x00 && tlen > 0 && tlen <= 32)
                ssid = String((char*)&body[p2+2]).substring(0, tlen);
            p2 += 2 + tlen;
        }

        // Second pass: find WPS IE (0xDD, OUI 00:50:F2:04)
        while (pos + 2 < blen) {
            uint8_t tag  = body[pos];
            uint8_t tlen = body[pos+1];
            if (pos + 2 + tlen > blen) break;
            if (tag == 0xDD && tlen >= 4) {
                const uint8_t* d = &body[pos+2];
                if (d[0]==0x00 && d[1]==0x50 && d[2]==0xF2 && d[3]==0x04) {
                    // Found WPS IE — parse TLVs
                    WPSResult res;
                    res.bssid   = _bssidStr(hdr->a3);
                    res.ssid    = ssid;
                    res.channel = pkt->rx_ctrl.channel;
                    res.rssi    = pkt->rx_ctrl.rssi;
                    res.locked  = false;
                    res.pinEnabled   = false;
                    res.pushButton   = false;
                    res.version      = "1.0";
                    res.configMethods = "Unknown";

                    size_t tp = 4;
                    while (tp + 4 < tlen) {
                        uint16_t attr = (d[tp]<<8)|d[tp+1];
                        uint16_t alen = (d[tp+2]<<8)|d[tp+3];
                        if (tp + 4 + alen > tlen) break;
                        const uint8_t* av = d + tp + 4;
                        switch (attr) {
                            case 0x104A: // Version
                                if (alen >= 1)
                                    res.version = String(av[0] >> 4) + "." + String(av[0] & 0xF);
                                break;
                            case 0x1057: // AP Setup Locked
                                if (alen >= 1) res.locked = (av[0] != 0);
                                break;
                            case 0x1012: // Config Methods
                                if (alen >= 2) {
                                    uint16_t m = (av[0]<<8)|av[1];
                                    res.configMethods = _cfgMethod(m);
                                    res.pushButton = (m & 0x0200) || (m & 0x0010);
                                    res.pinEnabled = (m & 0x0080) || (m & 0x0010);
                                }
                                break;
                        }
                        tp += 4 + alen;
                    }

                    // Evita duplicatas
                    bool dup = false;
                    for (auto& r : self.results)
                        if (r.bssid == res.bssid) { dup = true; break; }
                    if (!dup && self.results.size() < 64) {
                        self.results.push_back(res);
                        String log = "[WPS] " + res.ssid + " [" + res.bssid + "] v" + res.version;
                        if (res.locked) log += " LOCKED";
                        if (res.pinEnabled) log += " PIN-VULN";
                        LOG.data(log);
                    }
                }
            }
            pos += 2 + tlen;
        }
    }

    static String _bssidStr(const uint8_t* b) {
        char buf[18];
        snprintf(buf,18,"%02X:%02X:%02X:%02X:%02X:%02X",b[0],b[1],b[2],b[3],b[4],b[5]);
        return buf;
    }
};
