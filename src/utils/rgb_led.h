#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "../config.h"

/*
 * RGB LED — WS2812B onboard (GPIO 48 no ESP32-S3 DevKitC-1)
 *
 * Estados:
 *   IDLE      — azul fraco pulsando (sistema pronto)
 *   BOOT      — branco fadeando na inicialização
 *   ATTACK    — vermelho piscando rápido (ataque em andamento)
 *   FOUND     — verde pulsando (handshake/credencial capturada)
 *   ERROR     — laranja piscando lento
 *   SCAN      — ciano girando (sniffer ativo)
 *   CLONE     — roxo pulsando (AP Clone ativo)
 */
class RGBLed {
public:
    static RGBLed& get() { static RGBLed inst; return inst; }

    enum State { IDLE, BOOT, ATTACK, FOUND, ERROR_STATE, SCAN, CLONE };

    void begin() {
        _px.begin();
        _px.setBrightness(RGB_BRIGHTNESS);
        _px.clear();
        _px.show();
        // Sequência de teste: R→G→B para confirmar que o pino é correto
        // Se você não ver as 3 cores piscando, altere RGB_LED_PIN em config.h
        _px.setPixelColor(0, _color(200, 0, 0)); _px.show(); delay(200);
        _px.setPixelColor(0, _color(0, 200, 0)); _px.show(); delay(200);
        _px.setPixelColor(0, _color(0, 0, 200)); _px.show(); delay(200);
        _px.clear(); _px.show();
        setState(BOOT);
    }

    void setState(State s) {
        if (_state == s) return;
        _state = s;
        _phase = 0;
        _lastTick = 0;
    }

    // Pisca verde brevemente (disparo único quando captura algo)
    void flashFound() {
        _flashCount = 4;
        _flashColor = _color(0, 255, 80);
        _flashOn    = true;
        _flashNext  = millis();
    }

    // Pisca laranja brevemente (erro pontual)
    void flashError() {
        _flashCount = 3;
        _flashColor = _color(255, 80, 0);
        _flashOn    = true;
        _flashNext  = millis();
    }

    void tick() {
        unsigned long now = millis();

        // Flashes têm prioridade sobre o estado normal
        if (_flashCount > 0) {
            if (now >= _flashNext) {
                _flashOn = !_flashOn;
                _px.setPixelColor(0, _flashOn ? _flashColor : 0);
                _px.show();
                _flashNext = now + 80;
                if (!_flashOn) _flashCount--;
            }
            return;
        }

        if (now - _lastTick < 16) return; // ~60fps max
        _lastTick = now;

        switch (_state) {
            case BOOT:     _animFade(255, 255, 255, 1200); break;
            case IDLE:     _animPulse(0, 40, 120, 2000);  break;
            case ATTACK:   _animBlink(220, 0, 0, 150);    break;
            case FOUND:    _animPulse(0, 200, 60, 800);   break;
            case ERROR_STATE: _animBlink(255, 80, 0, 400); break;
            case SCAN:     _animChase(0, 180, 200, 600);  break;
            case CLONE:    _animPulse(120, 0, 200, 1000); break;
        }
    }

private:
    Adafruit_NeoPixel _px{RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800};
    State         _state     = IDLE;
    uint32_t      _phase     = 0;
    unsigned long _lastTick  = 0;

    // Flash one-shot
    int           _flashCount = 0;
    uint32_t      _flashColor = 0;
    bool          _flashOn    = false;
    unsigned long _flashNext  = 0;

    static uint32_t _color(uint8_t r, uint8_t g, uint8_t b) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    // Pisca on/off com período dado em ms
    void _animBlink(uint8_t r, uint8_t g, uint8_t b, uint32_t period) {
        _phase = (_phase + 1) % (period * 2 / 16);
        bool on = (_phase < (period / 16));
        _px.setPixelColor(0, on ? _color(r, g, b) : 0);
        _px.show();
    }

    // Pulsa brilho (sine-like via triangle wave)
    void _animPulse(uint8_t r, uint8_t g, uint8_t b, uint32_t period) {
        uint32_t steps = period / 16;
        _phase = (_phase + 1) % (steps * 2);
        uint32_t t = (_phase < steps) ? _phase : (steps * 2 - _phase);
        uint8_t bright = (uint8_t)(t * 255 / steps);
        _px.setPixelColor(0, _color(r * bright / 255, g * bright / 255, b * bright / 255));
        _px.show();
    }

    // Fade in de um único LED (útil no boot)
    void _animFade(uint8_t r, uint8_t g, uint8_t b, uint32_t duration) {
        uint32_t steps = duration / 16;
        if (_phase < steps) {
            uint8_t bright = (uint8_t)(_phase * 255 / steps);
            _px.setPixelColor(0, _color(r * bright / 255, g * bright / 255, b * bright / 255));
            _px.show();
            _phase++;
        } else {
            setState(IDLE); // boot terminado → vai para idle
        }
    }

    // Animação de "chase" com único LED usando hue cycling
    void _animChase(uint8_t r, uint8_t g, uint8_t b, uint32_t period) {
        uint32_t steps = period / 16;
        _phase = (_phase + 1) % steps;
        // Alterna entre a cor e metade dela para simular movimento
        bool bright = (_phase % 4) < 2;
        _px.setPixelColor(0, bright ? _color(r, g, b) : _color(r/3, g/3, b/3));
        _px.show();
    }
};
