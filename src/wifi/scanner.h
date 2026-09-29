#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <math.h>
#include "../utils/logger.h"

struct APInfo {
    String ssid;
    uint8_t bssid[6];
    String bssidStr;
    int32_t rssi;
    uint8_t channel;
    wifi_auth_mode_t auth;
    int clients = 0;
    bool hidden = false;
    bool wps    = false;
    String vendor;       // fabricante via OUI
    float  distM = 0.0f; // distância estimada em metros
};

struct ClientInfo {
    uint8_t mac[6];
    String macStr;
    String apBssid;
    int32_t rssi;
    unsigned long lastSeen;
    String vendor;
    std::vector<String> probedSSIDs;
};

class WiFiScanner {
public:
    static WiFiScanner& get() { static WiFiScanner inst; return inst; }

    void scan() {
        LOG.info("Iniciando scan de redes WiFi...");
        _aps.clear();
        int n = WiFi.scanNetworks(false, true);
        int hiddenCnt = 0;
        for (int i = 0; i < n && i < MAX_NETS; i++) {
            APInfo ap;
            ap.ssid    = WiFi.SSID(i);
            ap.rssi    = WiFi.RSSI(i);
            ap.channel = WiFi.channel(i);
            ap.auth    = WiFi.encryptionType(i);
            ap.hidden  = (ap.ssid.length() == 0);
            ap.clients = 0;
            if (ap.hidden) hiddenCnt++;
            uint8_t* bssidPtr = WiFi.BSSID(i);
            if (bssidPtr) memcpy(ap.bssid, bssidPtr, 6);
            ap.bssidStr = bssidToStr(ap.bssid);
            ap.vendor   = lookupOUI(ap.bssid);
            ap.distM    = rssiToDistance(ap.rssi);
            _aps.push_back(ap);
        }
        WiFi.scanDelete();
        LOG.success("Scan: " + String(_aps.size()) + " redes (" + String(hiddenCnt) + " ocultas)");
    }

    // RSSI → distância estimada (modelo log-distance, n=2.7, txPower=-65dBm@1m)
    static float rssiToDistance(int32_t rssi) {
        if (rssi == 0) return 999.0f;
        return powf(10.0f, (-65.0f - (float)rssi) / (10.0f * 2.7f));
    }

