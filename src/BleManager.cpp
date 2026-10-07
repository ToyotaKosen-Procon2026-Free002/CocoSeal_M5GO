#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include "BleManager.h"
#include "StateManager.h"
#include "StickerSosManager.h"
#include "ServerApiManager.h"

BleManager bleMgr;

// 大文字 UUID v4 生成関数
String generateUUID() {
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint32_t r3 = esp_random();
  uint32_t r4 = esp_random();

  char uuidBuf[37];
  snprintf(uuidBuf, sizeof(uuidBuf),
           "%08X-%04X-4%03X-%04X-%08X%04X",
           r1,
           (uint16_t)(r2 >> 16),
           (uint16_t)(r2 & 0x0FFF),
           (uint16_t)((r3 >> 16) & 0x3FFF) | 0x8000,
           r3,
           (uint16_t)(r4 & 0xFFFF));

  return String(uuidBuf);
}

void BleManager::init() {
  spotName = "Unregistered";
  distributeStickerId = "st_110";
  lastSyncTime = "None";
  gatewayRegistrationPending = false;

  Preferences prefs;
  prefs.begin("gateway_cfg", false);

  // NVSに保存済みのUUIDがあれば読み込み、無ければ新規生成する
  if (prefs.isKey("gateway_id")) {
    deviceId = prefs.getString("gateway_id");
  } else {
    deviceId = generateUUID();
    prefs.putString("gateway_id", deviceId);
  }
  prefs.end();

  Serial.println("==============================================");
  Serial.printf("  Gateway Device UUID:\n  %s\n", deviceId.c_str());
  Serial.println("==============================================\n");

  String bleDeviceName = "COCO-" + deviceId.substring(0, 8);
  NimBLEDevice::init(bleDeviceName.c_str());
  NimBLEDevice::setMTU(185);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this);

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pConfigChar = pService->createCharacteristic(
    CHAR_CONFIG_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pConfigChar->setCallbacks(this);

  pLogChar = pService->createCharacteristic(
    CHAR_LOG_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pStatusChar = pService->createCharacteristic(
    CHAR_STATUS_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);

  NimBLEDevice::startAdvertising();
  Serial.printf("[BLE] Advertising started as: %s\n", bleDeviceName.c_str());
}

String BleManager::getTimestamp() {
  unsigned long sec = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "+%lu sec", sec);
  return String(buf);
}

void BleManager::onConnect(NimBLEServer* pServer) {
  deviceConnected = true;
  StateManager::changeState(STATE_BLE_CONNECTED);
  updateStatus();
}

void BleManager::onDisconnect(NimBLEServer* pServer) {
  deviceConnected = false;
  StateManager::changeState(STATE_IDLE);
  NimBLEDevice::startAdvertising();
}

void BleManager::onWrite(NimBLECharacteristic* pCharacteristic) {
  std::string val = pCharacteristic->getValue();
  if (val.length() == 0) return;

  String payload = String(val.c_str());
  static String wifiSsid = "";
  static String wifiPass = "";

  if (payload.startsWith("SPOT:")) {
    spotName = payload.substring(5);
  } else if (payload.startsWith("STICKER:")) {
    distributeStickerId = payload.substring(8);
  } else if (payload == "GET_LOGS") {
    stickerSosMgr.flushLogsToBle();
  } else if (payload.startsWith("SSID:")) {
    wifiSsid = payload.substring(5);
  } else if (payload.startsWith("PASS:")) {
    wifiPass = payload.substring(5);
  }

  if (wifiSsid.length() > 0 && wifiPass.length() > 0) {
    Preferences prefs;
    prefs.begin("gateway_cfg", false);
    prefs.putString("wifi_ssid", wifiSsid);
    prefs.putString("wifi_pass", wifiPass);
    prefs.end();

    WiFi.disconnect();
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
      delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
      // Wi-Fi接続成功時に時刻同期（SSL通信用）を実施
      configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
      struct tm timeinfo;
      unsigned long ntpStart = millis();
      while (!getLocalTime(&timeinfo) && millis() - ntpStart < 3000) {
        delay(200);
      }
      if (!getLocalTime(&timeinfo)) {
        // NTP失敗時は2026年10月の時刻を自動セット
        struct timeval tv = { 1791500000, 0 };
        settimeofday(&tv, NULL);
      }

      uint8_t ch = WiFi.channel();
      esp_wifi_set_promiscuous(true);
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(false);

      gatewayRegistrationPending = true;
    }

    wifiSsid = "";
    wifiPass = "";
  }

  lastSyncTime = "App Synced";
  updateStatus();
  StateManager::changeState(STATE_IDLE);
}

void BleManager::processPendingTasks() {
  if (!gatewayRegistrationPending) return;
  gatewayRegistrationPending = false;

  if (WiFi.status() != WL_CONNECTED) return;

  serverApiMgr.registerGateway(deviceId, spotName);
  updateStatus();
}

void BleManager::updateStatus() {
  if (!pStatusChar) return;

  String status =
    "{\"station_id\":\"" + deviceId +
    "\",\"spot_name\":\"" + spotName +
    "\",\"distribute_sticker_id\":\"" + distributeStickerId +
    "\",\"wifi_connected\":" + (WiFi.status() == WL_CONNECTED ? "true" : "false") +
    "}";

  pStatusChar->setValue((uint8_t*)status.c_str(), status.length());
  if (deviceConnected) {
    pStatusChar->notify();
  }
}

void BleManager::sendLogsToApp(const String& jsonLogs) {
  if (!pLogChar || !deviceConnected) return;
  pLogChar->setValue((uint8_t*)jsonLogs.c_str(), jsonLogs.length());
  pLogChar->notify();
}