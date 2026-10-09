#pragma once
/*
 * Session Token — Melhoria #46
 * Token de sessão gerado aleatoriamente no boot (32 hex chars).
 * Autenticação: POST /api/auth { "pass": "predator" } → retorna token.
 * Todas as outras rotas exigem header  X-Token: <token>  ou param ?token=<token>
 * Expiração: SESSION_TTL_MS (padrão 24h). Re-login necessário após expirar.
 */
#include <Arduino.h>
#include "../config.h"

class SessionMgr {
public:
    static SessionMgr& get() { static SessionMgr inst; return inst; }

    String token;
    unsigned long expiresAt = 0;

    void begin() {
        _regenerate();
    }

    // Valida um token recebido
    bool validate(const String& t) const {
        if (t.isEmpty()) return false;
        if (millis() > expiresAt) return false; // expirado
        return t == token;
    }

    // Valida password e retorna novo token
    String authenticate(const String& pass) {
        if (pass != WEB_PASS) return "";
        _regenerate();
        return token;
    }

    // Extrai token de request (header X-Token ou param ?token=)
    String extractToken(AsyncWebServerRequest* req) const {
        if (req->hasHeader("X-Token")) return req->getHeader("X-Token")->value();
        if (req->hasParam("token"))    return req->getParam("token")->value();
        return "";
    }

    bool isAuthenticated(AsyncWebServerRequest* req) const {
        return validate(extractToken(req));
    }

    // Resposta de negação padronizada
    void sendUnauthorized(AsyncWebServerRequest* req) const {
        req->send(401, "application/json", "{\"ok\":false,\"msg\":\"Unauthorized\"}");
    }

private:
    void _regenerate() {
        token = "";
        for (int i = 0; i < 32; i++) {
            token += String(esp_random() % 16, HEX);
        }
        expiresAt = millis() + SESSION_TTL_MS;
    }
};
