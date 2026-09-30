#pragma once
#include <NimBLEDevice.h>
#include "Config.h"

class BleManager : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
private:
  NimBLEServer* pServer = nullptr;
  
  // 設定・ログ・ステータス
  NimBLECharacteristic* pConfigChar = nullptr;
  NimBLECharacteristic* pLogChar = nullptr;
  NimBLECharacteristic* pStatusChar = nullptr;
  
  bool deviceConnected = false;

public:
  // 設定値・保持データ
  String deviceId;
  String spotName;
  String distributeStickerId;
  String lastSyncTime;

  // 初期化 & 基本操作
  void init();
  void updateStatus();
  void sendLogsToApp(const String& jsonLogs);
  String getTimestamp();

  // BLEイベントコールバック
  void onConnect(NimBLEServer* pServer) override;
  void onDisconnect(NimBLEServer* pServer) override;
  void onWrite(NimBLECharacteristic* pCharacteristic) override;
};

// 外部参照
extern BleManager bleMgr;