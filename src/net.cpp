#include "net.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "ui.h"
#include "device.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "lwip/dns.h"
#include <time.h>

static uint32_t lastWifiCheck = 0;

// ── WiFi 초기화 ───────────────────────────────────────────
void initWiFi() {
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    addLog("WiFi connecting...");

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) { delay(500); retry++; }

    if (WiFi.status() == WL_CONNECTED) {
        char log[48];
        snprintf(log, sizeof(log), "STA: %s", WiFi.localIP().toString().c_str());
        addLog(log);
    } else {
        addLog("WiFi FAILED!");
    }

    tcpServer.begin();
    addLog("Bridge port ready");

    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    addLog("NTP syncing...");
    updateWifiUI();
}

// ── 연결 유지 ─────────────────────────────────────────────
void handleNet() {
    if (millis() - lastWifiCheck < 500) return;
    lastWifiCheck = millis();

    if (WiFi.status() != WL_CONNECTED) { updateWifiUI(); return; }

    // DNS 강제 적용 — 사무실 공유기 테스트로 비활성화
    // ip_addr_t dns0 = *dns_getserver(0);
    // if (ip_addr_isany(&dns0)) {
    //     WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
    //                 IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4));
    //     addLog("DNS re-applied");
    // }
}

// ── 브리지로 상태 통보 ────────────────────────────────────
void sendToBridge(uint8_t dev, uint8_t val) {
    if (!bridgeClient || !bridgeClient.connected()) return;

    uint8_t pkt[PKT_SIZE] = {0};
    pkt[PKT_IDX_HEADER] = PKT_HEADER;
    pkt[PKT_IDX_DEV]    = dev;
    pkt[PKT_IDX_VAL]    = val;
    pkt[PKT_IDX_SOURCE] = PKT_SRC_SERVER;
    pkt[PKT_IDX_CHKSUM] = dev ^ val ^ PKT_SRC_SERVER;
    pkt[PKT_IDX_TAIL]   = PKT_TAIL;
    bridgeClient.write(pkt, PKT_SIZE);
}

// ── 브리지 접속 수락 + 수신 ───────────────────────────────
void handleBridge() {
    // 끊김 감지
    if (bridgeClient && !bridgeClient.connected()) {
        bridgeClient.stop();
        bridgeClient = WiFiClient();
        addLog("Bridge disconnected");
        updateMqttUI(false);
    }

    // 접속 수락 (한 대만)
    WiFiClient incoming = tcpServer.accept();
    if (incoming) {
        if (bridgeClient && bridgeClient.connected()) {
            incoming.stop();                     // 이미 연결됨 — 거절
        } else {
            bridgeClient = incoming;
            addLog("Bridge connected!");
            updateMqttUI(true);
            // 현재 상태 4개를 즉시 밀어준다
            sendToBridge(DEV_LIGHT1, light1On ? VAL_ON : VAL_OFF);
            sendToBridge(DEV_LIGHT2, light2On ? VAL_ON : VAL_OFF);
            sendToBridge(DEV_FAN1,   fan1Speed);
            sendToBridge(DEV_FAN2,   fan2Speed);
        }
    }

    // 수신
    if (!bridgeClient || !bridgeClient.connected()) return;

    while (bridgeClient.available() > 0) {
        if (bridgeClient.peek() != PKT_HEADER) { bridgeClient.read(); continue; }
        if (bridgeClient.available() < PKT_SIZE) break;

        uint8_t pkt[PKT_SIZE];
        bridgeClient.read(pkt, PKT_SIZE);

        if (pkt[PKT_IDX_TAIL] != PKT_TAIL) continue;

        uint8_t chk = pkt[PKT_IDX_DEV] ^ pkt[PKT_IDX_VAL] ^ pkt[PKT_IDX_SOURCE];
        if (chk != pkt[PKT_IDX_CHKSUM]) continue;

        // 브리지가 보낸 것만 처리 (자기 패킷 되돌아오는 것 방지)
        if (pkt[PKT_IDX_SOURCE] != PKT_SRC_BRIDGE) continue;

        applyDevice(pkt[PKT_IDX_DEV], pkt[PKT_IDX_VAL]);
    }
}
