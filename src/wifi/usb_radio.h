#pragma once
/*
 * USB Radio — suporte a adaptadores de rede conectados via USB OTG
 *
 * O ESP32-S3 possui USB OTG nativo (GPIO19=D-, GPIO20=D+).
 * Este módulo gerencia adaptadores conectados ao ESP32-S3 via USB:
 *
 *   Modo 1 — USB-UART CDC (CH340/CP2102/CH343 externo):
 *     Conecte um segundo ESP32/ESP8266 com firmware AT ao USB OTG do S3.
 *     Funciona como um Radio2 adicional com AT commands.
 *
 *   Modo 2 — USB CDC-ACM nativo (ESP32-S3 como host):
 *     O ESP32-S3 atua como USB Host e enumera o adaptador via TinyUSB.
 *     Requer que o adaptador seja CDC-ACM (virtual serial).
 *
 *   Modo 3 — ESP-NOW Relay via USB:
 *     Segundo módulo ESP32 atua como rádio de injeção extra via ESP-NOW
 *     e recebe comandos pelo link USB serial.
 *
 * Pinagem USB OTG no ESP32-S3 DevKitC-1:
 *   GPIO19 = D-  (conecte ao D- do adaptador / hub)
 *   GPIO20 = D+  (conecte ao D+ do adaptador / hub)
 *   5V     = VBUS (alimente o adaptador pelo pino 5V do DevKit)
 *
 * Wiring para módulo ESP32 via USB-UART externo (CH343):
 *   CH343 TX → ESP32-S3 GPIO19 (via resistor 330Ω)
 *   CH343 RX → ESP32-S3 GPIO20
 *   CH343 GND → GND
 *   CH343 3.3V → 3.3V
 *   (use UART1 GPIO17/16 se USB OTG estiver ocupado)
 */

#include <Arduino.h>
#include "../config.h"
#include "../utils/logger.h"

// Protocolo de comunicação USB Radio ↔ ESP32-S3
// Cada mensagem: [STX=0xAA][LEN:1][CMD:1][PAYLOAD...][CRC:1]
namespace USBProto {
    static const uint8_t STX         = 0xAA;
    static const uint8_t CMD_PING    = 0x01;
    static const uint8_t CMD_DEAUTH  = 0x10; // BSSID[6] + CLIENT[6] + CH[1]
    static const uint8_t CMD_BEACON  = 0x11; // SSID[32] + CH[1]
    static const uint8_t CMD_SCAN    = 0x20; // → resposta JSON via serial
    static const uint8_t CMD_INJECT  = 0x30; // frame bruto [LEN:2][DATA...]
    static const uint8_t CMD_CHANNEL = 0x40; // CH[1]
    static const uint8_t CMD_STATUS  = 0xF0; // → resposta JSON
    static const uint8_t CMD_RESET   = 0xFF;
}

class USBRadio {
public:
    static USBRadio& get() { static USBRadio inst; return inst; }

    bool    available  = false;
    bool    running    = false;
    uint8_t channel    = 1;
    uint32_t txPackets = 0;
    uint32_t rxPackets = 0;
    String  fwVersion;
    String  chipModel;       // "ESP32", "ESP8266", etc.
    String  lastStatus;

    // Inicializa na UART configurada para USB radio
    void begin() {
#ifdef USB_RADIO_SERIAL
        USB_RADIO_SERIAL.begin(USB_RADIO_BAUD, SERIAL_8N1, USB_RADIO_RX, USB_RADIO_TX);
        delay(300);
        if (_ping()) {
            available = true;
            _queryStatus();
            LOG.success("[USBR] Adaptador USB detectado: " + chipModel + " fw=" + fwVersion);
        } else {
            LOG.info("[USBR] Nenhum adaptador USB detectado — operando rádio único");
        }
#else
        LOG.info("[USBR] USB Radio não configurado nesta placa");
#endif
    }

    // Tick — processa respostas assíncronas do adaptador
    void tick() {
        if (!available) return;
#ifdef USB_RADIO_SERIAL
        while (USB_RADIO_SERIAL.available()) {
            String line = USB_RADIO_SERIAL.readStringUntil('\n');
            line.trim();
            if (line.length() > 0) _handleLine(line);
        }
#endif
    }

