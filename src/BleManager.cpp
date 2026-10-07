#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <Preferences.h>
#include "BleManager.h"
#include "StateManager.h"
#include "StickerSosManager.h"
#include "ServerApiManager.h"

BleManager bleMgr;

// UUID v4 を生成する関数
String generateUUID() {
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint32_t r3 = esp_random();
  uint32_t r4 = esp_random();

  char uuidBuf[37];
  snprintf(uuidBuf, sizeof(uuidBuf),
           "%08x-%04x-4%03x-%04x-%08x%04x",
           r1,
           (uint16_t)(r2 >> 16),
           (uint16_t)(r2 & 0x0FFF),
           (uint16_t)((r3 >> 16) & 0x3FFF) | 0x8000,
           r3,
           (uint16_t)(r4 & 0xFFFF));

  return String(uuidBuf);
}

void BleManager::init() {
  // 初期設定値
  spotName = "Unregistered";
  distributeStickerId = "st_110";
  lastSyncTime = "None";
  gatewayRegistrationPending = false;

  // UUID の読み出し / 初回生成
  Preferences prefs;
  prefs.begin("gateway_cfg", false);

  if (prefs.isKey("gateway_id")) {
    deviceId = prefs.getString("gateway_id");
    Serial.println("\n[NVS] Loaded existing Gateway UUID from Flash Memory.");
  } else {
    deviceId = generateUUID();
    prefs.putString("gateway_id", deviceId);

    Serial.println("\n[NVS] No UUID found. Generated & saved NEW Gateway UUID!");
  }

  prefs.end();

  // ターミナル出力
  Serial.println("==============================================");
  Serial.printf("  Gateway Device UUID:\n  %s\n", deviceId.c_str());
  Serial.println("==============================================\n");
  Serial.flush();

  // BLEデバイス・サーバーの初期化
  String bleDeviceName = "COCO-" + deviceId.substring(0, 8);
  NimBLEDevice::init(bleDeviceName.c_str());

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this);

  // GATTサービス & キャラクタリスティック構築
  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pConfigChar = pService->createCharacteristic(
    CHAR_CONFIG_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
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

  // アドバタイズ開始
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  NimBLEDevice::startAdvertising();
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

  if (val.length() == 0) {
    return;
  }

  String payload = String(val.c_str());

  static String wifiSsid = "";
  static String wifiPass = "";

  // スポット名
  if (payload.startsWith("SPOT:")) {
    spotName = payload.substring(5);
  }

  // 配布ステッカー
  else if (payload.startsWith("STICKER:")) {
    distributeStickerId = payload.substring(8);
  }

  // ログ取得
  else if (payload == "GET_LOGS") {
    stickerSosMgr.flushLogsToBle();
  }

  // Wi-Fi SSID
  else if (payload.startsWith("SSID:")) {
    wifiSsid = payload.substring(5);

    Serial.printf(
      "[BLE] Received SSID: %s\n",
      wifiSsid.c_str()
    );
  }

  // Wi-Fi Password
  else if (payload.startsWith("PASS:")) {
    wifiPass = payload.substring(5);

    Serial.println("[BLE] Received Password.");
  }

  // SSID と PASS が揃った
  if (wifiSsid.length() > 0 && wifiPass.length() > 0) {

    Serial.println(
      "[WiFi] Connecting with received credentials..."
    );

    // NVSに保存
    Preferences prefs;

    prefs.begin("gateway_cfg", false);

    prefs.putString("wifi_ssid", wifiSsid);
    prefs.putString("wifi_pass", wifiPass);

    prefs.end();

    // Wi-Fi接続
    WiFi.disconnect();
    WiFi.begin(
      wifiSsid.c_str(),
      wifiPass.c_str()
    );

    unsigned long startTime = millis();

    // 最大15秒待つ
    while (
      WiFi.status() != WL_CONNECTED &&
      millis() - startTime < 15000
    ) {
      delay(200);
    }

    Serial.printf(
      "[WiFi] Final status: %d\n",
      WiFi.status()
    );

    Serial.printf(
      "[WiFi] Local IP: %s\n",
      WiFi.localIP().toString().c_str()
    );

    if (WiFi.status() == WL_CONNECTED) {

      uint8_t ch = WiFi.channel();

      Serial.printf(
        "[WiFi] Connected! Channel: %d\n",
        ch
      );

      // ESP-NOWのチャンネルをWi-Fiに合わせる
      esp_wifi_set_promiscuous(true);

      esp_wifi_set_channel(
        ch,
        WIFI_SECOND_CHAN_NONE
      );

      esp_wifi_set_promiscuous(false);

      // ★重要
      // ここではDB通信しない。
      // loop()側で登録させる。
      gatewayRegistrationPending = true;

      Serial.println(
        "[Gateway] Registration queued."
      );

    } else {

      Serial.println(
        "[WiFi] Connection failed. Resetting inputs."
      );
    }

    // 一時保持をクリア
    wifiSsid = "";
    wifiPass = "";
  }

  lastSyncTime = "iPad Synced";

  updateStatus();

  StateManager::changeState(STATE_IDLE);
}


// ========================================
// loop()から実行される処理
// ========================================

void BleManager::processPendingTasks() {

  if (!gatewayRegistrationPending) {
    return;
  }

  // フラグを先に下げて二重実行防止
  gatewayRegistrationPending = false;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(
      "[Gateway] Registration cancelled: Wi-Fi disconnected."
    );

    return;
  }

  Serial.println(
    "[Gateway] Starting DB registration outside BLE callback..."
  );

  // ★ここならnimble_hostのコールバック外
  serverApiMgr.registerGateway(
    deviceId,
    spotName
  );

  Serial.println(
    "[Gateway] DB registration finished."
  );

  updateStatus();
}


void BleManager::updateStatus() {
  if (!pStatusChar) {
    return;
  }

  String status =
    "{\"station_id\":\"" +
    deviceId +
    "\",\"spot_name\":\"" +
    spotName +
    "\",\"distribute_sticker_id\":\"" +
    distributeStickerId +
    "\",\"wifi_connected\":" +
    (WiFi.status() == WL_CONNECTED ? "true" : "false") +
    "}";

  pStatusChar->setValue(
    (uint8_t*)status.c_str(),
    status.length()
  );

  // BLE接続中だけnotify
  if (deviceConnected) {
    pStatusChar->notify();
  }
}


void BleManager::sendLogsToApp(
  const String& jsonLogs
) {

  if (!pLogChar || !deviceConnected) {
    return;
  }

  pLogChar->setValue(
    (uint8_t*)jsonLogs.c_str(),
    jsonLogs.length()
  );

  pLogChar->notify();
}