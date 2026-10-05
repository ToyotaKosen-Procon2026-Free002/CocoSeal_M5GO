#pragma once
#include <vector>
#include <algorithm>
#include "Config.h"

class StickerSosManager {
private:
  // 当日の配布済みシールIDリスト（重複配布防止用）
  std::vector<String> distributedTodayList;

public:
  // 未送信ログのキュー（BLE送信待ち）
  std::vector<DistributeLog> pendingDistributeLogs;
  std::vector<SosLog> pendingSosLogs;

  StickerSosManager();

  // パケット・SOS受信ハンドラ
  void handlePacket(const CommunicationPacket& packet, int rssi = -50);
  void handleSos(const String& childId, const String& source);

  // 蓄積ログをBLE経由でアプリへフラッシュ
  void flushLogsToBle();

  // 当日の配布記録とログをクリアするリセット処理
  void resetDailyData();
};

// 外部参照
extern StickerSosManager stickerSosMgr;