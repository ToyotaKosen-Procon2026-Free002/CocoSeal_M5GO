#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Micro-ECC または mbedtls 用
#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

class ServerApiManager {
private:
  const char* serverUrl = "https://coco-seal.mydns.jp";
  mbedtls_pk_context pk;
  bool keyInitialized = false;

  // ECDSA SECP256R1 (P-256) 鍵ペアの初期化・生成
  void initKeys();
  // リクエストデータに対するECDSA署名の生成 (Hex文字列で返却)
  String signMessage(const String& message);

public:
  ServerApiManager();
  ~ServerApiManager();

  // 認証付き親機情報取得 (GET /gateway)
  bool fetchGatewayInfo(const String& gatewayId);

  // 1. 親機の初回登録 (POST /devices/gateway)
  bool registerGateway(const String& gatewayId, const String& spotName);

  // 2. すれ違い・ステータス情報の更新 (POST /devices/status)
  bool sendStatusAndPassageLogs(const String& gatewayId, const String& childId, const String& stickerId);

  // 3. SOS情報の送信 (POST /devices/sos)
  bool sendSosAlert(const String& gatewayId, const String& childId);
};

extern ServerApiManager serverApiMgr;