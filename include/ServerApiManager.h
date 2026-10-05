#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class ServerApiManager {
private:
  const char* serverUrl = "https://coco-seal.mydns.jp";

public:
  ServerApiManager();

  // 1. 親機の初回登録 (POST /devices/gateway)
  bool registerGateway(const String& gatewayId, const String& spotName);

  // 2. すれ違い・ステータス情報の更新 (POST /devices/status)
  bool sendStatusAndPassageLogs(const String& gatewayId, const String& childId, const String& stickerId);

  // 3. SOS情報の送信 (POST /devices/sos)
  bool sendSosAlert(const String& gatewayId, const String& childId);
};

extern ServerApiManager serverApiMgr;