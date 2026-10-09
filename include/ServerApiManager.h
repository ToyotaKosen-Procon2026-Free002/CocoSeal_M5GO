#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/sha256.h"

class ServerApiManager {
private:
  const char* serverUrl = "https://coco-seal.mydns.jp";
  mbedtls_pk_context pk;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctrDrbg;
  bool keyInitialized = false;

  void initKeys();
  String signMessage(const String& message);
  String formatPublicKey(const String& rawKey);
  String generateRequestSignature(const String& gatewayId, const String& payload);

public:
  ServerApiManager();
  ~ServerApiManager();

  // 公開鍵を Hex (130文字) で取得
  String getPublicKeyHex();

  // 認証付き親機情報取得 (GET /devices/gateway/{gateway_id})
  bool fetchGatewayInfo(const String& gatewayId);

  // 配布シール情報取得 (GET /devices/seal_gateway)
  bool fetchDistributeSealInfo(const String& gatewayId, const String& sealId);

  // 1. 親機の初回登録・アクティベート (POST /devices/gateway/init)
  bool registerGateway(const String& gatewayId, const String& spotName);

  // 2. 親機経由のすれ違い・配布シール情報の送信 (POST /devices/status_from_gateway)
  bool sendStatusAndPassageLogs(const String& gatewayId, const String& childId, const String& stickerId);

  // 3. SOS情報の送信
  bool sendSosAlert(const String& gatewayId, const String& childId);
};

extern ServerApiManager serverApiMgr;