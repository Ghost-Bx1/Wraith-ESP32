# Ghost Predator Mobile — Plano de Expansão de Hardware

## Situação Atual (v3.3)

| Componente | Chip | Função |
|---|---|---|
| ESP32 (WROOM-32) | Xtensa LX6 dual-core 240MHz | MCU principal |
| CH340C | USB ↔ UART bridge | Programação/debug via USB |
| Rádio 2.4GHz integrado | 802.11 b/g/n | WiFi + BLE |

**Limitação crítica**: O CH340C é apenas um conversor serial. O ESP32 original **não tem controlador USB Host** — ele não consegue se comunicar diretamente com dispositivos USB (adaptadores WiFi, pendrives, etc.).

---

## Fase 1 — Curto Prazo (sem soldagem, só software)

### 1.1 SD Card via SPI (expansão de armazenamento)
**Objetivo**: salvar PMKIDs, handshakes e logs em SD card

**Pinos SPI do ESP32**:
```
SD MOSI  → GPIO23
SD MISO  → GPIO19
SD CLK   → GPIO18
SD CS    → GPIO5
SD VCC   → 3.3V
SD GND   → GND
```

**Biblioteca**: `SD.h` (Arduino) ou `SdFat`

**Benefícios**:
- Salvar wordlists para cracking offline
- Log persistente de capturas
- Exportar arquivos hashcat diretamente

### 1.2 Config persistence (LittleFS já existente)
Salvar configurações (senha, modo, canal) em `/config.json` via LittleFS.

---

## Fase 2 — Médio Prazo (expansão USB via MAX3421E)

### 2.1 MAX3421E — SPI to USB Host Controller

**O que é**: chip que implementa USB Host via SPI. Permite ao ESP32 atuar como host USB.

**Suporta**: USB Full Speed (12 Mbps) — compatível com os adaptadores WiFi listados abaixo.

**Pinagem** (ESP32 SPI1 / HSPI):
```
MAX3421E MOSI  → GPIO13
MAX3421E MISO  → GPIO12
MAX3421E SCLK  → GPIO14
MAX3421E CS    → GPIO15
MAX3421E INT   → GPIO4
MAX3421E RST   → GPIO2
MAX3421E VCC   → 3.3V
MAX3421E GND   → GND
```

