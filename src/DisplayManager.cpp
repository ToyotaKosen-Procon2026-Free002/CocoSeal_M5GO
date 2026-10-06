#include "DisplayManager.h"
#include "StateManager.h"
#include "BleManager.h"
#include "StickerSosManager.h"

DisplayManager displayMgr;

// 右上にバッテリー残量を表示
void drawBatteryStatus() {
  int bat = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging();

  if (bat < 0) bat = 0;
  if (bat > 100) bat = 100;

  // 1行目の右端にバッテリーを表示
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(210, 10);

  if (charging) {
    M5.Lcd.setTextColor(GREEN, BLACK);
    M5.Lcd.printf("%3d%%[C]", bat);
  } else if (bat <= 20) {
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.printf("%3d%%", bat);
  } else {
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.printf("%3d%%", bat);
  }

  M5.Lcd.setCursor(10, 45);
  M5.Lcd.setTextSize(2);
}

DisplayManager::DisplayManager() {}

bool DisplayManager::init() {
  // ディスプレイ基本設定
  M5.Lcd.setBrightness(100);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  return true;
}

void DisplayManager::update() {
  // 画面クリア ＆ カーソル位置のリセット
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 20);
  drawBatteryStatus();

  // 表示データのフォールバック処理（未設定時のデフォルト値）
  String devId = bleMgr.deviceId.isEmpty() ? "M5-INIT" : bleMgr.deviceId;
  String spot = bleMgr.spotName.isEmpty() ? "Unregistered" : bleMgr.spotName;
  String sticker = bleMgr.distributeStickerId.isEmpty() ? "none" : bleMgr.distributeStickerId;

  // 状態ごとの画面描画切り替え
  switch (StateManager::currentState) {
    case STATE_BLE_CONNECTED:
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("=== iPad Connected ===");
      M5.Lcd.println("\nSyncing with App...");
      break;

    case STATE_IDLE:
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.println("=== Station Mode ===");
      M5.Lcd.printf("Spot: %s\n", spot.c_str());
      M5.Lcd.printf("ID   : %s\n\n", devId.c_str());
      M5.Lcd.setTextColor(YELLOW, BLACK);
      M5.Lcd.println("[Distribute Sticker]");
      M5.Lcd.println(sticker);
      break;

    case STATE_STICKER_DISPLAY:
      M5.Lcd.setTextColor(GREEN, BLACK);
      M5.Lcd.println("=== Distributed ===");
      M5.Lcd.println("\nSticker Sent!");
      M5.Lcd.printf("ID: %s\n", sticker.c_str());
      break;

    case STATE_SOS_ALERT:
      M5.Lcd.setTextColor(RED, BLACK);
      M5.Lcd.println("!! SOS ALERT !!");
      M5.Lcd.println("\nSOS Signal Recv!");
      break;

    case STATE_DUMMY_SOS_ALERT:
      M5.Lcd.setTextColor(ORANGE, BLACK);
      M5.Lcd.println("-- DUMMY SOS --");
      M5.Lcd.println("\nTest Signal Recv");
      break;

    case STATE_SHOW_SETTING:
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("=== Settings ===");
      M5.Lcd.printf("ID     : %s\n", devId.c_str());
      M5.Lcd.printf("Spot   : %s\n", spot.c_str());
      M5.Lcd.printf("Sticker: %s\n", sticker.c_str());
      M5.Lcd.printf("Logs   : D:%d / S:%d\n", 
                    (int)stickerSosMgr.pendingDistributeLogs.size(), 
                    (int)stickerSosMgr.pendingSosLogs.size());
      M5.Lcd.printf("Sync   : %s\n", bleMgr.lastSyncTime.c_str());
      break;
  }
}