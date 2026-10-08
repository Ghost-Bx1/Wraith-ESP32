# Ghost Predator - Guia Completo de Ataques

> **AVISO LEGAL**: Use exclusivamente em redes e dispositivos que voce possui ou tem autorizacao explicita e documentada para testar. Uso nao autorizado e crime (Lei 12.737/2012 no Brasil). Os autores nao se responsabilizam por uso indevido.

---

## Sumario

1. [WiFi Scanner](#1-wifi-scanner)
2. [Packet Sniffer](#2-packet-sniffer)
3. [Deauth Attack](#3-deauth-attack-desautenticacao-80211)
4. [Evil Twin + Captive Portal](#4-evil-twin-ap-clone--captive-portal)
5. [Beacon Flood](#5-beacon-flood-inundacao-de-redes-falsas)
6. [Karma Attack](#6-karma-attack-armadilha-de-reconexao-automatica)
7. [Reconnect Blocker](#7-reconnect-blocker-bloqueador-de-reconexao)
8. [PMKID Fisher](#8-pmkid-fisher-captura-sem-cliente)
9. [WPS Scanner](#9-wps-scanner)
10. [Auto-Attack / Wifite Mode](#10-auto-attack--wifite-mode)
11. [Nuclear Auto-Attack](#11-nuclear-auto-attack)
12. [Target Intel](#12-target-intel-fingerprinting-de-dispositivos)
13. [LED Panel Attack](#13-led-panel-attack)
14. [BLE Scanner e Spam](#14-ble-scanner-e-spam)
15. [Fluxos de Ataque Completos](#15-fluxos-de-ataque-completos)

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
GET  /api/wifi/scan          → inicia scan e retorna lista de redes
GET  /api/wifi/clients       → lista clientes detectados por rede
GET  /api/wifi/scan/status   → status do scan em andamento
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
POST /api/sniffer/start               → inicia sniffer (channel hop ativo por padrao)
POST /api/sniffer/stop                → para sniffer
POST /api/sniffer/channel             → fixa em um canal especifico
  { "channel": 6 }
POST /api/sniffer/hop                 → volta ao channel hopping
GET  /api/sniffer/handshakes          → lista handshakes EAPOL capturados
GET  /api/sniffer/pmkids              → lista PMKIDs capturados
GET  /api/sniffer/probes              → lista probe requests capturados
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
1. O Ghost Predator monitora o canal do AP-alvo
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
| **Deauth AP→Cliente** | Finge ser o roteador expulsando o cliente | Caso mais comum, funciona na maioria das redes |
| **Deauth Cliente→AP** | Finge ser o cliente pedindo desconexao ao roteador | Complementar ao anterior, em redes com filtragem parcial |
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

O Ghost Predator tenta bypassar com **SA Query Flood**: envia tantas requisicoes de consulta de seguranca que alguns firmwares ficam sobrecarregados e nao verificam a protecao em todos os pacotes.

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
→ { "running": true, "sent": 1247, "fps": 89, "elapsed": 14 }
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

Cria uma copia identica de uma rede WiFi real (mesmo nome, mesma aparencia) mas sem senha e controlada pelo Ghost Predator. Quem se conectar ve uma pagina falsa pedindo a senha do WiFi.

> **Analogia**: Como clonar uma agencia bancaria do lado da original, fechar a original temporariamente e quando os clientes entrarem na falsa, pedir para "confirmar a senha para acessar a conta".

### Por que as pessoas se conectam?

1. O Ghost Predator usa Deauth para derrubar o sinal da rede real
2. O AP falso emite com sinal forte (esta perto)
3. O dispositivo ve o mesmo nome de rede disponivel e conecta automaticamente
4. Como e uma rede "aberta" (sem senha), a maioria dos dispositivos conecta sem perguntar

### Fluxo completo do ataque:

```
╔══════════════════════════════════════════════════════════╗
║  ETAPA 1 - RECONHECIMENTO                                ║
║  Scanner identifica: SSID, BSSID, canal, clientes        ║
╠══════════════════════════════════════════════════════════╣
║  ETAPA 2 - DEAUTH (derruba a rede real)                  ║
║  Ghost Predator → [pacotes deauth] → Clientes            ║
║  Resultado: todos os dispositivos sao desconectados      ║
╠══════════════════════════════════════════════════════════╣
║  ETAPA 3 - CLONE AP (cria a rede falsa)                  ║
║  Ghost Predator emite beacons com o mesmo SSID           ║
║  sem senha, sinal forte                                  ║
║  Resultado: dispositivos veem a "mesma" rede             ║
╠══════════════════════════════════════════════════════════╣
║  ETAPA 4 - CONEXAO DO ALVO                               ║
║  Dispositivo conecta automaticamente ao AP falso         ║
║  Recebe IP do DHCP do Ghost Predator                     ║
║  DNS do dispositivo aponta para o Ghost Predator         ║
╠══════════════════════════════════════════════════════════╣
║  ETAPA 5 - CAPTIVE PORTAL (pagina falsa)                 ║
║  Qualquer site HTTP → redirecionado para portal falso    ║
║  Pagina pede a senha do WiFi para "reconectar"           ║
║  Senha digitada → aparece no painel em tempo real        ║
╚══════════════════════════════════════════════════════════╝
```

### Por que o redirecionamento funciona:

- **DNS falso**: o Ghost Predator responde QUALQUER consulta DNS com seu proprio IP (192.168.4.1). Quando o dispositivo pergunta "qual o IP do google.com?", ele responde "192.168.4.1"
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
→ [{ "password": "senha123", "timestamp": "14:32:11", "client": "AA:BB:CC:DD:EE:FF" }]
```

**Dicas:**
- Use `"deauth": true` para derrubar o AP real simultaneamente
- As credenciais ficam salvas mesmo apos parar o ataque
- Cada tentativa e registrada, mesmo que o usuario erre a senha

---

## 5. Beacon Flood (Inundacao de Redes Falsas)

### O que e?

Cria centenas ou milhares de redes WiFi falsas ao mesmo tempo, enchendo a lista de WiFi de qualquer dispositivo proximo com redes que nao existem de verdade.

### Como funciona:

Toda rede WiFi anuncia sua presenca continuamente enviando pacotes chamados **beacons** a cada 100ms - e como um outdoor que pisca dizendo "estou aqui, me chamo X". O Ghost Predator injeta beacons fabricados com SSIDs da sua lista em alta velocidade, sem precisar de roteador real.

> **Analogia**: Como espalhar centenas de placas de "Loja Aberta" falsas em uma rua - as lojas nao existem, mas as placas confundem quem esta procurando.

### Opcoes disponiveis:

| Opcao | O que faz | Efeito pratico |
|-------|-----------|---------------|
| **Channel Hop** | Muda de canal automaticamente (1-13) apos cada ciclo completo de SSIDs | Redes falsas aparecem em todos os canais |
| **Hidden Mode** | Emite beacons com SSID vazio (rede sem nome) | Lista de WiFi mostra redes sem nome misteriosamente |
| **WPA3 Mode** | Inclui campos RSN com AKM SAE no beacon | Enganar scanners que filtram por tipo de seguranca |

### Por que cada SSID tem MAC diferente?

O Ghost Predator usa o algoritmo FNV-1a para gerar um MAC unico e deterministico para cada SSID. Isso faz cada rede falsa parecer vir de um roteador diferente, tornando o flood muito mais convincente e dificil de filtrar automaticamente.

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
→ { "running": true, "sent": 4821, "ssids": 4, "channel": 9 }
```

---

## 6. Karma Attack (Armadilha de Reconexao Automatica)

### O que e?

Explora o comportamento automatico dos dispositivos de procurar redes conhecidas. Quando um celular pergunta no ar "MinhaRedeEmpresa esta aqui?", o Ghost Predator responde "Sim, sou eu!" - mesmo nao sendo.

### Por que funciona?

Dispositivos WiFi com "Conexao automatica" ativada (que e o padrao em 100% dos sistemas operacionais) ficam constantemente enviando **probe requests** no ar perguntando por redes que ja conheceram no passado. Normalmente so o roteador real responde. O Karma intercepta e responde por qualquer rede perguntada.

> **Analogia**: Como alguem que fica em uma esquina respondendo a qualquer pessoa que chame um nome ("Joao, esta ai?") dizendo "Sim, sou eu aqui!" - a pessoa vai ate ele sem questionar.

### Passo a passo:

```
1. Seu celular (WiFi ligado, tela bloqueada) envia automaticamente:
   → "MinhaRedeEmpresa, voce esta ai?"
   → "WiFi_Casa, voce esta ai?"
   → "AeroportoFree, voce esta ai?"
   (isso acontece em segundo plano, sem voce saber)

2. O Ghost Predator ouve em modo monitor e captura esses probes

3. Para cada probe, responde imediatamente:
   → "Sim! Sou a MinhaRedeEmpresa. Conecte-se!" (com o mesmo SSID pedido)

4. O celular acredita e inicia conexao automatica (sem voce fazer nada)

5. O celular esta conectado ao Ghost Predator
   → Trafego HTTP pode ser capturado
   → Portal captive pode ser exibido para coletar senha
```

### Modo MANA (Karma Avancado):

- **Karma padrao**: so responde quando recebe um probe request
- **MANA**: alem de responder, tambem emite beacons **ativos** com todos os SSIDs que ja coletou de probes anteriores

O MANA e muito mais agressivo: mesmo dispositivos que nao estao enviando probes no momento podem ser capturados porque veem o beacon com o nome da rede que conhecem.

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
→ [{ "ssid": "Empresa_WiFi", "client": "AA:BB:CC:DD:EE:FF", "time": "14:55:02" }]

GET  /api/karma/probes
→ lista de todos os SSIDs que dispositivos proximos estao buscando
```

---

## 7. Reconnect Blocker (Bloqueador de Reconexao)

### O que e?

Impede que um dispositivo se reconecte a rede real apos ser deautenticado. E um "bloqueio persistente" que mantém o alvo desconectado mesmo quando ele tenta voltar.

### Como funciona:

Quando um celular e deautenticado, ele envia probe requests perguntando pela rede real. O Reconnect Blocker intercepta essas perguntas e responde com uma **probe response falsa**: usa o SSID correto mas um MAC de roteador diferente (gerado via FNV-1a XOR'd com o contador de sequencia). O celular tenta conectar com esse MAC errado, falha, e fica em loop.

```
Celular → "MinhaRede, voce esta ai?"
Ghost Predator → "Sim! [SSID: MinhaRede | MAC: AA:BB:CC:DD:EE:01]" (MAC errado)
Celular → tenta conectar com MAC AA:BB:CC:DD:EE:01 → falha
Celular → "MinhaRede, voce esta ai?"  (tenta de novo)
Ghost Predator → "Sim! [SSID: MinhaRede | MAC: AA:BB:CC:DD:EE:02]" (outro MAC errado)
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

## 8. PMKID Fisher (Captura Sem Cliente)

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
Ghost Predator → [Authentication Request] → AP
AP             → [Authentication Response] → Ghost Predator
Ghost Predator → [Association Request] → AP
AP             → [Association Response + RSNIE contendo PMKID] → Ghost Predator
                                          ↑ PMKID extraido aqui!
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
→ [{ "ssid": "MinhaRede", "bssid": "AA:BB:CC:DD:EE:FF", "pmkid": "abc123...", "hashcat": "..." }]

GET  /api/pmkid/export   → arquivo .hc22000 pronto para hashcat
```

---

## 9. WPS Scanner

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
POST /api/wps/scan      → escaneia APs com WPS ativo
GET  /api/wps/results   → lista APs vulneraveis detectados
```

---

## 10. Auto-Attack / Wifite Mode

### O que e?

Automacao completa da auditoria WiFi. Inspirado no Wifite (ferramenta classica do Linux), o Ghost Predator pega todas as redes do scan e tenta capturar a senha de cada uma automaticamente usando tres tecnicas em sequencia.

### Fluxo automatico para cada rede:

```
┌─────────────────────────────────────────────────────────────┐
│ Para CADA rede WPA2 no scan (ordenadas por RSSI):           │
│                                                             │
│  ┌─ TENTATIVA 1: PMKID Fisher (padrao: 30s)                │
│  │  Tenta capturar PMKID sem precisar de clientes           │
│  │  ✓ Sucesso → salva, vai para PROXIMA rede               │
│  │  ✗ Falhou  → vai para tentativa 2                        │
│  │                                                          │
│  ├─ TENTATIVA 2: Deauth + Handshake (padrao: 60s)          │
│  │  Expulsa clientes, aguarda reconexao, captura handshake  │
│  │  ✓ Sucesso → salva, vai para PROXIMA rede               │
│  │  ✗ Falhou  → vai para tentativa 3                        │
│  │  (falha se nao tiver clientes conectados)                │
│  │                                                          │
│  └─ TENTATIVA 3: Evil Twin (padrao: 120s)                   │
│     Clone AP + Deauth + Portal Captive                      │
│     ✓ Sucesso → salva senha, vai para PROXIMA rede         │
│     ✗ Falhou  → marca como "nao capturada", proxima        │
└─────────────────────────────────────────────────────────────┘
```

### Configuracoes:

| Parametro | Padrao | O que faz |
|-----------|--------|-----------|
| `minRssi` | -75 dBm | Ignora redes com sinal mais fraco que esse valor (evita perder tempo em redes distantes) |
| `pmkidTimeout` | 30s | Tempo maximo na fase PMKID |
| `deauthTimeout` | 60s | Tempo maximo aguardando handshake |
| `eviltwinTimeout` | 120s | Tempo maximo no Evil Twin por rede |
| `skipNoClients` | false | Pula fase Deauth se nao detectar clientes (economiza tempo) |

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
→ { "current": "MinhaRede", "phase": "deauth", "progress": [...] }
```

---

## 11. Nuclear Auto-Attack

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
→ {
    "sniffer": { "handshakes": 3, "pmkids": 7 },
    "deauth": { "sent": 4821, "networks": 5 },
    "karma": { "clients": 2, "probes": 47 }
  }
```

---

## 12. Target Intel (Fingerprinting de Dispositivos)

### O que e?

Coleta informacoes detalhadas sobre dispositivos especificos conectados a uma rede. Funciona em modo monitor capturando trafego nao criptografado.

### O que captura:

| Dado | Como captura | O que revela |
|------|-------------|-------------|
| **Consultas DNS** | Pacotes UDP porta 53 (sem criptografia) | Quais sites o dispositivo visita |
| **User-Agent HTTP** | Headers de requisicoes HTTP | Sistema operacional, navegador, versao |
| **Credenciais HTTP** | POST de formularios em sites sem HTTPS | Logins e senhas (sites sem HTTPS) |
| **Fingerprint de SO** | Analise de padroes de trafego 802.11 | Windows / Android / iOS / macOS |
| **Horarios de atividade** | Timestamps dos pacotes | Padroes de uso do dispositivo |

### API:

```json
POST /api/intel/start
{
  "target": "AA:BB:CC:DD:EE:FF",
  "channel": 6
}

GET  /api/intel/data
→ {
    "dns": ["google.com", "instagram.com", "api.whatsapp.com"],
    "useragent": "Mozilla/5.0 (Android 13; Samsung Galaxy S21)",
    "os": "Android",
    "activity": [...]
  }
```

---

## 13. LED Panel Attack

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
POST /api/led/scan      → detecta controladores LED na rede

POST /api/led/attack
{
  "target": "192.168.1.100",
  "protocol": "wled",
  "mode": "flood"
}
```

---

## 14. BLE Scanner e Spam

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
→ [{ "name": "Fone JBL", "class": "Audio", "rssi": -62 }]
```

---

## 15. Fluxos de Ataque Completos

Exemplos de cenarios completos de auditoria do inicio ao fim.

---

### Fluxo A - Capturar senha WPA2 (com clientes na rede)

**Objetivo**: Obter o handshake WPA2 de uma rede que tem dispositivos conectados.

```
Passo 1 - Reconhecimento
  → WiFi Scanner
  → Identificar rede-alvo: SSID, BSSID, canal, clientes conectados
  → Confirmar sinal: RSSI acima de -70 dBm

Passo 2 - Posicionar o sniffer
  → Packet Sniffer → start
  → Fixar no canal do alvo (nao usar hopping)

Passo 3 - Forccar reconexao
  → Deauth Attack → start (broadcast ou cliente especifico)
  → Aguardar 10-30 segundos

Passo 4 - Verificar captura
  → Sniffer → ver handshakes capturados
  → Deve aparecer EAPOL M1, M2, M3, M4 do alvo

Passo 5 - Exportar e quebrar
  → Export Hashcat → salvar .hc22000
  → hashcat -m 22000 captura.hc22000 rockyou.txt
```

---

### Fluxo B - Capturar PMKID (sem clientes necessarios)

**Objetivo**: Obter o PMKID mesmo que ninguem esteja conectado na rede.

```
Passo 1 - Reconhecimento
  → WiFi Scanner
  → Identificar rede-alvo com WPA2

Passo 2 - PMKID Fisher
  → PMKID Fisher → start com o AP selecionado
  → Aguardar 10-30 segundos (geralmente e imediato)

Passo 3 - Exportar e quebrar
  → Se capturou: Export Hashcat
  → hashcat -m 22000 pmkid.hc22000 rockyou.txt -r best64.rule
```

---

### Fluxo C - Capturar senha via portal (usuario digita a senha)

**Objetivo**: Enganar o usuario para que ele mesmo forneça a senha.

```
Passo 1 - Reconhecimento
  → WiFi Scanner
  → Identificar rede-alvo, canal e clientes

Passo 2 - Evil Twin
  → Selecionar rede-alvo
  → Ativar Evil Twin com deauth ativado e template "reconnect"
  → Aguardar clientes se conectarem (pode levar 1-5 minutos)

Passo 3 - Monitorar
  → Painel mostra clientes conectados ao AP falso em tempo real
  → Quando o usuario tentar acessar qualquer site, vê o portal
  → Ao digitar a senha, ela aparece instantaneamente no painel

Passo 4 - Finalizar
  → Stop Evil Twin
  → Credenciais ficam salvas no painel
```

---

### Fluxo D - Auditoria completa rapida

**Objetivo**: Coletar o maximo de informacao de um ambiente em poco tempo.

```
Opcao 1 - Automatica (recomendado):
  → Nuclear Auto-Attack → start
  → Aguardar 5-15 minutos
  → Ver resultados: handshakes, PMKIDs, clientes Karma

Opcao 2 - Manual sequencial:
  → WiFi Scanner → identificar todas as redes
  → PMKID Fisher → testar cada AP em sequencia (30s cada)
  → Deauth + Sniffer → nas redes com clientes (60s cada)
  → Karma Attack → ativo durante toda a auditoria para capturar dispositivos vagantes
```

---

### Fluxo E - Capturar dispositivos vagantes (Karma)

**Objetivo**: Capturar dispositivos que estao procurando redes conhecidas sem estar conectados a nenhuma.

```
Passo 1 - Karma + MANA
  → Karma Attack → start com MANA ativado
  → O painel comeca a mostrar SSIDs que dispositivos estao buscando

Passo 2 - Monitorar capturas
  → Quando um dispositivo conectar, aparece no painel
  → Portal captive e exibido automaticamente

Passo 3 - Coletar credenciais
  → Usuario ve portal, digita senha do WiFi
  → Credencial aparece no painel
```

---

*Ghost Predator Mobile v4.6 - Para uso educacional e testes autorizados apenas.*
