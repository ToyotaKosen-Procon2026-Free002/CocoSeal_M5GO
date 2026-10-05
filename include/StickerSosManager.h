#pragma once
#include <Arduino.h>
#include <vector>
#include "Config.h"

struct EncounterLog {
  String device_id_2;
  String device_timestamp;
};

class StickerSosManager {
public:
  std::vector<EncounterLog> pendingDistributeLogs;
  std::vector<SosLog> pendingSosLogs;
  std::vector<String> distributedTodayList;

  StickerSosManager();
  
  void handleSos(const String& childId, const String& source);
  void handlePacket(const CommunicationPacket& packet, int rssi);
  void flushLogsToBle();

  // 当日配布記録と未送信ログのリセット
  void resetDailyData();
};

extern StickerSosManager stickerSosMgr;