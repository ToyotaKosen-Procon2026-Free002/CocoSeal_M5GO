#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <ArduinoJson.h>
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
  distributeStickerId = "st_110";
  lastSyncTime = "None";
  gatewayRegistrationPending = false;
  wifiConnectionPending = false;
  wifiConnectionInProgress = false;

  Preferences prefs;
  prefs.begin("gateway_cfg", false);
  distributeStickerName = prefs.getString("sticker_name", "");
  spotName = prefs.getString("spot_name", "Unregistered");

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

  if (payload.startsWith("SPOT:")) {
    spotName = payload.substring(5);
    Preferences prefs;
    prefs.begin("gateway_cfg", false);
    prefs.putString("spot_name", spotName);
    prefs.end();
  } else if (payload.startsWith("STICKER:")) {
    distributeStickerName = payload.substring(8);
    Preferences prefs;
    prefs.begin("gateway_cfg", false);
    prefs.putString("sticker_name", distributeStickerName);
    prefs.end();
  } else if (payload == "GET_LOGS") {
    stickerSosMgr.flushLogsToBle();
  } else if (payload.startsWith("SSID:")) {
    pendingWifiSsid = payload.substring(5);
  } else if (payload.startsWith("PASS:")) {
    pendingWifiPass = payload.substring(5);
  }

  if (!pendingWifiSsid.isEmpty() && !pendingWifiPass.isEmpty()) {
    Preferences prefs;
    prefs.begin("gateway_cfg", false);
    prefs.putString("wifi_ssid", pendingWifiSsid);
    prefs.putString("wifi_pass", pendingWifiPass);
    prefs.end();

    wifiConnectionPending = true;
  }

  lastSyncTime = "App Synced";
  updateStatus();
  StateManager::changeState(STATE_IDLE);
}

void BleManager::processPendingTasks() {
  if (wifiConnectionPending) {
    wifiConnectionPending = false;

    Serial.printf("[WiFi] Connecting to SSID: %s\n", pendingWifiSsid.c_str());
    WiFi.disconnect();
    WiFi.begin(pendingWifiSsid.c_str(), pendingWifiPass.c_str());
    wifiConnectionStartedAt = millis();
    wifiConnectionInProgress = true;
  }

  if (wifiConnectionInProgress) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnectionInProgress = false;
      Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
      // Wi-Fi接続成功時に時刻同期（SSL通信用）を実施
      configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
      struct tm timeinfo;
      unsigned long ntpStart = millis();
      while (!getLocalTime(&timeinfo) && millis() - ntpStart < 3000) {
        delay(200);
      }
      if (!getLocalTime(&timeinfo)) {
        struct timeval tv = { 1791500000, 0 };
        settimeofday(&tv, NULL);
      }

      uint8_t ch = WiFi.channel();
      esp_wifi_set_promiscuous(true);
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(false);
      gatewayRegistrationPending = true;
    } else if (millis() - wifiConnectionStartedAt >= 60000) {
      wifiConnectionInProgress = false;
      Serial.println("[WiFi] Connection failed within 60 seconds.");
      WiFi.disconnect();
    }

    if (!wifiConnectionInProgress) {
      pendingWifiSsid = "";
      pendingWifiPass = "";
      updateStatus();
    }
  }

  if (!gatewayRegistrationPending) return;
  gatewayRegistrationPending = false;

  if (WiFi.status() != WL_CONNECTED) return;

  serverApiMgr.registerGateway(deviceId, spotName);
  updateStatus();
}

void BleManager::updateStatus() {
  if (!pStatusChar) return;

  JsonDocument statusDoc;
  statusDoc["station_id"] = deviceId;
  statusDoc["spot_name"] = spotName;
  statusDoc["distribute_sticker_id"] = distributeStickerId;
  statusDoc["distribute_sticker_name"] = distributeStickerName;
  statusDoc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  String status;
  serializeJson(statusDoc, status);

  pStatusChar->setValue((uint8_t*)status.c_str(), status.length());
  if (deviceConnected) {
    pStatusChar->notify();
  }
}

bool BleManager::sendLogsToApp(const String& jsonLogs) {
  if (!pLogChar || !deviceConnected) return false;
  pLogChar->setValue((uint8_t*)jsonLogs.c_str(), jsonLogs.length());
  pLogChar->notify();
  return true;
}