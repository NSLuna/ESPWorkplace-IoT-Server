// ╔══════════════════════════════════════════════════════════════╗
// ║              ESP32 Smart Display - TCP Server                ║
// ║       v6 - 안정화 + publishStatus + 싱크패킷(0xBB)           ║
// ╚══════════════════════════════════════════════════════════════╝
//
// ★ 평문 브로커(broker.hivemq.com:1883) + 안정화 개선 적용본
//   [안정화] 표시: String제거, HTTP타임아웃, MQTT백오프/LWT,
//                  DNS재적용, 힙/스택 모니터링
//   [테스트] 표시: 프로덕션 전환 시 되돌릴 곳 (평문→TLS 등)

#include <Arduino.h>
#include "lv_conf.h"
#include <esp32_smartdisplay.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>   // [테스트] 지금은 안 쓰지만 원복 대비 그대로 둠
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include <HTTPClient.h>
#include "lwip/dns.h"          // [안정화] DNS 재적용 체크용

// ── WiFi / TCP 설정 ────────────────────────────────────────
#define WIFI_SSID     "spaceshipA"
#define WIFI_PASSWORD "spaceshipA"
#define AP_SSID       "ESP32-Server"
#define AP_PASSWORD   "12345678"
#define TCP_PORT      8080

// ── MQTT 설정 ─────────────────────────────────────────────
// ▼▼▼ [테스트] 공개 평문 브로커 (인증 없음, TLS 없음) ▼▼▼
#define MQTT_BROKER          "broker.hivemq.com"
#define MQTT_PORT            1883
#define MQTT_ID              "esp32-server"
#define MQTT_TOPIC_CONTROL   "esp32/control"
#define MQTT_TOPIC_STATUS    "esp32/status"
// ▲▲▲ [테스트] 끝 ▲▲▲
//
// ── [원본] HiveMQ Cloud 다시 쓸 때: 위 테스트 블록 지우고 이걸 살려 ──
// #define MQTT_BROKER          "412e7190fb654a908a99255457020e91.s1.eu.hivemq.cloud"
// #define MQTT_PORT            8883
// #define MQTT_USER            "spaceshipA"
// #define MQTT_PASS            "1spaceshipA"
// #define MQTT_ID              "esp32-server"
// #define MQTT_TOPIC_CONTROL   "esp32/control"
// #define MQTT_TOPIC_STATUS    "esp32/status"

// ── 패킷 구조 ─────────────────────────────────────────────
#define PKT_HEADER          0xAA
#define PKT_TAIL            0xFF
#define PKT_SIZE            12
#define PKT_IDX_HEADER      0
#define PKT_IDX_SHAPE       1
#define PKT_IDX_STATE       2
#define PKT_IDX_SOURCE      3
#define PKT_IDX_CHKSUM      4
#define PKT_IDX_TAIL        11
#define PKT_SRC_SERVER      0x01
#define PKT_SRC_CLIENT      0x02
#define PKT_SHAPE_CIRCLE    0x01
#define PKT_SHAPE_TRIANGLE  0x02
#define PKT_SHAPE_SQUARE    0x03
#define PKT_SHAPE_FAN2      0x04
#define PKT_STATE_ON        0x01
#define PKT_STATE_OFF       0x00
#define PKT_FAN_SPD0        0x00
#define PKT_FAN_SPD1        0x01
#define PKT_FAN_SPD2        0x02
#define PKT_FAN_SPD3        0x03
#define MUTEX_TIMEOUT_MS    pdMS_TO_TICKS(100)

// ── 싱크 패킷 (서버→클라이언트 시계/날씨 동기화) ─────────
#define SYNC_HEADER         0xBB
#define SYNC_TAIL           0xFF
#define SYNC_SIZE           12

// ── 색상 정의 (코콤 스타일) ───────────────────────────────
#define C_BG            lv_color_hex(0x0F1129)
#define C_CARD          lv_color_hex(0x1A1D36)
#define C_CARD_BORDER   lv_color_hex(0x2A2D4A)
#define C_LOG_BG        lv_color_hex(0x0D0F22)
#define C_TEXT          lv_color_hex(0xE0E8F0)
#define C_TEXT_DIM      lv_color_hex(0x888899)
#define C_TEXT_DARK     lv_color_hex(0x555877)
#define C_BLUE_ON       lv_color_hex(0x00C8F5)
#define C_BLUE_CARD     lv_color_hex(0x111D2E)
#define C_PURPLE_ON     lv_color_hex(0x7C3AED)
#define C_PURPLE_CARD   lv_color_hex(0x16112E)
#define C_GREEN         lv_color_hex(0x00FF88)
#define C_RED           lv_color_hex(0xFF4444)
#define C_YELLOW        lv_color_hex(0xFFAA00)
#define C_BTN_OFF_BG    lv_color_hex(0x1E2038)
#define C_BTN_OFF_BD    lv_color_hex(0x2A2D4A)
#define C_BTN_ON_BG     lv_color_hex(0x3D2FA0)
#define C_BTN_ON_BD     lv_color_hex(0x7C6AED)
#define C_SPD_SEL_BG    lv_color_hex(0x003D5A)
#define C_LOG_TX        lv_color_hex(0x00FF88)
#define C_LOG_RX        lv_color_hex(0x00C8F5)
#define C_LOG_MQTT      lv_color_hex(0xFFAA00)


// ── 버튼→패킷 큐 ──────────────────────────────────────────
struct PktCmd { uint8_t shape; uint8_t state; };
static QueueHandle_t pktQueue = NULL;

// ── TCP 서버 ──────────────────────────────────────────────
WiFiServer tcpServer(TCP_PORT);
WiFiClient clients[4];
static uint8_t  clientCount        = 0;
static uint32_t lastWifiCheck      = 0;

// ── MQTT 클라이언트 ───────────────────────────────────────
// [테스트] 평문이라 WiFiClient 사용 (원복: WiFiClientSecure mqttWifi;)
WiFiClient       mqttWifi;
PubSubClient     mqttClient(mqttWifi);
static uint32_t  lastMqttReconnect = 0;

// ── WiFi UI 캐시 ──────────────────────────────────────────
static char lastDisplayedSTA[24] = "";
static int  lastDisplayedCount   = -1;

// ── 도형 상태 ─────────────────────────────────────────────
static bool    circleOn   = false;
static bool    triangleOn = false;
static uint8_t fan1Speed  = 0;  // 0~3단
static uint8_t fan2Speed  = 0;  // 0~3단

// ── LVGL 객체 ─────────────────────────────────────────────
// 화면
static lv_obj_t* scrMain     = nullptr;  // 메인 화면
static lv_obj_t* scrSettings = nullptr;  // 설정 화면

// 메인 화면
static lv_obj_t* circleBtn    = nullptr;
static lv_obj_t* triangleBtn  = nullptr;
static lv_obj_t* fan1Arc      = nullptr;
static lv_obj_t* fan1Slider   = nullptr;
static lv_obj_t* fan1SpeedLbl = nullptr;
static lv_obj_t* fan2Arc      = nullptr;
static lv_obj_t* fan2Slider   = nullptr;
static lv_obj_t* fan2SpeedLbl = nullptr;
static lv_obj_t* clockLbl     = nullptr;
static lv_obj_t* dateLbl      = nullptr;
static lv_obj_t* weatherLbl   = nullptr;  // 날씨 온도
static lv_obj_t* weatherDesc  = nullptr;  // 날씨 설명
static lv_obj_t* btnAllOff    = nullptr;
static lv_obj_t* btnAllOn     = nullptr;

