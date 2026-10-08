#include <M5Stack.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <HTTPClient.h>
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
const unsigned long BATTERY_CHECK_INTERVAL = 600000; // 10分ごと

// 定期設定取得タイマー（1分ごとにサーバーから最新設定を取得）
unsigned long lastFetchConfigTime = 0;
const unsigned long FETCH_CONFIG_INTERVAL = 60000;

// 時刻同期関数（NTP優先、失敗時は2026年フォールバック）
void syncSystemTime() {
  Serial.print("[Time Sync] Trying NTP sync...");
  configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // JST: UTC+9

  struct tm timeinfo;
  unsigned long start = millis();
  while (!getLocalTime(&timeinfo) && millis() - start < 4000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (getLocalTime(&timeinfo)) {
    Serial.printf("[Time Sync] NTP Success: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return;
  }

  // NTP失敗時のフォールバック (2026年10月のUnixTimeを自動セット)
  Serial.println("[Time Sync] NTP failed. Applying Oct 2026 fallback time...");
  struct timeval tv = { 1791500000, 0 };
  settimeofday(&tv, NULL);
}

void connectSavedWiFi() {
  Preferences prefs;
  prefs.begin("gateway_cfg", true); // 読み取りモード
  String savedSsid = prefs.getString("wifi_ssid", "");
  String savedPass = prefs.getString("wifi_pass", "");
  prefs.end();

  if (savedSsid.length() == 0) {
    Serial.println("[WiFi] No saved WiFi credentials found in NVS.");
    return;
  }

  Serial.printf("[WiFi] Connecting to saved SSID: %s\n", savedSsid.c_str());
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected successfully! IP: %s\n", WiFi.localIP().toString().c_str());
    syncSystemTime();
  } else {
    Serial.println("[WiFi] Failed to connect using saved credentials.");
    WiFi.disconnect();
  }
}

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

  // ★ オフラインモード: Wi-Fi接続処理（connectSavedWiFi）をスキップ
  // connectSavedWiFi(); 

  // ★ チャンネルを強制的に Channel 1 固定にする
  uint8_t primaryChannel = 1;

  // ESP-NOWのチャンネルを設定
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(primaryChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // 無線機能（ESP-NOW / BLE）初期化
  EspNowManager::init();
  bleMgr.init();

  // ★ オフラインモード: サーバーへの自己登録・設定取得をスキップ
  /*
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[Server] Registering Gateway & Fetching Config...");
    serverApiMgr.registerGateway(bleMgr.deviceId, bleMgr.spotName);
    serverApiMgr.fetchGatewayInfo(bleMgr.deviceId);
  }
  */

  // LoRa用シリアル通信（UART2）の開始
  Serial2.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // 起動時の初期状態へ移行
  StateManager::changeState(STATE_IDLE);
}

void loop() {
  M5.update();
  
  // バックグラウンドタスク処理（BLE経由の設定書き込みやDB登録など）
  bleMgr.processPendingTasks();

  // オフラインモード: サーバーからの定期設定取得をコメントアウト
  /*
  if (WiFi.status() == WL_CONNECTED && (millis() - lastFetchConfigTime > FETCH_CONFIG_INTERVAL)) {
    lastFetchConfigTime = millis();
    serverApiMgr.fetchGatewayInfo(bleMgr.deviceId);
  }
  */

  // 定期的に画面を更新してバッテリー残量を最新化する
  if (millis() - lastBatteryCheckTime > BATTERY_CHECK_INTERVAL) {
    lastBatteryCheckTime = millis();
    displayMgr.update();
  }

  // 左ボタン（BtnA）が押されたらリセット実行
  if (M5.BtnA.wasPressed()) {
    stickerSosMgr.resetDailyData();
    lastBatteryCheckTime = millis();

    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(20, 100);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("RESET DONE!");
    delay(1000);

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