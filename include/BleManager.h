#pragma once
#include <NimBLEDevice.h>
#include "Config.h"

class BleManager : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
private:
  NimBLEServer* pServer = nullptr;

  NimBLECharacteristic* pConfigChar = nullptr;
  NimBLECharacteristic* pLogChar = nullptr;
  NimBLECharacteristic* pStatusChar = nullptr;

  bool deviceConnected = false;
  bool gatewayRegistrationPending = false;
  bool wifiConnectionPending = false;
  bool wifiConnectionInProgress = false;
  unsigned long wifiConnectionStartedAt = 0;
  String pendingWifiSsid;
  String pendingWifiPass;

public:
  // 仮想デストラクタを追加
  virtual ~BleManager() = default;

  // 設定値・保持データ
  String deviceId;
  String spotName;
  String distributeStickerId;
  String distributeStickerName;
  String lastSyncTime;

  // 初期化 & 基本操作
  void init();
  void updateStatus();
  bool sendLogsToApp(const String& jsonLogs);
  String getTimestamp();

  // loop() から呼び出す
  void processPendingTasks();

  // BLEイベントコールバック
  void onConnect(NimBLEServer* pServer) override;
  void onDisconnect(NimBLEServer* pServer) override;
  void onWrite(NimBLECharacteristic* pCharacteristic) override;
};

// 外部参照
extern BleManager bleMgr;