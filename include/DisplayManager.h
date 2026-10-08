#pragma once
#include <Arduino.h>
#include <M5GFX.h>

class DisplayManager {
private:
  M5GFX gfx;
  void drawBatteryStatus();

public:
  DisplayManager();
  bool init();
  void update(); // 描画更新
  void showResetDone();
};

// 外部参照
extern DisplayManager displayMgr;