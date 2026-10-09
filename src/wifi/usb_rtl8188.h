#pragma once
/*
 * Driver USB RTL8188EU/RTL8188CUS para ESP32-S3 via TinyUSB Host
 *
 * Chips suportados:
 *   RTL8188EU  — VID 0x0BDA  PID 0x8179  (TP-Link TL-WN722N v1, Alfa AWUS036NHA-like)
 *   RTL8188CUS — VID 0x0BDA  PID 0x8176  (Alfa AWUS036NHR, clones baratos)
 *   RTL8188ETV — VID 0x0BDA  PID 0x018A
 *
 * Porta: USB OTG conector USB-C esquerdo (GPIO19 D-, GPIO20 D+)
 * Ative: #define USB_RTL8188 1  em config.h
 *
 * Capacidades:
 *   - Modo monitor (captura de frames 802.11 raw)
 *   - Injeção de frames (deauth, beacon, probe)
 *   - Troca de canal 1-14
 *   - Callback por frame recebido com RSSI
 *
 * Limitação: initChip() faz init mínimo sem carregar firmware proprietário.
 *            Para RX funcional no modo monitor o firmware RTL8188EU.bin
 *            precisa ser embedado (ver USB_RTL8188_FW_EMBED abaixo).
 */
#include <Arduino.h>
#include "../config.h"

#ifdef USB_RTL8188

#include "tusb.h"

// ── Constantes RTL8188 ───────────────────────────────────────────────────────
namespace RTL8188 {
    static const uint16_t VID              = 0x0BDA;
    static const uint16_t PID_RTL8188EU   = 0x8179;
    static const uint16_t PID_RTL8188CUS  = 0x8176;
    static const uint16_t PID_RTL8188ETV  = 0x018A;

    static const uint8_t EP_BULK_IN   = 0x81;
    static const uint8_t EP_BULK_OUT  = 0x02;

    static const uint8_t REQ_WRITE_REG  = 0x05;
    static const uint8_t REQ_READ_REG   = 0x06;

    // Registradores
    static const uint16_t REG_SYS_FUNC  = 0x0010;
    static const uint16_t REG_CR        = 0x0100;
    static const uint16_t REG_RCR       = 0x0608;
    static const uint16_t REG_CHANNEL   = 0x040A;

    // RCR — modo monitor: aceita todos os frames
    static const uint32_t RCR_MONITOR   = 0x7000028F;

    // TX descriptor size
    static const int TX_DESC_SIZE = 40;
    static const int RX_DESC_SIZE = 24;
    static const int RX_BUF_SIZE  = 65536;
    static const int TX_BUF_SIZE  = 4096;
}

// ── Instância global (singleton via ponteiro) ────────────────────────────────
// Declarada como struct para que os callbacks extern "C" acessem sem friccão
struct USBRTL8188 {
    // ── Estado ───────────────────────────────────────────────────────────────
    enum class State { DISCONNECTED, ENUMERATING, INIT, MONITOR, ERROR };
    State    state   = State::DISCONNECTED;
    bool     running = false;
    uint8_t  devAddr = 0;
    uint8_t  channel = 6;
    uint32_t rxCount = 0;
    uint32_t txCount = 0;

    // Callback: chamado a cada frame capturado no modo monitor
    std::function<void(const uint8_t* frame, size_t len, int8_t rssi, uint8_t ch)> onFrame;

    // ── Singleton ────────────────────────────────────────────────────────────
    static USBRTL8188& get() { static USBRTL8188 inst; return inst; }

    // ── Inicializa TinyUSB host ──────────────────────────────────────────────
    bool begin() {
        if (_initialized) return true;

        if (!tusb_init()) {
            _log("TinyUSB init falhou");
            state = State::ERROR;
            return false;
        }

        _initialized = true;
        _log("Aguardando adaptador RTL8188 na porta USB OTG...");

        xTaskCreatePinnedToCore(_usbTask, "usb_host", 8192, this, 5, nullptr, 0);
        return true;
    }

