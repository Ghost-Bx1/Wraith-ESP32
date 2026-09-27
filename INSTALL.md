# Instalação e Build

## Requisitos

- [Python 3.x](https://python.org) — necessário para PlatformIO
- [PlatformIO Core](https://platformio.org/install/cli)
- Driver USB-Serial CH340 ([download WCH](https://www.wch-ic.com/downloads/CH341SER_EXE.html))
- Git

## 1. Instalar PlatformIO

```bash
pip install platformio
```

Verifique:
```bash
pio --version
```

## 2. Clonar o repositório

```bash
git clone https://github.com/SEU_USUARIO/CLUSTER.git
cd CLUSTER
```

## 3. Compilar firmware

```bash
pio run
```

Saída esperada:
```
RAM:   [==        ]  17.3% (used 56796 bytes from 327680 bytes)
Flash: [====      ]  37.1% (used ~1.1MB from 3MB)
[SUCCESS]
```

## 4. Gravar firmware no ESP32

Descubra a porta COM:

**Windows:**
```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

**Linux/Mac:**
```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

Grave:
```bash
pio run --target upload --upload-port COM3     # Windows
pio run --target upload --upload-port /dev/ttyUSB0  # Linux
```

## 5. Gravar filesystem (Web UI)

```bash
pio run --target uploadfs --upload-port COM3
```

## 6. Monitor serial (debug)

```bash
pio device monitor --port COM3 --baud 115200
```

Saída esperada no boot:
```
=== CLUSTER v3.0 ===
[+] FS OK
[+] AP: ESP32-Pentest → 192.168.4.1
[+] mDNS: http://cluster.local
[+] Web server ativo
[+] BLE (NimBLE) inicializado
[+] Pronto!
```

## 7. Acessar o painel

1. Conecte no WiFi **`ESP32-Pentest`** (senha: `pentest123`)
2. Abra **`http://cluster.local`** ou **`http://192.168.4.1`**

---

## Troubleshooting

| Problema | Solução |
|---|---|
| `pio` não encontrado | Adicione `~/.local/bin` (Linux) ou `%APPDATA%\Python\Scripts` (Windows) ao PATH |
| `Could not open COMx` | Instale driver CH340; verifique Device Manager |
| `LittleFS mount falhou` | Normal na primeira vez — formata automaticamente |
| Web UI não carrega | Certifique-se de ter rodado `uploadfs` |
| `cluster.local` não resolve | Use `192.168.4.1` diretamente; mDNS pode não funcionar no Android |
| BLE não encontra dispositivos | Certifique-se de que o Bluetooth do cliente de teste está ligado |
