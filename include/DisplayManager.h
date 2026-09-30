#pragma once
#include <M5Stack.h>
#include "Config.h"

class DisplayManager {
private:
 // 画面のチラつき防止スプライト
  TFT_eSprite* sprite = nullptr;

public:
  DisplayManager();
  bool init();
  void update(); // 描画更新
};

// 外部参照
extern DisplayManager displayMgr;