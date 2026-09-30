#pragma once
#include <Adafruit_NeoPixel.h>
#include "Config.h"

class LedBuzzerManager {
private:
  Adafruit_NeoPixel pixels;

public:
  LedBuzzerManager();
  void init();

  // LEDとブザーのパターン発光・鳴動
  void showIdle();      // 待機中
  void showRealSos();   // SOS
  void showDummySos();  // 誤作動・ダミー発報
  void clear();         // 消灯・消音
};

// 外部参照
extern LedBuzzerManager ledBuzzerMgr;