    // ── tick() — chamado do attackTask() ────────────────────────────────────
    void tick() { /* processamento acontece na _usbTask */ }

    // ── Injeta frame 802.11 raw ──────────────────────────────────────────────
    bool injectFrame(const uint8_t* frame, size_t len) {
        if (devAddr == 0) return false;
        if (len + RTL8188::TX_DESC_SIZE > RTL8188::TX_BUF_SIZE) return false;

        static uint8_t txbuf[RTL8188::TX_BUF_SIZE];
        memset(txbuf, 0, RTL8188::TX_DESC_SIZE);

        // TX Descriptor mínimo para frames MGMT
        txbuf[0] = (uint8_t)(len & 0xFF);
        txbuf[1] = (uint8_t)(len >> 8);
        txbuf[4] = RTL8188::TX_DESC_SIZE;  // offset até o frame
        txbuf[8] = 0x12;  // QSEL = MGMT queue

        memcpy(txbuf + RTL8188::TX_DESC_SIZE, frame, len);
        size_t total = len + RTL8188::TX_DESC_SIZE;

        bool ok = tuh_edpt_xfer(devAddr, RTL8188::EP_BULK_OUT, txbuf, total);
        if (ok) txCount++;
        return ok;
    }

    // ── Envia deauth (broadcast ou unicast) ──────────────────────────────────
    bool sendDeauth(const uint8_t* bssid, const uint8_t* client = nullptr, uint8_t reason = 7) {
        uint8_t f[26] = {};
        f[0] = 0xC0;  // Type: deauth
        f[2] = 0x3A; f[3] = 0x01;  // duration
        if (client) memcpy(f + 4,  client, 6); else memset(f + 4, 0xFF, 6);
        memcpy(f + 10, bssid, 6);
        memcpy(f + 16, bssid, 6);
        f[24] = reason;
        return injectFrame(f, 26);
    }

    // ── Troca de canal ───────────────────────────────────────────────────────
    bool setChannel(uint8_t ch) {
        if (ch < 1 || ch > 14) return false;
        channel = ch;
        return _writeReg(RTL8188::REG_CHANNEL, ch, 1);
    }

    // ── Status JSON ──────────────────────────────────────────────────────────
    String statusJson() const {
        const char* stateStr = "unknown";
        switch (state) {
            case State::DISCONNECTED: stateStr = "disconnected"; break;
            case State::ENUMERATING:  stateStr = "enumerating";  break;
            case State::INIT:         stateStr = "init";         break;
            case State::MONITOR:      stateStr = "monitor";      break;
            case State::ERROR:        stateStr = "error";        break;
        }
        return String("{\"driver\":\"rtl8188\",\"state\":\"") + stateStr +
               "\",\"channel\":" + channel +
               ",\"rx\":" + rxCount +
               ",\"tx\":" + txCount + "}";
    }

    // ── Inicializa chip após USB connect ─────────────────────────────────────
    void initChip() {
        state = State::INIT;
        _log("Inicializando RTL8188...");

        _writeReg(RTL8188::REG_SYS_FUNC, 0xFD, 2);  delay(20);
        _writeReg(RTL8188::REG_CR,       0xFF, 1);   delay(10);
        _writeReg(RTL8188::REG_RCR, RTL8188::RCR_MONITOR, 4);
        setChannel(6);

        state = State::MONITOR;
        running = true;
        _log("Modo monitor ativo, canal 6");

        // Submete primeiro RX
        static uint8_t rxbuf[RTL8188::RX_BUF_SIZE];
        tuh_edpt_xfer(devAddr, RTL8188::EP_BULK_IN, rxbuf, sizeof(rxbuf));
    }

    // ── Lê registrador ───────────────────────────────────────────────────────
    bool _readReg(uint16_t addr, uint8_t* out, size_t len) {
        tusb_control_request_t req = {};
        req.bmRequestType_bit.recipient = TUSB_REQ_RCPT_DEVICE;
        req.bmRequestType_bit.type      = TUSB_REQ_TYPE_VENDOR;
        req.bmRequestType_bit.direction = TUSB_DIR_IN;
        req.bRequest = RTL8188::REQ_READ_REG;
        req.wValue   = tu_htole16(addr);
        req.wLength  = tu_htole16((uint16_t)len);
        return tuh_control_xfer(devAddr, &req, out, nullptr);
    }

