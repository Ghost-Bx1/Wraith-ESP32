#pragma once
// BT Classic não pode rodar simultaneamente com NimBLE no mesmo ESP32.
// Para habilitar: remova NimBLE-Arduino do platformio.ini e descomente
// BTClassic::get().begin() em main.cpp.

#include <Arduino.h>
#include <vector>
#include "../utils/logger.h"

struct BTClassicDev {
    String mac, name;
    uint32_t cod = 0;
    int rssi = 0;
    unsigned long ts = 0;
};

class BTClassic {
public:
    static BTClassic& get() { static BTClassic inst; return inst; }

    bool running = false;
    std::vector<BTClassicDev> devices;

    void begin()                     { LOG.warn("BT Classic desabilitado (NimBLE ativo)"); }
    void startInquiry(int secs = 10) { LOG.warn("BT Classic indisponível com NimBLE"); }
    void stop()                      { running = false; }
    void setName(const String& n)    { LOG.warn("BT Classic indisponível — nome ignorado: " + n); }
};
