#pragma once
/*
 * CaptureStore — Persistência de capturas em LittleFS — Melhoria #44
 * Salva handshakes .hc22000, PMKIDs e credenciais automaticamente no filesystem.
 * Os arquivos sobrevivem ao reboot e podem ser baixados via /api/captures/download
 *
 * Estrutura no filesystem:
 *   /captures/hs_BSSID.hc22000     — handshakes WPA2 (formato hashcat)
 *   /captures/pmkid_BSSID.hc22000  — PMKIDs (formato hashcat -m 22000)
 *   /captures/creds.json           — credenciais evil twin
 *   /captures/log.jsonl            — eventos importantes (JSONL)
 */
#include <Arduino.h>
#include <LittleFS.h>
#include "../utils/logger.h"

class CaptureStore {
public:
    static CaptureStore& get() { static CaptureStore inst; return inst; }

    void begin() {
        LittleFS.mkdir("/captures");
        // Conta arquivos existentes
        File root = LittleFS.open("/captures");
        if (!root) return;
        int cnt = 0;
        File f = root.openNextFile();
        while (f) { cnt++; f = root.openNextFile(); }
        LOG.info("[STORE] " + String(cnt) + " capturas no filesystem");
    }

    // Salva handshake no formato hc22000
    // hc22000: WPA*02*PMKID*AP_MAC*STA_MAC*ESSID***
    bool saveHandshake(const String& ssid, const String& bssid, const String& client,
                       const uint8_t* mic, const uint8_t* anonce, const uint8_t* snonce,
                       const uint8_t* eapolData, size_t eapolLen) {
        String fname = "/captures/hs_" + _sanitize(bssid) + ".hc22000";
        File f = LittleFS.open(fname, "a");
        if (!f) return false;

        // Linha hc22000 WPA*02 (handshake)
        String line = "WPA*02*";
        line += _hexStr(mic, 16) + "*";
        line += _macFlat(bssid) + "*";
        line += _macFlat(client) + "*";
        line += _hexStr((uint8_t*)ssid.c_str(), ssid.length()) + "*";
        line += _hexStr(anonce, 32) + "*";
        line += _hexStr(snonce, 32) + "*";
        line += _hexStr(eapolData, eapolLen) + "\n";

        f.print(line);
        f.close();
        _captureCount++;
        LOG.success("[STORE] Handshake salvo: " + fname);
        return true;
    }

    // Salva PMKID no formato hc22000
    bool savePMKID(const String& ssid, const String& bssid, const String& client,
                   const uint8_t pmkid[16]) {
        String fname = "/captures/pmkid_" + _sanitize(bssid) + ".hc22000";
        File f = LittleFS.open(fname, "a");
        if (!f) return false;

        // hc22000 WPA*01 (PMKID)
        String line = "WPA*01*";
        line += _hexStr(pmkid, 16) + "*";
        line += _macFlat(bssid) + "*";
        line += _macFlat(client) + "*";
        line += _hexStr((uint8_t*)ssid.c_str(), ssid.length()) + "***\n";

        f.print(line);
        f.close();
        _captureCount++;
        LOG.success("[STORE] PMKID salvo: " + fname);
        return true;
    }

    // Salva credencial (evil twin / portal)
    bool saveCred(const String& ssid, const String& password, const String& clientMac) {
        File f = LittleFS.open("/captures/creds.json", "a");
        if (!f) return false;

        String entry = "{\"ts\":" + String(millis() / 1000) +
                       ",\"ssid\":\"" + ssid + "\"" +
                       ",\"pass\":\"" + password + "\"" +
                       ",\"mac\":\"" + clientMac + "\"}\n";
        f.print(entry);
        f.close();
        return true;
    }

    // Lista todos os arquivos de captura
    String listJson() {
        String json = "[";
        File root = LittleFS.open("/captures");
        if (!root) return "[]";
        File f = root.openNextFile();
        bool first = true;
        while (f) {
            if (!first) json += ",";
            json += "{\"name\":\"" + String(f.name()) +
                    "\",\"size\":" + String(f.size()) + "}";
            first = false;
            f = root.openNextFile();
        }
        return json + "]";
    }

    // Apaga todas as capturas (auto-destruct — melhoria #48)
    void wipeAll() {
        File root = LittleFS.open("/captures");
        if (!root) return;
        File f = root.openNextFile();
        while (f) {
            String name = "/captures/" + String(f.name());
            f = root.openNextFile();
            LittleFS.remove(name);
        }
        _captureCount = 0;
        LOG.warn("[STORE] Todas as capturas apagadas (auto-destruct)");
    }

    int captureCount() const { return _captureCount; }

private:
    int _captureCount = 0;

    String _sanitize(const String& s) {
        String out = s;
        out.replace(":", "");
        return out;
    }

    String _macFlat(const String& mac) {
        String out = mac;
        out.replace(":", "");
        return out;
    }

    String _hexStr(const uint8_t* data, size_t len) {
        String out;
        for (size_t i = 0; i < len; i++) {
            if (data[i] < 0x10) out += "0";
            out += String(data[i], HEX);
        }
        return out;
    }
};