**Biblioteca**: [felis/USB_Host_Shield_2.0](https://github.com/felis/USB_Host_Shield_2.0) portada para ESP32

**Custo estimado**: MAX3421E ≈ R$15-20 no AliExpress

### 2.2 Adaptadores WiFi USB compatíveis (suas placas)

| Placa | Chip | USB PID | Monitor Mode | Injeção |
|---|---|---|---|---|
| Intelbras IWA 3000 (x2) | Realtek RTL8188EUS | 2357:0109 | ✅ | ✅ |
| White USB dongle | Realtek RTL8188 (provável) | variado | ✅ | ✅ |

**Limitação**: Drivers Realtek no ESP32 precisarão de port customizado do `rtl8188eu` para o MAX3421E. Trabalho significativo.

**Alternativa mais fácil**: usar os adaptadores USB com um SBC (Single Board Computer) como Raspberry Pi.

### 2.3 Mini PCIe → USB conversion

As suas placas mini PCIe (Dell DW1703, WN530, Liteon WN6301L) **não são nativamente USB** — são PCIe + USB combo ou PCIe puro:

| Placa | Interface real | Chip |
|---|---|---|
| Dell DW1703 | USB 2.0 (embutido no conector mPCIe) | Broadcom BCM43142 |
| WN530 | PCIe | Ralink RT5390 |
| Liteon WN6301L | USB 2.0 (pins 36/38 do mPCIe) | Ralink RT3290 |

**Para Dell DW1703 e WN6301L**: extraia os pinos USB do conector mPCIe:
```
mPCIe Pin 36 → USB D-
mPCIe Pin 38 → USB D+
mPCIe Pin 2  → VCC 3.3V
mPCIe Pin 1  → GND
```
Conecte diretamente ao MAX3421E.

**Para WN530** (PCIe puro): requer adaptador mPCIe-to-USB (existem breakout boards no AliExpress ~R$8).

---

## Fase 3 — Longo Prazo (migração para ESP32-S3)

### 3.1 Por que ESP32-S3?

| Feature | ESP32 original | ESP32-S3 |
|---|---|---|
| USB OTG nativo | ❌ | ✅ (USB 2.0 Full Speed) |
| USB Host nativo | ❌ | ✅ |
| RAM | 520KB | 512KB + PSRAM até 8MB |
| Clock | 240MHz | 240MHz |
| AI/DSP | ❌ | ✅ (Vector instructions) |

**Com ESP32-S3**, conecte diretamente:
```
USB D-  → GPIO19
USB D+  → GPIO20
VCC     → 3.3V
GND     → GND
```

**Sem necessidade de MAX3421E**. Conecte qualquer hub USB e todos os adaptadores de uma vez.

### 3.2 Setup completo ESP32-S3 + USB hub

```
ESP32-S3
    ↓ USB OTG
USB Hub (4 portas)
    ├── Intelbras IWA 3000 #1 (sniff/inject canal 1-7)
    ├── Intelbras IWA 3000 #2 (sniff/inject canal 8-13)
    ├── Dell DW1703 (Bluetooth 4.0 + WiFi)
    └── Liteon WN6301L (segunda frequência)
```

**Resultado**: 4 interfaces WiFi simultâneas → monitoramento multi-canal + injeção + AP falso tudo ao mesmo tempo.

---

## Fase 4 — Sistema Completo com Raspberry Pi Zero 2W

Se quiser o máximo de potência com os seus hardwares existentes:

```
Raspberry Pi Zero 2W
    ├── SD card 32GB (OS: Kali Linux armhf)
    ├── USB OTG → Hub USB
    │       ├── Intelbras IWA 3000 #1 (wlan1 — monitor mode)
    │       ├── Intelbras IWA 3000 #2 (wlan2 — injection)
    │       └── Dell DW1703 (wlan3 — bluetooth + AP)
    └── GPIO UART → ESP32 (para BLE spam + raw 802.11 injection)
```

**Ferramentas disponíveis no RPi**:
- `aircrack-ng` / `airmon-ng` / `aireplay-ng`
- `hashcat` (CPU, lento mas funcional)
- `wifite` (ataque automatizado)
- `hostapd-wpe` (Evil Twin avançado com WPA Enterprise)
- `bettercap` (MitM completo com HSTS stripping)
- `reaver` / `pixiewps` (WPS bruteforce)
- `mdk4` (beacon flood, deauth, WPA michael attack)

**RPi Zero 2W**: tem 512MB RAM, quad-core ARM Cortex-A53 — roda Kali leve.

**SD Card**: Raspberry Pi Zero 2W **requer** boot via SD. Use cartão microSD 32GB Classe 10 com Kali Linux ARM.

---

## Lista de Materiais (BOM)

### Fase 1 (já disponível / barato)
| Item | Qtd | Custo aprox. |
|---|---|---|
| Módulo SD SPI (micro SD breakout) | 1 | R$8 |
| Jumpers fêmea-fêmea | 10 | R$5 |

### Fase 2 (USB Host via MAX3421E)
| Item | Qtd | Custo aprox. |
|---|---|---|
| MAX3421E breakout board | 1 | R$20 |
| USB Type-A socket | 1-4 | R$5 |
| Hub USB passivo | 1 | R$15 |

### Fase 3 (ESP32-S3 upgrade)
| Item | Qtd | Custo aprox. |
|---|---|---|
| ESP32-S3-DevKitC-1 | 1 | R$45 |
| USB Hub powered 4 portas | 1 | R$25 |

### Fase 4 (RPi completo)
| Item | Qtd | Custo aprox. |
|---|---|---|
| Raspberry Pi Zero 2W | 1 | R$120 |
| microSD 32GB Classe 10 | 1 | R$30 |
| USB OTG hub | 1 | R$25 |
| Cabos micro USB | 2 | R$10 |

---

## Roadmap de Software por Fase

### Fase 2 (ESP32 + MAX3421E)
- [ ] Port da biblioteca USB_Host_Shield_2.0 para ESP32
- [ ] Driver básico CDC/HID USB
- [ ] Inicialização de adaptador Realtek RTL8188
- [ ] Modo monitor via USB (MLME commands)
- [ ] Frame injection via USB adapter

### Fase 3 (ESP32-S3)
- [ ] Migrar firmware para ESP32-S3 (board: esp32s3dev)
- [ ] Implementar USB Host stack (TinyUSB ou ESP-IDF USB)
- [ ] Multi-radio coordenado (canal fixo em cada interface)
- [ ] PSRAM para buffers maiores (capturas longas)

### Fase 4 (RPi + ESP32 coordenados)
- [ ] API REST no RPi para receber comandos da UI web do ESP32
- [ ] ESP32 como frontend BLE/raw injection, RPi como backend pesado
- [ ] Hashcat automático ao capturar PMKID/handshake
- [ ] Dashboard unificado (ESP32 serve HTML, RPi processa capturas)

---

*Ghost Predator Mobile — Plano gerado automaticamente em 2026-09-27*
