#!/bin/bash
# Ghost Predator Mobile — Instalador Inteligente (Linux / macOS)

set -e

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[1;33m'
CYN='\033[0;36m'; MAG='\033[0;35m'; WHT='\033[1;37m'; NC='\033[0m'

banner() {
    clear
    echo ""
    echo -e "${MAG}  ██████╗ ██╗  ██╗ ██████╗ ███████╗████████╗${NC}"
    echo -e "${MAG}  ██╔════╝██║  ██║██╔═══██╗██╔════╝╚══██╔══╝${NC}"
    echo -e "${MAG}  ██║  ███╗███████║██║   ██║███████╗   ██║   ${NC}"
    echo -e "${MAG}  ██║   ██║██╔══██║██║   ██║╚════██║   ██║   ${NC}"
    echo -e "${MAG}  ╚██████╔╝██║  ██║╚██████╔╝███████║   ██║   ${NC}"
    echo -e "${MAG}   ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝   ╚═╝  ${NC}"
    echo ""
    echo -e "${CYN}  PREDATOR MOBILE v3.3 — Instalador Automatico${NC}"
    echo -e "\033[90m  ─────────────────────────────────────────────${NC}"
    echo ""
}

ok()   { echo -e "${GRN}  ✔  $1${NC}"; }
warn() { echo -e "${YEL}  ⚠  $1${NC}"; }
err()  { echo -e "${RED}  ✘  $1${NC}"; exit 1; }
step() { echo -e "${CYN}  [$1] $2${NC}"; }

# Verifica PlatformIO
if ! command -v pio &>/dev/null; then
    err "PlatformIO nao encontrado. Instale: pip install platformio"
fi

banner

# ── 1. Detectar porta ─────────────────────────────────────────────────────────
step 1 "Detectando ESP32..."

PORT=""
CHIP_USB=""

if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    for dev in /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem*; do
        [[ -e "$dev" ]] && PORT="$dev" && break
    done
else
    # Linux
    for dev in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
        [[ -e "$dev" ]] && PORT="$dev" && break
    done
fi

# Identifica chip USB-serial pelo nome do dispositivo
if [[ -n "$PORT" ]]; then
    DEV_INFO=$(ls -la /sys/class/tty/$(basename $PORT)/device/../ 2>/dev/null || dmesg 2>/dev/null | tail -30)
    if   echo "$DEV_INFO" | grep -qi "ch34"; then CHIP_USB="CH340/CH341"
    elif echo "$DEV_INFO" | grep -qi "cp21"; then CHIP_USB="CP210x"
    elif echo "$DEV_INFO" | grep -qi "ch910"; then CHIP_USB="CH9102"
    elif echo "$DEV_INFO" | grep -qi "ft232"; then CHIP_USB="FT232"
    else CHIP_USB="USB-Serial"
    fi
fi

if [[ -z "$PORT" ]]; then
    warn "Nenhum ESP32 detectado automaticamente."
    echo ""
    echo "  Dispositivos disponiveis:"
    ls /dev/ttyUSB* /dev/ttyACM* /dev/cu.usb* 2>/dev/null | sed 's/^/    /'
    echo ""
    read -p "  Digite a porta (ex: /dev/ttyUSB0): " PORT
    CHIP_USB="Manual"
fi

ok "Porta: $PORT | Chip USB: $CHIP_USB"

# ── 2. Identificar chip ESP32 ─────────────────────────────────────────────────
step 2 "Identificando chip ESP32..."

ENV="esp32dev"
CHIP_ESP="ESP32 (WROOM/WROVER)"

if command -v esptool.py &>/dev/null || python3 -m esptool --help &>/dev/null 2>&1; then
    RAW=$(python3 -m esptool --port "$PORT" chip_id 2>&1 || true)
    if   echo "$RAW" | grep -q "ESP32-S3"; then ENV="esp32-s3"; CHIP_ESP="ESP32-S3"
    elif echo "$RAW" | grep -q "ESP32-S2"; then err "ESP32-S2 nao suportado (sem BLE). Use ESP32 ou ESP32-S3."
    elif echo "$RAW" | grep -q "ESP32-C3"; then ENV="esp32-c3"; CHIP_ESP="ESP32-C3"
    elif echo "$RAW" | grep -q "ESP32";    then ENV="esp32dev"; CHIP_ESP="ESP32 (WROOM/WROVER)"
    fi
fi

ok "Chip: $CHIP_ESP | Ambiente: $ENV"

# ── 3. Confirmar ──────────────────────────────────────────────────────────────
echo ""
echo -e "\033[90m  ┌──────────────────────────────────────────┐${NC}"
printf  "  │  Porta    : %-28s │\n" "$PORT"
printf  "  │  Chip USB : %-28s │\n" "$CHIP_USB"
printf  "  │  ESP32    : %-28s │\n" "$CHIP_ESP"
printf  "  │  Ambiente : %-28s │\n" "$ENV"
echo -e "\033[90m  └──────────────────────────────────────────┘${NC}"
echo ""
read -p "  Prosseguir? (S/n): " CONFIRM
[[ "$CONFIRM" =~ ^[Nn] ]] && echo "  Cancelado." && exit 0

# ── 4. Filesystem ─────────────────────────────────────────────────────────────
step 3 "Gravando filesystem (interface web)..."
pio run --environment "$ENV" --target uploadfs --upload-port "$PORT" || err "Falha ao gravar filesystem."
ok "Filesystem gravado!"

# ── 5. Firmware ───────────────────────────────────────────────────────────────
step 4 "Compilando e gravando firmware..."
pio run --environment "$ENV" --target upload --upload-port "$PORT" || err "Falha ao gravar firmware."

# ── 6. Conclusão ──────────────────────────────────────────────────────────────
echo ""
echo -e "${GRN}  ╔══════════════════════════════════════════╗${NC}"
echo -e "${GRN}  ║       INSTALACAO CONCLUIDA!              ║${NC}"
echo -e "${GRN}  ╠══════════════════════════════════════════╣${NC}"
echo -e "${WHT}  ║  WiFi  :  GhostPredator / predator123   ║${NC}"
echo -e "${WHT}  ║  URL   :  http://ghostpredator.local     ║${NC}"
echo -e "${WHT}  ║  IP    :  http://192.168.4.1             ║${NC}"
echo -e "${WHT}  ║  Senha :  predator                       ║${NC}"
echo -e "${GRN}  ╚══════════════════════════════════════════╝${NC}"
echo ""
