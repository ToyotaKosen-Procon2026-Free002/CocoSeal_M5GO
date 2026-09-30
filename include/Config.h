#pragma once
#include <Arduino.h>

// ピンの設定
#define LED_BAR_PIN 15
#define NUM_LED 10
#define RSSI_THRESHOLD -90

//UUID設定
#define SERVICE_UUID           "42fbd1f2-b02c-1ba6-87f8-7d9ca4f3a343"
#define CHAR_CONFIG_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_LOG_UUID          "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define CHAR_STATUS_UUID       "d29ae63e-b7d3-4874-a690-3432b85e05a5"

// BLEのステータス
enum State {
  STATE_IDLE,
  STATE_STICKER_DISPLAY,
  STATE_SOS_ALERT,
  STATE_DUMMY_SOS_ALERT,
  STATE_SHOW_SETTING,
  STATE_BLE_CONNECTED
};

// 子機 ⇔ 親機 ESP-NOW パケット構造体
struct CommunicationPacket {
  char device_id[37];  // UUID対応のため37byte (36文字 + NULL終端) に拡張
  int type;            // 0: 通過/シール要求, 1: SOS
  char stickerId[16];
  bool isGateway;      // true: 親機(Gateway), false: 子機
};

struct DistributeLog {
  String device_id_2; // すれ違った相手のデバイスID
  String device_timestamp; // すれ違った日時
};

struct SosLog {
  String child_id; // SOSを発信した子機のID
  String device_timestamp; // 子機がSOSボタンを押した時間
};