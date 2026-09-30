#include "StateManager.h"
#include "DisplayManager.h"
#include "LedBuzzerManager.h"

// 静的メンバ変数の実体定義（初期状態：待機モード）
State StateManager::currentState = STATE_IDLE;
unsigned long StateManager::stateTimer = 0;

void StateManager::changeState(State newState) {
  currentState = newState;
  stateTimer = millis(); // 遷移時刻を記録
  displayMgr.update();   // 状態変更に伴い画面更新
}

void StateManager::checkStateTimeout(unsigned long timeoutMs) {
  // 一時表示状態（配布完了画面・SOSアラート表示など）の場合のみタイマー判定
  if (currentState == STATE_STICKER_DISPLAY || 
      currentState == STATE_SOS_ALERT || 
      currentState == STATE_DUMMY_SOS_ALERT) {
    
    // タイムアウト超過でLED/ブザーを停止し通常待機へ復帰
    if (millis() - stateTimer > timeoutMs) {
      ledBuzzerMgr.clear();
      changeState(STATE_IDLE);
    }
  }
}