// 설정 화면
static lv_obj_t* logLabel     = nullptr;
static lv_obj_t* wifiDot      = nullptr;
static lv_obj_t* staLabel     = nullptr;
static lv_obj_t* apLabel      = nullptr;
static lv_obj_t* mqttDot      = nullptr;
static lv_obj_t* mqttLabel    = nullptr;
static lv_obj_t* tabLogPanel  = nullptr;
static lv_obj_t* tabMqttPanel = nullptr;
static lv_obj_t* tabBtnLog    = nullptr;
static lv_obj_t* tabBtnMqtt   = nullptr;

// ── 로그 버퍼 ─────────────────────────────────────────────
static char     logBuffer[512]        = "";
static char     lastLogDisplayed[512] = "";
static uint32_t lastLogUpdate         = 0;

// ── 뮤텍스 ────────────────────────────────────────────────
static SemaphoreHandle_t lvglMutex = NULL;
static SemaphoreHandle_t logMutex  = NULL;

// ── LVGL 틱 콜백 ──────────────────────────────────────────
static uint32_t lvgl_tick_cb() { return (uint32_t)millis(); }

// ── 로그 추가 ─────────────────────────────────────────────
void addLog(const char* msg) {
    if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(20))) {
        char newLog[512];
        snprintf(newLog, sizeof(newLog), "> %s\n%s", msg, logBuffer);
        strncpy(logBuffer, newLog, sizeof(logBuffer) - 1);
        logBuffer[sizeof(logBuffer) - 1] = '\0';
        if (strlen(logBuffer) > 400) {
            char* fn = strchr(logBuffer + 400, '\n');
            if (fn) *fn = '\0'; else logBuffer[400] = '\0';
        }
        xSemaphoreGive(logMutex);
    }
    Serial.println(msg);
}

// ── 날씨 설정 ── (addLog 함수 아래에 위치)
#define WEATHER_LAT  "37.24"    // 용인
#define WEATHER_LON  "127.18"
static uint32_t lastWeatherUpdate = 0;
static bool     weatherLoaded     = false;  // [안정화] 날씨 성공 전 30초 재시도용
static uint8_t  mqttFailCount     = 0;      // [안정화] MQTT 백오프용
static float    lastSyncTemp      = 0.0f;  // 싱크 패킷: 마지막 온도
static int      lastSyncWcode     = -1;    // 싱크 패킷: 마지막 날씨코드

static const char* weatherCodeToStr(int code) {
    if (code == 0)                return "Clear";
    if (code <= 3)                return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code >= 85 && code <= 86) return "Snow";
    if (code >= 95)               return "Storm";
    return "--";
}

// [안정화] String 제거 + 스트림 파싱 + HTTP 타임아웃
void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClient client;
    HTTPClient http;
    // [안정화] String 대신 고정 버퍼 → 힙 파편화 방지
    char url[200];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
        "&current=temperature_2m,weather_code&timezone=auto",
        WEATHER_LAT, WEATHER_LON);
    http.setTimeout(5000);  // [안정화] 5초 타임아웃 → mainTask 블로킹 방지
    http.useHTTP10(true);
    if (!http.begin(client, url)) { addLog("Weather: begin fail"); return; }
    int code = http.GET();
    if (code != 200) {
        char log[32]; snprintf(log, sizeof(log), "Weather HTTP %d", code);
        addLog(log); http.end(); return;
    }
 // 고정 버퍼로 읽기 (String 없음 + 스트림 끊김 방지)
    char buf[512] = "";
    WiFiClient* stream = http.getStreamPtr();
    int idx = 0;
    unsigned long start = millis();
    while (idx < (int)sizeof(buf)-1 && millis() - start < 3000) {
        if (stream->available()) {
            buf[idx++] = stream->read();
            start = millis();
        }
    }
    buf[idx] = '\0';
    http.end();
    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) {
        addLog("Weather: JSON fail"); return;
    }
    float temp  = doc["current"]["temperature_2m"] | 0.0f;
    int   wcode = doc["current"]["weather_code"]    | -1;
    lastSyncTemp  = temp;   // 싱크 패킷용 저장
    lastSyncWcode = wcode;
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f C", temp);
    const char* descStr = weatherCodeToStr(wcode);
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        if (weatherLbl)  lv_label_set_text(weatherLbl, tempBuf);
        if (weatherDesc) lv_label_set_text(weatherDesc, descStr);
        xSemaphoreGive(lvglMutex);
    }
    char log[40]; snprintf(log, sizeof(log), "Weather: %s %s", tempBuf, descStr);
    addLog(log);
    weatherLoaded = true;  // [안정화] 성공 플래그 → 재시도 간격 전환
}

// ── 로그 UI 업데이트 ──────────────────────────────────────
void updateLogUI() {
    if (!logLabel) return;
    if (millis() - lastLogUpdate < 50) return;
    char localBuf[512] = "";
    if (!logMutex || !xSemaphoreTake(logMutex, pdMS_TO_TICKS(20))) return;
    strncpy(localBuf, logBuffer, sizeof(localBuf) - 1);
    xSemaphoreGive(logMutex);
    lastLogUpdate = millis();
    if (strcmp(localBuf, lastLogDisplayed) == 0) return;
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        lv_label_set_text(logLabel, localBuf);
        strncpy(lastLogDisplayed, localBuf, sizeof(lastLogDisplayed) - 1);
        xSemaphoreGive(lvglMutex);
    }
}

// ── WiFi UI 업데이트 ──────────────────────────────────────
void updateWifiUI() {
    if (!staLabel || !apLabel || !wifiDot) return;
    bool connected = (WiFi.status() == WL_CONNECTED);
    char newSTA[24] = "";
    if (connected) snprintf(newSTA, sizeof(newSTA), "%s", WiFi.localIP().toString().c_str());
    if (strcmp(newSTA, lastDisplayedSTA) == 0 && lastDisplayedCount == (int)clientCount) return;
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        if (connected) {
            char staStr[48];
            snprintf(staStr, sizeof(staStr), "%s", newSTA);
            lv_label_set_text(staLabel, staStr);
            lv_obj_set_style_text_color(staLabel, C_GREEN, LV_PART_MAIN);
            lv_obj_set_style_bg_color(wifiDot,   C_GREEN, LV_PART_MAIN);
            char apStr[48];
            snprintf(apStr, sizeof(apStr), "AP: %s [%d]",
                WiFi.softAPIP().toString().c_str(), clientCount);
            lv_label_set_text(apLabel, apStr);
            lv_obj_set_style_text_color(apLabel, C_TEXT_DIM, LV_PART_MAIN);
        } else {
            lv_label_set_text(staLabel, "Connecting...");
            lv_label_set_text(apLabel, "");
            lv_obj_set_style_text_color(staLabel, C_RED, LV_PART_MAIN);
            lv_obj_set_style_bg_color(wifiDot,   C_RED, LV_PART_MAIN);
        }
        strncpy(lastDisplayedSTA, newSTA, sizeof(lastDisplayedSTA) - 1);
        lastDisplayedCount = (int)clientCount;
        xSemaphoreGive(lvglMutex);
    }
}

// ── MQTT UI 업데이트 ──────────────────────────────────────
void updateMqttUI(bool connected) {
    if (!mqttDot || !mqttLabel) return;
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        lv_obj_set_style_bg_color(mqttDot, connected ? C_GREEN : C_RED, LV_PART_MAIN);
        lv_label_set_text(mqttLabel, connected ? "MQTT connected" : "MQTT connecting...");
        lv_obj_set_style_text_color(mqttLabel, connected ? C_GREEN : C_RED, LV_PART_MAIN);
        xSemaphoreGive(lvglMutex);
    }
}

