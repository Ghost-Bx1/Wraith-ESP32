#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include "../utils/logger.h"

struct BLEDevInfo {
    String address;
    String name;
    int    rssi;
    String serviceUUIDs;
    String manufacturerData;
    bool   connectable;
    unsigned long firstSeen;
    unsigned long lastSeen;
};

class BLEScannerCb : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* dev) override;
};

class BLEScanner {
public:
    static BLEScanner& get() { static BLEScanner inst; return inst; }

    bool running = false;
    std::vector<BLEDevInfo> devices;

    void begin() {
        NimBLEDevice::init("ESP32-PenTest");
        _pScan = NimBLEDevice::getScan();
        _pScan->setAdvertisedDeviceCallbacks(&_cb, true);
        _pScan->setActiveScan(true);
        _pScan->setInterval(97);
        _pScan->setWindow(37);
        _pScan->setMaxResults(0);
        LOG.info("BLE (NimBLE) inicializado");
    }

    void startScan(int seconds = 5) {
        devices.clear();
        running = true;
        LOG.info("BLE scan " + String(seconds) + "s...");
        _pScan->start(seconds, _scanDone, false);
    }

    void stopScan() {
        _pScan->stop();
        running = false;
    }

    void addDevice(NimBLEAdvertisedDevice* d) {
        String addr = String(d->getAddress().toString().c_str());
        for (auto& dev : devices) {
            if (dev.address == addr) {
                dev.lastSeen = millis();
                dev.rssi     = d->getRSSI();
                return;
            }
        }
        BLEDevInfo info;
        info.address     = addr;
        info.name        = d->haveName() ? d->getName().c_str() : "";
        info.rssi        = d->getRSSI();
        info.connectable = d->isConnectable();
        info.firstSeen   = millis();
        info.lastSeen    = info.firstSeen;
        if (d->haveServiceUUID())
            info.serviceUUIDs = d->getServiceUUID().toString().c_str();
        if (d->haveManufacturerData()) {
            auto raw = d->getManufacturerData();
            String hex = "";
            for (uint8_t b : raw) { char buf[3]; sprintf(buf, "%02X", b); hex += buf; }
            info.manufacturerData = hex;
        }
        devices.push_back(info);
        LOG.data("BLE: " + addr + " \"" + info.name + "\" " + String(info.rssi) + "dBm");
    }

    String enumerate(const String& addr) {
        LOG.info("Conectando BLE a " + addr);
        NimBLEClient* client = NimBLEDevice::createClient();
        client->setConnectTimeout(5);
        if (!client->connect(NimBLEAddress(addr.c_str()))) {
            LOG.error("Falha BLE connect: " + addr);
            NimBLEDevice::deleteClient(client);
            return "{\"error\":\"connect failed\"}";
        }
        String result = "{\"addr\":\"" + addr + "\",\"services\":[";
        bool first = true;
        auto svcs = client->getServices(true);
        for (auto* svc : *svcs) {
            if (!first) result += ",";
            first = false;
            result += "\"" + String(svc->getUUID().toString().c_str()) + "\"";
            auto chars = svc->getCharacteristics(true);
            for (auto* ch : *chars) {
                String val = "";
                if (ch->canRead()) {
                    NimBLEAttValue v = ch->readValue();
                    for (size_t i = 0; i < v.size() && i < 32; i++) {
                        char buf[3]; sprintf(buf, "%02X", v.data()[i]); val += buf;
                    }
                }
                String props = "";
                if (ch->canRead())    props += "R";
                if (ch->canWrite())   props += "W";
                if (ch->canNotify())  props += "N";
                if (ch->canIndicate()) props += "I";
                LOG.data("  CHAR " + String(ch->getUUID().toString().c_str()) +
                         " [" + props + "] val:" + val);
            }
        }
        result += "]}";
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        LOG.success("Enum BLE concluido: " + addr);
        return result;
    }

private:
    NimBLEScan*  _pScan = nullptr;
    BLEScannerCb _cb;

    static void _scanDone(NimBLEScanResults results) {
        BLEScanner::get().running = false;
        LOG.success("BLE scan done: " + String(BLEScanner::get().devices.size()) + " devs");
    }
};

inline void BLEScannerCb::onResult(NimBLEAdvertisedDevice* dev) {
    BLEScanner::get().addDevice(dev);
}
