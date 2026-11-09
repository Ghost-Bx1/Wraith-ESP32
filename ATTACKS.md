# Wraith - Guia Completo de Ataques

> **AVISO LEGAL**: Use exclusivamente em redes e dispositivos que voce possui ou tem autorizacao explicita e documentada para testar. Uso nao autorizado e crime (Lei 12.737/2012 no Brasil). Os autores nao se responsabilizam por uso indevido.

---

## Hardware - ESP32-S3-WROOM-1-N16R8

### Especificacoes da Placa

| Componente | Especificacao |
|-----------|---------------|
| **Modulo** | ESP32-S3-WROOM-1-N16R8 |
| **CPU** | Xtensa LX7 dual-core @ 240 MHz |
| **Flash** | 8 MB (QIO) |
| **PSRAM** | 8 MB OPI (integrada ao modulo) |
| **WiFi** | 802.11 b/g/n 2.4 GHz com injecao de frames |
| **Bluetooth** | BLE 5.0 + BT Classic |
| **USB** | 2x USB-C (esquerdo = OTG Host, direito = CH343P serial) |
| **Dimensoes** | 57.15 x 27.94 mm |

### Componentes Onboard Utilizados

| Componente | GPIO | Funcao no Wraith |
|-----------|------|--------------------------|
| **WS2812 RGB LED** | GPIO 48 (tente 38 se nao acender) | Indicador visual de estado do sistema |
| **Botao BOOT** | GPIO 0 | Pausar/retomar ataques; 2x cliques = scan rapido |
| **Botao RST** | RST | Reiniciar o dispositivo |
| **USB OTG** | GPIO19/20 | Conectar segundo radio via USB (modulo externo) |
| **PWR LED** | - | Indica alimentacao ativa (sempre aceso) |
| **TX/RX LEDs** | - | Atividade serial (piscam durante flash/debug) |

### LED RGB - Significado das Cores

O LED WS2812 no GPIO 48 indica o estado atual do sistema em tempo real:

| Cor | Padrao | Estado |
|-----|--------|--------|
| **Branco** | Fade in (boot) | Inicializando o sistema |
| **Azul** | Pulsando lento | Ocioso - aguardando comando |
| **Ciano** | Piscando alternado | Sniffer ativo (capturando pacotes) |
| **Vermelho** | Piscando rapido | Ataque em andamento (Deauth/Flood/Karma/PMKID/BLE Spam) |
| **Roxo** | Pulsando | AP Clone ou Evil Twin ativos |
| **Verde** | Flash rapido (4x) | Captura concluida (handshake, credencial, PMKID) |
| **Laranja** | Flash lento (3x) | Erro ou ataque pausado manualmente |

> O LED atualiza automaticamente - nenhuma configuracao necessaria.

### Botao BOOT como Controle de Ataque

- **1 clique**: Pausa todos os ataques ativos / retoma se ja pausado
- **2 cliques rapidos**: Inicia scan rapido de redes
- **Segurar durante power-on**: Entra em modo bootloader (para novo firmware)

### Acesso ao Painel Web

| Item | Valor |
|------|-------|
| **Rede WiFi** | Wraith |
| **Senha WiFi** | predator123 |
| **IP do painel** | http://192.168.4.1 |
| **URL alternativa** | http://wraith.local |
| **Senha do painel** | predator |

> Primeiro acesso: conecte ao WiFi "Wraith" com senha **predator123**, depois abra http://192.168.4.1 no navegador.

---

### Porta USB OTG - Segundo Radio e Adaptadores USB

O conector USB-C **esquerdo** e a porta USB OTG do ESP32-S3. Suporta dois modos:

#### Modo 1 - Adaptador RTL8188 (modo monitor + injecao)

Conecte um adaptador WiFi RTL8188EU ou RTL8188CUS para ter um segundo radio independente capaz de modo monitor real e injecao de frames simultaneamente ao AP principal.

```
[ESP32-S3 N16R8]
  USB OTG (esq) ──USB-C──► Adaptador RTL8188EU/CUS
                              │ Modo monitor ativo
                              └─► Captura + injeta frames 802.11
```

**Adaptadores compativeis:**

| Chip | Exemplos de adaptadores | PID USB |
|------|------------------------|---------|
| **RTL8188EU** | TL-WN722N v1, Alfa AWUS036NHR | 0x8179 |
| **RTL8188CUS** | Adaptadores USB nano baratos | 0x8176 |

> **ATENCAO**: TL-WN722N **v2 e v3** usam chip AR9271 (Atheros) - **incompativeis**. So a v1 usa RTL8188EU. Verifique a versao na embalagem.

**Como ativar:**

1. Conecte o adaptador RTL8188 na porta USB-C esquerda (OTG)
2. Em `src/config.h`, descomente:
   ```cpp
   #define USB_RTL8188 1
   ```
3. Regrave o firmware: `pio run -e esp32-s3 -t upload --upload-port COM5`
4. O driver detecta e inicializa automaticamente ao conectar o adaptador

**Recursos do driver RTL8188:**
- Modo monitor automatico (captura todos os frames 802.11 do ar)
- Injecao de frames raw (deauth, beacon, probe) sem afetar o AP principal
- Troca de canal via registrador (1-14)
- Frames capturados sao processados pelo Sniffer interno automaticamente

#### Modo 2 - Segundo ESP32 como radio escravo

```
[ESP32-S3 N16R8]          [Segundo ESP32 - radio externo]
  USB OTG (esq) ──USB-C──► USB-UART (CH340/CP2102)
                             │
                           GPIO → antena WiFi externa
```

1. Flash o segundo modulo com o firmware `ghost-radio-slave` (em `/tools/slave_firmware/`)
2. Conecte via cabo USB-C ao conector OTG (esquerdo)
3. Ative em `config.h`:
   ```cpp
   #define USB_RADIO_SERIAL  Serial1
   #define USB_RADIO_BAUD    921600
   #define USB_RADIO_TX      17
   #define USB_RADIO_RX      18
   ```
4. O segundo radio aparecera no painel como "USB Radio"

**Protocolo de comunicacao USB Radio:**

| Comando | Codigo | Payload | Funcao |
|---------|--------|---------|--------|
| PING | 0x01 | - | Verifica presenca |
| DEAUTH | 0x10 | BSSID[6]+CLI[6]+CH[1] | Deauth via segundo radio |
| BEACON | 0x11 | SSID[32]+CH[1] | Beacon flood remoto |
| SCAN | 0x20 | - | Scan de redes |
| INJECT | 0x30 | LEN[2]+frame[] | Frame 802.11 bruto |
| CHANNEL | 0x40 | CH[1] | Troca de canal |
| STATUS | 0xF0 | - | JSON de status |

---

## Sumario

