# Wraith - Instalador Inteligente (Windows PowerShell)
# Detecta automaticamente o ESP32, chip USB-serial e porta COM

$PIO = "$env:APPDATA\Python\Python314\Scripts\pio.exe"
if (-not (Test-Path $PIO)) {
    $PIO = (Get-Command pio -ErrorAction SilentlyContinue)?.Source
}
if (-not $PIO) {
    Write-Host ""
    Write-Host "  [ERRO] PlatformIO nao encontrado." -ForegroundColor Red
    Write-Host "  Instale com: pip install platformio" -ForegroundColor Yellow
    exit 1
}

function Banner {
    Clear-Host
    Write-Host ""
    Write-Host "  ██████╗ ██╗  ██╗ ██████╗ ███████╗████████╗" -ForegroundColor Magenta
    Write-Host "  ██╔════╝██║  ██║██╔═══██╗██╔════╝╚══██╔══╝" -ForegroundColor Magenta
    Write-Host "  ██║  ███╗███████║██║   ██║███████╗   ██║   " -ForegroundColor Magenta
    Write-Host "  ██║   ██║██╔══██║██║   ██║╚════██║   ██║   " -ForegroundColor Magenta
    Write-Host "  ╚██████╔╝██║  ██║╚██████╔╝███████║   ██║   " -ForegroundColor Magenta
    Write-Host "   ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝   ╚═╝   " -ForegroundColor Magenta
    Write-Host ""
    Write-Host "  WRAITH v5.0 - Instalador Automatico" -ForegroundColor Cyan
    Write-Host "  ─────────────────────────────────────────────" -ForegroundColor DarkGray
    Write-Host ""
}

function Step($n, $msg) {
    Write-Host "  [$n] $msg" -ForegroundColor Cyan
}

function Ok($msg)   { Write-Host "  ✔  $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "  ⚠  $msg" -ForegroundColor Yellow }
function Err($msg)  { Write-Host "  ✘  $msg" -ForegroundColor Red }

# ── 1. Detectar porta COM ─────────────────────────────────────────────────────
Banner
Step 1 "Detectando ESP32..."

$usbSerialChips = @("CH340","CH341","CP210","CH9102","FT232","Silicon Labs","USB Serial","USB-SERIAL")
$port = $null
$chipName = ""

$ports = Get-WmiObject Win32_PnPEntity | Where-Object {
    $_.Name -match "COM\d+" -and ($usbSerialChips | Where-Object { $_.Name -match $_ })
}

foreach ($p in $ports) {
    $comMatch = [regex]::Match($p.Name, "COM(\d+)")
    if ($comMatch.Success) {
        $port = "COM" + $comMatch.Groups[1].Value
        if     ($p.Name -match "CH340|CH341") { $chipName = "CH340/CH341" }
        elseif ($p.Name -match "CH9102")       { $chipName = "CH9102" }
        elseif ($p.Name -match "CP210")        { $chipName = "CP210x" }
        elseif ($p.Name -match "FT232")        { $chipName = "FT232" }
        elseif ($p.Name -match "Silicon Labs") { $chipName = "CP210x (Silicon Labs)" }
        else                                    { $chipName = "Desconhecido" }
        break
    }
}

if (-not $port) {
    Warn "Nenhum ESP32 detectado automaticamente."
    Write-Host ""
    Write-Host "  Portas COM disponiveis:" -ForegroundColor DarkGray
    Get-WmiObject Win32_PnPEntity | Where-Object { $_.Name -match "COM\d+" } | ForEach-Object {
        Write-Host "    $($_.Name)" -ForegroundColor Gray
    }
    Write-Host ""
    $port = Read-Host "  Digite a porta manualmente (ex: COM4)"
    $chipName = "Manual"
}

Ok "Porta: $port | Chip USB-Serial: $chipName"

# ── 2. Identificar chip ESP32 via esptool ─────────────────────────────────────
Step 2 "Identificando chip ESP32..."

$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$python  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"

$chipInfo = ""
$env_name = "esp32dev"   # padrão
$chipDisplay = "ESP32 (detectando...)"

if (Test-Path $esptool) {
    try {
        $raw = & $python $esptool --port $port chip_id 2>&1 | Out-String
        if     ($raw -match "ESP32-S3") { $env_name = "esp32-s3"; $chipDisplay = "ESP32-S3" }
        elseif ($raw -match "ESP32-S2") { $env_name = "esp32-s2"; $chipDisplay = "ESP32-S2 (nao suportado)" }
        elseif ($raw -match "ESP32-C3") { $env_name = "esp32-c3"; $chipDisplay = "ESP32-C3" }
        elseif ($raw -match "ESP32")    { $env_name = "esp32dev"; $chipDisplay = "ESP32 (WROOM/WROVER)" }
    } catch { }
}

if ($chipDisplay -match "nao suportado") {
    Err "ESP32-S2 nao e suportado por este firmware (sem BLE)."
    Write-Host "  Use ESP32 original, ESP32-S3 ou ESP32-C3." -ForegroundColor Yellow
    exit 1
}

Ok "Chip: $chipDisplay | Ambiente: $env_name"

# ── 3. Confirmar e gravar ─────────────────────────────────────────────────────
Write-Host ""
Write-Host "  ┌──────────────────────────────────────────────┐" -ForegroundColor DarkGray
Write-Host "  │  Porta    : $($port.PadRight(35))│" -ForegroundColor White
Write-Host "  │  Chip USB : $($chipName.PadRight(35))│" -ForegroundColor White
Write-Host "  │  ESP32    : $($chipDisplay.PadRight(35))│" -ForegroundColor White
Write-Host "  │  Ambiente : $($env_name.PadRight(35))│" -ForegroundColor White
Write-Host "  └──────────────────────────────────────────────┘" -ForegroundColor DarkGray
Write-Host ""

$confirm = Read-Host "  Prosseguir com a instalacao? (S/n)"
if ($confirm -match "^[Nn]") { Write-Host "  Cancelado." -ForegroundColor Yellow; exit 0 }

# ── 4. Gravar filesystem ──────────────────────────────────────────────────────
Step 3 "Gravando filesystem (interface web)..."
& $PIO run --environment $env_name --target uploadfs --upload-port $port
if ($LASTEXITCODE -ne 0) { Err "Falha ao gravar filesystem."; exit 1 }
Ok "Filesystem gravado!"

# ── 5. Compilar e gravar firmware ─────────────────────────────────────────────
Step 4 "Compilando e gravando firmware..."
& $PIO run --environment $env_name --target upload --upload-port $port
if ($LASTEXITCODE -ne 0) { Err "Falha ao gravar firmware."; exit 1 }

# ── 6. Conclusão ──────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "  ╔══════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "  ║         INSTALACAO CONCLUIDA!                ║" -ForegroundColor Green
Write-Host "  ╠══════════════════════════════════════════════╣" -ForegroundColor Green
Write-Host "  ║  WiFi  :  Wraith / predator123        ║" -ForegroundColor White
Write-Host "  ║  URL   :  http://wraith.local         ║" -ForegroundColor White
Write-Host "  ║  IP    :  http://192.168.4.1                 ║" -ForegroundColor White
Write-Host "  ║  Senha :  predator                           ║" -ForegroundColor White
Write-Host "  ╚══════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