// ── 도형 스타일 업데이트 (카드형) ────────────────────────
void updateCircleStyle() {
    if (!circleBtn) return;
    lv_obj_t* card = lv_obj_get_parent(circleBtn);
    if (circleOn) {
        lv_obj_add_state(circleBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_BLUE_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_BLUE_ON, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(circleBtn, 0);
        if (lbl) { lv_label_set_text(lbl, "ON");
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);     
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xC4B8FF), LV_PART_MAIN); }
    } else {
        lv_obj_clear_state(circleBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_CARD_BORDER, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(circleBtn, 0);
        if (lbl) { lv_label_set_text(lbl, "OFF");
        lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -8, 0); 
        lv_obj_set_style_text_color(lbl, C_TEXT_DARK, LV_PART_MAIN); }
    }
}

void updateTriangleStyle() {
    if (!triangleBtn) return;
    lv_obj_t* card = lv_obj_get_parent(triangleBtn);
    if (triangleOn) {
        lv_obj_add_state(triangleBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_BLUE_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_BLUE_ON, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(triangleBtn, 0);
        if (lbl) { lv_label_set_text(lbl, "ON"); 
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xC4B8FF), LV_PART_MAIN); }
    } else {
        lv_obj_clear_state(triangleBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_CARD_BORDER, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(triangleBtn, 0);
        if (lbl) { lv_label_set_text(lbl, "OFF"); 
        lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_text_color(lbl, C_TEXT_DARK, LV_PART_MAIN); }
    }
}

void updateSquareStyle() {} // 미사용

// ── 전방 선언 ─────────────────────────────────────────────
void updateAllBtnUI();

// ── 팬 속도 UI 업데이트 (다이얼 + 슬라이더 연동) ─────────
void updateFanUI(uint8_t fanIdx, uint8_t speed) {
    lv_obj_t* arc    = (fanIdx == 0) ? fan1Arc      : fan2Arc;
    lv_obj_t* slider = (fanIdx == 0) ? fan1Slider   : fan2Slider;
    lv_obj_t* lbl    = (fanIdx == 0) ? fan1SpeedLbl : fan2SpeedLbl;
    if (!lbl) return;

    // 다이얼 업데이트
    if (arc) lv_arc_set_value(arc, speed);
    // 슬라이더 업데이트
    if (slider) lv_slider_set_value(slider, speed, LV_ANIM_OFF);

    // 속도 레이블
    char buf[8];
    if (speed == 0) snprintf(buf, sizeof(buf), "OFF");
    else            snprintf(buf, sizeof(buf), "%d", speed);
    lv_label_set_text(lbl, buf);

    // 색상
    lv_color_t c = (speed == 0) ? C_BTN_OFF_BD :
                   (speed == 1) ? C_BLUE_ON     :
                   (speed == 2) ? C_YELLOW      : C_RED;
    if (arc) lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    if (slider) {
        lv_obj_set_style_bg_color(slider, c, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, c, LV_PART_KNOB);
    }
    lv_obj_set_style_text_color(lbl, speed == 0 ? C_TEXT_DARK : c, LV_PART_MAIN);
}

// ── 패킷 브로드캐스트 ─────────────────────────────────────
void broadcastPacket(uint8_t shape, uint8_t state) {
    uint8_t pkt[PKT_SIZE] = {0};
    pkt[PKT_IDX_HEADER] = PKT_HEADER;
    pkt[PKT_IDX_SHAPE]  = shape;
    pkt[PKT_IDX_STATE]  = state;
    pkt[PKT_IDX_SOURCE] = PKT_SRC_SERVER;
    pkt[PKT_IDX_CHKSUM] = shape ^ state ^ PKT_SRC_SERVER;
    pkt[PKT_IDX_TAIL]   = PKT_TAIL;
    int sent = 0;
    for (int i = 0; i < 4; i++) {
        if (clients[i] && clients[i].connected()) { clients[i].write(pkt, PKT_SIZE); sent++; }
    }
    char log[48];
    const char* shapeName =
        shape == PKT_SHAPE_CIRCLE   ? "Circle"   :
        shape == PKT_SHAPE_TRIANGLE ? "Triangle" :
        shape == PKT_SHAPE_SQUARE   ? "Fan1"     :
        shape == PKT_SHAPE_FAN2     ? "Fan2"     : "Unknown";
    snprintf(log, sizeof(log), "TX(%d): %s spd=%d", sent, shapeName, state);
    addLog(log);
}


// ── 싱크 패킷 브로드캐스트 (시계+날씨 → 클라이언트) ──────
void broadcastSyncPacket() {
    struct tm t;
    if (!getLocalTime(&t)) return;
    uint8_t pkt[SYNC_SIZE] = {0};
    pkt[0]  = SYNC_HEADER;            // 0xBB
    pkt[1]  = (uint8_t)t.tm_hour;     // HH
    pkt[2]  = (uint8_t)t.tm_min;      // MM
    pkt[3]  = (uint8_t)(t.tm_year % 100); // YY (26)
    pkt[4]  = (uint8_t)(t.tm_mon + 1);    // MM (1~12)
    pkt[5]  = (uint8_t)t.tm_mday;     // DD
    int tempInt = (int)lastSyncTemp;
    int tempDec = (int)((lastSyncTemp - tempInt) * 10);
    if (tempDec < 0) tempDec = -tempDec;
    pkt[6]  = (uint8_t)(tempInt & 0xFF);  // 온도 정수
    pkt[7]  = (uint8_t)(tempDec & 0xFF);  // 온도 소수
    pkt[8]  = (uint8_t)(lastSyncWcode & 0xFF); // WMO code
    pkt[9]  = pkt[1] ^ pkt[2] ^ pkt[3] ^ pkt[4] ^ pkt[5] ^ pkt[6] ^ pkt[7] ^ pkt[8]; // 체크섬
    pkt[10] = 0x00;                    // 예비
    pkt[11] = SYNC_TAIL;              // 0xFF
    for (int i = 0; i < 4; i++) {
        if (clients[i] && clients[i].connected()) {
            clients[i].write(pkt, SYNC_SIZE);
        }
    }
}

// ── MQTT 상태 발행 [안정화] fan1/fan2 구분 + speed 전송 ──
void publishStatus(uint8_t shape, uint8_t state) {
    if (!mqttClient.connected()) return;
    JsonDocument doc;
    const char* shapeName =
        shape == PKT_SHAPE_CIRCLE   ? "circle"   :
        shape == PKT_SHAPE_TRIANGLE ? "triangle" :
        shape == PKT_SHAPE_SQUARE   ? "fan1"     :
        shape == PKT_SHAPE_FAN2     ? "fan2"     : "unknown";
    doc["shape"] = shapeName;
    if (shape == PKT_SHAPE_CIRCLE || shape == PKT_SHAPE_TRIANGLE) {
        doc["state"] = (state == PKT_STATE_ON) ? "on" : "off";
    } else {
        doc["speed"] = (int)state;
    }
    char payload[64];
    serializeJson(doc, payload);
    mqttClient.publish(MQTT_TOPIC_STATUS, payload);
    char log[48];
    snprintf(log, sizeof(log), "MQTT PUB: %s", payload);
    addLog(log);
}