    // Injeta frame 802.11 bruto via segundo rádio
    bool injectFrame(const uint8_t* frame, uint16_t len) {
        if (!available || len > 400) return false;
#ifdef USB_RADIO_SERIAL
        uint8_t buf[4];
        buf[0] = USBProto::STX;
        buf[1] = (uint8_t)(len + 2);
        buf[2] = USBProto::CMD_INJECT;
        USB_RADIO_SERIAL.write(buf, 3);
        USB_RADIO_SERIAL.write((uint8_t)(len >> 8));
        USB_RADIO_SERIAL.write((uint8_t)(len & 0xFF));
        USB_RADIO_SERIAL.write(frame, len);
        USB_RADIO_SERIAL.write(_crc(frame, len));
        txPackets++;
        return true;
#else
        return false;
#endif
    }

    // Ordena deauth ao segundo rádio (alivia carga do rádio principal)
    bool sendDeauth(const uint8_t ap[6], const uint8_t cli[6], uint8_t ch) {
        if (!available) return false;
#ifdef USB_RADIO_SERIAL
        uint8_t payload[13];
        payload[0] = USBProto::STX;
        payload[1] = 14;
        payload[2] = USBProto::CMD_DEAUTH;
        memcpy(&payload[3], ap, 6);
        memcpy(&payload[9], cli ? cli : ap, 6);
        payload[12] = ch;
        USB_RADIO_SERIAL.write(payload, 13);
        txPackets++;
        return true;
#else
        return false;
#endif
    }

    // Seta canal do segundo rádio
    void setChannel(uint8_t ch) {
        channel = ch;
        if (!available) return;
#ifdef USB_RADIO_SERIAL
        uint8_t buf[4] = { USBProto::STX, 2, USBProto::CMD_CHANNEL, ch };
        USB_RADIO_SERIAL.write(buf, 4);
#endif
    }

    // Scan de redes via segundo rádio (retorna raw SSID list)
    String scanNetworks() {
        if (!available) return "[]";
#ifdef USB_RADIO_SERIAL
        uint8_t buf[3] = { USBProto::STX, 1, USBProto::CMD_SCAN };
        USB_RADIO_SERIAL.write(buf, 3);
        delay(5000);
        String result;
        unsigned long t = millis();
        while (millis() - t < 8000 && USB_RADIO_SERIAL.available())
            result += USB_RADIO_SERIAL.readString();
        return result;
#else
        return "[]";
#endif
    }

    String statusJson() const {
        String j = "{\"available\":" + String(available ? "true":"false");
        j += ",\"chip\":\"" + chipModel + "\"";
        j += ",\"fw\":\"" + fwVersion + "\"";
        j += ",\"ch\":" + String(channel);
        j += ",\"txPkts\":" + String(txPackets);
        j += ",\"rxPkts\":" + String(rxPackets);
        j += "}";
        return j;
    }

private:
#ifdef USB_RADIO_SERIAL
    bool _ping() {
        uint8_t buf[3] = { USBProto::STX, 1, USBProto::CMD_PING };
        USB_RADIO_SERIAL.write(buf, 3);
        unsigned long t = millis();
        String resp;
        while (millis() - t < 2000) {
            while (USB_RADIO_SERIAL.available())
                resp += (char)USB_RADIO_SERIAL.read();
            if (resp.indexOf("PONG") >= 0) return true;
        }
        return false;
    }

    void _queryStatus() {
        uint8_t buf[3] = { USBProto::STX, 1, USBProto::CMD_STATUS };
        USB_RADIO_SERIAL.write(buf, 3);
        delay(500);
        String resp;
        while (USB_RADIO_SERIAL.available()) resp += (char)USB_RADIO_SERIAL.read();
        lastStatus = resp;
        // Extrai chip e versão do JSON de status
        int ci = resp.indexOf("\"chip\":\"");
        if (ci >= 0) { ci += 8; chipModel = resp.substring(ci, resp.indexOf('"', ci)); }
        int vi = resp.indexOf("\"fw\":\"");
        if (vi >= 0) { vi += 6; fwVersion = resp.substring(vi, resp.indexOf('"', vi)); }
        if (chipModel.isEmpty()) chipModel = "ESP?";
        if (fwVersion.isEmpty()) fwVersion = "?";
    }

    void _handleLine(const String& line) {
        rxPackets++;
        if (line.startsWith("{")) {
            // JSON de status/evento
            if (line.indexOf("\"evt\":\"found\"") >= 0)
                LOG.success("[USBR] Captura remota: " + line);
            else if (line.indexOf("\"evt\":\"error\"") >= 0)
                LOG.error("[USBR] Erro remoto: " + line);
            else
                LOG.data("[USBR] " + line);
        } else {
            LOG.data("[USBR] " + line);
        }
    }
#endif

    static uint8_t _crc(const uint8_t* d, uint16_t len) {
        uint8_t crc = 0;
        for (uint16_t i = 0; i < len; i++) crc ^= d[i];
        return crc;
    }
};
