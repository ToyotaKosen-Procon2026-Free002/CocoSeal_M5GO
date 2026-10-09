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

struct GatewaySosCommunicationPacket {
    GatewayCommunicationPacket packet;
    char event_id[37];
    uint8_t reserved[3];
    uint32_t trigger_timestamp;
    uint8_t signature[80];
    uint8_t signature_length;
};

struct PendingPacket {
    CommunicationPacket packet;
    char event_id[37];
    uint32_t trigger_timestamp;
    uint8_t signature[80];
    uint8_t signature_length;
    bool hasSignedSos;
};

static_assert(sizeof(GatewayCommunicationPacket) == 64,
              "Gateway packet layout must match the child firmware");
static_assert(offsetof(GatewaySosCommunicationPacket, event_id) == 64,
              "SOS event ID protocol offset mismatch");
static_assert(offsetof(GatewaySosCommunicationPacket, trigger_timestamp) == 104,
              "SOS timestamp protocol offset mismatch");
static_assert(offsetof(GatewaySosCommunicationPacket, signature) == 108,
              "SOS signature protocol offset mismatch");
static_assert(sizeof(GatewaySosCommunicationPacket) == 192,
              "SOS packet layout must match the child firmware");

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

    if (len != sizeof(GatewaySosCommunicationPacket) &&
        len != sizeof(GatewayCommunicationPacket) &&
        len != sizeof(CommunicationPacket)) {
        Serial.printf("[ESP-NOW Error] Size mismatch! Expected %d, %d or %d bytes, got %d bytes\n",
                      (int)sizeof(GatewaySosCommunicationPacket),
                      (int)sizeof(GatewayCommunicationPacket), (int)sizeof(CommunicationPacket), len);
        return;
    }

    CommunicationPacket packet = {};
    PendingPacket pending = {};

    if (len == sizeof(GatewaySosCommunicationPacket)) {
        GatewaySosCommunicationPacket sosPacket = {};
        memcpy(&sosPacket, incomingData, sizeof(sosPacket));

        if (sosPacket.packet.type != 1 || sosPacket.packet.isGateway ||
            strnlen(sosPacket.event_id, sizeof(sosPacket.event_id)) != 36 ||
            sosPacket.trigger_timestamp == 0 ||
            sosPacket.signature_length == 0 ||
            sosPacket.signature_length > sizeof(sosPacket.signature)) {
            Serial.println("[ESP-NOW Error] Invalid signed SOS packet; dropped.");
            return;
        }

        memcpy(packet.device_id, sosPacket.packet.device_id,
               sizeof(packet.device_id) - 1);
        memcpy(packet.stickerId, sosPacket.packet.stickerId,
               sizeof(packet.stickerId) - 1);
        packet.type = sosPacket.packet.type;
        packet.isGateway = sosPacket.packet.isGateway;
        memcpy(pending.event_id, sosPacket.event_id,
               sizeof(pending.event_id));
        pending.trigger_timestamp = sosPacket.trigger_timestamp;
        memcpy(pending.signature, sosPacket.signature,
               sosPacket.signature_length);
        pending.signature_length = sosPacket.signature_length;
        pending.hasSignedSos = true;
    } else if (len == sizeof(GatewayCommunicationPacket)) {
        GatewayCommunicationPacket gPacket;
        memcpy(&gPacket, incomingData, sizeof(gPacket));
        
        memcpy(packet.device_id, gPacket.device_id,
               sizeof(packet.device_id) - 1);
        memcpy(packet.stickerId, gPacket.stickerId,
               sizeof(packet.stickerId) - 1);
        packet.type = gPacket.type;
        packet.isGateway = gPacket.isGateway;
    } else {
        memcpy(&packet, incomingData, sizeof(packet));
    }

    packet.device_id[36] = '\0';
    packet.stickerId[15] = '\0';
    pending.packet = packet;

    if (!receivedPackets || xQueueSend(receivedPackets, &pending, 0) != pdTRUE) {
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
    receivedPackets = xQueueCreate(8, sizeof(PendingPacket));
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

    PendingPacket pending = {};
    if (xQueueReceive(receivedPackets, &pending, 0) != pdTRUE) {
        return;
    }
    CommunicationPacket& packet = pending.packet;

    Serial.printf("[ESP-NOW Recv] Type: %d, Device: %s, IsGateway: %d\n",
                  packet.type, packet.device_id, packet.isGateway);

    if (packet.type == 0) {
        Serial.printf("[Encounter Detect] Child ID: %s\n", packet.device_id);
        stickerSosMgr.handlePacket(packet, -50);
    } else if (packet.type == 1) {
        Serial.printf("[SOS EMERGENCY] From Child ID: %s\n", packet.device_id);
        if (pending.hasSignedSos) {
            stickerSosMgr.handleSignedSos(
                packet.device_id, pending.event_id,
                pending.trigger_timestamp, pending.signature,
                pending.signature_length);
        } else {
            stickerSosMgr.handleSos(packet.device_id, "ESP-NOW");
        }
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