#include "DisplayManager.h"
#include "StateManager.h"
#include "BleManager.h"
#include "StickerSosManager.h"

// M5StackのPowerモジュールヘッダー
#include <utility/Power.h>

DisplayManager displayMgr;

DisplayManager::DisplayManager() {}

bool DisplayManager::init() {
  gfx.begin();
  gfx.setRotation(1);
  gfx.fillScreen(TFT_BLACK);
  return true;
}

// 1行目右上にバッテリー残量を描画 (Y: 10)
void DisplayManager::drawBatteryStatus() {
  // M5Stackの電源管理オブジェクト
  POWER m5Power;
  int bat = m5Power.getBatteryLevel();
  bool charging = m5Power.isCharging();

  if (bat < 0) bat = 0;
  if (bat > 100) bat = 100;

  gfx.setFont((const lgfx::GFXfont*)&lgfx::fonts::efontJA_16);
  gfx.setCursor(230, 10);

  if (charging) {
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    gfx.printf("%3d%%[充電]", bat);
  } else if (bat <= 20) {
    gfx.setTextColor(TFT_RED, TFT_BLACK);
    gfx.printf("%3d%%", bat);
  } else {
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.printf("%3d%%", bat);
  }
}

void DisplayManager::update() {
  gfx.fillScreen(TFT_BLACK);

  // 1行目右上にバッテリーを描画
  drawBatteryStatus();

  String devId = bleMgr.deviceId.isEmpty() ? "M5-未初期化" : bleMgr.deviceId;
  String spot = bleMgr.spotName.isEmpty() ? "未登録" : bleMgr.spotName;
  String sticker = bleMgr.distributeStickerId.isEmpty() ? "なし" : bleMgr.distributeStickerId;

  const lgfx::GFXfont* font16 = (const lgfx::GFXfont*)&lgfx::fonts::efontJA_16;
  const lgfx::GFXfont* font24 = (const lgfx::GFXfont*)&lgfx::fonts::efontJA_24;

  switch (StateManager::currentState) {
    case STATE_BLE_CONNECTED:
      gfx.setTextColor(TFT_CYAN, TFT_BLACK);
      gfx.setFont(font24);
      gfx.setCursor(10, 45);
      gfx.println("=== iPad 接続中 ===");
      gfx.setFont(font16);
      gfx.setCursor(10, 90);
      gfx.println("アプリと同期しています...");
      break;

    case STATE_IDLE:
      gfx.setFont(font24);
      gfx.setTextColor(TFT_WHITE, TFT_BLACK);
      gfx.setCursor(10, 45);
      gfx.println("=== 親機モード ===");

      gfx.setFont(font16);
      gfx.setCursor(10, 85);
      gfx.printf("スポット: %s\n", spot.c_str());
      // gfx.setCursor(10, 115);
      // gfx.printf("親機 ID  : %s\n", devId.c_str());

      gfx.setTextColor(TFT_YELLOW, TFT_BLACK);
      gfx.setCursor(10, 155);
      gfx.println("[配布シール]");
      gfx.setCursor(10, 185);
      gfx.println(sticker);
      break;

    case STATE_STICKER_DISPLAY:
      gfx.setFont(font24);
      gfx.setTextColor(TFT_GREEN, TFT_BLACK);
      gfx.setCursor(10, 45);
      gfx.println("=== 配布完了 ===");
      
      gfx.setFont(font16);
      gfx.setCursor(10, 95);
      gfx.println("シールを送信しました！");
      gfx.setCursor(10, 135);
      gfx.printf("シールID: %s\n", sticker.c_str());
      break;

    case STATE_SOS_ALERT:
      gfx.fillScreen(TFT_RED);
      gfx.setFont(font24);
      gfx.setTextColor(TFT_WHITE, TFT_RED);
      gfx.setCursor(10, 45);
      gfx.println("!! SOS 緊急発生 !!");
      
      gfx.setFont(font16);
      gfx.setCursor(10, 100);
      gfx.println("子機からSOS信号を受信しました！");
      break;

    case STATE_DUMMY_SOS_ALERT:
      gfx.setFont(font24);
      gfx.setTextColor(TFT_ORANGE, TFT_BLACK);
      gfx.setCursor(10, 45);
      gfx.println("-- SOS テスト --");
      
      gfx.setFont(font16);
      gfx.setCursor(10, 95);
      gfx.println("テスト信号を受信しました");
      break;

    case STATE_SHOW_SETTING:
      gfx.setFont(font24);
      gfx.setTextColor(TFT_CYAN, TFT_BLACK);
      gfx.setCursor(10, 45);
      gfx.println("=== 設定確認 ===");

      gfx.setFont(font16);
      gfx.setCursor(10, 80);
      gfx.print("ID: ");
      gfx.println(devId.c_str());

      gfx.setCursor(10, 120);
      gfx.printf("スポット: %s\n", spot.c_str());
      
      gfx.setCursor(10, 145);
      gfx.printf("シール  : %s\n", sticker.c_str());
      
      gfx.setCursor(10, 170);
      gfx.printf("ログ保留: 配布:%d / SOS:%d\n", 
                    (int)stickerSosMgr.pendingDistributeLogs.size(), 
                    (int)stickerSosMgr.pendingSosLogs.size());
      
      gfx.setCursor(10, 195);
      gfx.printf("同期時間: %s\n", bleMgr.lastSyncTime.c_str());
      break;
  }
}

// リセット完了表示
void DisplayManager::showResetDone() {
  gfx.fillScreen(TFT_BLACK);
  gfx.setFont((const lgfx::GFXfont*)&lgfx::fonts::efontJA_24);
  gfx.setTextColor(TFT_GREEN, TFT_BLACK);
  gfx.setCursor(60, 100);
  gfx.println("リセット完了！");
}