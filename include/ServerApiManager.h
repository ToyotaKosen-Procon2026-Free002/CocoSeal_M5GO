#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

class ServerApiManager {
private:
  const char* serverUrl = "https://coco-seal.mydns.jp";
  mbedtls_pk_context pk;
  bool keyInitialized = false;

  void initKeys();
  String signMessage(const String& message);

public:
  ServerApiManager();
  ~ServerApiManager();

  // 公開鍵を Hex (130文字) で取得
  String getPublicKeyHex();

  // 認証付き親機情報取得 (GET /devices/gateway/{gateway_id})
  bool fetchGatewayInfo(const String& gatewayId);

  // 1. 親機の初回登録・アクティベート (POST /devices/gateway/init)
  bool registerGateway(const String& gatewayId, const String& spotName);

  // 2. すれ違い・ステータス情報の更新
  bool sendStatusAndPassageLogs(const String& gatewayId, const String& childId, const String& stickerId);

  // 3. SOS情報の送信
  bool sendSosAlert(const String& gatewayId, const String& childId);
};

extern ServerApiManager serverApiMgr;