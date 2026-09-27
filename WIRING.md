# Pinagem / Wiring Guide

## ESP32 DevKit v1 (38 pinos)

```
                    ┌─────────────────────┐
               3.3V │ 3V3            GND  │ GND
                EN  │ EN             GPIO23│
             GPIO36 │ VP             GPIO22│
             GPIO39 │ VN             GPIO1 │ TX0
             GPIO34 │ GPIO34         GPIO3 │ RX0
             GPIO35 │ GPIO35         GPIO21│
             GPIO32 │ GPIO32         GND  │
             GPIO33 │ GPIO33         GPIO19│
             GPIO25 │ GPIO25         GPIO18│
             GPIO26 │ GPIO26         GPIO5 │
             GPIO27 │ GPIO27         GPIO17│ ← UART2 TX  (→ ESP8266 RX)
             GPIO14 │ GPIO14         GPIO16│ ← UART2 RX  (← ESP8266 TX)
             GPIO12 │ GPIO12         GPIO4 │
               GND  │ GND            GPIO2 │ ← LED STATUS (onboard)
             GPIO13 │ GPIO13         GPIO15│
              GPIO9 │ SD2            GPIO8 │ SD3
             GPIO10 │ SD3            GPIO7 │ CMD
             GPIO11 │ CMD            GPIO6 │ CLK
                5V  │ 5V             GPIO0 │
                    └─────────────────────┘
```

---

## Conexão ESP32 → ESP8266 (Segundo Rádio WiFi)

| ESP32 Pin | Fio   | ESP8266 Pin | Descrição            |
|-----------|-------|-------------|----------------------|
| GPIO17    | →     | RX (GPIO3)  | ESP32 TX → ESP8266 RX |
| GPIO16    | ←     | TX (GPIO1)  | ESP8266 TX → ESP32 RX |
| GND       | ─     | GND         | Terra compartilhado   |
| 3.3V      | →     | VCC (3.3V)  | Alimentação          |

> ⚠️ **Atenção:** O ESP8266 opera em 3.3V. **NÃO** conecte ao pino 5V.  
> O ESP8266 deve estar com firmware AT (AT v1.7+) para funcionar com o Radio2.

```
ESP32                       ESP8266 (NodeMCU / bare module)
┌──────────┐               ┌──────────┐
│ GPIO17   │──────────────▶│ RX       │
│ GPIO16   │◀──────────────│ TX       │
│ GND      │───────────────│ GND      │
│ 3.3V     │───────────────│ VCC      │
└──────────┘               └──────────┘
```

---

## LED de Status (GPIO2)

| Estado                  | Padrão           |
|-------------------------|------------------|
| Tudo inativo            | LED fixo ligado  |
| Sniffer ativo           | Pisca 150ms      |
| Deauth / Beacon / Twin  | Pisca 800ms      |
| Boot                    | LED apagado      |

---

## Alimentação

| Fonte       | Tensão | Corrente mín. | Observação                    |
|-------------|--------|---------------|-------------------------------|
| USB (micro) | 5V     | 500 mA        | Suficiente para ESP32 sozinho |
| USB + ESP8266 | 5V   | 800 mA        | Use USB 3.0 ou hub ativo      |
| LiPo (bateria) | 3.7V | 1A+          | Via pino VIN + regulador 3.3V |

---

## Acesso ao Painel

1. Conecte no WiFi: **`ESP32-Pentest`** / senha: **`pentest123`**
2. Abra: **`http://cluster.local`** ← mDNS (recomendado)
3. Ou: **`http://192.168.4.1`** ← IP direto (fallback)
