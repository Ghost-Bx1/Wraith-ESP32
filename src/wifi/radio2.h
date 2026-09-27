#pragma once
// ── Segundo rádio WiFi via UART (ESP8266 com firmware AT ou NodeMCU) ──────────
// Wiring: ESP32 TX2(GPIO17) → ESP8266 RX | ESP32 RX2(GPIO16) → ESP8266 TX
// O ESP8266 deve estar rodando o firmware esp8266-deauther OU AT commands

#include <Arduino.h>
#include "../config.h"
#include "../utils/logger.h"

class Radio2 {
public:
    static Radio2& get() { static Radio2 inst; return inst; }

    bool available = false;
    String lastResponse;

    void begin() {
        RADIO2_SERIAL.begin(RADIO2_BAUD, SERIAL_8N1, RADIO2_RX, RADIO2_TX);
        delay(500);
        send("AT");
        if (lastResponse.indexOf("OK") >= 0) {
            available = true;
            LOG.success("Segundo rádio WiFi detectado (ESP8266)");
        } else {
            LOG.warn("Segundo rádio não respondeu — operando com rádio único");
        }
    }

    // Envia comando AT e aguarda resposta
    String send(const String& cmd, uint32_t timeout = 1500) {
        RADIO2_SERIAL.println(cmd);
        unsigned long t = millis();
        lastResponse = "";
        while (millis() - t < timeout) {
            while (RADIO2_SERIAL.available())
                lastResponse += (char)RADIO2_SERIAL.read();
        }
        return lastResponse;
    }

    // Faz scan via segundo rádio (retorna raw AT+CWLAP)
    String scanNetworks() {
        return send("AT+CWLAP", 8000);
    }

    // Conecta segundo rádio a uma rede alvo (útil para MITM)
    bool connectTo(const String& ssid, const String& pass) {
        String r = send("AT+CWJAP=\"" + ssid + "\",\"" + pass + "\"", 10000);
        bool ok = r.indexOf("WIFI CONNECTED") >= 0;
        if (ok) LOG.success("Radio2 conectado a " + ssid);
        else    LOG.error("Radio2 falhou conectar a " + ssid);
        return ok;
    }

    // Lê dados brutos que chegam do segundo rádio
    void tick() {
        while (RADIO2_SERIAL.available()) {
            String line = RADIO2_SERIAL.readStringUntil('\n');
            if (line.length() > 0)
                LOG.data("[R2] " + line);
        }
    }
};
