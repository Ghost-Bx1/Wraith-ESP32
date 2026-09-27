#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include "../utils/logger.h"

struct APInfo {
    String ssid;
    uint8_t bssid[6];
    String bssidStr;
    int32_t rssi;
    uint8_t channel;
    wifi_auth_mode_t auth;
    int clients;
};

struct ClientInfo {
    uint8_t mac[6];
    String macStr;
    String apBssid;
    int32_t rssi;
    unsigned long lastSeen;
};

class WiFiScanner {
public:
    static WiFiScanner& get() { static WiFiScanner inst; return inst; }

    void scan() {
        LOG.info("Iniciando scan de redes WiFi...");
        _aps.clear();
        int n = WiFi.scanNetworks(false, true);
        for (int i = 0; i < n && i < MAX_NETS; i++) {
            APInfo ap;
            ap.ssid    = WiFi.SSID(i);
            ap.rssi    = WiFi.RSSI(i);
            ap.channel = WiFi.channel(i);
            ap.auth    = WiFi.encryptionType(i);
            ap.clients = 0;
            uint8_t* bssidPtr = WiFi.BSSID(i);
            if (bssidPtr) memcpy(ap.bssid, bssidPtr, 6);
            ap.bssidStr = bssidToStr(ap.bssid);
            _aps.push_back(ap);
        }
        WiFi.scanDelete();
        LOG.success("Scan concluído: " + String(_aps.size()) + " redes encontradas");
    }

    const std::vector<APInfo>&    aps()     const { return _aps; }
    const std::vector<ClientInfo>& clients() const { return _clients; }

    // Chamado pelo sniffer de promiscuous para registrar clientes
    void addClient(const uint8_t* mac, const uint8_t* apMac, int8_t rssi) {
        String macStr = bssidToStr(mac);
        for (auto& c : _clients) {
            if (c.macStr == macStr) { c.lastSeen = millis(); c.rssi = rssi; return; }
        }
        if (_clients.size() >= MAX_CLIENTS) _clients.erase(_clients.begin());
        ClientInfo c;
        memcpy(c.mac, mac, 6);
        c.macStr   = macStr;
        c.apBssid  = bssidToStr(apMac);
        c.rssi     = rssi;
        c.lastSeen = millis();
        _clients.push_back(c);
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