    // OUI lookup (primeiros 3 bytes do MAC → fabricante)
    static String lookupOUI(const uint8_t* mac) {
        // Compara primeiros 3 octetos
        struct { uint8_t o[3]; const char* name; } tbl[] = {
            {{0xB8,0x27,0xEB},"Raspberry Pi"},  {{0xDC,0xA6,0x32},"Raspberry Pi"},
            {{0xE4,0x5F,0x01},"Raspberry Pi"},  {{0x00,0x50,0xF2},"Microsoft"},
            {{0x28,0xCF,0xE9},"Apple"},          {{0xAC,0xBC,0x32},"Apple"},
            {{0xF0,0x18,0x98},"Apple"},          {{0x84,0x85,0x06},"Apple"},
            {{0xA4,0xC3,0xF0},"Apple"},          {{0x3C,0xCD,0x5D},"Apple"},
            {{0x14,0xF6,0x5A},"Apple"},          {{0x10,0x9A,0xDD},"Apple"},
            {{0x38,0xF9,0xD3},"Apple"},          {{0x00,0x17,0xF2},"Apple"},
            {{0x68,0x09,0x27},"Samsung"},        {{0xE4,0x92,0xFB},"Samsung"},
            {{0xA0,0x82,0x1F},"Samsung"},        {{0x50,0xA4,0xD0},"Samsung"},
            {{0xF4,0x42,0x8F},"Google"},         {{0x54,0x60,0x09},"Google"},
            {{0x48,0xD6,0xD5},"Google"},         {{0x90,0x61,0xAE},"Xiaomi"},
            {{0x08,0x21,0xEF},"Xiaomi"},         {{0x78,0x11,0xDC},"Xiaomi"},
            {{0x60,0x38,0xE0},"Xiaomi"},         {{0xFC,0x64,0xBA},"Xiaomi"},
            {{0x94,0xB8,0x6D},"Motorola"},       {{0x00,0x24,0x1D},"Motorola"},
            {{0x14,0x2D,0x27},"TP-Link"},        {{0x14,0xCC,0x20},"TP-Link"},
            {{0x50,0x3E,0xAA},"TP-Link"},        {{0xC4,0xE9,0x84},"TP-Link"},
            {{0xEC,0x08,0x6B},"TP-Link"},        {{0xB0,0x95,0x8E},"TP-Link"},
            {{0x30,0x9C,0x23},"Netgear"},        {{0xA0,0x63,0x91},"Netgear"},
            {{0xC4,0x04,0x15},"Netgear"},        {{0xC0,0x3F,0xD5},"D-Link"},
            {{0x28,0x10,0x7B},"D-Link"},         {{0xB0,0xC5,0x54},"Asus"},
            {{0x10,0x7B,0x44},"Asus"},           {{0x2C,0xFD,0xA1},"Asus"},
            {{0x60,0x45,0xCB},"Intelbras"},      {{0x00,0x1E,0x8C},"Intelbras"},
            {{0xDC,0x9F,0xDB},"Huawei"},         {{0x48,0x46,0xFB},"Huawei"},
            {{0x70,0x72,0xCF},"Huawei"},         {{0x00,0xE0,0xFC},"Huawei"},
            {{0x00,0x50,0x7F},"Cisco"},          {{0x00,0x1A,0xA1},"Cisco"},
            {{0x00,0x26,0xCA},"Cisco"},          {{0xC4,0x71,0xFE},"Cisco"},
            {{0x00,0x0C,0xE7},"Mikrotik"},       {{0x6C,0x3B,0x6B},"Mikrotik"},
            {{0xD4,0xCA,0x6D},"Mikrotik"},       {{0x18,0xFD,0x74},"Ubiquiti"},
            {{0x24,0xA4,0x3C},"Ubiquiti"},       {{0x78,0x8A,0x20},"Ubiquiti"},
            {{0x00,0x00,0x00},nullptr}
        };
        for (int i = 0; tbl[i].name; i++)
            if (mac[0]==tbl[i].o[0] && mac[1]==tbl[i].o[1] && mac[2]==tbl[i].o[2])
                return String(tbl[i].name);
        return "";
    }

    const std::vector<APInfo>&    aps()     const { return _aps; }
    const std::vector<ClientInfo>& clients() const { return _clients; }

    // Chamado pelo sniffer de promiscuous para registrar clientes
    void addClient(const uint8_t* mac, const uint8_t* apMac, int8_t rssi) {
        String macStr = bssidToStr(mac);
        for (auto& c : _clients) {
            if (c.macStr == macStr) { c.lastSeen = millis(); c.rssi = rssi; return; }
        }
        // Aging: remove cliente mais antigo quando lotado
        if (_clients.size() >= MAX_CLIENTS) {
            size_t oldest = 0;
            for (size_t i = 1; i < _clients.size(); i++)
                if (_clients[i].lastSeen < _clients[oldest].lastSeen) oldest = i;
            _clients.erase(_clients.begin() + oldest);
        }
        ClientInfo c;
        memcpy(c.mac, mac, 6);
        c.macStr   = macStr;
        c.apBssid  = bssidToStr(apMac);
        c.rssi     = rssi;
        c.lastSeen = millis();
        c.vendor   = lookupOUI(mac);
        _clients.push_back(c);
    }

    void addClientProbe(const String& mac, const String& ssid) {
        for (auto& c : _clients) {
            if (c.macStr == mac) {
                for (auto& s : c.probedSSIDs) if (s == ssid) return;
                if (c.probedSSIDs.size() < 20) c.probedSSIDs.push_back(ssid);
                return;
            }
        }
    }

    static String bssidToStr(const uint8_t* b) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 b[0],b[1],b[2],b[3],b[4],b[5]);
        return String(buf);
    }

    void clearClients() { _clients.clear(); }

private:
    std::vector<APInfo>    _aps;
    std::vector<ClientInfo> _clients;
};
