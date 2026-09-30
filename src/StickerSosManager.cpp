#include "StickerSosManager.h"
#include "BleManager.h"
#include "EspNowManager.h"
#include "StateManager.h"
#include "LedBuzzerManager.h"

StickerSosManager stickerSosMgr;

StickerSosManager::StickerSosManager() {}

void StickerSosManager::handleSos(const String& childId, const String& source) {
  // SOSログの蓄積
  String timestamp = bleMgr.getTimestamp();
  pendingSosLogs.push_back({childId, timestamp});

  // 警報演出 & SOS状態へ移行
  ledBuzzerMgr.showRealSos();
  StateManager::changeState(STATE_SOS_ALERT);
}

void StickerSosManager::handlePacket(const CommunicationPacket& packet, int rssi) {
  String senderId = String(packet.device_id);
  String timestamp = bleMgr.getTimestamp();

  // 他の親機（Station）からのパケットは無視
  if (packet.isGateway) {
    Serial.printf("[ESP-NOW] Another Gateway detected: %s (Ignored)\n", senderId.c_str());
    return;
  }

  // 子機からの各種パケット処理
  if (packet.type == 1) { 
    // SOS信号の受信
    handleSos(senderId, "ESP-NOW");
  } 
  else if (packet.type == 0) { 
    // 通過検知・シール要求（一定以上の電波強度のみ処理）
    if (rssi >= RSSI_THRESHOLD) {
      // 未配布（当日初検知）の子機かどうか判定
      auto it = std::find(distributedTodayList.begin(), distributedTodayList.end(), senderId);
      if (it == distributedTodayList.end()) {
        distributedTodayList.push_back(senderId);
        pendingDistributeLogs.push_back({senderId, timestamp});

        // シールレスポンス返信 & 配布完了画面表示
        EspNowManager::sendSticker(bleMgr.deviceId, bleMgr.distributeStickerId);
        StateManager::changeState(STATE_STICKER_DISPLAY);
      }
    }
  }
}

void StickerSosManager::flushLogsToBle() {
  // 蓄積ログをJSON形式に整形
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

  // BLEでアプリへ送信し、送信済みログをクリア
  bleMgr.sendLogsToApp(json);
  pendingDistributeLogs.clear();
  pendingSosLogs.clear();
}