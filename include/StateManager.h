#pragma once
#include "Config.h"

class StateManager {
public:
  // 現在の状態と状態維持タイマー
  static State currentState;
  static unsigned long stateTimer;

  // 状態遷移 & タイムアウト監視
  static void changeState(State newState);
  static void checkStateTimeout(unsigned long timeoutMs = 5000);
};