    bool _writeReg(uint16_t addr, uint32_t val, size_t len) {
        uint8_t buf[4]; memcpy(buf, &val, len);
        tusb_control_request_t req = {};
        req.bmRequestType_bit.recipient = TUSB_REQ_RCPT_DEVICE;
        req.bmRequestType_bit.type      = TUSB_REQ_TYPE_VENDOR;
        req.bmRequestType_bit.direction = TUSB_DIR_OUT;
        req.bRequest = RTL8188::REQ_WRITE_REG;
        req.wValue   = tu_htole16(addr);
        req.wLength  = tu_htole16((uint16_t)len);
        return tuh_control_xfer(devAddr, &req, buf, nullptr);
    }

private:
    bool _initialized = false;

    static void _usbTask(void* arg) {
        for (;;) {
            tuh_task();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    static void _log(const char* msg) {
        Serial.print("[RTL8188] "); Serial.println(msg);
    }
};

// ── Callbacks TinyUSB Host (linkage C, globais) ──────────────────────────────
extern "C" {

void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    Serial.printf("[RTL8188] USB addr=%u VID=0x%04X PID=0x%04X\n", dev_addr, vid, pid);

    bool isRTL = (vid == RTL8188::VID) &&
                 (pid == RTL8188::PID_RTL8188EU  ||
                  pid == RTL8188::PID_RTL8188CUS ||
                  pid == RTL8188::PID_RTL8188ETV);
    if (!isRTL) {
        Serial.println("[RTL8188] Dispositivo ignorado (não é RTL8188)");
        return;
    }

    USBRTL8188& d = USBRTL8188::get();
    d.devAddr = dev_addr;
    d.state   = USBRTL8188::State::ENUMERATING;
    d.initChip();
}

void tuh_umount_cb(uint8_t dev_addr) {
    USBRTL8188& d = USBRTL8188::get();
    if (d.devAddr == dev_addr) {
        d.devAddr = 0;
        d.running = false;
        d.state   = USBRTL8188::State::DISCONNECTED;
        d.rxCount = 0;
        d.txCount = 0;
        Serial.println("[RTL8188] Adaptador desconectado");
    }
}

void tuh_xfer_cb(tuh_xfer_t* xfer) {
    if (!xfer || xfer->result != XFER_RESULT_SUCCESS) return;
    USBRTL8188& d = USBRTL8188::get();
    if (xfer->dev_addr != d.devAddr) return;

    if (xfer->ep_addr == RTL8188::EP_BULK_IN && d.onFrame) {
        const uint8_t* buf = xfer->buffer;
        size_t total = xfer->actual_len;
        size_t pos = 0;

        while (pos + RTL8188::RX_DESC_SIZE <= total) {
            uint16_t pktlen = (uint16_t)(buf[pos] | (buf[pos+1] << 8)) & 0x3FFF;
            if (pktlen < RTL8188::RX_DESC_SIZE || pos + pktlen > total) break;

            const uint8_t* frame = buf + pos + RTL8188::RX_DESC_SIZE;
            size_t flen = pktlen - RTL8188::RX_DESC_SIZE;
            int8_t rssi = (int8_t)buf[pos + 12]; // RSSI no desc

            if (flen > 0) {
                d.onFrame(frame, flen, rssi, d.channel);
                d.rxCount++;
            }
            pos += ((pktlen + 127) & ~127u);
        }

        // Re-enfileira RX
        static uint8_t rxbuf[RTL8188::RX_BUF_SIZE];
        tuh_edpt_xfer(d.devAddr, RTL8188::EP_BULK_IN, rxbuf, sizeof(rxbuf));
    }
}

} // extern "C"

#endif // USB_RTL8188
