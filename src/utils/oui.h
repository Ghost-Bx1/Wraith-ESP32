#pragma once
/*
 * OUI Lookup — Identificação de fabricante por MAC address
 * Melhoria #11: tabela comprimida dos OUIs mais comuns (offline, sem internet)
 */
#include <Arduino.h>
#include <pgmspace.h>

namespace OUI {

struct Entry {
    uint32_t prefix; // 3 bytes OUI em uint32 (big-endian)
    const char* name;
};

// Top fabricantes + IoT + roteadores comuns (brasileiros incluídos)
static const Entry TABLE[] PROGMEM = {
    {0xD86162,"Apple"},    {0xA45E60,"Apple"},    {0xF0D1A9,"Apple"},
    {0x9CB370,"Apple"},    {0xE0B9BA,"Apple"},    {0x3C22FB,"Apple"},
    {0x781D4A,"Apple"},    {0x8C858B,"Apple"},    {0x00174F,"Apple"},

    {0x8C1645,"Samsung"}, {0x8CF5A3,"Samsung"},  {0xA0B4A5,"Samsung"},
    {0xF4F5E8,"Samsung"}, {0x5495D2,"Samsung"},  {0x84B541,"Samsung"},

    {0xC0EE40,"Google"},  {0xDA6C38,"Google"},   {0x04D3B0,"Google"},

    {0x50C7BF,"TP-Link"}, {0x84D9C1,"TP-Link"},  {0x54AF97,"TP-Link"},
    {0x6466B3,"TP-Link"}, {0xC0254D,"TP-Link"},  {0xF0A731,"TP-Link"},

    {0x74DA38,"Intelbras"},{0xFC5475,"Intelbras"},{0x001077,"Intelbras"},

    {0xE8D8D1,"Xiaomi"},  {0x000028,"Xiaomi"},   {0x7CC3A1,"Xiaomi"},
    {0xF4F0D3,"Xiaomi"},  {0x28D1274,"Xiaomi"},

    {0xCC2D8C,"Huawei"},  {0x001E10,"Huawei"},   {0x48FD8E,"Huawei"},
    {0xF4CB52,"Huawei"},  {0x44004D,"Huawei"},

    {0x685F74,"Amazon"},  {0xFC65DE,"Amazon"},   {0x44650D,"Amazon"},

    {0xD8D43C,"Motorola"},{0x00125A,"Motorola"},
    {0x34D270,"LG"},      {0x1C98EC,"LG"},
    {0x50AF73,"Sony"},    {0x001A75,"Sony"},
    {0xA44CC8,"Lenovo"},  {0x54EEE8,"Lenovo"},
    {0x8832CE,"Dell"},    {0xF8B156,"Dell"},
    {0x3085A9,"HP"},      {0xC4346B,"HP"},
    {0xE0CB1D,"Asus"},    {0x2C56DC,"Asus"},

    {0x000C29,"VMware"},  {0x080027,"VirtualBox"},
    {0xDEB8D9,"Raspberry Pi"}, {0xDCA632,"Raspberry Pi"},
    {0x2C549A,"Qualcomm"},{0x001B63,"Broadcom"},

    // Roteadores domésticos
    {0x686F2D,"Cisco"},   {0x001A2F,"Cisco"},    {0x6480D2,"Netgear"},
    {0x20E52A,"Netgear"}, {0x20CF30,"D-Link"},   {0xB0487A,"D-Link"},
    {0x00C0CA,"Alfa"},    {0x00904C,"Alfa"},

    // Câmeras IP
    {0x001CC0,"Hikvision"},{0xBC5451,"Hikvision"},{0x482C6A,"Dahua"},
    {0x3C4973,"Dahua"},

    // IoT genérico
    {0xAC67B2,"Espressif"},{0x240AC4,"Espressif"},{0xDCA6AB,"Espressif"},
    {0x30AEA4,"Espressif"},{0xB4E62D,"Espressif"},{0x94B97E,"Espressif"},
};

static const int TABLE_SIZE = sizeof(TABLE) / sizeof(Entry);

// Retorna nome do fabricante ou "Unknown"
inline const char* lookup(const uint8_t mac[6]) {
    uint32_t prefix = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
    for (int i = 0; i < TABLE_SIZE; i++) {
        if (pgm_read_dword(&TABLE[i].prefix) == prefix)
            return (const char*)pgm_read_ptr(&TABLE[i].name);
    }
    return "Unknown";
}

// Retorna true se o MAC é localmente administrado (randomizado — privacidade iOS/Android)
inline bool isRandomized(const uint8_t mac[6]) {
    return (mac[0] & 0x02) != 0; // bit U/L = 1
}

// Retorna string do tipo de dispositivo baseado no MAC e flags
inline String describe(const uint8_t mac[6], bool isApple = false, bool isAndroid = false) {
    if (isRandomized(mac)) return String("Randomized (private)");
    String vendor = lookup(mac);
    return vendor;
}

} // namespace OUI