- [**Hardware - ESP32-S3-WROOM-1-N16R8**](#hardware--esp32-s3-wroom-1-n16r8) ← LED RGB, BOOT button, USB OTG
1. [WiFi Scanner](#1-wifi-scanner)
2. [Packet Sniffer](#2-packet-sniffer)
3. [Deauth Attack](#3-deauth-attack-desautenticacao-80211)
4. [Evil Twin + Captive Portal](#4-evil-twin-ap-clone--captive-portal)
5. [**AP Clone** *(NOVO)*](#5-ap-clone-clonagem-fiel-de-redes)
6. [Beacon Flood](#6-beacon-flood-inundacao-de-redes-falsas)
7. [Karma Attack](#7-karma-attack-armadilha-de-reconexao-automatica)
8. [Reconnect Blocker](#8-reconnect-blocker-bloqueador-de-reconexao)
9. [PMKID Fisher](#9-pmkid-fisher-captura-sem-cliente)
10. [WPS Scanner](#10-wps-scanner)
11. [Auto-Attack / Wifite Mode](#11-auto-attack--wifite-mode)
12. [Nuclear Auto-Attack](#12-nuclear-auto-attack)
13. [Target Intel](#13-target-intel-fingerprinting-de-dispositivos)
14. [**Hidden SSID Scanner** *(NOVO)*](#14-hidden-ssid-scanner-revelador-de-redes-ocultas)
15. [LED Panel Attack](#15-led-panel-attack)
16. [BLE Scanner e Spam](#16-ble-scanner-e-spam)
17. [Fluxos de Ataque Completos](#17-fluxos-de-ataque-completos)

---

## 1. WiFi Scanner

### O que e?

O ponto de partida de qualquer auditoria. O scanner coloca o ESP32 em modo ativo e varre todos os canais WiFi (1 a 13) listando todas as redes e dispositivos proximos.

### O que voce ve:

| Campo | O que significa |
|-------|----------------|
| **SSID** | Nome da rede WiFi |
| **BSSID** | MAC address do roteador (identificador unico fisico) |
| **Canal** | Canal de operacao (1-13, ou 36-165 para 5GHz) |
| **RSSI** | Forca do sinal em dBm (quanto mais perto de 0, mais forte: -40 = otimo, -80 = fraco) |
| **Auth** | Tipo de seguranca (Open, WPA, WPA2, WPA3, WPS) |
| **Clientes** | Dispositivos conectados a aquela rede |

### Dica pratica:

Preste atencao no **RSSI**. Redes com sinal abaixo de -80 dBm vao ser muito dificeis de atacar - voce esta longe demais. Foque em redes com RSSI acima de -70 dBm para melhores resultados.

### API:

```
GET  /api/wifi/scan          â†’ inicia scan e retorna lista de redes
GET  /api/wifi/clients       â†’ lista clientes detectados por rede
GET  /api/wifi/scan/status   â†’ status do scan em andamento
```

**Resposta de exemplo:**
```json
{
  "networks": [
    {
      "ssid": "MinhaRede",
      "bssid": "AA:BB:CC:DD:EE:FF",
      "channel": 6,
      "rssi": -55,
      "auth": "WPA2",
      "clients": ["11:22:33:44:55:66"]
    }
  ]
}
```

---

## 2. Packet Sniffer

### O que e?

O Sniffer coloca o ESP32 em **modo promiscuo**: ao inves de ignorar pacotes que nao sao para ele, ele captura e analisa TUDO que passa pelo ar no canal monitorado.

### Por que funciona?

O WiFi e como um radio FM - qualquer um com o receptor certo no mesmo canal consegue ouvir. A maioria dos dispositivos simplesmente ignora o que nao e deles. O modo promiscuo muda isso: o ESP32 para e analisa cada pacote.

### O que ele captura:

| Tipo de pacote | O que e | Para que serve |
|----------------|----------|----------------|
| **Probe Requests** | Celular perguntando "MinhaRede esta ai?" | Mapear quais redes um dispositivo ja conheceu; base para Karma Attack |
| **EAPOL M1/M2/M3/M4** | Os 4 pacotes do handshake WPA2 durante a conexao | Com os 4 pacotes capturados, a senha pode ser quebrada offline |
| **PMKID** | Hash presente no primeiro pacote do handshake | Cracking offline sem precisar dos 4 pacotes completos |
| **Beacons** | Anuncio de redes WiFi | Mapear todas as redes do ambiente |
| **Data frames** | Trafego de dados (criptografado) | Analise de padroes de uso |

### Channel Hopping (pular de canal):

O sniffer pode ficar fixo em um canal ou fazer **channel hopping**: pular entre os canais 1 a 13 automaticamente a cada 200ms. Use hopping para monitorar todo o ambiente; use canal fixo quando voce ja sabe o canal do alvo e nao quer perder pacotes.

### API:

```
POST /api/sniffer/start               â†’ inicia sniffer (channel hop ativo por padrao)
POST /api/sniffer/stop                â†’ para sniffer
POST /api/sniffer/channel             â†’ fixa em um canal especifico
  { "channel": 6 }
POST /api/sniffer/hop                 â†’ volta ao channel hopping
GET  /api/sniffer/handshakes          â†’ lista handshakes EAPOL capturados
GET  /api/sniffer/pmkids              â†’ lista PMKIDs capturados
GET  /api/sniffer/probes              â†’ lista probe requests capturados
```

### Como usar o handshake capturado (cracking offline):

Quando o painel mostrar que capturou um handshake ou PMKID, exporte e use o hashcat:

```bash
# PMKID (modo 22000 - mais rapido, nao precisa de cliente)
hashcat -m 22000 captura.hc22000 wordlist.txt

# Handshake WPA2 completo (modo 22000 tambem)
hashcat -m 22000 captura.hc22000 wordlist.txt

# Com regras para aumentar cobertura de senhas comuns
hashcat -m 22000 captura.hc22000 wordlist.txt -r best64.rule

# Wordlist recomendada para redes brasileiras
hashcat -m 22000 captura.hc22000 rockyou.txt
```

> **Dica**: O PMKID e melhor que o handshake tradicional porque nao precisa de nenhum cliente conectado - voce captura apenas tentando se associar ao AP.

---

## 3. Deauth Attack (Desautenticacao 802.11)

### O que e?

Desconecta forcadamente qualquer dispositivo (celular, notebook, smart TV, camera IP) de uma rede WiFi. A desconexao ocorre instantaneamente e o dispositivo tenta se reconectar automaticamente logo em seguida.

### Por que funciona?

O protocolo WiFi (IEEE 802.11) foi criado nos anos 90 sem autenticacao nos pacotes de **gerenciamento** (frames de controle como deauth e disassoc). Isso significa que qualquer pessoa pode fabricar e enviar um pacote de "desconexao" se passando pelo roteador, e o dispositivo-alvo obedece sem verificar se realmente veio do roteador.

> **Analogia**: Como alguem que fica na frente da sua casa gritando com a voz do seu pai "sai de casa agora!" - e voce sai sem questionar porque parece real.

### Como funciona na pratica:

```
1. O Wraith monitora o canal do AP-alvo
2. Voce seleciona o AP e o cliente (ou broadcast para todos)
3. O ESP32 fabrica pacotes de deautenticacao:
   - Remetente falsificado: MAC do roteador
   - Destinatario: MAC do celular (ou FF:FF:FF:FF:FF:FF para todos)
   - Motivo: qualquer um dos 16 reason codes do protocolo
4. Os pacotes sao injetados direto no ar via esp_wifi_80211_tx()
5. O celular recebe, acha que e do roteador de verdade, e desconecta
6. O celular tenta se reconectar automaticamente
7. Durante a reconexao, o handshake WPA2 e capturado pelo sniffer
```

### Tecnicas implementadas:

| Tecnica | Como funciona | Quando usar |
|---------|--------------|-------------|
| **Deauth APâ†’Cliente** | Finge ser o roteador expulsando o cliente | Caso mais comum, funciona na maioria das redes |
| **Deauth Clienteâ†’AP** | Finge ser o cliente pedindo desconexao ao roteador | Complementar ao anterior, em redes com filtragem parcial |
| **Disassociation** | Variante mais agressiva (nivel de protocolo diferente, subtipo 0x0A) | Quando deauth normal nao surte efeito |
| **16 Reason Codes** | Rotaciona entre todos os 16 motivos oficiais do protocolo IEEE | Dificultar deteccao por IDS/WIDS |
| **Null Data Frame (PM=1)** | Finge que o cliente entrou em modo de economia de energia, fazendo o AP guardar pacotes e parar de entrega-los | Ataque silencioso, menos detectavel |
| **SA Query Flood** | Inunda o AP com requisicoes SA Query para tentar bypassar PMF | Quando a rede tem PMF ativado |
| **Auth Flood** | Enche a tabela de conexoes do AP com centenas de MACs falsos | Pode causar lentidao ou travamento no roteador |

### Modos de intensidade:

| Modo | Pacotes por ciclo | Intervalo | Quando usar |
|------|-------------------|-----------|-------------|
| **Suave** | 64 | Maior | Teste inicial; menor impacto no ambiente |
| **Normal** | 256 | Medio | Uso geral na maioria dos cenarios |
| **Agressivo** | 512 | Menor | Maximo impacto; tentar bypassar PMF basico |

### O que e PMF e como bypassar:

**PMF (Protected Management Frames, 802.11w)** e uma protecao que assina criptograficamente os pacotes de gerenciamento. Roteadores modernos com PMF obrigatorio sao imunes a deauth simples.

O Wraith tenta bypassar com **SA Query Flood**: envia tantas requisicoes de consulta de seguranca que alguns firmwares ficam sobrecarregados e nao verificam a protecao em todos os pacotes.

### API:

```json
POST /api/deauth/start
{
  "bssid": "AA:BB:CC:DD:EE:FF",
  "client": "FF:FF:FF:FF:FF:FF",
  "channel": 6,
  "mode": "normal",
  "autoCapture": true,
  "reconnectBlock": false,
  "targetSSID": ""
}

POST /api/deauth/stop

GET  /api/deauth/status
â†’ { "running": true, "sent": 1247, "fps": 89, "elapsed": 14 }
```

**Parametros:**

| Campo | Valores | Descricao |
|-------|---------|-----------|
| `bssid` | MAC do AP | Roteador-alvo (do scan) |
| `client` | MAC ou `FF:FF:FF:FF:FF:FF` | Cliente especifico ou broadcast (todos) |
| `channel` | 1-13 | Canal do AP (do scan) |
| `mode` | `soft` / `normal` / `hard` | Intensidade |
| `autoCapture` | `true` / `false` | Liga sniffer no canal automaticamente |
| `reconnectBlock` | `true` / `false` | Ativa Reconnect Blocker junto |
| `targetSSID` | string | SSID do AP (necessario para Reconnect Blocker) |

---

## 4. Evil Twin (AP Clone + Captive Portal)

### O que e?

Cria uma copia identica de uma rede WiFi real (mesmo nome, mesma aparencia) mas sem senha e controlada pelo Wraith. Quem se conectar ve uma pagina falsa pedindo a senha do WiFi.

> **Analogia**: Como clonar uma agencia bancaria do lado da original, fechar a original temporariamente e quando os clientes entrarem na falsa, pedir para "confirmar a senha para acessar a conta".

### Por que as pessoas se conectam?

1. O Wraith usa Deauth para derrubar o sinal da rede real
2. O AP falso emite com sinal forte (esta perto)
3. O dispositivo ve o mesmo nome de rede disponivel e conecta automaticamente
4. Como e uma rede "aberta" (sem senha), a maioria dos dispositivos conecta sem perguntar

### Fluxo completo do ataque:

```
â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•-
â•‘  ETAPA 1 - RECONHECIMENTO                                â•‘
â•‘  Scanner identifica: SSID, BSSID, canal, clientes        â•‘
â• â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•£
â•‘  ETAPA 2 - DEAUTH (derruba a rede real)                  â•‘
â•‘  Wraith â†’ [pacotes deauth] â†’ Clientes            â•‘
â•‘  Resultado: todos os dispositivos sao desconectados      â•‘
â• â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•£
â•‘  ETAPA 3 - CLONE AP (cria a rede falsa)                  â•‘
â•‘  Wraith emite beacons com o mesmo SSID           â•‘
â•‘  sem senha, sinal forte                                  â•‘
â•‘  Resultado: dispositivos veem a "mesma" rede             â•‘
â• â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•£
â•‘  ETAPA 4 - CONEXAO DO ALVO                               â•‘
â•‘  Dispositivo conecta automaticamente ao AP falso         â•‘
â•‘  Recebe IP do DHCP do Wraith                     â•‘
â•‘  DNS do dispositivo aponta para o Wraith         â•‘
â• â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•£
â•‘  ETAPA 5 - CAPTIVE PORTAL (pagina falsa)                 â•‘
â•‘  Qualquer site HTTP â†’ redirecionado para portal falso    â•‘
â•‘  Pagina pede a senha do WiFi para "reconectar"           â•‘
â•‘  Senha digitada â†’ aparece no painel em tempo real        â•‘
â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
```

### Por que o redirecionamento funciona:

- **DNS falso**: o Wraith responde QUALQUER consulta DNS com seu proprio IP (192.168.4.1). Quando o dispositivo pergunta "qual o IP do google.com?", ele responde "192.168.4.1"
- **DHCP**: distribui o proprio IP como gateway e servidor DNS para todos os clientes
- **HTTP**: todo acesso HTTP e interceptado e redirecionado para o portal
- **HTTPS**: nao pode ser redirecionado (o navegador bloqueia sem certificado valido). Porem o portal captive em si usa HTTP e funciona na maioria dos dispositivos

### Templates de portal disponiveis:

| Template | Aparencia | Mensagem | Melhor para |
|----------|-----------|----------|-------------|
| **WiFi Reconnect** | Pagina generica | "Sessao expirada, reinsira a senha" | Qualquer rede residencial |
| **ISP/Provedor** | Imita pagina de provedor | "Atualizacao de seguranca necessaria" | Redes de provedores de internet |
| **Router Admin** | Interface de admin do roteador | "Acesso administrativo - autentique-se" | Usuarios tecnicos |

### API:

```json
POST /api/eviltwin/start
{
  "ssid": "MinhaRede",
  "channel": 6,
  "deauth": true,
  "template": "reconnect"
}

POST /api/eviltwin/stop

GET  /api/eviltwin/creds
â†’ [{ "password": "senha123", "timestamp": "14:32:11", "client": "AA:BB:CC:DD:EE:FF" }]
```

**Dicas:**
- Use `"deauth": true` para derrubar o AP real simultaneamente
- As credenciais ficam salvas mesmo apos parar o ataque
- Cada tentativa e registrada, mesmo que o usuario erre a senha

---

## 5. AP Clone (Clonagem Fiel de Redes)

### O que e?

O AP Clone vai muito alem do Evil Twin tradicional. Em vez de criar um AP parecido, ele **copia com fidelidade absoluta** o AP alvo: mesmo BSSID (endereco MAC), mesmos parametros de seguranca (WPA2/WPA3), mesmos indicadores de velocidade (N/AC), mesmos dados de fabricante. O dispositivo deixa de ser reconhecivel como diferente do AP original.

### A diferenca do Evil Twin comum:

| Caracteristica | Evil Twin Comum | AP Clone (novo) |
|---|---|---|
| SSID | Igual | Igual |
| BSSID (MAC) | Diferente â€” detectavel! | **Identico ao alvo** |
| Seguranca WPA2 | Generica | **IEs exatos copiados** |
| Beacon interval | 100ms padrao | **Igual ao AP real** |
| Capacidades HT/VHT | Nao incluidas | **Copiadas do alvo** |
| Detectavel por apps | Sim | Muito mais dificil |

### Como funciona passo a passo:

```
1. Sniffer captura beacons do AR real (automatico, em background)
   - Extrai: SSID, BSSID, canal, capabilities, todos os IEs
   - Identifica: WPA2/WPA3, HT (N), VHT (AC), PMF, vendor IEs

2. AP Clone: BSSID Spoof
   - esp_wifi_set_mac(WIFI_IF_AP, bssid_alvo)
   - O ESP32 passa a ter o MAC EXATO do roteador alvo
   - Clientes associam ao nosso AP sem perceber diferenca

3. Beacon Replay
   - Injeta beacons continuos com os IEs EXATOS do alvo
   - Mesmo timestamp progressivo, mesmo beacon interval
   - Clientes veem nosso AP como identico ao real

4. CSA (Channel Switch Announcement)
   - Opcional: envia frames ordenando clientes do AP real
     a migrarem para o nosso canal
   - Combina com Deauth para migrar todos os clientes

5. Captive Portal
   - Clientes conectados sao redirecionados para portal
   - Captura credenciais WiFi
```

### Deteccao avancada no beacon capturado:

O sniffer analisa cada IE do beacon para identificar com precisao as capacidades do AP:

| IE | Tag | O que detecta |
|----|-----|--------------|
| RSN (WPA2/WPA3) | 48 | Cifra, AKM (PSK/SAE), PMF obrigatorio |
| HT Capabilities | 45 | 802.11n (WiFi 4) |
| VHT Capabilities | 191 | 802.11ac (WiFi 5) |
| Vendor Specific | 221 | WPA (TKIP legado), OUI 00:50:F2 |
| DS Parameter Set | 3 | Canal real (patchado no CSA) |

**PMF (Protected Management Frames / 802.11w)**: detectado pelo bit 7 do RSN Capabilities byte. APs com PMF obrigatorio sao mais resistentes a deauth/disassoc - o AP Clone ainda funciona mas o deauth pode nao desconectar clientes PMF.

**WPA3**: detectado pelo AKM SAE (00:0F:AC:08) no RSN IE. O clone replica o RSN IE exato, incluindo SAE, para enganar dispositivos WPA3.

### CSA Injection - Migracao Forcada de Clientes:

O CSA (Channel Switch Announcement) forca todos os clientes de um AP a migrarem para outro canal, sem precisar que eles se desconectem primeiro:

```
sendCSA(bssid_real, novo_canal, count=3)
  → 5x Action frames Spectrum Management (categoria 0, acao 4)
  → 5x Beacons com CSA IE incluido
  → Clientes migram para o canal do nosso clone automaticamente
```

O CSA usa dois mecanismos em paralelo (Action frame + Beacon) para cobertura maxima - clientes que nao aceitam Action frames ainda obedecem ao Beacon.

### Novos modos do Deauth (v5):

| Modo | Flag | O que faz |
|------|------|-----------|
| **CSA Attack** | `csaMode` | Channel Switch Announcement - move clientes para nosso canal |
| **EAP Flood** | `eapFlood` | Manda EAP Identity Requests falsos - confunde autenticadores |
| **TKIP MIC** | `tkipInjection` | Reporta falha MIC falsa - aciona blackout de 60s em WPA-TKIP |

### Fingerprinting de dispositivos (novo no Sniffer):

O sniffer agora analisa os IEs dentro de cada Probe Request para identificar o tipo de dispositivo:

- **Apple**: presenca de Vendor IE com OUI `00:17:F2`
- **Windows**: Vendor IE com OUI `00:50:F2` tipo 4 (WCN)
- **Wi-Fi 6**: HE Capabilities (IEEE 802.11ax) presente
- **MAC aleatorio**: bit U/L = 1 no primeiro octeto (privacidade do iOS/Android)

### API:

```
GET  /api/clone/list            â†’ lista beacons capturados pelo sniffer
POST /api/clone/start           â†’ inicia clone de AP (idx = indice da lista)
POST /api/clone/stop            â†’ para o clone e restaura AP gerencial
POST /api/clone/csa             â†’ envia CSA forcando migracao de clientes
GET  /api/clone/status          â†’ status atual do clone
```

---

## 6. Beacon Flood (Inundacao de Redes Falsas)

### O que e?

Cria centenas ou milhares de redes WiFi falsas ao mesmo tempo, enchendo a lista de WiFi de qualquer dispositivo proximo com redes que nao existem de verdade.

### Como funciona:

Toda rede WiFi anuncia sua presenca continuamente enviando pacotes chamados **beacons** a cada 100ms - e como um outdoor que pisca dizendo "estou aqui, me chamo X". O Wraith injeta beacons fabricados com SSIDs da sua lista em alta velocidade, sem precisar de roteador real.

> **Analogia**: Como espalhar centenas de placas de "Loja Aberta" falsas em uma rua - as lojas nao existem, mas as placas confundem quem esta procurando.

### Opcoes disponiveis:

| Opcao | O que faz | Efeito pratico |
|-------|-----------|---------------|
| **Channel Hop** | Muda de canal automaticamente (1-13) apos cada ciclo completo de SSIDs | Redes falsas aparecem em todos os canais |
| **Hidden Mode** | Emite beacons com SSID vazio (rede sem nome) | Lista de WiFi mostra redes sem nome misteriosamente |
| **WPA3 Mode** | Inclui campos RSN com AKM SAE no beacon | Enganar scanners que filtram por tipo de seguranca |

### Por que cada SSID tem MAC diferente?

O Wraith usa o algoritmo FNV-1a para gerar um MAC unico e deterministico para cada SSID. Isso faz cada rede falsa parecer vir de um roteador diferente, tornando o flood muito mais convincente e dificil de filtrar automaticamente.

### Usos praticos:

- **Cobertura**: esconder um ataque principal (Karma, Evil Twin) no meio de dezenas de redes falsas
- **Teste de carga**: verificar como ferramentas de monitoramento reagem a muitos APs
- **Demonstracao de seguranca**: mostrar a facilidade de criar redes falsas

### API:

```json
POST /api/beacon/start
{
  "ssids": ["FreeWiFi", "McDonalds_Guest", "Aeroporto_Free", "Hotel_Guest"],
  "channel": 6,
  "hop": true,
  "hidden": false,
  "wpa3": false
}

POST /api/beacon/stop

GET  /api/beacon/status
â†’ { "running": true, "sent": 4821, "ssids": 4, "channel": 9 }
```

---

## 7. Karma Attack (Armadilha de Reconexao Automatica)

### O que e?

Explora o comportamento automatico dos dispositivos de procurar redes conhecidas. Quando um celular pergunta no ar "MinhaRedeEmpresa esta aqui?", o Wraith responde "Sim, sou eu!" - mesmo nao sendo.

### Por que funciona?

Dispositivos WiFi com "Conexao automatica" ativada (que e o padrao em 100% dos sistemas operacionais) ficam constantemente enviando **probe requests** no ar perguntando por redes que ja conheceram no passado. Normalmente so o roteador real responde. O Karma intercepta e responde por qualquer rede perguntada.

> **Analogia**: Como alguem que fica em uma esquina respondendo a qualquer pessoa que chame um nome ("Joao, esta ai?") dizendo "Sim, sou eu aqui!" - a pessoa vai ate ele sem questionar.

### Passo a passo:

```
1. Seu celular (WiFi ligado, tela bloqueada) envia automaticamente:
   â†’ "MinhaRedeEmpresa, voce esta ai?"
   â†’ "WiFi_Casa, voce esta ai?"
   â†’ "AeroportoFree, voce esta ai?"
   (isso acontece em segundo plano, sem voce saber)

2. O Wraith ouve em modo monitor e captura esses probes

3. Para cada probe, responde imediatamente:
   â†’ "Sim! Sou a MinhaRedeEmpresa. Conecte-se!" (com o mesmo SSID pedido)

4. O celular acredita e inicia conexao automatica (sem voce fazer nada)

5. O celular esta conectado ao Wraith
   â†’ Trafego HTTP pode ser capturado
   â†’ Portal captive pode ser exibido para coletar senha
```

### Modo MANA (Karma Avancado):

- **Karma padrao**: responde apenas com o SSID especificamente pedido no probe request
- **MANA**: responde a cada probe com ate 20 SSIDs conhecidos coletados anteriormente, maximizando associacoes

O MANA e muito mais agressivo: mesmo dispositivos que nao estao enviando probes no momento podem ser capturados porque veem o beacon com o nome da rede que conhecem.

> Implementacao de referencia: Dominic White & Ian de Villiers (SensePost, DEF CON 22, 2014).

### Acumulacao de SSIDs Conhecidos:

Todo SSID visto em qualquer probe request e adicionado a uma lista interna (ate 256 entradas, deduplicada). O MANA usa essa lista para responder. Isso significa que quanto mais tempo o Karma fica ativo, mais eficaz o MANA se torna.

### Clone IEs - Respostas Indistinguiveis do AP Real:

O Karma aproveita os beacons capturados pelo modulo APClone. Quando um dispositivo pede um SSID que ja foi escaneado e tem beacon armazenado, a Probe Response usa:

- O **BSSID real** do AP (nao um BSSID sintetico)
- O **beacon interval exato** do AP real
- As **IEs exatas** (RSN, HT Capabilities, Vendor IEs) do beacon capturado
- O **canal real** do AP

Resultado: o dispositivo recebe uma Probe Response identica ao que o AP real enviaria - impossivel distinguir sem analisar o comportamento do handshake.

### Blacklist - Nunca Responder a Redes Sensiveis:

Configuravel em `config.h → KARMA_BLACKLIST`. O Karma nunca responde a SSIDs nessa lista, evitando responder a redes bancarias, corporativas criticas, etc. que possam levantar suspeitas.

```cpp
// config.h
#define KARMA_BLACKLIST "BancoBradesco", "RedeHospitalar", "GovBR_Wifi"
```

### Quem e vulneravel:

**Praticamente qualquer dispositivo com WiFi ligado:**
- Smartphones Android e iPhone (qualquer versao)
- Notebooks Windows, macOS e Linux
- Tablets, smart TVs, cameras IP, impressoras WiFi
- Dispositivos IoT (alexa, google home, etc.)
- Qualquer dispositivo que ja conectou em uma rede antes

### Dicas para maximizar capturas:

1. Use em locais com muito trafego (shopping, aeroporto, evento)
2. Ative MANA para nao depender de probes ativos
3. Combine com Evil Twin: o Karma captura o cliente, o captive portal coleta a senha
4. Observe os SSIDs coletados - redes corporativas sao alvos valiosos

### API:

```json
POST /api/karma/start
{
  "mana": true,
  "captivePortal": true,
  "template": "reconnect"
}

POST /api/karma/stop

GET  /api/karma/responded
â†’ [{ "ssid": "Empresa_WiFi", "client": "AA:BB:CC:DD:EE:FF", "time": "14:55:02" }]

GET  /api/karma/probes
â†’ lista de todos os SSIDs que dispositivos proximos estao buscando
```

---

## 8. Reconnect Blocker (Bloqueador de Reconexao)

### O que e?

Impede que um dispositivo se reconecte a rede real apos ser deautenticado. E um "bloqueio persistente" que mantÃ©m o alvo desconectado mesmo quando ele tenta voltar.

### Como funciona:

Quando um celular e deautenticado, ele envia probe requests perguntando pela rede real. O Reconnect Blocker intercepta essas perguntas e responde com uma **probe response falsa**: usa o SSID correto mas um MAC de roteador diferente (gerado via FNV-1a XOR'd com o contador de sequencia). O celular tenta conectar com esse MAC errado, falha, e fica em loop.

```
Celular â†’ "MinhaRede, voce esta ai?"
Wraith â†’ "Sim! [SSID: MinhaRede | MAC: AA:BB:CC:DD:EE:01]" (MAC errado)
Celular â†’ tenta conectar com MAC AA:BB:CC:DD:EE:01 â†’ falha
Celular â†’ "MinhaRede, voce esta ai?"  (tenta de novo)
Wraith â†’ "Sim! [SSID: MinhaRede | MAC: AA:BB:CC:DD:EE:02]" (outro MAC errado)
... loop infinito ...
```

### Quando usar:

- **Com Evil Twin**: mantem o alvo bloqueado da rede real enquanto o captive portal e exibido
- **Teste de persistencia**: verificar quanto tempo um dispositivo fica bloqueado
- **Auditoria de resiliencia**: testar se equipamentos de rede detectam esse comportamento

### Como ativar:

Ative junto com o Deauth passando os parametros:

```json
POST /api/deauth/start
{
  "bssid": "AA:BB:CC:DD:EE:FF",
  "client": "FF:FF:FF:FF:FF:FF",
  "channel": 6,
  "reconnectBlock": true,
  "targetSSID": "MinhaRede"
}
```

---

## 9. PMKID Fisher (Captura Sem Cliente)

### O que e?

Captura o PMKID de um AP sem precisar que nenhum cliente esteja conectado. O PMKID e um hash matematicamente relacionado com a senha do WiFi e pode ser quebrado offline com hashcat.

### Por que e melhor que o handshake tradicional?

| | PMKID Fisher | Handshake tradicional |
|--|---|---|
| Precisa de cliente conectado? | **Nao** | Sim |
| Precisa de deauth? | **Nao** | Sim (para forcar reconexao) |
| Velocidade | **Segundos** | Depende de ter clientes |
| Funciona 24h por dia? | **Sim** | So quando tem clientes |

### Como funciona:

```
Wraith â†’ [Authentication Request] â†’ AP
AP             â†’ [Authentication Response] â†’ Wraith
Wraith â†’ [Association Request] â†’ AP
AP             â†’ [Association Response + RSNIE contendo PMKID] â†’ Wraith
                                          â†‘ PMKID extraido aqui!
```

O PMKID e derivado assim pela matematica do protocolo:
```
PMK = PBKDF2(HMAC-SHA1, senha, SSID, 4096, 256 bits)
PMKID = HMAC-SHA1-128(PMK, "PMK Name" || BSSID_do_AP || MAC_do_dispositivo)
```

Como o PMKID depende matematicamente da senha, o hashcat consegue testar milhoes de senhas por segundo para encontrar qual gera o mesmo PMKID.

### Como usar no painel:

1. Faca o scan WiFi e selecione o AP alvo
2. Inicie o PMKID Fisher - ele tenta se associar ao AP automaticamente
3. Se o AP responder (maioria responde), o PMKID e capturado em segundos
4. Clique em "Export Hashcat" para baixar o arquivo
5. Execute o hashcat na sua maquina

### Cracking com hashcat:

```bash
# Modo basico com wordlist
hashcat -m 22000 pmkid.hc22000 rockyou.txt

# Com regras (testa variacoes da wordlist: maiusculas, numeros no final, etc.)
hashcat -m 22000 pmkid.hc22000 rockyou.txt -r best64.rule -r toggles1.rule

# Ataque de mascara (para senhas numericas de 8 digitos: muito comum em roteadores)
hashcat -m 22000 pmkid.hc22000 -a 3 ?d?d?d?d?d?d?d?d

# Combinacao de wordlist + mascara (senha + numeros)
hashcat -m 22000 pmkid.hc22000 -a 6 wordlist.txt ?d?d?d?d

# Ver progresso do cracking
hashcat -m 22000 pmkid.hc22000 wordlist.txt --status --status-timer 10
```

### API:

```json
POST /api/pmkid/start
{
  "bssid": "AA:BB:CC:DD:EE:FF",
  "ssid": "MinhaRede",
  "channel": 6,
  "timeout": 30
}

POST /api/pmkid/stop

GET  /api/pmkid/results
â†’ [{ "ssid": "MinhaRede", "bssid": "AA:BB:CC:DD:EE:FF", "pmkid": "abc123...", "hashcat": "..." }]

GET  /api/pmkid/export   â†’ arquivo .hc22000 pronto para hashcat
```

---

## 10. WPS Scanner

### O que e WPS?

WPS (Wi-Fi Protected Setup) foi criado para facilitar a conexao de dispositivos sem digitar senha - basta apertar um botao ou digitar um PIN de 8 digitos. O problema e que esse PIN tem falhas matematicas graves.

### Vulnerabilidades do WPS:

| Vulnerabilidade | Como funciona | Gravidade |
|----------------|---------------|-----------|
| **PIN Brute Force** | O PIN de 8 digitos e verificado em 2 metades independentes (4+4). Isso reduz as combinacoes de 100 milhoes para apenas ~11.000. Em alguns roteadores sem bloqueio por tentativas, a senha pode ser obtida em minutos | Alta |
| **Pixie Dust** | Ataque offline contra roteadores que usam numeros aleatorios fracos na geracao do PIN WPS. A senha pode ser descoberta sem tentar varias combinacoes | Critica |
| **PBC Window** | Quando voce aperta o botao WPS, ha uma janela de 120 segundos onde qualquer pessoa pode se associar sem autenticacao | Media |

### O que o scanner detecta:

- APs com WPS ativo (e possivel confirmar a vulnerabilidade)
- Versao do WPS (1.0 = mais vulneravel; 2.0 = parcialmente corrigido)
- Se o PIN esta habilitado (vulneravel a brute force e Pixie Dust)
- Se ha bloqueio por tentativas (alguns roteadores bloqueiam apos 3 tentativas erradas)

### API:

```
POST /api/wps/scan      â†’ escaneia APs com WPS ativo
GET  /api/wps/results   â†’ lista APs vulneraveis detectados
```

---

## 11. Auto-Attack / Wifite Mode

### O que e?

Automacao completa da auditoria WiFi. Inspirado no Wifite (ferramenta classica do Linux), o Wraith pega todas as redes do scan e tenta capturar a senha de cada uma automaticamente usando tres tecnicas em sequencia.

### Fluxo automatico para cada rede:

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚ Para CADA rede WPA2 no scan (ordenadas por RSSI):           â”‚
â”‚                                                             â”‚
â”‚  â”Œâ”€ TENTATIVA 1: PMKID Fisher (padrao: 30s)                â”‚
â”‚  â”‚  Tenta capturar PMKID sem precisar de clientes           â”‚
â”‚  â”‚  âœ“ Sucesso â†’ salva, vai para PROXIMA rede               â”‚
â”‚  â”‚  âœ- Falhou  â†’ vai para tentativa 2                        â”‚
â”‚  â”‚                                                          â”‚
â”‚  â”œâ”€ TENTATIVA 2: Deauth + Handshake (padrao: 60s)          â”‚
â”‚  â”‚  Expulsa clientes, aguarda reconexao, captura handshake  â”‚
â”‚  â”‚  âœ“ Sucesso â†’ salva, vai para PROXIMA rede               â”‚
â”‚  â”‚  âœ- Falhou  â†’ vai para tentativa 3                        â”‚
â”‚  â”‚  (falha se nao tiver clientes conectados)                â”‚
â”‚  â”‚                                                          â”‚
â”‚  â””â”€ TENTATIVA 3: Evil Twin (padrao: 120s)                   â”‚
â”‚     Clone AP + Deauth + Portal Captive                      â”‚
â”‚     âœ“ Sucesso â†’ salva senha, vai para PROXIMA rede         â”‚
â”‚     âœ- Falhou  â†’ marca como "nao capturada", proxima        â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

### Logica adaptativa por RSSI:

O WifiteMode ajusta tempos e tentativas baseado na forca do sinal de cada alvo:

| Forca do sinal | PMKID timeout | Deauth timeout | Retentativas deauth |
|---------------|--------------|----------------|---------------------|
| Muito proximo (> -55 dBm) | 15s | 30s | 3x |
| Proximo (-55 a -70 dBm) | 30s | 60s | 2x |
| Longe (-70 a -85 dBm) | 45s | 80s | 1x |
| Muito longe (< -85 dBm) | Pulado (skipWeak) | - | - |

**Retry de Deauth com escalonamento de intensidade**: se a fase Deauth falha, ela retenta com intensidade crescente. Rodada 1: SOFT (64 pacotes/burst). Rodada 2: HARD (512 pacotes/burst).

### Ordenacao de alvos:

Os alvos sao ordenados automaticamente para maximizar taxa de sucesso:
1. **WPS PIN vulneravel** - prioridade maxima (flag manual disponivel no painel)
2. **RSSI mais forte** - alvos proximos primeiro (maior chance de captura)
3. Somente redes WPA2+ (WEP e Open sao ignorados)

### Deteccao de WPS Pixie Dust:

Durante o scan WPS, o WifiteMode identifica APs potencialmente vulneraveis ao ataque Pixie Dust (geracao fraca de E-S1/E-S2 nonces por certas implementacoes Broadcom/Ralink). Esses APs sao marcados como `pixieDust: true` no status JSON para ataque manual.

### Configuracoes:

| Parametro | Padrao | O que faz |
|-----------|--------|-----------|
| `minRssi` | -70 dBm | Ignora redes com sinal mais fraco que esse valor |
| `pmkidTimeout` | 30s | Tempo maximo na fase PMKID (adaptativo por RSSI) |
| `deauthTimeout` | 60s | Tempo maximo aguardando handshake (adaptativo) |
| `eviltwinTimeout` | 120s | Tempo maximo no Evil Twin por rede |
| `skipWeak` | true | Pula APs com RSSI < -85 dBm automaticamente |

### Tabela de progresso no painel:

| Rede | Sinal | PMKID | Handshake | Evil Twin | Status |
|------|-------|-------|-----------|-----------|--------|
| MinhaRede_Casa | -52 | Capturado | - | - | Concluido |
| WiFi_Vizinho | -61 | Falhou | Capturado | - | Concluido |
| RedeEmpresa | -68 | Falhou | Falhou | Em andamento | ... |
| WiFiDistante | -82 | Ignorado | Ignorado | Ignorado | Sinal fraco |

### API:

```json
POST /api/wifite/start
{
  "minRssi": -75,
  "pmkidTimeout": 30,
  "deauthTimeout": 60,
  "eviltwinTimeout": 120
}

POST /api/wifite/stop

GET  /api/wifite/status
â†’ { "current": "MinhaRede", "phase": "deauth", "progress": [...] }
```

---

## 12. Nuclear Auto-Attack

### O que e?

O modo mais agressivo: ativa **todos** os modulos ofensivos em paralelo para capturar o maximo de informacoes de todas as redes no alcance ao mesmo tempo.

### O que roda simultaneamente:

| Modulo | O que faz no Nuclear |
|--------|---------------------|
| **Packet Sniffer** | Channel hopping continuo capturando EAPOL e PMKID de TODAS as redes |
| **Deauth Attack** | Expulsa clientes de TODAS as redes WPA2 detectadas (forcando handshakes) |
| **PMKID Fisher** | Tenta capturar PMKID de cada AP acessivel em sequencia |
| **Karma Attack** | Captura qualquer dispositivo que tente se reconectar a uma rede conhecida |

### Resultado esperado em 5-10 minutos:

- Handshakes WPA2 de multiplas redes (prontos para cracking com hashcat)
- PMKIDs de todos os APs acessiveis
- Dispositivos conectados ao Karma (para captive portal)
- Lista completa de probe requests do ambiente

### Quando usar:

- Auditoria completa de um ambiente sem querer atacar rede por rede
- Competicoes CTF com foco em WiFi
- Demonstracao do nivel de exposicao de um ambiente

### API:

```json
POST /api/nuclear/start

POST /api/nuclear/stop

GET  /api/nuclear/status
â†’ {
    "sniffer": { "handshakes": 3, "pmkids": 7 },
    "deauth": { "sent": 4821, "networks": 5 },
    "karma": { "clients": 2, "probes": 47 }
  }
```

---

## 13. Target Intel (Fingerprinting de Dispositivos)

### O que e?

Motor de reconhecimento passivo que captura trafego 802.11 em modo promiscuo e extrai dados de todos os dispositivos ao redor - sem injecao de frames, sem fazer nada ativo. Coleta: fabricante pelo MAC (OUI lookup), tipo de dispositivo via fingerprint de probe requests, consultas DNS em texto claro, e credenciais HTTP enviadas em formularios sem HTTPS.

### Capacidades em detalhe:

#### OUI Lookup - Fabricante pelo MAC

Identifica o fabricante de qualquer dispositivo pelos primeiros 3 octetos do MAC address. Base de dados embutida cobre os fabricantes mais comuns:

| OUI | Fabricante | Tipo provavel |
|-----|-----------|---------------|
| 28:CF:E9, AC:BC:32, 38:F9:D3... | Apple | iPhone, iPad, MacBook |
| 68:09:27, E4:92:FB, A0:82:1F... | Samsung | Android Galaxy |
| 90:61:AE, 08:21:EF, 78:11:DC... | Xiaomi | Android Redmi/POCO |
| F4:42:8F, 48:D6:D5... | Google | Pixel, ChromeOS |
| 14:2D:27, C4:E9:84... | TP-Link | Roteador/AP |
| 60:45:CB, 00:1E:8C... | Intelbras | Roteador BR |

MACs localmente administrados (bit U/L = 1, primeiro octeto impar) indicam randomizacao de MAC - o dispositivo esta escondendo sua identidade real.

#### Device Fingerprint - Tipo de Dispositivo

Combina OUI com SSIDs encontrados nos probe requests para identificar o tipo do dispositivo sem nenhuma interacao:

- Fabricante Apple → "iOS/macOS - Apple"
- Fabricante Samsung → "Android - Samsung"
- SSID pedido contem "iPhone" → "iOS - iPhone"
- SSID pedido contem "Galaxy" → "Android - Samsung"
- Fabricante TP-Link/Intelbras/Asus → "Router/AP - [marca]"

#### Captura DNS Passiva

Parseia pacotes data 802.11 em busca de consultas DNS (UDP porta 53). Como DNS nao tem criptografia (sem DoH/DoT), cada dominio consultado e capturado e associado ao MAC do cliente. Limite de 200 consultas, deduplicadas por par (dominio + MAC cliente).

#### Captura de Credenciais HTTP

Monitora trafego TCP nas portas 80, 8080 e 8888. Quando detecta um POST HTTP com body contendo palavras-chave (`pass`, `pwd`, `senha`, `login`, `user`), salva o MAC, host, path e body (primeiros 256 bytes).

> Apenas sites sem HTTPS sao vulneraveis. HTTPS (porta 443) e criptografado e nao capturavel passivamente.

### O que captura:

| Dado | Como captura | O que revela |
|------|-------------|-------------|
| **Fabricante** | OUI dos 3 primeiros octetos do MAC | Marca do dispositivo (Apple, Samsung, Xiaomi...) |
| **Tipo de dispositivo** | OUI + SSIDs nos probe requests | iPhone, Android, Roteador, Windows |
| **Consultas DNS** | Pacotes UDP porta 53 em claro | Quais apps/sites o dispositivo usa |
| **Credenciais HTTP** | POST sem HTTPS | Login e senha em sites HTTP |
| **MAC aleatorio** | Bit U/L do primeiro octeto | Dispositivo usa randomizacao de privacidade |
| **Probe requests** | Management frames tipo 0x04 | Redes que o dispositivo ja conheceu |

### API:

```json
POST /api/intel/start   -> inicia captura promiscua

POST /api/intel/stop    -> para captura

GET  /api/intel/data
{
  "devices": [
    {
      "mac": "A4:C3:F0:...",
      "manufacturer": "Apple",
      "deviceType": "iOS/macOS - Apple",
      "rssi": -62,
      "probedSSIDs": ["MinhaRede", "Trabalho"]
    }
  ],
  "dns": [
    { "clientMac": "A4:C3:F0:...", "domain": "api.whatsapp.com" }
  ],
  "httpCreds": [
    { "host": "portal.exemplo.com", "path": "/login", "body": "user=joao&pass=1234" }
  ]
}
```

### API (legado):

```json
POST /api/intel/start
{
  "target": "AA:BB:CC:DD:EE:FF",
  "channel": 6
}

GET  /api/intel/data
â†’ {
    "dns": ["google.com", "instagram.com", "api.whatsapp.com"],
    "useragent": "Mozilla/5.0 (Android 13; Samsung Galaxy S21)",
    "os": "Android",
    "activity": [...]
  }
```

---

## 14. Hidden SSID Scanner (Revelador de Redes Ocultas)

### O que e?

Redes WiFi podem ser configuradas como "ocultas" - o AP nao transmite o SSID nos beacons periodicos, entao a rede nao aparece na lista de WiFi normal. O Hidden SSID Scanner usa duas tecnicas para forcas o AP a revelar o SSID escondido.

### Por que funciona?

O protocolo 802.11 exige que um AP responda a uma Probe Request direcionada com o SSID real, mesmo que o AP esteja em modo oculto. O scanner explora isso de duas formas:

1. **Probe Request direcionada (unicast)**: Envia probe com SSID vazio diretamente para o BSSID do AP oculto. O AP e obrigado a responder com o SSID real na Probe Response.
2. **Probe Request com wordlist (broadcast + unicast)**: Envia probes com SSIDs da wordlist. Se o AP reconhece o SSID pedido como sendo o seu, ele confirma com Probe Response contendo o SSID.

### Mecanismo interno:

```
1. loadFromScan()   - usa WiFi.scanNetworks(includeHidden=true) para listar BSSIDs com SSID vazio
2. tick()           - para cada AP oculto:
     a. Probe unicast com SSID vazio         → AP responde com SSID real
     b. 2x Probe unicast com SSID da wordlist → confirma se acertar
     c. Probe broadcast em canal atual        → cobertura ampla
3. _rxCb()          - captura Probe Responses (subtype 5) e extrai SSID do IE 0x00
4. Resultado: lista "revealed" com SSIDs descobertos
```

### Cadencia de varredura:

- ~11 probes por segundo por canal
- Hop completo nos canais 1 a 13 a cada tick
- 3 probes por alvo por tick: vazio + 2 da wordlist
- Wordlist de 80 SSIDs: nomes genericos + provedores BR (CLARO, VIVO, TIM, OI) + fabricantes + dispositivos moveis

### Wordlist integrada (exemplos):

```
Genericos:    home, wifi, router, network, default, hidden, secure, office
Provedores:   NET, CLARO, VIVO, TIM, OI, minhacasa, meu_wifi, wifi_casa
Fabricantes:  NETGEAR, TP-LINK, ASUS, Xiaomi, Huawei, Intelbras, D-Link
Moveis:       AndroidAP, iPhone, Redmi, Galaxy, OnePlus, Pixel
Comercial:    hotel_wifi, lobby, conference, staff, pos, caixa, pdv
```

### Limitacoes:

- Nao funciona em APs com PMF obrigatorio que ignoram probes de clientes nao autenticados
- SSIDs muito incomuns (nao na wordlist) so sao revelados pela probe vazia - que funciona em ~90% dos APs
- Requer que o AP esteja no mesmo alcance de RF

### API:

```json
POST /api/hidden/scan   -> inicia o scanner

POST /api/hidden/stop   -> para o scanner

GET  /api/hidden/status
{
  "running": true,
  "targets": [
    { "bssid": "AA:BB:CC:DD:EE:FF", "channel": 6, "ssid": "" }
  ],
  "revealed": ["MinhaRedeOculta", "RedeSecreta"]
}
```

### Combinacao recomendada:

Use antes do Karma Attack: se um AP oculto tem clientes, esses clientes enviarao probes com o SSID real. O Hidden Scanner revela o SSID e entao o Karma pode responder a esses probes.

---

## 15. LED Panel Attack

### O que e?

Detecta e explora controladores de iluminacao inteligente (strips de LED RGB) que usam protocolos WiFi sem autenticacao.

### Por que sao vulneraveis?

Fabricantes de produtos baratos priorizaram custo sobre seguranca. Os protocolos enviam comandos em texto puro via UDP, sem senha, sem verificacao de origem - qualquer dispositivo na mesma rede (ou que consiga se conectar via Evil Twin) pode controlar as luzes.

### Protocolos suportados:

| Protocolo | Porta UDP | Fabricantes / Plataformas |
|-----------|-----------|--------------------------|
| **CL (Color Light)** | 9980 | Controladores genericos chineses (aliexpress, shopee) |
| **WLED** | 5005 | Plataforma open source WLED (muito usada em projetos DIY) |

### Modos de ataque:

| Modo | O que faz | Efeito visivel |
|------|-----------|---------------|
| **Deauth** | Desconecta o controlador da rede WiFi | Luzes ficam no ultimo estado ou apagam |
| **Flood** | Envia rajada de pacotes UDP invalidos | Luzes piscam, travamento ou reinicio do controlador |
| **Disrupt** | Envia comandos malformados | Comportamento inesperado: cores erradas, padroes aleatorios |

### API:

```json
POST /api/led/scan      â†’ detecta controladores LED na rede

POST /api/led/attack
{
  "target": "192.168.1.100",
  "protocol": "wled",
  "mode": "flood"
}
```

---

## 16. BLE Scanner e Spam

### BLE Scanner

Varre o espectro Bluetooth Low Energy e coleta informacoes de todos os dispositivos ao redor.

**O que detecta:**
- Nome do dispositivo (GAP Advertisement)
- MAC address (publico ou aleatorio/privado)
- RSSI e distancia estimada
- Fabricante (Company ID do advertisement)
- Servicos GATT anunciados (UUIDs - identifica tipo de dispositivo)
- Nivel de bateria (quando disponivel nos advertisements)

```json
POST /api/ble/scan       { "seconds": 10 }
POST /api/ble/enumerate  { "addr": "AA:BB:CC:DD:EE:FF" }
GET  /api/ble/devices
```

### BLE Spam

Injeta advertisements BLE falsos imitando dispositivos populares para gerar notificacoes em dispositivos proximos.

**Como funciona**: Sistemas operacionais modernos exibem notificacoes automaticas quando detectam dispositivos proximos (AirPods, AirTags, Fast Pair, etc.). O BLE Spam injeta os advertisements exatos que ativam essas notificacoes.

| Tipo | Dispositivos afetados | Notificacao gerada |
|------|-----------------------|--------------------|
| **Apple AirPods** | iPhone, iPad, MacBook | "AirPods encontrados - conectar?" |
| **Apple AirTag** | iPhone | "AirTag detectado perto de voce" |
| **Apple Keyboard** | iPhone, iPad | "Teclado Apple encontrado" |
| **Android Fast Pair** | Android 6.0+ | Popup de emparelhamento rapido |
| **Samsung Quick Share** | Samsung Galaxy | Notificacao de compartilhamento |
| **Windows Swift Pair** | Windows 10/11 | Popup de emparelhamento |

```json
POST /api/ble/spam
{
  "types": ["apple", "android", "samsung"],
  "interval": 50
}

POST /api/ble/spam/stop
```

### BT Classic Scanner

Detecta dispositivos Bluetooth Classic (BR/EDR) em modo discoverable (visiveis para emparelhamento).

```
POST /api/bt/scan
GET  /api/bt/devices
â†’ [{ "name": "Fone JBL", "class": "Audio", "rssi": -62 }]
```

---

## 17. Fluxos de Ataque Completos

Exemplos de cenarios completos de auditoria do inicio ao fim.

---

### Fluxo A - Capturar senha WPA2 (com clientes na rede)

**Objetivo**: Obter o handshake WPA2 de uma rede que tem dispositivos conectados.

```
Passo 1 - Reconhecimento
  â†’ WiFi Scanner
  â†’ Identificar rede-alvo: SSID, BSSID, canal, clientes conectados
  â†’ Confirmar sinal: RSSI acima de -70 dBm

Passo 2 - Posicionar o sniffer
  â†’ Packet Sniffer â†’ start
  â†’ Fixar no canal do alvo (nao usar hopping)

Passo 3 - Forccar reconexao
  â†’ Deauth Attack â†’ start (broadcast ou cliente especifico)
  â†’ Aguardar 10-30 segundos

Passo 4 - Verificar captura
  â†’ Sniffer â†’ ver handshakes capturados
  â†’ Deve aparecer EAPOL M1, M2, M3, M4 do alvo

Passo 5 - Exportar e quebrar
  â†’ Export Hashcat â†’ salvar .hc22000
  â†’ hashcat -m 22000 captura.hc22000 rockyou.txt
```

---

### Fluxo B - Capturar PMKID (sem clientes necessarios)

**Objetivo**: Obter o PMKID mesmo que ninguem esteja conectado na rede.

```
Passo 1 - Reconhecimento
  â†’ WiFi Scanner
  â†’ Identificar rede-alvo com WPA2

Passo 2 - PMKID Fisher
  â†’ PMKID Fisher â†’ start com o AP selecionado
  â†’ Aguardar 10-30 segundos (geralmente e imediato)

Passo 3 - Exportar e quebrar
  â†’ Se capturou: Export Hashcat
  â†’ hashcat -m 22000 pmkid.hc22000 rockyou.txt -r best64.rule
```

---

### Fluxo C - Capturar senha via portal (usuario digita a senha)

**Objetivo**: Enganar o usuario para que ele mesmo forneÃ§a a senha.

```
Passo 1 - Reconhecimento
  â†’ WiFi Scanner
  â†’ Identificar rede-alvo, canal e clientes

Passo 2 - Evil Twin
  â†’ Selecionar rede-alvo
  â†’ Ativar Evil Twin com deauth ativado e template "reconnect"
  â†’ Aguardar clientes se conectarem (pode levar 1-5 minutos)

Passo 3 - Monitorar
  â†’ Painel mostra clientes conectados ao AP falso em tempo real
  â†’ Quando o usuario tentar acessar qualquer site, vÃª o portal
  â†’ Ao digitar a senha, ela aparece instantaneamente no painel

Passo 4 - Finalizar
  â†’ Stop Evil Twin
  â†’ Credenciais ficam salvas no painel
```

---

### Fluxo D - Auditoria completa rapida

**Objetivo**: Coletar o maximo de informacao de um ambiente em poco tempo.

```
Opcao 1 - Automatica (recomendado):
  â†’ Nuclear Auto-Attack â†’ start
  â†’ Aguardar 5-15 minutos
  â†’ Ver resultados: handshakes, PMKIDs, clientes Karma

Opcao 2 - Manual sequencial:
  â†’ WiFi Scanner â†’ identificar todas as redes
  â†’ PMKID Fisher â†’ testar cada AP em sequencia (30s cada)
  â†’ Deauth + Sniffer â†’ nas redes com clientes (60s cada)
  â†’ Karma Attack â†’ ativo durante toda a auditoria para capturar dispositivos vagantes
```

---

### Fluxo E - Capturar dispositivos vagantes (Karma)

**Objetivo**: Capturar dispositivos que estao procurando redes conhecidas sem estar conectados a nenhuma.

```
Passo 1 - Karma + MANA
  â†’ Karma Attack â†’ start com MANA ativado
  â†’ O painel comeca a mostrar SSIDs que dispositivos estao buscando

Passo 2 - Monitorar capturas
  â†’ Quando um dispositivo conectar, aparece no painel
  â†’ Portal captive e exibido automaticamente

Passo 3 - Coletar credenciais
  â†’ Usuario ve portal, digita senha do WiFi
  â†’ Credencial aparece no painel
```

---

---

## Primeiros Passos - Guia Rapido

```
1. Ligue o ESP32 (qualquer fonte USB 5V, minimo 500mA)
   → LED pisca branco no boot (sequencia R/G/B de teste)
   → LED azul pulsando = sistema pronto

2. Conecte ao WiFi
   Rede:  Wraith
   Senha: predator123

3. Acesse o painel
   → http://192.168.4.1
   → Senha: predator

4. Faca o scan
   → WiFi Scanner → Scan
   → Aguarde 5-10 segundos

5. Escolha o ataque
   → Selecione a rede-alvo
   → Escolha o modulo desejado
   → LED vermelho = ataque ativo
   → LED verde piscando = captura realizada!

DICA: Use o botao BOOT para pausar todos os ataques rapidamente.
```

---

## Console USB - Debug via Porta USB-C Esquerda

O ESP32-S3 expoe um console serial nativo via USB CDC (sem CH343). Permite log em tempo real pelo conector USB-C esquerdo.

**Como usar:**
1. Conecte o USB-C esquerdo (OTG) ao PC
2. Uma nova porta COM aparece no Gerenciador de Dispositivos
3. Abra com qualquer monitor serial a 115200 baud
4. Todos os logs do Wraith aparecem em tempo real

> Se precisar usar a porta OTG para um adaptador RTL8188 ao mesmo tempo, use o conector USB-C direito (CH343P) para o console. As duas portas funcionam independentemente.

---

## Novas Funcionalidades v4.0 - 50 Melhorias

### Seguranca e Autenticacao

| API | Metodo | Descricao |
|-----|--------|-----------|
| `/api/auth` | POST `{"pass":"predator"}` | Login com token de sessao (24h) |
| `/api/auth/validate` | GET `?token=<tok>` | Valida token ativo |

**Auto-destruct (#48):** apos 10 senhas erradas consecutivas, todas as capturas sao apagadas automaticamente. Configuravel em `config.h` → `AUTODESTRUCT_TRIES`.

### Capturas Persistentes (#44)

Handshakes, PMKIDs e credenciais sao salvos no filesystem LittleFS e sobrevivem ao reboot e desligamento. A estrutura de arquivos no ESP32:

```
/captures/
  hs_AA-BB-CC-DD-EE-FF.hc22000    <- handshake WPA2 (por BSSID)
  pmkid_AA-BB-CC-DD-EE-FF.hc22000 <- PMKID capturado
  creds.json                        <- credenciais Evil Twin
  log.jsonl                         <- log de eventos (JSONL)
```

**Formato hc22000** - padrao hashcat para WPA2/PMKID:

```
WPA*02*<MIC 16B hex>*<BSSID flat>*<ClientMAC flat>*<SSID hex>*<ANonce>*<SNonce>*<EAPOL data>
```

Exemplo de uso imediato apos exportar:

```bash
hashcat -m 22000 hs_AABBCCDDEE.hc22000 rockyou.txt
hashcat -m 22000 hs_AABBCCDDEE.hc22000 rockyou.txt -r best64.rule
hashcat -m 22000 hs_AABBCCDDEE.hc22000 -a 3 ?d?d?d?d?d?d?d?d  # brute 8 digitos
```

| API | Descricao |
|-----|-----------|
| `/api/captures/list` | Lista arquivos de captura no filesystem |
| `/api/captures/download?file=hs_BSSID.hc22000` | Baixa captura diretamente no browser |
| `/api/captures/wipe` | Apaga todas as capturas (auto-destruct manual) |

**Handshake crackable vs completo**: o sniffer aceita handshake parcial `M1+M2` ou `M2+M3` - suficiente para hashcat `-m 22000`. Nao e necessario capturar os 4 pacotes do 4-way handshake.

### Mapa de Canais (#42)

| API | Descricao |
|-----|-----------|
| `/api/channels` | JSON com count de APs e RSSI medio por canal (1-13) |
| `/api/channels/best` | Canal menos congestionado para Evil Twin/Karma |

Populado automaticamente no boot com scan inicial.

### Interferencia Bluetooth (#BT_JAM)

Novo modo `MODE_INTERFERE` no BLE Spam - satura os 3 canais de advertisement BLE (37, 38, 39) com ~800 pacotes/segundo de payload aleatorio, degradando conexoes Bluetooth proximas.

```
POST /api/blespam/interfere
```

> Afeta BLE 4.x/5.x. BT Classic nao e afetado (opera em canais diferentes).

### Deauth Stealth (#21)

Modo furtivo: intervalos aleatorios de 200ms-2s entre rajadas de deauth, evitando deteccao por IDS/WIDS baseados em frequencia de frames.

```json
POST /api/deauth/options
{"stealth": true}
```

### Karma com Blacklist (#19)

O Karma nunca responde a SSIDs sensiveis configurados em `config.h → KARMA_BLACKLIST`:
- Redes corporativas: eduroam, govwifi
- Operadoras BR: Claro, TIM, Vivo, Oi, NET
- Hotspots moveis: AndroidAP, iPhone, Hotspot

### Lookup de Fabricante OUI (#11)

```
GET /api/oui?mac=AA:BB:CC:DD:EE:FF
```
Retorna fabricante (Apple, Samsung, TP-Link, etc.) e se o MAC e randomizado (privacidade).
Funciona offline - tabela de ~60 fabricantes em PROGMEM.

### Monitoramento de Bateria (#34)

Ative em `config.h` descomentando `BATTERY_PIN`:
```
GET /api/battery  →  {"batt_mv":3850, "batt_pct":72, "charging":false}
```

### OTA Firmware (#36)

```
POST /api/ota/update  (multipart/form-data, campo "firmware")
GET  /api/ota/status  →  {"updating":false, "progress":100, "success":true}
```
Compile com `pio run -e esp32-s3`, pegue `.pio/build/esp32-s3/firmware.bin` e envie pelo painel.

### Sleep Profundo (#35)

```json
POST /api/system/sleep
{"seconds": 3600}
```
ESP32 entra em deep sleep e acorda apos o tempo especificado. Consome < 20µA no sono.

### Opcoes Avancadas Deauth

| Opcao JSON | Descricao |
|-----------|-----------|
| `"stealth": true` | Intervalos aleatorios entre bursts |
| `"csaMode": true` | Channel Switch Announcement (move clientes para outro canal) |
| `"csaChannel": 13` | Canal destino do CSA |
| `"eapFlood": true` | EAP Identity Request flood (WPA2-Enterprise) |
| `"tkip": true` | TKIP MIC Failure injection (WPA-TKIP legado) |
| `"reconnectBlock": true` | Probe Response falsa - impede reconexao ao AP real |

---

## Novas Funcionalidades v4.1

### Ataques de DoS no Meio Fisico 802.11 - Channel-Level Flooding

Ataques que comprometem o **canal inteiro**, nao apenas um AP ou cliente especifico. Todos os dispositivos no canal sao afetados.

```json
POST /api/deauth/options
{
  "navFlood":  true,
  "ctsFlood":  true,
  "probeFlood": true,
  "fragFlood": true,
  "deauthAll": true
}
```

#### NAV Duration Poison (`navFlood`)

**Mecanismo**: Injeta frames de dados com campo Duration = 32767µs (maximo IEEE 802.11-2020). Todos os dispositivos no canal configuram seu Network Allocation Vector (NAV) para esse valor, acreditando que o meio esta ocupado por 32ms.

**Efeito**: Throughput do canal cai para perto de zero. Nenhum dispositivo transmite enquanto o NAV nao expira (renovado continuamente pelo ataque).

**Frames/s**: ~32 frames a cada tick (10ms) = ~3200 frames/s

---

#### CTS-to-Self Flood (`ctsFlood`)

**Mecanismo**: Frames CTS (Clear To Send) sao frames de controle que reservam o meio. Injeta CTS com Duration = 32767µs spoofando o BSSID do AP alvo.

**Diferencial**: CTS e um frame legitimo esperado pelo protocolo - menos provavel de ser filtrado por IDS do que frames de gerenciamento. Afeta o canal sem exigir que clientes estejam associados.

**Frames/s**: ~32 por tick

---

#### Probe Request Flood (`probeFlood`)

**Mecanismo**: Envia Probe Requests com MACs aleatorios (localmente administrados) para o SSID alvo. O AP precisa processar e responder cada uma.

**Efeito**: Esgota CPU e memoria do AP lidando com respostas. APs com pouca RAM (roteadores domesticos) podem travar, reiniciar ou parar de aceitar novos clientes.

**Frames/s**: ~16 por tick

---

#### Fragmentation Flood (`fragFlood`)

**Mecanismo**: Envia o primeiro fragmento (fragment 0, More Fragments = 1) de frames de dados sem jamais enviar os fragmentos seguintes. O AP aloca buffer de reassembly para cada fragmento pendente.

**Efeito**: Com ~128 fragmentos pendentes distintos (MACs de origem unicos), a maioria dos APs esgota o pool de reassembly e trava ou reinicia.

**Frames/s**: ~8 por tick (mais pesados)

---

#### Universal Deauth (`deauthAll`)

**Mecanismo**: Deauth e disassoc para o endereco broadcast com BSSIDs gerados aleatoriamente. Atinge todos os clientes no canal sem precisar conhecer o BSSID especifico.

**Combinacao recomendada**: usar junto com `navFlood` para bloquear reconexoes.

---

### Evil Twin Avancado com Captura de Handshake

O ataque Evil Twin foi completamente reescrito em v4.1 com as seguintes capacidades:

#### Fluxo do Ataque

```
1. Escaneia e seleciona AP alvo (SSID + BSSID + canal)
2. Sobe AP falso com mesmo SSID (sem senha) no mesmo canal
3. Tenta spoof do BSSID via esp_wifi_set_mac()
4. Inicia deauth broadcast no canal do AP real (loop a cada 500ms)
5. Clientes perdem conexao com o AP real e se associam ao AP falso
6. Portal cativo intercepta todos os requests HTTP
7. Captura EAPOL handshakes WPA2 via sniffer promiscuo simultaneamente
8. Credenciais digitadas no portal sao salvas em LittleFS
```

#### API

```json
POST /api/eviltwin/start
{
  "ssid":      "MinhaRede",
  "bssid":     "AA:BB:CC:DD:EE:FF",
  "channel":   6,
  "deauthLoop": true,
  "captureHS":  true
}

POST /api/eviltwin/stop

GET /api/eviltwin/creds
```

#### Portal Cativo - Templates

O portal cativo tem 3 templates configuráveis via API:

| Template | Descricao | Caso de Uso |
|----------|-----------|-------------|
| `0` (padrao) | "Atualizacao de Seguranca" - pede reautenticacao | Redes residenciais |
| `1` | "Portal do Cliente ISP" - simula portal de provedor | Hotspots e redes de provedores |
| `2` | "Admin do Roteador" - tela de login estilo TP-Link/Intelbras | Redes corporativas |

```json
POST /api/eviltwin/template
{"template": 1}
```

#### Captura de Handshake WPA2

Simultaneamente ao portal cativo, o sniffer captura handshakes 4-way (EAPOL) automaticamente:

- **Msg1 capturada**: extrai ANonce do AP real
- **Msg2 capturada**: extrai SNonce + MIC do cliente
- **Handshake completo** → salvo em `/captures/hs_SSID.hc22000`

Formato compativel com **hashcat -m 22000**:
```bash
hashcat -m 22000 hs_MinhaRede.hc22000 rockyou.txt
```

---

## Resolucao de Problemas

| Problema | Causa provavel | Solucao |
|----------|---------------|---------|
| Rede Wraith nao aparece | Dispositivo nao inicializou | Aguarde 5s e atualize a lista de WiFi |
| "Nao foi possivel conectar" | Senha errada salva no Windows | Clique direito → Esquecer → Reconectar com "predator123" |
| Painel nao carrega | IP errado | Use http://192.168.4.1 (nao HTTPS) |
| LED sempre verde | GPIO do LED incorreto | Altere RGB_LED_PIN para 38 em config.h e regrave |
| LED nao acende | GPIO do LED incorreto | Altere RGB_LED_PIN para 48 em config.h e regrave |
| Adaptador USB nao reconhecido | Chip nao compativel (AR9271) | Use adaptador RTL8188EU/CUS (TL-WN722N v1) |
| Crash loop na inicializacao | Brownout por fonte fraca | Use fonte USB com minimo 500mA; o brownout detector esta desabilitado por firmware |
| Capturas desapareceram | Auto-destruct ativado | 10 senhas erradas seguidas apagam capturas - mude AUTODESTRUCT_TRIES=0 para desabilitar |
| Console USB nao aparece | CDC nao habilitado | Verifique que o build usa env:esp32-s3 com ARDUINO_USB_CDC_ON_BOOT=1 |

---

*Wraith v5.0 - Para uso educacional e testes autorizados apenas.*

