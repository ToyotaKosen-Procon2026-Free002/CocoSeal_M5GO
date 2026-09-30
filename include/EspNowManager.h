#pragma once
#include <esp_now.h>
#include "Config.h"

class EspNowManager {
public:
  // 初期化・メッセージ送信
  static void init();
  static void sendSticker(const String& stationId, const String& stickerId);
  // パケット受信時のコールバック
  static void onDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len);
};