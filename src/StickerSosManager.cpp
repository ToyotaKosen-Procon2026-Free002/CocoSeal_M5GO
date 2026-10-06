#include "StickerSosManager.h"
#include "BleManager.h"
#include "EspNowManager.h"
#include "StateManager.h"
#include "LedBuzzerManager.h"
#include "ServerApiManager.h"

StickerSosManager stickerSosMgr;

StickerSosManager::StickerSosManager() {}

// SOS受信時の処理 (ESP-NOW / LoRa)
void StickerSosManager::handleSos(const String& childId, const String& source) {
  String timestamp = bleMgr.getTimestamp();

  // 1. 音とLEDで緊急警報を作動
  ledBuzzerMgr.showRealSos();
  StateManager::changeState(STATE_SOS_ALERT);

  // 2. Wi-Fiに繋がっていればサーバーへ直通送信 (POST /devices/sos)
  bool sentSuccess = serverApiMgr.sendSosAlert(bleMgr.deviceId, childId);

  // 3. サーバー送信に失敗した場合（オフライン時等）のみ、内部ログ（未送信キャッシュ）に追加
  if (!sentSuccess) {
    pendingSosLogs.push_back({childId, timestamp});
    Serial.println("[SOS Log] Direct send failed or offline. Saved to pending queue.");
  } else {
    Serial.println("[SOS Log] Direct send success. Skipped saving to pending queue.");
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

        // 子機へシール情報を返信
        EspNowManager::sendSticker(bleMgr.deviceId, bleMgr.distributeStickerId);

        // 画面表示を「配布完了」に切り替え
        StateManager::changeState(STATE_STICKER_DISPLAY);

        // Wi-Fiに繋がっていればサーバーへ直通送信 (POST /devices/status)
        bool sentSuccess = serverApiMgr.sendStatusAndPassageLogs(bleMgr.deviceId, senderId, bleMgr.distributeStickerId);

        // サーバー送信に失敗した場合（オフライン時等）のみ、内部通過ログ（未送信キャッシュ）に追加
        if (!sentSuccess) {
          pendingDistributeLogs.push_back({senderId, timestamp});
          Serial.println("[Passage Log] Direct send failed or offline. Saved to pending queue.");
        } else {
          Serial.println("[Passage Log] Direct send success. Skipped saving to pending queue.");
        }
      }
    }
  }
}

// BLE経由でタブレットへ蓄積ログを一括転送
void StickerSosManager::flushLogsToBle() {
  String json = "{\"station_id\":\"" + bleMgr.deviceId + "\",";
  json += "\"encounter_logs\":[";
  for (size_t i = 0; i < pendingDistributeLogs.size(); i++) {
    json += "{\"device_id_2\":\"" + pendingDistributeLogs[i].device_id_2 + "\",\"device_timestamp\":\"" + pendingDistributeLogs[i].device_timestamp + "\"}";
    if (i < pendingDistributeLogs.size() - 1) json += ",";
  }
  json += "],\"sos_logs\":[";
  for (size_t i = 0; i < pendingSosLogs.size(); i++) {
    json += "{\"child_id\":\"" + pendingSosLogs[i].child_id + "\",\"device_timestamp\":\"" + pendingSosLogs[i].device_timestamp + "\"}";
    if (i < pendingSosLogs.size() - 1) json += ",";
  }
  json += "]}";

  // BLE CharacteristicでアプリへNotify送信
  bleMgr.sendLogsToApp(json);

  // 送信完了したログをクリア
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