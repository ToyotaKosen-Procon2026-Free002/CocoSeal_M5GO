#include <M5Stack.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "DisplayManager.h"
#include "StateManager.h"
#include "BleManager.h"
#include "EspNowManager.h"
#include "LedBuzzerManager.h"
#include "StickerSosManager.h"
#include "ServerApiManager.h"

#define LORA_RX_PIN 16
#define LORA_TX_PIN 17

// タイマー用変数
unsigned long lastBatteryCheckTime = 0;
const unsigned long BATTERY_CHECK_INTERVAL = 600000; // 600秒(10分)ごとに自動更新 (ミリ秒)

// 定期設定取得タイマー（1分ごとにサーバーから最新設定を取得）
unsigned long lastFetchConfigTime = 0;
const unsigned long FETCH_CONFIG_INTERVAL = 60000;

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

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  // NVSから保存されている Wi-Fi 情報を読み出して自動接続
  Preferences prefs;
  prefs.begin("gateway_cfg", true);
  String savedSsid = prefs.getString("wifi_ssid", "");
  String savedPass = prefs.getString("wifi_pass", "");
  prefs.end();

  uint8_t primaryChannel = 1;

  if (savedSsid.length() > 0) {
    Serial.printf("[WiFi] Auto connecting to: %s\n", savedSsid.c_str());
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
      delay(100);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    primaryChannel = WiFi.channel();
    Serial.printf("[WiFi] Auto connected! Channel: %d\n", primaryChannel);
  } else {
    Serial.println("[WiFi] Not connected on boot.");
    WiFi.disconnect();
  }

  // ESP-NOWの現在チャンネルに合わせる
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(primaryChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // 無線機能（ESP-NOW / BLE）初期化
  EspNowManager::init();
  bleMgr.init();

  // 起動時に Wi-Fi が繋がっていればサーバーから最新の設定（スポット名・シールID）を取得
  if (WiFi.status() == WL_CONNECTED) {
    serverApiMgr.fetchGatewayInfo(bleMgr.deviceId);
  }

  // LoRa用シリアル通信（UART2）の開始
  Serial2.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // 起動時の初期状態へ移行
  StateManager::changeState(STATE_IDLE);
}

void loop() {
  M5.update();
  
  // バックグラウンドタスク処理（DB登録など）
  bleMgr.processPendingTasks();

  // Wi-Fi接続時、1分ごとにサーバーAPIから設定（スポット名・シールID）を取得して同期
  if (WiFi.status() == WL_CONNECTED && (millis() - lastFetchConfigTime > FETCH_CONFIG_INTERVAL)) {
    lastFetchConfigTime = millis();
    serverApiMgr.fetchGatewayInfo(bleMgr.deviceId);
  }

  // 定期的に画面を更新してバッテリー残量を最新化する
  if (millis() - lastBatteryCheckTime > BATTERY_CHECK_INTERVAL) {
    lastBatteryCheckTime = millis();
    displayMgr.update();
  }

  // 左ボタン（BtnA）が押されたらリセット実行
  if (M5.BtnA.wasPressed()) {
    stickerSosMgr.resetDailyData();

    // 自動更新タイマーをリセット
    lastBatteryCheckTime = millis();

    // 画面に一時的にリセット完了を表示
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(20, 100);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("RESET DONE!");
    delay(1000);

    // 文字サイズを標準（サイズ2）に戻してから待機画面に遷移
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