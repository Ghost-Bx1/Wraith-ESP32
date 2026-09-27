# Upload do filesystem LittleFS para o ESP32
# Requer PlatformIO instalado

Write-Host "=== Upload Firmware + Filesystem ===" -ForegroundColor Cyan

# 1. Compila e faz upload do firmware
Write-Host "`n[1/3] Compilando firmware..." -ForegroundColor Yellow
pio run

# 2. Gera imagem LittleFS e faz upload
Write-Host "`n[2/3] Gerando e fazendo upload do filesystem (UI web)..." -ForegroundColor Yellow
pio run --target uploadfs

# 3. Upload do firmware
Write-Host "`n[3/3] Upload do firmware..." -ForegroundColor Yellow
pio run --target upload

Write-Host "`n=== Concluído! ===" -ForegroundColor Green
Write-Host "Conecte ao WiFi: ESP32-Pentest" -ForegroundColor Green
Write-Host "Senha: pentest123" -ForegroundColor Green
Write-Host "Acesse: http://192.168.4.1" -ForegroundColor Green
