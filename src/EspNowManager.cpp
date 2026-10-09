#include "EspNowManager.h"
#include "Config.h"
#include "DisplayManager.h"
#include "LedBuzzerManager.h"
#include "StickerSosManager.h"
#include "BleManager.h"
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// 子機側と完全に一致させた64バイト構造体定義
struct GatewayCommunicationPacket {
    char device_id[37];
    int type; // 0: encounter, 1: SOS, 2: name
    char stickerId[16];
    bool isGateway;
};

// ブロードキャスト用MACアドレス
static uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static QueueHandle_t receivedPackets = nullptr;

// -------------------------------------------------------------------
// ESP-NOW 受信コールバック関数（ESP32 SDK Version 互換対応）
// -------------------------------------------------------------------
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
#else
void onDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
#endif

    // 64バイトの通信パケットかチェック (旧58バイト構造体からの互換も考慮)
    if (len != sizeof(GatewayCommunicationPacket) && len != sizeof(CommunicationPacket)) {
        Serial.printf("[ESP-NOW Error] Size mismatch! Expected %d or %d bytes, got %d bytes\n", 
                      (int)sizeof(GatewayCommunicationPacket), (int)sizeof(CommunicationPacket), len);
        return;
    }

    CommunicationPacket packet = {};
    
    if (len == sizeof(GatewayCommunicationPacket)) {
        GatewayCommunicationPacket gPacket;
        memcpy(&gPacket, incomingData, sizeof(gPacket));
        
        snprintf(packet.device_id, sizeof(packet.device_id), "%s", gPacket.device_id);
        snprintf(packet.stickerId, sizeof(packet.stickerId), "%s", gPacket.stickerId);
        packet.type = gPacket.type;
        packet.isGateway = gPacket.isGateway;
    } else {
        memcpy(&packet, incomingData, sizeof(packet));
    }

    packet.device_id[36] = '\0';
    packet.stickerId[15] = '\0';

    if (!receivedPackets || xQueueSend(receivedPackets, &packet, 0) != pdTRUE) {
        Serial.println("[ESP-NOW Error] Received packet queue full; packet dropped.");
    }
}

// -------------------------------------------------------------------
// EspNowManager クラスメンバ関数群
// -------------------------------------------------------------------

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void EspNowManager::onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    ::onDataRecv(recv_info, incomingData, len);
}
#else
void EspNowManager::onDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    ::onDataRecv(mac_addr, incomingData, len);
}
#endif

void EspNowManager::init() {
    receivedPackets = xQueueCreate(8, sizeof(CommunicationPacket));
    if (!receivedPackets) {
        Serial.println("[ESP-NOW Error] Failed to create receive queue.");
        return;
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init Failed!");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (!esp_now_is_peer_exist(broadcastMac)) {
        esp_now_add_peer(&peerInfo);
    }
    
    esp_now_register_recv_cb(::onDataRecv);
    Serial.println("[ESP-NOW] Initialized successfully on Gateway!");
}

void EspNowManager::processPendingPackets() {
    if (!receivedPackets) {
        return;
    }

    CommunicationPacket packet = {};
    if (xQueueReceive(receivedPackets, &packet, 0) != pdTRUE) {
        return;
    }

    Serial.printf("[ESP-NOW Recv] Type: %d, Device: %s, IsGateway: %d\n",
                  packet.type, packet.device_id, packet.isGateway);

    if (packet.type == 0) {
        Serial.printf("[Encounter Detect] Child ID: %s\n", packet.device_id);
        stickerSosMgr.handlePacket(packet, -50);
    } else if (packet.type == 1) {
        Serial.printf("[SOS EMERGENCY] From Child ID: %s\n", packet.device_id);
        stickerSosMgr.handleSos(packet.device_id, "ESP-NOW");
    }
}

void EspNowManager::sendSticker(const String& targetChildId, const String& stickerId) {
    GatewayCommunicationPacket packet = {};
    
    snprintf(packet.device_id, sizeof(packet.device_id), "%s", bleMgr.deviceId.c_str());
    snprintf(packet.stickerId, sizeof(packet.stickerId), "%s", stickerId.c_str());
    packet.type = 0;          // MESSAGE_TYPE_ENCOUNTER
    packet.isGateway = true;  // 親機フラグON

    esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
    if (result == ESP_OK) {
        Serial.printf("[ESP-NOW SendSticker] Successfully sent sticker '%s' for child '%s'\n", 
                      stickerId.c_str(), targetChildId.c_str());
    } else {
        Serial.printf("[ESP-NOW SendSticker Error] Send failed with code: %d\n", result);
    }
}

void EspNowManager::sendSpotName(const String& spotName) {
    GatewayCommunicationPacket packet = {};

    snprintf(packet.device_id, sizeof(packet.device_id), "%s", bleMgr.deviceId.c_str());
    int written = snprintf(packet.stickerId, sizeof(packet.stickerId), "%s", spotName.c_str());
    packet.type = 2;          // MESSAGE_TYPE_NAME
    packet.isGateway = true;  // 親機フラグON

    if (written < 0 || written >= sizeof(packet.stickerId)) {
        Serial.printf("[ESP-NOW SendSpotName] Spot name exceeds %u bytes and will be truncated.\n",
                      (unsigned int)(sizeof(packet.stickerId) - 1));
    }

    esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
    if (result == ESP_OK) {
        Serial.printf("[ESP-NOW SendSpotName] Successfully sent spot name '%s'\n",
                      packet.stickerId);
    } else {
        Serial.printf("[ESP-NOW SendSpotName Error] Send failed with code: %d\n", result);
    }
}