// ── MQTT 수신 콜백 ────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char msg[128] = "";
    for (unsigned int i = 0; i < length && i < sizeof(msg)-1; i++) msg[i] = (char)payload[i];
    char log[48];
    snprintf(log, sizeof(log), "MQTT RX: %s", msg);
    addLog(log);
    JsonDocument doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) return;
    const char* shapeStr = doc["shape"];
    if (!shapeStr) return;

    uint8_t shape = 0;
    if      (strcmp(shapeStr, "circle")   == 0) shape = PKT_SHAPE_CIRCLE;
    else if (strcmp(shapeStr, "triangle") == 0) shape = PKT_SHAPE_TRIANGLE;
    else if (strcmp(shapeStr, "fan1")     == 0) shape = PKT_SHAPE_SQUARE;
    else if (strcmp(shapeStr, "fan2")     == 0) shape = PKT_SHAPE_FAN2;
    else return;

    uint8_t state = 0;
    // 전등: on/off / 팬: 0~3단
    if (shape == PKT_SHAPE_CIRCLE || shape == PKT_SHAPE_TRIANGLE) {
        const char* stateStr = doc["state"];
        if (!stateStr) return;
        state = (strcmp(stateStr, "on") == 0) ? PKT_STATE_ON : PKT_STATE_OFF;
    } else {
        state = (uint8_t)(int)doc["speed"];
        if (state > 3) state = 0;
    }

    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        switch (shape) {
            case PKT_SHAPE_CIRCLE:   circleOn   = (state == PKT_STATE_ON); updateCircleStyle();   break;
            case PKT_SHAPE_TRIANGLE: triangleOn = (state == PKT_STATE_ON); updateTriangleStyle(); break;
            case PKT_SHAPE_SQUARE:   fan1Speed  = state; updateFanUI(0, fan1Speed); break;
            case PKT_SHAPE_FAN2:     fan2Speed  = state; updateFanUI(1, fan2Speed); break;
        }
        xSemaphoreGive(lvglMutex);
    }
    broadcastPacket(shape, state);
    publishStatus(shape, state);
}

// ── MQTT 연결 ─────────────────────────────────────────────
// ── MQTT 연결 [안정화] 백오프 + Last Will ─────────────────
void connectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqttClient.connected()) return;
    // [안정화] 실패 횟수에 따라 재시도 간격 증가 (5→10→20→40→60초)
    uint32_t backoff = 5000 * (1 << mqttFailCount);
    if (backoff > 60000) backoff = 60000;
    if (millis() - lastMqttReconnect < backoff) return;
    lastMqttReconnect = millis();
    // [테스트] 공개 브로커: 유니크 client ID + Last Will
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "esp32srv-%06X",
             (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));
    // [안정화] Last Will: 서버 죽으면 HiveMQ가 자동으로 offline 발행
    if (mqttClient.connect(clientId, NULL, NULL,
            MQTT_TOPIC_STATUS, 0, true,
            "{\"server\":\"offline\"}")) {
        mqttFailCount = 0;  // [안정화] 성공 시 백오프 리셋
        addLog("MQTT Connected!");
        mqttClient.subscribe(MQTT_TOPIC_CONTROL);
        // [안정화] 접속 직후 online 상태 발행 (retain)
        mqttClient.publish(MQTT_TOPIC_STATUS, "{\"server\":\"online\"}", true);
        updateMqttUI(true);
    } else {
        if (mqttFailCount < 5) mqttFailCount++;  // [안정화] 최대 5단계
        char log[48];
        snprintf(log, sizeof(log), "MQTT FAILED: %d (retry %ds)",
                 mqttClient.state(), (int)(backoff/1000));
        addLog(log);
        updateMqttUI(false);
    }
}

// ── TCP 패킷 파싱 ─────────────────────────────────────────
void parsePacket(uint8_t* pkt) {
    if (pkt[PKT_IDX_HEADER] != PKT_HEADER) return;
    if (pkt[PKT_IDX_TAIL]   != PKT_TAIL)   return;
    if ((pkt[PKT_IDX_SHAPE] ^ pkt[PKT_IDX_STATE] ^ pkt[PKT_IDX_SOURCE]) != pkt[PKT_IDX_CHKSUM]) return;
    if (pkt[PKT_IDX_SOURCE] == PKT_SRC_SERVER) return;
    uint8_t shape = pkt[PKT_IDX_SHAPE];
    uint8_t state = pkt[PKT_IDX_STATE];
    char log[48];
    const char* sn = shape == PKT_SHAPE_CIRCLE ? "Circle" : shape == PKT_SHAPE_TRIANGLE ? "Triangle" :
                     shape == PKT_SHAPE_SQUARE  ? "Fan1"  : shape == PKT_SHAPE_FAN2     ? "Fan2" : "?";
    snprintf(log, sizeof(log), "RX: %s state=%d", sn, state);
    addLog(log);
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        switch (shape) {
            case PKT_SHAPE_CIRCLE:   circleOn   = (state == PKT_STATE_ON); updateCircleStyle();   break;
            case PKT_SHAPE_TRIANGLE: triangleOn = (state == PKT_STATE_ON); updateTriangleStyle(); break;
            case PKT_SHAPE_SQUARE:   fan1Speed  = state; updateFanUI(0, fan1Speed); break;
            case PKT_SHAPE_FAN2:     fan2Speed  = state; updateFanUI(1, fan2Speed); break;
        }
        xSemaphoreGive(lvglMutex);
    }
    broadcastPacket(shape, state);
    publishStatus(shape, state);
}

// ── 버튼 콜백 ─────────────────────────────────────────────
static void onCircleBtn(lv_event_t* e) {
    circleOn = !circleOn; updateCircleStyle();
    updateAllBtnUI();
    PktCmd cmd = { PKT_SHAPE_CIRCLE, (uint8_t)(circleOn ? PKT_STATE_ON : PKT_STATE_OFF) };
    xQueueSend(pktQueue, &cmd, 0);
}
static void onTriangleBtn(lv_event_t* e) {
    triangleOn = !triangleOn; updateTriangleStyle();
    updateAllBtnUI();
    PktCmd cmd = { PKT_SHAPE_TRIANGLE, (uint8_t)(triangleOn ? PKT_STATE_ON : PKT_STATE_OFF) };
    xQueueSend(pktQueue, &cmd, 0);
}

// ── 팬 Arc 콜백 ───────────────────────────────────────────
static void onFan1Arc(lv_event_t* e) {
    fan1Speed = (uint8_t)lv_arc_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(0, fan1Speed);
    updateAllBtnUI();
    PktCmd cmd = { PKT_SHAPE_SQUARE, fan1Speed };
    xQueueSend(pktQueue, &cmd, 0);
}
static void onFan2Arc(lv_event_t* e) {
    fan2Speed = (uint8_t)lv_arc_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(1, fan2Speed);
    updateAllBtnUI();
    PktCmd cmd = { PKT_SHAPE_FAN2, fan2Speed };
    xQueueSend(pktQueue, &cmd, 0);
}

// ── 팬 슬라이더 콜백 ─────────────────────────────────────
static void onFan1Slider(lv_event_t* e) {
    fan1Speed = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(0, fan1Speed);
    updateAllBtnUI();
    PktCmd cmd = { PKT_SHAPE_SQUARE, fan1Speed };
    xQueueSend(pktQueue, &cmd, 0);
}
static void onFan2Slider(lv_event_t* e) {
    fan2Speed = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(1, fan2Speed);
    updateAllBtnUI();
    PktCmd cmd = { PKT_SHAPE_FAN2, fan2Speed };
    xQueueSend(pktQueue, &cmd, 0);
}

