# Ghost Predator Mobile — Guia de Instalacao Completo

> **AVISO LEGAL**: Use exclusivamente em redes e dispositivos que voce possui ou tem autorizacao explicita. Uso nao autorizado e crime (Lei 12.737/2012 no Brasil).

---

## Indice

1. [Requisitos de Hardware](#1-requisitos-de-hardware)
2. [Instalacao no Windows](#2-instalacao-no-windows)
3. [Instalacao no Linux](#3-instalacao-no-linux)
4. [Compilando o Firmware](#4-compilando-o-firmware)
5. [Gravando no ESP32-S3](#5-gravando-no-esp32-s3)
6. [Primeiro Acesso](#6-primeiro-acesso)
7. [Erros Comuns e Solucoes](#7-erros-comuns-e-solucoes)
8. [Atualizacao OTA (sem cabo)](#8-atualizacao-ota-sem-cabo)

---

## 1. Requisitos de Hardware

| Item | Descricao |
|------|-----------|
| **Placa** | ESP32-S3-WROOM-1-N16R8 (8MB Flash + 8MB PSRAM) |
| **Cabo USB** | USB-C para USB-A ou USB-C para USB-C |
| **Porta correta** | Use o cabo no USB-C **DIREITO** (CH343P serial) para gravar |
| **Fonte** | 5V via USB ou bateria LiPo 3.7V com conector JST-PH 2.0 |

> A placa tem **dois USB-C**:
> - **Esquerdo**: OTG/Host (GPIO 19/20) — para radio USB externo e console serial
> - **Direito**: CH343P serial — usado para gravar o firmware

---

## 2. Instalacao no Windows

### 2.1 Instalar Python 3.x

1. Acesse https://www.python.org/downloads/ e baixe o instalador
2. Na tela de instalacao, marque **"Add Python to PATH"** antes de clicar em Install
3. Verifique no terminal (CMD ou PowerShell):
   ```
   python --version
   ```
   Esperado: `Python 3.12.x` ou superior

### 2.2 Instalar PlatformIO

Abra o **Prompt de Comando** (cmd) ou **PowerShell** como usuario normal:

```bash
pip install platformio
```

Verifique:
```bash
pio --version
```
Se aparecer `PlatformIO Core, version X.X.X`, esta funcionando.

> **Se "pio" nao for reconhecido**: feche e abra o terminal novamente.
> Se ainda nao funcionar, adicione manualmente ao PATH:
> `C:\Users\SEU_USUARIO\AppData\Roaming\Python\Python3XX\Scripts`

### 2.3 Instalar o driver CH343P

O ESP32-S3 usa o chip serial **CH343P** (USB direito). Sem o driver, a porta COM nao aparece.

1. Baixe em: https://www.wch.cn/downloads/CH343SER_ZIP.html
2. Extraia e execute **SETUP.EXE**
3. Reinicie o Windows
4. Conecte o ESP32 pelo USB direito
5. Abra **Gerenciador de Dispositivos** (Win+X > Gerenciador)
6. Em "Portas (COM e LPT)" deve aparecer **"USB-Enhanced-SERIAL CH343 (COM5)"** (o numero pode variar)

> **Windows 10/11 modern**: na maioria dos casos o driver instala automaticamente via Windows Update quando conectado.

### 2.4 Instalar Git

1. Baixe em: https://git-scm.com/download/win
2. Instale com opcoes padrao
3. Verifique: `git --version`

### 2.5 Clonar o repositorio

```bash
git clone https://github.com/Ghost-Bx1/Ghost-Raptor-Mobile-ESP32.git
cd Ghost-Raptor-Mobile-ESP32
```

---

## 3. Instalacao no Linux

### 3.1 Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y python3 python3-pip git

# Instalar PlatformIO
pip3 install platformio

# Adicionar ao PATH se necessario
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Verificar
pio --version
```

### 3.2 Fedora / RHEL

```bash
sudo dnf install -y python3 python3-pip git
pip3 install --user platformio
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### 3.3 Arch Linux / Manjaro

```bash
sudo pacman -S python python-pip git
pip install --user platformio
```

### 3.4 Permissoes de porta serial

No Linux a porta serial fica em `/dev/ttyUSB0` ou `/dev/ttyACM0`.
Por padrao usuarios comuns nao tem acesso. Corrija com:

```bash
sudo usermod -a -G dialout $USER
sudo usermod -a -G plugdev $USER
```

**Faca logout e login novamente** para aplicar as permissoes.

Verifique se a porta aparece ao conectar o ESP32:
```bash
ls /dev/tty*
# ou
dmesg | tail -20
```

### 3.5 Driver CH343P no Linux

O kernel Linux 6.x ja inclui suporte ao CH343. Se nao reconhecer:

```bash
sudo modprobe ch341
# ou instale o driver do fabricante
```

### 3.6 Clonar o repositorio

```bash
git clone https://github.com/Ghost-Bx1/Ghost-Raptor-Mobile-ESP32.git
cd Ghost-Raptor-Mobile-ESP32
```

---

## 4. Compilando o Firmware

Dentro da pasta do projeto:

```bash
pio run -e esp32-s3
```

Na primeira execucao, o PlatformIO vai baixar automaticamente:
- Toolchain Xtensa GCC (compilador para ESP32-S3) ~300MB
- Framework Arduino ESP32
- Todas as bibliotecas (NimBLE, ESPAsyncWebServer, ArduinoJson, etc.)

Isso pode levar **5 a 15 minutos** dependendo da sua internet.

Sucesso esperado:
```
RAM:   [===       ]  26.9% (used 88xxx bytes from 327680 bytes)
Flash: [=====     ]  54.4% (used 1710xxx bytes from 3145728 bytes)
[SUCCESS] Took XX seconds
```

---

## 5. Gravando no ESP32-S3

### 5.1 Descobrir a porta

**Windows**: Gerenciador de Dispositivos > Portas (COM e LPT)
**Linux/Mac**: `ls /dev/tty*` (geralmente `ttyUSB0` ou `ttyACM0`)

### 5.2 Gravar o firmware

**Windows (porta COM5)**:
```bash
pio run -e esp32-s3 -t upload --upload-port COM5
```

**Linux (porta /dev/ttyUSB0)**:
```bash
pio run -e esp32-s3 -t upload --upload-port /dev/ttyUSB0
```

**Auto-detectar porta**:
```bash
pio run -e esp32-s3 -t upload
```

### 5.3 Modo de gravacao

O ESP32-S3 geralmente entra em modo bootloader automaticamente.
Se falhar com "connection refused" ou "timeout":
1. Segure o botao **BOOT** na placa
2. Pressione e solte **RST**
3. Solte **BOOT**
4. Execute o comando de upload novamente

### 5.4 Compilar + Gravar em um comando

```bash
pio run -e esp32-s3 -t upload --upload-port COM5
```

---

## 6. Primeiro Acesso

Apos gravar, o ESP32 reinicia e:

1. **LED RGB pisca verde** (inicializando) → depois fica **azul** (pronto)
2. Uma rede WiFi aparece nos seus dispositivos:
   - **SSID**: `GhostPredator`
   - **Senha**: `predator`
3. Conecte-se a essa rede
4. Abra o navegador em: **http://192.168.4.1**
5. Senha do painel web: `predator`

> **Console via USB-C esquerdo**: conecte o USB esquerdo ao PC e abra monitor serial em 115200 baud. Funciona simultaneamente com o USB direito.

---

## 7. Erros Comuns e Solucoes

### Erro: `Could not open COM5, the port is busy or doesn't exist`

**Causa**: ESP32 nao conectado, cabo errado, ou porta ocupada por outro programa.

**Solucao**:
- Verifique se o cabo esta no USB **DIREITO** (CH343P)
- Feche o monitor serial se estiver aberto
- No Gerenciador de Dispositivos, confirme o numero da porta COM
- Tente desconectar e reconectar o cabo

---

### Erro: `A fatal error occurred: Failed to connect to ESP32-S3: Timed out waiting for packet header`

**Causa**: ESP32 nao esta em modo bootloader.

**Solucao**: Entrar manualmente no bootloader:
1. Segure **BOOT**
2. Pressione e solte **RST**
3. Solte **BOOT**
4. Execute upload novamente imediatamente

---

### Erro: `pio nao e reconhecido` (Windows)

**Causa**: PlatformIO nao esta no PATH.

**Solucao**:
```bash
# Descubra onde o pip instalou
pip show platformio
# Adicione o diretorio Scripts ao PATH do Windows
# Ou use o caminho completo:
C:\Users\SEU_USUARIO\AppData\Roaming\Python\Python3XX\Scripts\pio.exe run -e esp32-s3
```

---

### Erro: `error: 'PSRAM' not found` ou crash no boot com `E (xxx) esp_psram: PSRAM ID read error`

**Causa**: PSRAM nao detectada ou tipo incorreto no `platformio.ini`.

**Solucao**: Verifique no `platformio.ini`:
```ini
board_build.arduino.memory_type = qio_opi
build_flags =
    -DBOARD_HAS_PSRAM=1
    -DCONFIG_SPIRAM_MODE_OCT=1
    -mfix-esp32-psram-cache-issue
```

---

### Erro: `Permission denied: '/dev/ttyUSB0'` (Linux)

**Causa**: Usuario sem permissao na porta serial.

**Solucao**:
```bash
sudo usermod -a -G dialout $USER
# Faca logout e login, depois:
pio run -e esp32-s3 -t upload --upload-port /dev/ttyUSB0
```

---

### Erro: `Brownout detector was triggered` no monitor serial

**Causa**: Fonte de energia insuficiente.

**Solucao**:
- Use cabo USB de qualidade
- Evite hubs USB sem alimentacao externa
- Prefira porta USB 3.0 traseira do PC

---

### LED RGB nao acende

**Causa**: GPIO errado no config.h.

**Solucao**: Edite `src/config.h` e troque o GPIO:
```cpp
#define RGB_PIN 38  // tente 38 se 48 nao funcionar
```

---

### Rede WiFi `GhostPredator` nao aparece

**Causa**: AP nao iniciou ou SSID errado.

**Verificacao**: Abra monitor serial (115200 baud) e procure por:
```
[WiFi] AP iniciado: GhostPredator
[WebServer] Servidor iniciado em 192.168.4.1
```
Se nao aparecer, reinicie com o botao RST.

---

### Compilacao demora mais de 30 minutos

**Causa**: Download do toolchain na primeira vez.

**Solucao**: E normal na primeira vez (~300MB de downloads). Subsequentes sao rapidas (cache local).

---

## 8. Atualizacao OTA (sem cabo)

Apos o primeiro flash, voce pode atualizar via WiFi:

1. Compile o firmware: `pio run -e esp32-s3`
2. O arquivo gerado e: `.pio/build/esp32-s3/firmware.bin`
3. Conecte ao WiFi `GhostPredator`
4. Acesse: http://192.168.4.1 > menu **Sistema** > **Atualizar Firmware**
5. Selecione o arquivo `firmware.bin` e aguarde o upload
6. O dispositivo reinicia automaticamente com o novo firmware

---

## Resumo Rapido

```bash
# Windows — instalar tudo do zero
pip install platformio
git clone https://github.com/Ghost-Bx1/Ghost-Raptor-Mobile-ESP32.git
cd Ghost-Raptor-Mobile-ESP32
pio run -e esp32-s3 -t upload --upload-port COM5

# Linux — instalar tudo do zero
pip3 install platformio
sudo usermod -a -G dialout $USER
git clone https://github.com/Ghost-Bx1/Ghost-Raptor-Mobile-ESP32.git
cd Ghost-Raptor-Mobile-ESP32
pio run -e esp32-s3 -t upload --upload-port /dev/ttyUSB0
```

**WiFi**: `GhostPredator` / Senha: `predator`  
**Painel web**: http://192.168.4.1 / Senha: `predator`
