#include "StickerSosManager.h"
#include "BleManager.h"
#include "EspNowManager.h"
#include "StateManager.h"
#include "LedBuzzerManager.h"
#include "ServerApiManager.h"
#include <ArduinoJson.h>

StickerSosManager stickerSosMgr;

StickerSosManager::StickerSosManager() {}

// SOS受信時の処理 (ESP-NOW / LoRa)
void StickerSosManager::handleSos(const String& childId, const String& source) {
  processSos(childId, source, "", 0, "");
}

void StickerSosManager::handleSignedSos(const String& childId,
                                        const String& eventId,
                                        uint32_t triggerTimestamp,
                                        const uint8_t* childSignature,
                                        size_t signatureLength) {
  if (!childSignature || signatureLength == 0 || signatureLength > 80) {
    Serial.println("[SOS] Invalid child signature; received SOS locally only.");
    processSos(childId, "ESP-NOW", "", 0, "");
    return;
  }

  String signatureHex;
  signatureHex.reserve(signatureLength * 2);
  for (size_t i = 0; i < signatureLength; ++i) {
    char byteHex[3];
    snprintf(byteHex, sizeof(byteHex), "%02x", childSignature[i]);
    signatureHex += byteHex;
  }
  processSos(childId, "ESP-NOW", eventId, triggerTimestamp, signatureHex);
}

void StickerSosManager::processSos(const String& childId,
                                   const String& source,
                                   const String& eventId,
                                   uint32_t triggerTimestamp,
                                   const String& childSignature) {
  String timestamp = bleMgr.getTimestamp();

  // 1. 音とLEDで緊急警報を作動
  ledBuzzerMgr.showRealSos();
  StateManager::changeState(STATE_SOS_ALERT);

  pendingSosLogs.push_back({childId, timestamp});
  flushLogsToBle();

  bool sentSuccess = false;
  if (!eventId.isEmpty() && triggerTimestamp > 0 && !childSignature.isEmpty()) {
    sentSuccess = serverApiMgr.sendSosAlert(
        bleMgr.deviceId, childId, eventId, triggerTimestamp, childSignature);
  } else {
    Serial.printf("[SOS] %s alert has no child signature; not sent to server.\n",
                  source.c_str());
  }

  if (sentSuccess) {
    Serial.println("[SOS Log] Server relay succeeded.");
  } else {
    Serial.println("[SOS Log] Server relay failed; BLE alert was sent or queued.");
  }
}

// ESP-NOW パケット受信時の処理 (通過 / SOS)
void StickerSosManager::handlePacket(const CommunicationPacket& packet, int rssi) {
  String senderId = String(packet.device_id);
  String timestamp = bleMgr.getTimestamp();

  // 相手が親機（Gateway）の場合は無視して終了
  if (packet.isGateway) {
    Serial.printf("[ESP-NOW] Another Gateway detected: %s (Ignored)\n", senderId.c_str());
    return;
  }

  // --- パケット種別ごとの処理 ---
  if (packet.type == 1) { 
    // SOSパケット受信
    handleSos(senderId, "ESP-NOW");
  } 
  else if (packet.type == 0) { 
    // 通過・シール要求パケット受信
    if (rssi >= RSSI_THRESHOLD) {
      // 1日1回重複配布防止チェック
      auto it = std::find(distributedTodayList.begin(), distributedTodayList.end(), senderId);
      if (it == distributedTodayList.end()) {
        // 本日の配布リストに登録
        distributedTodayList.push_back(senderId);

        // 名前通知を先に送り、子機が獲得イベントを記録するときに
        // スポット名を相手名として保存できるようにする。
        EspNowManager::sendSpotName(bleMgr.spotName);
        delay(20);
        // 子機へシール情報を返信
        EspNowManager::sendSticker(bleMgr.deviceId, bleMgr.distributeStickerId);

        // 画面表示を「配布完了」に切り替え
        StateManager::changeState(STATE_STICKER_DISPLAY);

        // BLE接続中ならアプリへ即時通知し、未接続なら後で取得できるよう保持
        pendingDistributeLogs.push_back({senderId, timestamp, bleMgr.distributeStickerId});
        flushLogsToBle();

        // Wi-Fiに繋がっていればサーバーへ直通送信 (POST /devices/status)
        bool sentSuccess = serverApiMgr.sendStatusAndPassageLogs(bleMgr.deviceId, senderId, bleMgr.distributeStickerId);

        if (!sentSuccess) {
          Serial.println("[Passage Log] Direct send failed or offline. BLE app log remains queued if not delivered.");
        } else {
          Serial.println("[Passage Log] Direct send success.");
        }
      }
    }
  }
}

// BLE経由でタブレットへ蓄積ログを一括転送
void StickerSosManager::flushLogsToBle() {
  JsonDocument doc;
  doc["station_id"] = bleMgr.deviceId;
  JsonArray encounterLogs = doc["encounter_logs"].to<JsonArray>();
  for (size_t i = 0; i < pendingDistributeLogs.size(); i++) {
    JsonObject log = encounterLogs.add<JsonObject>();
    log["device_id_2"] = pendingDistributeLogs[i].device_id_2;
    log["device_timestamp"] = pendingDistributeLogs[i].device_timestamp;
    log["send_seal_id"] = pendingDistributeLogs[i].send_seal_id;
  }

  JsonArray sosLogs = doc["sos_logs"].to<JsonArray>();
  for (size_t i = 0; i < pendingSosLogs.size(); i++) {
    JsonObject log = sosLogs.add<JsonObject>();
    log["child_id"] = pendingSosLogs[i].child_id;
    log["device_timestamp"] = pendingSosLogs[i].device_timestamp;
  }

  String json;
  serializeJson(doc, json);

  // Notifyできたときだけログを消し、未接続なら次回要求まで保持する
  if (!bleMgr.sendLogsToApp(json)) return;

  pendingDistributeLogs.clear();
  pendingSosLogs.clear();
}

// 当日の配布記録と未送信ログのリセット（左ボタン）
void StickerSosManager::resetDailyData() {
  distributedTodayList.clear();
  pendingDistributeLogs.clear();
  pendingSosLogs.clear();
  StateManager::changeState(STATE_IDLE);
  Serial.println("[Reset] Daily distributed list and pending logs cleared!");
}