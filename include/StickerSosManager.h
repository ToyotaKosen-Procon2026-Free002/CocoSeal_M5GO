#pragma once
#include <Arduino.h>
#include <vector>
#include "Config.h"

struct EncounterLog {
  String device_id_2;
  String device_timestamp;
  String send_seal_id;
};

class StickerSosManager {
private:
  void processSos(const String& childId,
                  const String& source,
                  const String& eventId,
                  uint32_t triggerTimestamp,
                  const String& childSignature);

public:
  std::vector<EncounterLog> pendingDistributeLogs;
  std::vector<SosLog> pendingSosLogs;
  std::vector<String> distributedTodayList;

  StickerSosManager();
  
  void handleSos(const String& childId, const String& source);
  void handleSignedSos(const String& childId,
                       const String& eventId,
                       uint32_t triggerTimestamp,
                       const uint8_t* childSignature,
                       size_t signatureLength);
  void handlePacket(const CommunicationPacket& packet, int rssi);
  void flushLogsToBle();

  // 当日配布記録と未送信ログのリセット
  void resetDailyData();
};

extern StickerSosManager stickerSosMgr;