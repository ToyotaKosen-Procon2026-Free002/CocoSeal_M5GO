#include <M5Stack.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "DisplayManager.h"
#include "StateManager.h"
#include "BleManager.h"
#include "EspNowManager.h"
#include "LedBuzzerManager.h"
#include "StickerSosManager.h"

#define LORA_RX_PIN 16
#define LORA_TX_PIN 17

void setup() {
  // M5Stack本体 & 電源管理の初期化
  M5.begin(true, false, true);
  M5.Power.begin();
  Serial.begin(115200);
  delay(100);
  
  // 周辺機器（画面・LED/ブザー）初期化
  displayMgr.init();
  ledBuzzerMgr.init();
  delay(50);

  // Wi-Fiドライバを起動し、ESP-NOW通信安定化のためWi-Fiチャンネルを 1 に固定
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  delay(100);

  // 無線機能（ESP-NOW / BLE）初期化
  EspNowManager::init();
  bleMgr.init();

  // LoRa用シリアル通信（UART2）の開始
  Serial2.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // 起動時の初期状態（待機モード）へ移行
  StateManager::changeState(STATE_IDLE);
}

void loop() {
  M5.update();

  // ===================
  // 【テスト用コード】
  // ===================
  /*
  // ボタンA（左）: ダミー子機の通過検知
  if (M5.BtnA.wasPressed()) {
    CommunicationPacket dummyPkt;
    memset(&dummyPkt, 0, sizeof(dummyPkt));
    dummyPkt.type = 0;
    strncpy(dummyPkt.device_id, "TEST-CHILD", sizeof(dummyPkt.device_id) - 1);
    stickerSosMgr.handlePacket(dummyPkt, -45);
  }

  // ボタンC（右）: ダミーSOS発信
  if (M5.BtnC.wasPressed()) {
    stickerSosMgr.handleSos("TEST-SOS", "BUTTON_C");
  }
  */

  // 左ボタン（BtnA）が押されたらリセット実行
  if (M5.BtnA.wasPressed()) {
    stickerSosMgr.resetDailyData();

    // 画面に一時的にリセット完了を表示
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(20, 100);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setTextSize(3); // 一時的に大きくする
    M5.Lcd.println("RESET DONE!");
    delay(1000);

    // 文字サイズを標準（サイズ2）に戻してから待機画面に戻る
    M5.Lcd.setTextSize(2); 
    StateManager::changeState(STATE_IDLE);
  }
  
  // 中央ボタン(BtnB): 設定確認画面の切り替え
  if (M5.BtnB.wasPressed()) {
    if (StateManager::currentState == STATE_SHOW_SETTING) {
      StateManager::changeState(STATE_IDLE);
    } else {
      StateManager::changeState(STATE_SHOW_SETTING);
    }
  }

  // LoRa経由のSOS信号受信
  if (Serial2.available() > 0) {
    String loraMsg = Serial2.readStringUntil('\n');
    loraMsg.trim();
    if (loraMsg.indexOf("SOS") >= 0) {
      String childId = "LORA-CHILD";
      if (loraMsg.startsWith("SOS:")) {
        childId = loraMsg.substring(4);
      }
      stickerSosMgr.handleSos(childId, "LoRa");
    }
  }

  // タイムアウト監視（10秒で通常画面へ復帰）
  StateManager::checkStateTimeout(10000);
  delay(20);
}