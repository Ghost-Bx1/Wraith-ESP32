#pragma once
/*
 * OTA (Over-The-Air) Firmware Update — Melhoria #36
 * Permite atualizar o firmware via painel web sem cabo USB.
 * Endpoint: POST /api/ota/update  (multipart/form-data, campo "firmware")
 *
 * Uso no painel:
 *   1. Compile: pio run -e esp32-s3  → gera .pio/build/esp32-s3/firmware.bin
 *   2. Painel → OTA → selecione firmware.bin → Upload
 *   3. ESP32 reinicia com novo firmware automaticamente
 */
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "../utils/logger.h"

class OTAManager {
public:
    static OTAManager& get() { static OTAManager inst; return inst; }

    bool updating = false;
    int  progress = 0;    // 0-100%
    bool success  = false;

    // Registra os endpoints no servidor
    void registerRoutes(AsyncWebServer& server) {
        // GET /api/ota/status
        server.on("/api/ota/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
            String json = "{\"updating\":" + String(updating ? "true" : "false") +
                          ",\"progress\":" + String(progress) +
                          ",\"success\":"  + String(success  ? "true" : "false") + "}";
            req->send(200, "application/json", json);
        });

        // POST /api/ota/update  (arquivo firmware.bin via multipart)
        server.on("/api/ota/update", HTTP_POST,
            // onRequest (após upload completo)
            [this](AsyncWebServerRequest* req) {
                bool ok = !Update.hasError();
                success = ok;
                updating = false;
                progress = ok ? 100 : -1;
                AsyncWebServerResponse* resp = req->beginResponse(
                    200, "application/json",
                    ok ? "{\"ok\":true,\"msg\":\"Reboot em 2s...\"}"
                       : "{\"ok\":false,\"msg\":\"Erro no upload\"}");
                resp->addHeader("Connection", "close");
                req->send(resp);
                if (ok) {
                    delay(2000);
                    ESP.restart();
                }
            },
            // onUpload (chunks do arquivo)
            [this](AsyncWebServerRequest* req, const String& filename,
                   size_t index, uint8_t* data, size_t len, bool final) {
                if (!index) {
                    updating = true;
                    progress = 0;
                    success  = false;
                    LOG.warn("[OTA] Iniciando update: " + filename);
                    size_t freeSpace = UPDATE_SIZE_UNKNOWN;
                    if (!Update.begin(freeSpace)) {
                        LOG.error("[OTA] Sem espaco: " + String(Update.errorString()));
                        return;
                    }
                }
                if (Update.write(data, len) != len) {
                    LOG.error("[OTA] Erro de escrita");
                    return;
                }
                // Atualiza progresso aproximado pelo tamanho do buffer recebido
                progress = min(99, (int)(index * 100 / (index + len + 1)));

                if (final) {
                    if (Update.end(true)) {
                        LOG.success("[OTA] Update OK! " + String(index + len) + " bytes");
                    } else {
                        LOG.error("[OTA] Falha ao finalizar: " + String(Update.errorString()));
                    }
                }
            }
        );
    }
};
