#pragma once
/*
 * Channel Map — Mapa de densidade de redes por canal — Melhoria #42
 * Rastreia quantas redes existem em cada canal (1-13) e qual tem menor
 * congestionamento — útil para escolher canal do AP falso.
 */
#include <Arduino.h>

class ChannelMap {
public:
    static ChannelMap& get() { static ChannelMap inst; return inst; }

    struct ChanInfo {
        uint8_t  channel  = 0;
        int      count    = 0;   // número de APs no canal
        int8_t   avgRssi  = -100;
        int      rssiSum  = 0;
    };

    ChanInfo channels[14] = {}; // índice 0 não usado, 1-13

    void begin() {
        for (int i = 1; i <= 13; i++) {
            channels[i].channel = i;
            channels[i].count   = 0;
            channels[i].rssiSum = 0;
        }
    }

    void reset() { begin(); }

    void addNetwork(uint8_t ch, int8_t rssi) {
        if (ch < 1 || ch > 13) return;
        channels[ch].count++;
        channels[ch].rssiSum += rssi;
        channels[ch].avgRssi = (int8_t)(channels[ch].rssiSum / channels[ch].count);
    }

    // Retorna canal menos congestionado (menor count, entre 1,6,11 primeiro)
    uint8_t bestFreeChannel() const {
        // Prefere canais não sobrepostos
        uint8_t pref[] = {1, 6, 11, 3, 8, 13, 2, 5, 7, 9, 4, 10, 12};
        uint8_t best = 6;
        int minCount = 9999;
        for (uint8_t ch : pref) {
            if (channels[ch].count < minCount) {
                minCount = channels[ch].count;
                best = ch;
            }
        }
        return best;
    }

    String toJson() const {
        String json = "[";
        for (int i = 1; i <= 13; i++) {
            if (i > 1) json += ",";
            json += "{\"ch\":" + String(i) +
                    ",\"aps\":" + String(channels[i].count) +
                    ",\"rssi\":" + String(channels[i].avgRssi) + "}";
        }
        return json + "]";
    }
};