// ── All 버튼 UI 업데이트 ──────────────────────────────────
// 전부 OFF → AllOff 파랑 / AllOn 회색
// 전부 ON  → AllOff 회색 / AllOn 파랑
// 중간      → AllOff 회색 / AllOn 초록
void updateAllBtnUI() {
    if (!btnAllOff || !btnAllOn) return;
    bool allOff = !circleOn && !triangleOn && fan1Speed == 0 && fan2Speed == 0;
    bool allOn  =  circleOn &&  triangleOn && fan1Speed == 3 && fan2Speed == 3;

    // All OFF 버튼
    lv_color_t offBg = allOff ? C_BLUE_ON    : C_BTN_OFF_BG;
    lv_color_t offBd = allOff ? C_BLUE_ON    : C_BTN_OFF_BD;
    lv_color_t offTx = allOff ? C_TEXT       : C_TEXT_DARK;
    lv_obj_set_style_bg_color(btnAllOff,     offBg, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOff, offBd, LV_PART_MAIN);
    lv_obj_t* offLbl = lv_obj_get_child(btnAllOff, 0);
    if (offLbl) lv_obj_set_style_text_color(offLbl, offTx, LV_PART_MAIN);

    // All ON 버튼
    lv_color_t onBg = allOn  ? C_BLUE_ON :
                      allOff ? C_BTN_OFF_BG : C_GREEN;
    lv_color_t onBd = allOn  ? C_BLUE_ON :
                      allOff ? C_BTN_OFF_BD : C_GREEN;
    lv_color_t onTx = (allOff && !allOn) ? C_TEXT_DARK : C_TEXT;
    lv_obj_set_style_bg_color(btnAllOn,     onBg, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOn, onBd, LV_PART_MAIN);
    lv_obj_t* onLbl = lv_obj_get_child(btnAllOn, 0);
    if (onLbl) lv_obj_set_style_text_color(onLbl, onTx, LV_PART_MAIN);
}

// ── All OFF 콜백 ──────────────────────────────────────────
static void onAllOff(lv_event_t* e) {
    circleOn   = false; updateCircleStyle();
    triangleOn = false; updateTriangleStyle();
    fan1Speed  = 0;     updateFanUI(0, 0);
    fan2Speed  = 0;     updateFanUI(1, 0);
    updateAllBtnUI();
    PktCmd cmds[4] = {
        { PKT_SHAPE_CIRCLE,   PKT_STATE_OFF },
        { PKT_SHAPE_TRIANGLE, PKT_STATE_OFF },
        { PKT_SHAPE_SQUARE,   0 },
        { PKT_SHAPE_FAN2,     0 },
    };
    for (auto& c : cmds) xQueueSend(pktQueue, &c, 0);
}

// ── All ON 콜백 ───────────────────────────────────────────
static void onAllOn(lv_event_t* e) {
    circleOn   = true; updateCircleStyle();
    triangleOn = true; updateTriangleStyle();
    fan1Speed  = 3;    updateFanUI(0, 3);
    fan2Speed  = 3;    updateFanUI(1, 3);
    updateAllBtnUI();
    PktCmd cmds[4] = {
        { PKT_SHAPE_CIRCLE,   PKT_STATE_ON },
        { PKT_SHAPE_TRIANGLE, PKT_STATE_ON },
        { PKT_SHAPE_SQUARE,   3 },
        { PKT_SHAPE_FAN2,     3 },
    };
    for (auto& c : cmds) xQueueSend(pktQueue, &c, 0);
}


// ── 화면 전환 콜백 ────────────────────────────────────────
static void onGoSettings(lv_event_t* e) { lv_scr_load(scrSettings); }
static void onGoMain(lv_event_t* e) { lv_scr_load(scrMain); }

static void onTabLog(lv_event_t* e) {
    lv_obj_set_style_bg_color(tabBtnLog,  C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnMqtt, C_CARD,        LV_PART_MAIN);
    lv_obj_clear_flag(tabLogPanel,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tabMqttPanel,   LV_OBJ_FLAG_HIDDEN);
}
static void onTabMqtt(lv_event_t* e) {
    lv_obj_set_style_bg_color(tabBtnMqtt, C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnLog,  C_CARD,        LV_PART_MAIN);
    lv_obj_clear_flag(tabMqttPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tabLogPanel,    LV_OBJ_FLAG_HIDDEN);
}

