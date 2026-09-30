#include <WiFi.h>
#include "BleManager.h"
#include "StateManager.h"
#include "StickerSosManager.h"

BleManager bleMgr;

void BleManager::init() {
  // 初期設定値
  spotName = "Unregistered";
  distributeStickerId = "st_110";
  lastSyncTime = "None";

  // MACアドレスの下位4バイトから一意のデバイスIDを生成
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char idBuf[16];
  snprintf(idBuf, sizeof(idBuf), "M5-%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
  deviceId = String(idBuf);

  // BLEデバイス・サーバーの初期化
  String bleDeviceName = "COCO-" + deviceId;
  NimBLEDevice::init(bleDeviceName.c_str());

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this);

  // GATTサービス & キャラクタリスティック構築
  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  // 設定用（アプリからの読み書き）
  pConfigChar = pService->createCharacteristic(
    CHAR_CONFIG_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
  );
  pConfigChar->setCallbacks(this);

  // ログ送信・ステータス通知用（READ/NOTIFY）
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