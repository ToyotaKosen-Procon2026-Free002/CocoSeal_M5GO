#include <WiFi.h>
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

  // UUID の読み出し / 初回生成
  Preferences prefs;
  prefs.begin("gateway_cfg", false);

  bool isNewDevice = false;

  if (prefs.isKey("gateway_id")) {
    // 過去に保存されたUUIDがあればそれを取得
    deviceId = prefs.getString("gateway_id");
    Serial.println("\n[NVS] Loaded existing Gateway UUID from Flash Memory.");
  } else {
    // 初回起動時：新しいUUIDを生成してNVSに書き込み保存
    deviceId = generateUUID();
    prefs.putString("gateway_id", deviceId);
    isNewDevice = true;
    Serial.println("\n[NVS] No UUID found. Generated & saved NEW Gateway UUID!");
  }
  prefs.end();

  // ターミナル出力
  Serial.println("==============================================");
  Serial.printf("  Gateway Device UUID:\n  %s\n", deviceId.c_str());
  Serial.println("==============================================\n");
  Serial.flush();

  // 初回生成時の場合、Wi-Fi接続があればサーバー(DB)に初回登録 (POST /devices/gateway)
  if (isNewDevice) {
    if (WiFi.status() == WL_CONNECTED) {
      bool registered = serverApiMgr.registerGateway(deviceId, spotName);
      if (registered) {
        Serial.println("[API] Successfully registered new Gateway UUID to DB!");
      } else {
        Serial.println("[API] Failed to register Gateway to DB. Will retry on next boot or sync.");
      }
    } else {
      Serial.println("[API] Wi-Fi not connected. DB registration skipped for now.");
    }
  }

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

 // アドバタイズ開始
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  NimBLEDevice::startAdvertising();
}

String BleManager::getTimestamp() {
  // 起動からの経過時間を文字列として返す
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
  NimBLEDevice::startAdvertising(); // 切断時は自動で再アドバタイズ
}

void BleManager::onWrite(NimBLECharacteristic* pCharacteristic) {
  std::string val = pCharacteristic->getValue();
  if (val.length() > 0) {
    String payload = String(val.c_str());
    
    // 受信コマンドの解析
    if (payload.startsWith("STICKER:")) {
      distributeStickerId = payload.substring(8);
    } else if (payload.startsWith("SPOT:")) {
      spotName = payload.substring(5);
    } else if (payload == "GET_LOGS") {
      stickerSosMgr.flushLogsToBle();
    }

    lastSyncTime = "iPad Synced";
    updateStatus();
    StateManager::changeState(STATE_IDLE);
  }
}

void BleManager::updateStatus() {
  if (!pStatusChar) return;
  
  // 現在の設定状態をJSONでNotify通知
  String status = "{\"station_id\":\"" + deviceId + "\",\"spot_name\":\"" + spotName + "\",\"distribute_sticker_id\":\"" + distributeStickerId + "\"}";
  pStatusChar->setValue((uint8_t*)status.c_str(), status.length());
  pStatusChar->notify();
}

void BleManager::sendLogsToApp(const String& jsonLogs) {
  if (!pLogChar || !deviceConnected) return;
  
  // 蓄積されたログデータをアプリへNotify送信
  pLogChar->setValue((uint8_t*)jsonLogs.c_str(), jsonLogs.length());
  pLogChar->notify();
}