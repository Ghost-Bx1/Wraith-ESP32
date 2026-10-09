#pragma once
/*
 * Monitor de tensão da bateria via ADC — Melhoria #34
 * Lê tensão de uma célula LiPo (4.2V max) via divisor resistivo no GPIO ADC.
 * Configurar BATTERY_PIN em config.h para o GPIO do ADC.
 * Divisor recomendado: 100kΩ + 100kΩ → VBat / 2 → ADC (max ~2.1V no ADC)
 */
#include <Arduino.h>
#include "../config.h"

#ifdef BATTERY_PIN

#include <driver/adc.h>

class BatteryMonitor {
public:
    static BatteryMonitor& get() { static BatteryMonitor inst; return inst; }

    // Nível: 0-100%
    int level = -1;        // -1 = não inicializado
    float voltage = 0.0f;  // tensão em V
    bool charging = false; // detecção de carga (se BATTERY_CHG_PIN definido)

    void begin() {
        pinMode(BATTERY_PIN, INPUT);
        analogSetAttenuation(ADC_11db);  // range 0-3.3V
        analogSetWidth(12);              // 12-bit (0-4095)
#ifdef BATTERY_CHG_PIN
        pinMode(BATTERY_CHG_PIN, INPUT_PULLUP);
#endif
        update();
    }

    // Chama a cada 10 segundos no loop
    void update() {
        // Média de 16 leituras para reduzir ruído
        uint32_t raw = 0;
        for (int i = 0; i < 16; i++) raw += analogRead(BATTERY_PIN);
        raw /= 16;

        // Converte para tensão (divisor 1:2, ADC 3.3V ref, 12-bit)
        voltage = (raw / 4095.0f) * 3.3f * 2.0f;

        // Clamp para range LiPo (3.0V–4.2V)
        voltage = constrain(voltage, 3.0f, 4.2f);

        // Mapeia para percentual (curva aproximada LiPo)
        if (voltage >= 4.15f)      level = 100;
        else if (voltage >= 4.0f)  level = (int)(80 + (voltage - 4.0f) / 0.15f * 20);
        else if (voltage >= 3.7f)  level = (int)(20 + (voltage - 3.7f) / 0.30f * 60);
        else if (voltage >= 3.0f)  level = (int)((voltage - 3.0f) / 0.70f * 20);
        else                       level = 0;

#ifdef BATTERY_CHG_PIN
        charging = (digitalRead(BATTERY_CHG_PIN) == LOW);
#endif
    }

    String statusJson() const {
        return "{\"batt_mv\":" + String((int)(voltage * 1000)) +
               ",\"batt_pct\":" + String(level) +
               ",\"charging\":" + String(charging ? "true" : "false") + "}";
    }
};

#endif // BATTERY_PIN