// ── 헬퍼: 카드 생성 ──────────────────────────────────────
static lv_obj_t* makeCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card,     C_CARD,        LV_PART_MAIN);
    lv_obj_set_style_border_color(card, C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1,             LV_PART_MAIN);
    lv_obj_set_style_radius(card,       10,            LV_PART_MAIN);
    lv_obj_set_style_pad_all(card,      0,             LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

// ── 헬퍼: 제어 카드 (아이콘 + 이름 + 버튼) ───────────────
static lv_obj_t* makeCtrlCard(lv_obj_t* scr,
                                lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                                const char* cardTitle, const char* symbol, const char* name,
                                lv_event_cb_t cb, lv_obj_t** btnOut) {
    lv_obj_t* card = makeCard(scr, x, y, w, h);
    // 카드 전체 터치 인식
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

    // 카드 제목
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, cardTitle);
    lv_obj_set_style_text_color(title, C_TEXT_DIM,           LV_PART_MAIN);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(title, 12, 10);

    // 아이콘 원
    lv_obj_t* icon = lv_obj_create(card);
    lv_obj_set_pos(icon, w/2 - 26, 32);
    lv_obj_set_size(icon, 52, 52);
    lv_obj_set_style_radius(icon,       LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon,     C_BTN_OFF_BG,     LV_PART_MAIN);
    lv_obj_set_style_border_color(icon, C_BTN_OFF_BD,     LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 2,                LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon,      0,                LV_PART_MAIN);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* iconLbl = lv_label_create(icon);
    lv_label_set_text(iconLbl, symbol);
    lv_obj_set_style_text_font(iconLbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(iconLbl, C_TEXT_DARK,           LV_PART_MAIN);
    lv_obj_center(iconLbl);

    // 이름
    lv_obj_t* nameLbl = lv_label_create(card);
    lv_label_set_text(nameLbl, name);
    lv_obj_set_style_text_color(nameLbl, C_TEXT,               LV_PART_MAIN);
    lv_obj_set_style_text_font(nameLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(nameLbl, w/2 - 30, 92);

    // ON/OFF 버튼
    // 토글 스위치 (아이폰 스타일)
    lv_obj_t* sw = lv_switch_create(card);
    lv_obj_set_pos(sw, w/2 - 40, 122);
    lv_obj_set_size(sw, 100, 38);
    lv_obj_set_style_radius(sw, 16, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, 16, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sw, 14, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, C_BTN_ON_BD, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, 3, LV_PART_MAIN);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);  // 카드 터치로만 동작
    lv_obj_t* swLbl = lv_label_create(sw);
    lv_label_set_text(swLbl, "OFF");
    lv_obj_set_style_text_color(swLbl, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(swLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(swLbl);
    if (btnOut) *btnOut = sw;
    return card; 
}
// ── 설정 화면 빌드 ────────────────────────────────────────
void buildSettingsUI() {
    scrSettings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scrSettings, C_BG, LV_PART_MAIN);
    lv_obj_clear_flag(scrSettings, LV_OBJ_FLAG_SCROLLABLE);

    // 상단 바
    lv_obj_t* topBar = lv_obj_create(scrSettings);
    lv_obj_set_pos(topBar, 0, 0);
    lv_obj_set_size(topBar, 800, 50);
    lv_obj_set_style_bg_color(topBar,     C_CARD,        LV_PART_MAIN);
    lv_obj_set_style_border_width(topBar, 0,             LV_PART_MAIN);
    lv_obj_set_style_radius(topBar,       0,             LV_PART_MAIN);
    lv_obj_set_style_pad_all(topBar,      0,             LV_PART_MAIN);
    lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    // 뒤로가기 버튼
    lv_obj_t* backBtn = lv_obj_create(topBar);
    lv_obj_set_pos(backBtn, 8, 8);
    lv_obj_set_size(backBtn, 80, 34);
    lv_obj_set_style_radius(backBtn,       17,           LV_PART_MAIN);
    lv_obj_set_style_bg_color(backBtn,     C_BTN_OFF_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(backBtn, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(backBtn, 1,            LV_PART_MAIN);
    lv_obj_set_style_pad_all(backBtn,      0,            LV_PART_MAIN);
    lv_obj_add_flag(backBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(backBtn, onGoMain, LV_EVENT_CLICKED, NULL);
    lv_obj_t* backLbl = lv_label_create(backBtn);
    lv_label_set_text(backLbl, "< Back");
    lv_obj_set_style_text_color(backLbl, C_TEXT,               LV_PART_MAIN);
    lv_obj_set_style_text_font(backLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(backLbl);

    lv_obj_t* titleLbl = lv_label_create(topBar);
    lv_label_set_text(titleLbl, "Settings");
    lv_obj_set_style_text_color(titleLbl, C_TEXT,              LV_PART_MAIN);
    lv_obj_set_style_text_font(titleLbl,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(titleLbl, LV_ALIGN_CENTER, 0, 0);

    // WiFi 상태 카드
    lv_obj_t* wifiCard = makeCard(scrSettings, 8, 58, 380, 110);
    lv_obj_t* wTitleLbl = lv_label_create(wifiCard);
    lv_label_set_text(wTitleLbl, "WiFi Status");
    lv_obj_set_style_text_color(wTitleLbl, C_TEXT_DARK,          LV_PART_MAIN);
    lv_obj_set_style_text_font(wTitleLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(wTitleLbl, 10, 8);

    wifiDot = lv_obj_create(wifiCard);
    lv_obj_set_pos(wifiDot, 10, 34);
    lv_obj_set_size(wifiDot, 8, 8);
    lv_obj_set_style_radius(wifiDot,       LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(wifiDot,     C_RED,            LV_PART_MAIN);
    lv_obj_set_style_border_width(wifiDot, 0,                LV_PART_MAIN);
    staLabel = lv_label_create(wifiCard);
    lv_label_set_text(staLabel, "Connecting...");
    lv_obj_set_style_text_color(staLabel, C_RED,               LV_PART_MAIN);
    lv_obj_set_style_text_font(staLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(staLabel, 24, 30);

    mqttDot = lv_obj_create(wifiCard);
    lv_obj_set_pos(mqttDot, 10, 60);
    lv_obj_set_size(mqttDot, 8, 8);
    lv_obj_set_style_radius(mqttDot,       LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(mqttDot,     C_RED,            LV_PART_MAIN);
    lv_obj_set_style_border_width(mqttDot, 0,                LV_PART_MAIN);
    mqttLabel = lv_label_create(wifiCard);
    lv_label_set_text(mqttLabel, "MQTT connecting...");
    lv_obj_set_style_text_color(mqttLabel, C_RED,               LV_PART_MAIN);
    lv_obj_set_style_text_font(mqttLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(mqttLabel, 24, 56);

    apLabel = lv_label_create(wifiCard);
    lv_label_set_text(apLabel, "");
    lv_obj_set_style_text_color(apLabel, C_TEXT_DIM,           LV_PART_MAIN);
    lv_obj_set_style_text_font(apLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(apLabel, 10, 84);

    // 로그 탭 바
    lv_obj_t* tabBar = makeCard(scrSettings, 8, 176, 784, 34);
    lv_obj_set_style_radius(tabBar, 6, LV_PART_MAIN);

    tabBtnLog = lv_obj_create(tabBar);
    lv_obj_set_pos(tabBtnLog, 2, 2);
    lv_obj_set_size(tabBtnLog, 120, 28);
    lv_obj_set_style_radius(tabBtnLog,       5,             LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnLog,     C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabBtnLog, 0,             LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabBtnLog,      0,             LV_PART_MAIN);
    lv_obj_add_flag(tabBtnLog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tabBtnLog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tabBtnLog, onTabLog, LV_EVENT_CLICKED, NULL);
    lv_obj_t* tlLbl = lv_label_create(tabBtnLog);
    lv_label_set_text(tlLbl, "TX/RX Log");
    lv_obj_set_style_text_color(tlLbl, C_TEXT,               LV_PART_MAIN);
    lv_obj_set_style_text_font(tlLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(tlLbl);

    tabBtnMqtt = lv_obj_create(tabBar);
    lv_obj_set_pos(tabBtnMqtt, 126, 2);
    lv_obj_set_size(tabBtnMqtt, 120, 28);
    lv_obj_set_style_radius(tabBtnMqtt,       5,      LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnMqtt,     C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabBtnMqtt, 0,      LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabBtnMqtt,      0,      LV_PART_MAIN);
    lv_obj_add_flag(tabBtnMqtt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tabBtnMqtt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tabBtnMqtt, onTabMqtt, LV_EVENT_CLICKED, NULL);
    lv_obj_t* tmLbl = lv_label_create(tabBtnMqtt);
    lv_label_set_text(tmLbl, "MQTT");
    lv_obj_set_style_text_color(tmLbl, C_TEXT_DIM,           LV_PART_MAIN);
    lv_obj_set_style_text_font(tmLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(tmLbl);

    // 로그 영역
    lv_obj_t* logArea = makeCard(scrSettings, 8, 216, 784, 256);
    lv_obj_set_style_bg_color(logArea, C_LOG_BG, LV_PART_MAIN);
    lv_obj_set_style_pad_all(logArea, 8, LV_PART_MAIN);

    tabLogPanel = lv_obj_create(logArea);
    lv_obj_set_size(tabLogPanel, 768, 240);
    lv_obj_set_style_bg_color(tabLogPanel,     C_LOG_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabLogPanel, 0,        LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabLogPanel,      0,        LV_PART_MAIN);
    lv_obj_clear_flag(tabLogPanel, LV_OBJ_FLAG_SCROLLABLE);
    logLabel = lv_label_create(tabLogPanel);
    lv_label_set_text(logLabel, "");
    lv_obj_set_style_text_color(logLabel, C_LOG_TX,           LV_PART_MAIN);
    lv_obj_set_style_text_font(logLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(logLabel, 768);

    tabMqttPanel = lv_obj_create(logArea);
    lv_obj_set_size(tabMqttPanel, 768, 240);
    lv_obj_set_style_bg_color(tabMqttPanel,     C_LOG_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabMqttPanel, 0,        LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabMqttPanel,      0,        LV_PART_MAIN);
    lv_obj_clear_flag(tabMqttPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tabMqttPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* mqttLogLabel = lv_label_create(tabMqttPanel);
    lv_label_set_text(mqttLogLabel, "");
    lv_obj_set_style_text_color(mqttLogLabel, C_LOG_MQTT,          LV_PART_MAIN);
    lv_obj_set_style_text_font(mqttLogLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(mqttLogLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mqttLogLabel, 768);
}

// ── 메인 화면 빌드 ────────────────────────────────────────
void buildMainUI() {
    scrMain = lv_obj_create(NULL);
    lv_obj_t* scr = scrMain;
    lv_obj_set_style_bg_color(scr, C_BG, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ════════════════ 좌측 패널 ════════════════
    // 날짜/시계
    dateLbl = lv_label_create(scr);
    lv_label_set_text(dateLbl, "--.--.--");
    lv_obj_set_style_text_color(dateLbl, C_TEXT_DIM,           LV_PART_MAIN);
    lv_obj_set_style_text_font(dateLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(dateLbl, 12, 12);

    clockLbl = lv_label_create(scr);
    lv_label_set_text(clockLbl, "--:--");
    lv_obj_set_style_text_color(clockLbl, C_TEXT,              LV_PART_MAIN);
    lv_obj_set_style_text_font(clockLbl,  &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_pos(clockLbl, 8, 30);

    lv_obj_t* weatherCard = makeCard(scr, 8, 96, 212, 80);
    weatherLbl = lv_label_create(weatherCard);
    lv_label_set_text(weatherLbl, "-- C");
    lv_obj_set_style_text_color(weatherLbl, C_TEXT,              LV_PART_MAIN);
    lv_obj_set_style_text_font(weatherLbl,  &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_pos(weatherLbl, 12, 10);
    weatherDesc = lv_label_create(weatherCard);
    lv_label_set_text(weatherDesc, "Loading...");
    lv_obj_set_style_text_color(weatherDesc, C_TEXT_DIM,           LV_PART_MAIN);
    lv_obj_set_style_text_font(weatherDesc,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(weatherDesc, 12, 46);

    // All OFF / All ON 버튼 (맨 아래)
    btnAllOff = lv_obj_create(scr);
    lv_obj_set_pos(btnAllOff, 8, 420);
    lv_obj_set_size(btnAllOff, 100, 44);
    lv_obj_set_style_radius(btnAllOff,       22,           LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnAllOff,     C_BTN_OFF_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOff, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(btnAllOff, 1,            LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnAllOff,      0,            LV_PART_MAIN);
    lv_obj_add_flag(btnAllOff, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btnAllOff, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btnAllOff, onAllOff, LV_EVENT_CLICKED, NULL);
    lv_obj_t* offLbl = lv_label_create(btnAllOff);
    lv_label_set_text(offLbl, "All OFF");
    lv_obj_set_style_text_color(offLbl, C_TEXT_DARK,           LV_PART_MAIN);
    lv_obj_set_style_text_font(offLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(offLbl);

    btnAllOn = lv_obj_create(scr);
    lv_obj_set_pos(btnAllOn, 116, 420);
    lv_obj_set_size(btnAllOn, 100, 44);
    lv_obj_set_style_radius(btnAllOn,       22,           LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnAllOn,     C_BTN_ON_BG,  LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOn, C_BTN_ON_BD,  LV_PART_MAIN);
    lv_obj_set_style_border_width(btnAllOn, 1,            LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnAllOn,      0,            LV_PART_MAIN);
    lv_obj_add_flag(btnAllOn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btnAllOn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btnAllOn, onAllOn, LV_EVENT_CLICKED, NULL);
    lv_obj_t* onLbl = lv_label_create(btnAllOn);
    lv_label_set_text(onLbl, "All ON");
    lv_obj_set_style_text_color(onLbl, lv_color_hex(0xC4B8FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(onLbl,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(onLbl);

    // 설정 톱니바퀴 버튼 (우측 하단)
    lv_obj_t* gearBtn = lv_obj_create(scr);
    lv_obj_set_pos(gearBtn, 748, 436);
    lv_obj_set_size(gearBtn, 44, 44);
    lv_obj_set_style_radius(gearBtn,       LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gearBtn,     C_CARD,           LV_PART_MAIN);
    lv_obj_set_style_border_color(gearBtn, C_CARD_BORDER,    LV_PART_MAIN);
    lv_obj_set_style_border_width(gearBtn, 1,                LV_PART_MAIN);
    lv_obj_set_style_pad_all(gearBtn,      0,                LV_PART_MAIN);
    lv_obj_add_flag(gearBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(gearBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gearBtn, onGoSettings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* gearLbl = lv_label_create(gearBtn);
    lv_label_set_text(gearLbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gearLbl, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(gearLbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(gearLbl);

    // ════════════════ 우측 카드 2x2 ════════════════
    lv_obj_t* c1 = makeCtrlCard(scr, 228, 8, 272, 210,
                                  "Light 1", "O", "",
                                  onCircleBtn, &circleBtn);
    lv_obj_t* c2 = makeCtrlCard(scr, 508, 8, 272, 210,
                                  "Light 2", "^", "",
                                  onTriangleBtn, &triangleBtn);

    // Fan 1
    lv_obj_t* c3 = makeCard(scr, 228, 226, 272, 210);
    lv_obj_t* c3Title = lv_label_create(c3);
    lv_label_set_text(c3Title, "");
    lv_obj_set_style_text_color(c3Title, C_TEXT_DIM,           LV_PART_MAIN);
    lv_obj_set_style_text_font(c3Title,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(c3Title, 12, 8);
    fan1Arc = lv_arc_create(c3);
    lv_obj_set_size(fan1Arc, 100, 100);
    lv_obj_set_pos(fan1Arc, 86, 20);
    lv_arc_set_range(fan1Arc, 0, 3);
    lv_arc_set_value(fan1Arc, 0);
    lv_arc_set_bg_angles(fan1Arc, 135, 405);
    lv_obj_set_style_arc_color(fan1Arc, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_arc_color(fan1Arc, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(fan1Arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(fan1Arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan1Arc,  C_CARD, LV_PART_KNOB);
    lv_obj_set_style_pad_all(fan1Arc,   4,      LV_PART_KNOB);
    lv_obj_add_event_cb(fan1Arc, onFan1Arc, LV_EVENT_VALUE_CHANGED, NULL);
    fan1SpeedLbl = lv_label_create(fan1Arc);
    lv_label_set_text(fan1SpeedLbl, "OFF");
    lv_obj_set_style_text_color(fan1SpeedLbl, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(fan1SpeedLbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(fan1SpeedLbl);
    fan1Slider = lv_slider_create(c3);
    lv_obj_set_pos(fan1Slider, 16, 148);
    lv_obj_set_size(fan1Slider, 240, 20);
    lv_slider_set_range(fan1Slider, 0, 3);
    lv_slider_set_value(fan1Slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fan1Slider, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fan1Slider, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan1Slider, C_BTN_OFF_BD, LV_PART_KNOB);
    lv_obj_set_style_radius(fan1Slider,   10, LV_PART_MAIN);
    lv_obj_set_style_radius(fan1Slider,   10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(fan1Slider,  6,  LV_PART_KNOB);
    lv_obj_add_event_cb(fan1Slider, onFan1Slider, LV_EVENT_VALUE_CHANGED, NULL);
    const char* spdLbls[] = {"0","1","2","3"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* sl = lv_label_create(c3);
        lv_label_set_text(sl, spdLbls[i]);
        lv_obj_set_style_text_color(sl, C_TEXT_DARK,           LV_PART_MAIN);
        lv_obj_set_style_text_font(sl,  &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(sl, 14 + i*78, 174);
    }
    lv_obj_t* c3Name = lv_label_create(c3);
    lv_label_set_text(c3Name, "Fan 1");
    lv_obj_set_style_text_color(c3Name, C_TEXT_DARK,           LV_PART_MAIN);
    lv_obj_set_style_text_font(c3Name,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(c3Name, 90, 192);

    // Fan 2
    lv_obj_t* c4 = makeCard(scr, 508, 226, 272, 210);
    lv_obj_t* c4Title = lv_label_create(c4);
    lv_label_set_text(c4Title, "");
    lv_obj_set_style_text_color(c4Title, C_TEXT_DIM,           LV_PART_MAIN);
    lv_obj_set_style_text_font(c4Title,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(c4Title, 12, 8);
    fan2Arc = lv_arc_create(c4);
    lv_obj_set_size(fan2Arc, 100, 100);
    lv_obj_set_pos(fan2Arc, 86, 20);
    lv_arc_set_range(fan2Arc, 0, 3);
    lv_arc_set_value(fan2Arc, 0);
    lv_arc_set_bg_angles(fan2Arc, 135, 405);
    lv_obj_set_style_arc_color(fan2Arc, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_arc_color(fan2Arc, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(fan2Arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(fan2Arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan2Arc,  C_CARD, LV_PART_KNOB);
    lv_obj_set_style_pad_all(fan2Arc,   4,      LV_PART_KNOB);
    lv_obj_add_event_cb(fan2Arc, onFan2Arc, LV_EVENT_VALUE_CHANGED, NULL);
    fan2SpeedLbl = lv_label_create(fan2Arc);
    lv_label_set_text(fan2SpeedLbl, "OFF");
    lv_obj_set_style_text_color(fan2SpeedLbl, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(fan2SpeedLbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(fan2SpeedLbl);
    fan2Slider = lv_slider_create(c4);
    lv_obj_set_pos(fan2Slider, 16, 148);
    lv_obj_set_size(fan2Slider, 240, 20);
    lv_slider_set_range(fan2Slider, 0, 3);
    lv_slider_set_value(fan2Slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fan2Slider, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fan2Slider, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan2Slider, C_BTN_OFF_BD, LV_PART_KNOB);
    lv_obj_set_style_radius(fan2Slider,   10, LV_PART_MAIN);
    lv_obj_set_style_radius(fan2Slider,   10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(fan2Slider,  6,  LV_PART_KNOB);
    lv_obj_add_event_cb(fan2Slider, onFan2Slider, LV_EVENT_VALUE_CHANGED, NULL);
    for (int i = 0; i < 4; i++) {
        lv_obj_t* sl = lv_label_create(c4);
        lv_label_set_text(sl, spdLbls[i]);
        lv_obj_set_style_text_color(sl, C_TEXT_DARK,           LV_PART_MAIN);
        lv_obj_set_style_text_font(sl,  &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(sl, 14 + i*78, 174);
    }
    lv_obj_t* c4Name = lv_label_create(c4);
    lv_label_set_text(c4Name, "Fan2");
    lv_obj_set_style_text_color(c4Name, C_TEXT_DARK,           LV_PART_MAIN);
    lv_obj_set_style_text_font(c4Name,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(c4Name, 82, 192);

    updateCircleStyle();
    updateTriangleStyle();
    updateFanUI(0, 0);
    updateFanUI(1, 0);
    updateAllBtnUI();
}

// ── UI 빌드 (두 화면 생성) ────────────────────────────────
void buildUI() {
    buildSettingsUI();
    buildMainUI();
    lv_scr_load(scrMain);
}


// ── WiFi 초기화 ───────────────────────────────────────────
void initWiFi() {
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    addLog("AP started!");
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    addLog("WiFi connecting...");
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) { delay(500); retry++; }
   if (WiFi.status() == WL_CONNECTED) {
        // ★ DNS 강제 지정 (AP_STA에서 DNS 안 잡히는 문제 우회)
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                    IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4));
        char log[48];
        snprintf(log, sizeof(log), "STA: %s", WiFi.localIP().toString().c_str());
        addLog(log);
    } else {
        addLog("WiFi FAILED!");
    }
    char apLog[48];
    snprintf(apLog, sizeof(apLog), "AP: %s", WiFi.softAPIP().toString().c_str());
    addLog(apLog);
    tcpServer.begin();
    addLog("TCP Server ready!");

    // NTP 시간 동기화
    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    addLog("NTP syncing...");

    updateWifiUI();
}

// ── TCP 클라이언트 관리 ───────────────────────────────────
void handleClients() {
    if (millis() - lastWifiCheck > 50) {
        lastWifiCheck = millis();
        if (WiFi.status() == WL_CONNECTED) {
            // [안정화] WiFi 재접속 후 DNS가 날아갔으면 재적용
            ip_addr_t dns0;
            dns0 = *dns_getserver(0);
            if (ip_addr_isany(&dns0)) {
                WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                            IPAddress(8,8,8,8), IPAddress(8,8,4,4));
                addLog("DNS re-applied");
            }
        } else {
            updateWifiUI();
        }
    }
    for (int i = 0; i < 4; i++) {
        if (clients[i] && !clients[i].connected()) {
            clients[i].stop(); clients[i] = WiFiClient();
            if (clientCount > 0) clientCount--;
            updateWifiUI();
        }
    }
    WiFiClient newClient = tcpServer.accept();
    if (newClient) {
        for (int i = 0; i < 4; i++) {
            if (!clients[i]) {
                clients[i] = newClient; clientCount++;
                char log[32];
                snprintf(log, sizeof(log), "Client %d connected!", i + 1);
                addLog(log); updateWifiUI(); break;
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!clients[i] || !clients[i].connected()) continue;
        while (clients[i].available() > 0) {
            if (clients[i].peek() != PKT_HEADER) { clients[i].read(); continue; }
            if (clients[i].available() < PKT_SIZE) break;
            uint8_t pkt[PKT_SIZE];
            clients[i].read(pkt, PKT_SIZE);
            parsePacket(pkt);
        }
    }
}

// ── LVGL 태스크 (Core 1) ──────────────────────────────────
void lvglTask(void* param) {
    while (true) {
        if (xSemaphoreTake(lvglMutex, portMAX_DELAY)) {
            lv_timer_handler();
            xSemaphoreGive(lvglMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ── 메인 태스크 (Core 0) ──────────────────────────────────
void mainTask(void* param) {
    initWiFi();
    // [안정화] MQTT 설정은 한 번만 (매 reconnect마다 반복하지 않음)
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(60);
    mqttClient.setBufferSize(512);
    connectMQTT();

    static uint32_t lastClock   = 0;
    static uint32_t lastHeapLog = 0;  // [안정화] 힙/스택 모니터링

    while (true) {
        // 1초마다 시계 업데이트
        if (millis() - lastClock > 1000) {
            lastClock = millis();
            struct tm t;
            if (getLocalTime(&t)) {
                char timeBuf[8];
                char dateBuf[20];
                strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
                strftime(dateBuf, sizeof(dateBuf), "%Y.%m.%d", &t);
                if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
                    if (clockLbl) lv_label_set_text(clockLbl, timeBuf);
                    if (dateLbl)  lv_label_set_text(dateLbl,  dateBuf);
                    xSemaphoreGive(lvglMutex);
                }
            }
        }
        // [안정화] 날씨: 성공 전 30초 재시도, 성공 후 10분
        if (WiFi.status() == WL_CONNECTED &&
            (lastWeatherUpdate == 0 || millis() - lastWeatherUpdate >
             (weatherLoaded ? 600000 : 30000))) {
            lastWeatherUpdate = millis();
            fetchWeather();
        }
        // 싱크 패킷: 60초마다 클라이언트에 시계+날씨 전송
        {
            static uint32_t lastSync = 0;
            if (millis() - lastSync > 60000) {
                lastSync = millis();
                broadcastSyncPacket();
            }
        }
        // [안정화] 5분마다 힙/스택 상태 찍기 (장시간 파편화 감시)
        if (millis() - lastHeapLog > 300000) {
            lastHeapLog = millis();
            Serial.printf("[MON] heap=%d largest=%d stack=%d\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                uxTaskGetStackHighWaterMark(NULL));
        }
        PktCmd cmd;
        while (xQueueReceive(pktQueue, &cmd, 0) == pdTRUE) {
            broadcastPacket(cmd.shape, cmd.state);
            publishStatus(cmd.shape, cmd.state);
        }
        if (!mqttClient.connected()) connectMQTT();
        else mqttClient.loop();
        handleClients();
        updateLogUI();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\r\n=== SERVER BOOT ===\r\n");
    if (psramFound()) Serial.printf("[PSRAM] OK: %d bytes\n", ESP.getPsramSize());
    else Serial.println("[PSRAM] NOT FOUND!");
    smartdisplay_init();
    lv_tick_set_cb(lvgl_tick_cb);
    auto disp = lv_disp_get_default();
    lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_180);
    lv_indev_t* touch = lv_indev_get_next(NULL);
    Serial.println(touch ? "[TOUCH] OK" : "[TOUCH] 없음");
    lvglMutex = xSemaphoreCreateMutex();
    logMutex  = xSemaphoreCreateMutex();
    pktQueue  = xQueueCreate(8, sizeof(PktCmd));
    buildUI();
    Serial.println("[SERVER] 준비 완료!");
    xTaskCreatePinnedToCore(lvglTask, "LVGL", 16384, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(mainTask, "Main", 16384, NULL, 1, NULL, 0);
}

void loop() { vTaskDelay(portMAX_DELAY); }
