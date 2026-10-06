#include "EspNowManager.h"
#include "StickerSosManager.h"
#include <WiFi.h>

// ESP-NOWのC言語用コールバック関数からC++クラス静的メソッドへ接続するためのラッパー
static void onDataRecvWrapper(const uint8_t* mac, const uint8_t* incomingData, int len) {
  EspNowManager::onDataRecv(mac, incomingData, len);
}

void EspNowManager::init() {
  // ESP-NOWの初期化 & 受信コールバック登録
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataRecvWrapper);
  }
}

void EspNowManager::sendSticker(const String& stationId, const String& stickerId) {
  // 送信用パケット構造体の作成と初期化
  CommunicationPacket reply;
  memset(&reply, 0, sizeof(reply));
  strncpy(reply.device_id, stationId.c_str(), sizeof(reply.device_id) - 1);
  reply.type = 0;
  strncpy(reply.stickerId, stickerId.c_str(), sizeof(reply.stickerId) - 1);
  reply.isGateway = true; // 親機フラグ

  // ブロードキャスト用ピアの設定
  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  
  // WIFIの現在チャンネルを取得してピア情報に設定
  uint8_t currentCh = WiFi.channel();
  peerInfo.channel = (currentCh > 0) ? currentCh : 0; // 0 を指定すると現在の自機チャンネルを使用
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peerInfo); //ESP-NOWの管理リストへ登録
  }

  // ブロードキャスト送信
  esp_now_send(broadcastAddress, (uint8_t*)&reply, sizeof(reply));
}

void EspNowManager::onDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
  // パケットサイズが想定と一致する場合のみ正常処理
  if (len == sizeof(CommunicationPacket)) {
    CommunicationPacket packet;
    memcpy(&packet, incomingData, sizeof(CommunicationPacket));
    stickerSosMgr.handlePacket(packet, -50);
  }
}