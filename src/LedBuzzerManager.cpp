#include "LedBuzzerManager.h"
#include <M5Stack.h>

LedBuzzerManager ledBuzzerMgr;

LedBuzzerManager::LedBuzzerManager() {}

void LedBuzzerManager::init() {
  // NeoPixelの仕様設定 & ポート初期化
  pixels.updateType(NEO_GRB + NEO_KHZ800);
  pixels.updateLength(NUM_LED);
  pixels.setPin(LED_BAR_PIN);
  pixels.begin();
  pixels.clear();
  pixels.show(); // 一旦全消灯でスタート
}

void LedBuzzerManager::showIdle() {
  // 待機状態（LED消灯）
  pixels.clear();
  pixels.show();
}

void LedBuzzerManager::showRealSos() {
  // SOS発報：赤色全点灯 ＋ アラーム鳴動（1kHz / 1秒）
  pixels.fill(pixels.Color(255, 0, 0), 0, NUM_LED);
  pixels.show();
  M5.Speaker.tone(1000, 1000);
}

void LedBuzzerManager::showDummySos() {
  // テスト/ダミー発報：オレンジ色全点灯（ブザーなし）
  pixels.fill(pixels.Color(255, 140, 0), 0, NUM_LED);
  pixels.show();
}

void LedBuzzerManager::clear() {
  // LED消灯処理
  pixels.clear();
  pixels.show();
}