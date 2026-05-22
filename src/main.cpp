// ╔══════════════════════════════════════════════════════════════╗
// ║              ESP32 Smart Display - TCP Server                ║
// ║                    [v2 - 클라이언트 최신화 반영]              ║
// ╚══════════════════════════════════════════════════════════════╝
//
// ── 수정 내역 (클라이언트 v3/v4 기준 동기화) ─────────────────
// [수정 1]  lv_tick_inc → lv_tick_set_cb(lvgl_tick_cb)
// [수정 2]  addLog() 버퍼 절삭 strrchr → strchr (오래된 로그 제거)
// [수정 3]  lv_obj_remove_flag → lv_obj_clear_flag 통일
// [수정 4]  shadow_width 25 → 8 (재렌더링 영역 축소)
// [수정 5]  FreeRTOS Core 분리 (lvglTask Core1 / mainTask Core0)
// [수정 6]  lvglMutex + logMutex 추가
// [수정 7]  pktQueue 비동기 버튼 처리
// [수정 8]  WiFi.setSleep(false) + esp_wifi_set_storage(RAM)
// [수정 9]  updateWifiUI() 캐시 비교로 불필요한 재렌더링 차단
// [수정 10] updateLogUI() 캐시 비교로 불필요한 재렌더링 차단
// [수정 11] TCP 수신 패킷 동기화 (헤더 0xAA 탐색)
// ──────────────────────────────────────────────────────────────

#include <Arduino.h>
#include "lv_conf.h"
#include <esp32_smartdisplay.h>
#include <WiFi.h>
#include "esp_wifi.h"  // [수정 8]

// ── WiFi 설정 ──────────────────────────────────────────────
#define WIFI_SSID     "spaceshipA"
#define WIFI_PASSWORD "spaceshipA"
#define AP_SSID       "ESP32-Server"
#define AP_PASSWORD   "12345678"
#define TCP_PORT      8080

// ── 패킷 구조 정의 ────────────────────────────────────────
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

#define PKT_STATE_ON        0x01
#define PKT_STATE_OFF       0x00

#define MUTEX_TIMEOUT_MS    pdMS_TO_TICKS(100)

// ── [수정 7] 버튼→패킷 전송 큐 ───────────────────────────
struct PktCmd { uint8_t shape; uint8_t state; };
static QueueHandle_t pktQueue = NULL;

// ── TCP 서버 ───────────────────────────────────────────────
WiFiServer tcpServer(TCP_PORT);
WiFiClient clients[4];
static uint8_t  clientCount          = 0;
static uint32_t lastWifiCheck        = 0;

// ── [수정 9] WiFi UI 캐시 ─────────────────────────────────
static char lastDisplayedSTA[24] = "";
static int  lastDisplayedCount   = -1;

// ── 도형 상태 ─────────────────────────────────────────────
static bool circleOn   = false;
static bool triangleOn = false;
static bool squareOn   = false;

// ── 색상 정의 ─────────────────────────────────────────────
#define COLOR_BG          lv_color_hex(0x1A1A2E)
#define COLOR_GRAY        lv_color_hex(0x4A4A5A)
#define COLOR_GRAY_DARK   lv_color_hex(0x2A2A3A)
#define COLOR_CIRCLE_ON   lv_color_hex(0x00D4FF)
#define COLOR_TRIANGLE_ON lv_color_hex(0xFF6B35)
#define COLOR_SQUARE_ON   lv_color_hex(0x7C3AED)
#define COLOR_TEXT        lv_color_hex(0xE0E8F0)
#define COLOR_TEXT_DARK   lv_color_hex(0x888899)
#define COLOR_LOG_BG      lv_color_hex(0x0D0D1A)
#define COLOR_LOG_BORDER  lv_color_hex(0x333355)
#define COLOR_WIFI_ON     lv_color_hex(0x00FF88)
#define COLOR_WIFI_OFF    lv_color_hex(0xFF4444)

// ── LVGL 객체 ─────────────────────────────────────────────
static lv_obj_t* circleBtn   = nullptr;
static lv_obj_t* triangleBtn = nullptr;
static lv_obj_t* squareBtn   = nullptr;
static lv_obj_t* logLabel    = nullptr;
static lv_obj_t* wifiDot     = nullptr;
static lv_obj_t* staLabel    = nullptr;
static lv_obj_t* apLabel     = nullptr;

// ── 로그 버퍼 ─────────────────────────────────────────────
static char     logBuffer[512]        = "";
static char     lastLogDisplayed[512] = "";
static uint32_t lastLogUpdate         = 0;

// ── [수정 6] 뮤텍스 ───────────────────────────────────────
static SemaphoreHandle_t lvglMutex = NULL;
static SemaphoreHandle_t logMutex  = NULL;

// ── [수정 1] LVGL 틱 콜백 ─────────────────────────────────
static uint32_t lvgl_tick_cb() {
    return (uint32_t)millis();
}

// ── [수정 2] 로그 버퍼 업데이트 ──────────────────────────
// strrchr → strchr 수정: 오래된 로그가 잘리도록
void addLog(const char* msg) {
    if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(20))) {
        char newLog[512];
        snprintf(newLog, sizeof(newLog), "> %s\n%s", msg, logBuffer);
        strncpy(logBuffer, newLog, sizeof(logBuffer) - 1);
        logBuffer[sizeof(logBuffer) - 1] = '\0';

        if (strlen(logBuffer) > 400) {
            char* firstNewline = strchr(logBuffer + 400, '\n');
            if (firstNewline) *firstNewline = '\0';
            else              logBuffer[400] = '\0';
        }
        xSemaphoreGive(logMutex);
    }
    Serial.println(msg);
}

// ── [수정 10] UI 로그 업데이트 ────────────────────────────
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

// ── [수정 9] WiFi UI 업데이트 (캐시 비교) ────────────────
void updateWifiUI() {
    if (!staLabel || !apLabel || !wifiDot) return;

    bool connected = (WiFi.status() == WL_CONNECTED);
    char newSTA[24] = "";
    if (connected) {
        snprintf(newSTA, sizeof(newSTA), "%s", WiFi.localIP().toString().c_str());
    }

    // 내용이 바뀔 때만 LVGL 갱신
    if (strcmp(newSTA, lastDisplayedSTA) == 0 &&
        lastDisplayedCount == (int)clientCount) return;

    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        if (connected) {
            char staStr[48];
            snprintf(staStr, sizeof(staStr), "STA: %s", newSTA);
            lv_label_set_text(staLabel, staStr);
            lv_obj_set_style_text_color(staLabel, COLOR_WIFI_ON, LV_PART_MAIN);
            lv_obj_set_style_bg_color(wifiDot,   COLOR_WIFI_ON, LV_PART_MAIN);

            char apStr[48];
            snprintf(apStr, sizeof(apStr), "AP:  %s [%d]",
                WiFi.softAPIP().toString().c_str(), clientCount);
            lv_label_set_text(apLabel, apStr);
            lv_obj_set_style_text_color(apLabel, COLOR_WIFI_ON, LV_PART_MAIN);
        } else {
            lv_label_set_text(staLabel, "WiFi 연결 중...");
            lv_label_set_text(apLabel, "");
            lv_obj_set_style_text_color(staLabel, COLOR_WIFI_OFF, LV_PART_MAIN);
            lv_obj_set_style_bg_color(wifiDot,   COLOR_WIFI_OFF, LV_PART_MAIN);
        }
        strncpy(lastDisplayedSTA, newSTA, sizeof(lastDisplayedSTA) - 1);
        lastDisplayedCount = (int)clientCount;
        xSemaphoreGive(lvglMutex);
    }
}

// ── 도형 스타일 업데이트 ──────────────────────────────────
// [수정 4] shadow_width 25 → 8
void updateCircleStyle() {
    if (circleOn) {
        lv_obj_set_style_bg_color(circleBtn,     COLOR_CIRCLE_ON, LV_PART_MAIN);
        lv_obj_set_style_border_color(circleBtn, COLOR_CIRCLE_ON, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(circleBtn, COLOR_CIRCLE_ON, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(circleBtn, 8,               LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(circleBtn,     COLOR_GRAY_DARK, LV_PART_MAIN);
        lv_obj_set_style_border_color(circleBtn, COLOR_GRAY,      LV_PART_MAIN);
        lv_obj_set_style_shadow_width(circleBtn, 0,               LV_PART_MAIN);
    }
}

void updateTriangleStyle() {
    if (triangleOn) {
        lv_obj_set_style_bg_color(triangleBtn,     COLOR_TRIANGLE_ON, LV_PART_MAIN);
        lv_obj_set_style_border_color(triangleBtn, COLOR_TRIANGLE_ON, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(triangleBtn, COLOR_TRIANGLE_ON, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(triangleBtn, 8,                 LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(triangleBtn,     COLOR_GRAY_DARK, LV_PART_MAIN);
        lv_obj_set_style_border_color(triangleBtn, COLOR_GRAY,      LV_PART_MAIN);
        lv_obj_set_style_shadow_width(triangleBtn, 0,               LV_PART_MAIN);
    }
}

void updateSquareStyle() {
    if (squareOn) {
        lv_obj_set_style_bg_color(squareBtn,     COLOR_SQUARE_ON, LV_PART_MAIN);
        lv_obj_set_style_border_color(squareBtn, COLOR_SQUARE_ON, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(squareBtn, COLOR_SQUARE_ON, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(squareBtn, 8,               LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(squareBtn,     COLOR_GRAY_DARK, LV_PART_MAIN);
        lv_obj_set_style_border_color(squareBtn, COLOR_GRAY,      LV_PART_MAIN);
        lv_obj_set_style_shadow_width(squareBtn, 0,               LV_PART_MAIN);
    }
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
        if (clients[i] && clients[i].connected()) {
            clients[i].write(pkt, PKT_SIZE);
            sent++;
        }
    }

    char log[32];
    snprintf(log, sizeof(log), "TX(%d): %s %s", sent,
        shape == PKT_SHAPE_CIRCLE   ? "Circle"   :
        shape == PKT_SHAPE_TRIANGLE ? "Triangle" : "Square",
        state == PKT_STATE_ON ? "ON" : "OFF");
    addLog(log);
}

// ── 패킷 파싱 ─────────────────────────────────────────────
void parsePacket(uint8_t* pkt) {
    if (pkt[PKT_IDX_HEADER] != PKT_HEADER) return;
    if (pkt[PKT_IDX_TAIL]   != PKT_TAIL)   return;
    if ((pkt[PKT_IDX_SHAPE] ^ pkt[PKT_IDX_STATE] ^ pkt[PKT_IDX_SOURCE]) != pkt[PKT_IDX_CHKSUM]) return;
    if (pkt[PKT_IDX_SOURCE] == PKT_SRC_SERVER) return;

    uint8_t shape = pkt[PKT_IDX_SHAPE];
    uint8_t state = pkt[PKT_IDX_STATE];

    char log[32];
    snprintf(log, sizeof(log), "RX: %s %s",
        shape == PKT_SHAPE_CIRCLE   ? "Circle"   :
        shape == PKT_SHAPE_TRIANGLE ? "Triangle" : "Square",
        state == PKT_STATE_ON ? "ON" : "OFF");
    addLog(log);

    // [수정 6] LVGL 스타일 변경 시 뮤텍스 보호
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        switch (shape) {
            case PKT_SHAPE_CIRCLE:
                circleOn = (state == PKT_STATE_ON);
                updateCircleStyle();
                break;
            case PKT_SHAPE_TRIANGLE:
                triangleOn = (state == PKT_STATE_ON);
                updateTriangleStyle();
                break;
            case PKT_SHAPE_SQUARE:
                squareOn = (state == PKT_STATE_ON);
                updateSquareStyle();
                break;
        }
        xSemaphoreGive(lvglMutex);
    }
}

// ── [수정 7] 버튼 콜백: 큐에 명령만 넣고 즉시 리턴 ──────
static void onCircleBtn(lv_event_t* e) {
    circleOn = !circleOn;
    updateCircleStyle();
    PktCmd cmd = { PKT_SHAPE_CIRCLE, circleOn ? PKT_STATE_ON : PKT_STATE_OFF };
    xQueueSend(pktQueue, &cmd, 0);
}

static void onTriangleBtn(lv_event_t* e) {
    triangleOn = !triangleOn;
    updateTriangleStyle();
    PktCmd cmd = { PKT_SHAPE_TRIANGLE, triangleOn ? PKT_STATE_ON : PKT_STATE_OFF };
    xQueueSend(pktQueue, &cmd, 0);
}

static void onSquareBtn(lv_event_t* e) {
    squareOn = !squareOn;
    updateSquareStyle();
    PktCmd cmd = { PKT_SHAPE_SQUARE, squareOn ? PKT_STATE_ON : PKT_STATE_OFF };
    xQueueSend(pktQueue, &cmd, 0);
}

// ── UI 빌드 ───────────────────────────────────────────────
// [수정 3] lv_obj_remove_flag → lv_obj_clear_flag 통일
void buildUI() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32 UI Panel");
    lv_obj_set_style_text_color(title, COLOR_TEXT,             LV_PART_MAIN);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, -80, 16);

    lv_obj_t* wifiContainer = lv_obj_create(scr);
    lv_obj_set_size(wifiContainer, 260, 60);
    lv_obj_align(wifiContainer, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_set_style_bg_color(wifiContainer,     lv_color_hex(0x0E2038), LV_PART_MAIN);
    lv_obj_set_style_border_color(wifiContainer, lv_color_hex(0x1E3A5F), LV_PART_MAIN);
    lv_obj_set_style_border_width(wifiContainer, 1,                       LV_PART_MAIN);
    lv_obj_set_style_radius(wifiContainer,       20,                      LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifiContainer,      0,                       LV_PART_MAIN);
    lv_obj_clear_flag(wifiContainer, LV_OBJ_FLAG_SCROLLABLE);

    wifiDot = lv_obj_create(wifiContainer);
    lv_obj_set_size(wifiDot, 10, 10);
    lv_obj_set_style_radius(wifiDot,       LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(wifiDot,     COLOR_WIFI_OFF,   LV_PART_MAIN);
    lv_obj_set_style_border_width(wifiDot, 0,                LV_PART_MAIN);
    lv_obj_align(wifiDot, LV_ALIGN_LEFT_MID, 10, 0);

    staLabel = lv_label_create(wifiContainer);
    lv_label_set_text(staLabel, "WiFi 연결 중...");
    lv_obj_set_style_text_color(staLabel, COLOR_WIFI_OFF,         LV_PART_MAIN);
    lv_obj_set_style_text_font(staLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(staLabel, LV_ALIGN_LEFT_MID, 28, -10);

    apLabel = lv_label_create(wifiContainer);
    lv_label_set_text(apLabel, "");
    lv_obj_set_style_text_color(apLabel, COLOR_WIFI_OFF,         LV_PART_MAIN);
    lv_obj_set_style_text_font(apLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(apLabel, LV_ALIGN_LEFT_MID, 28, 10);

    lv_obj_t* line = lv_obj_create(scr);
    lv_obj_set_size(line, 760, 2);
    lv_obj_set_style_bg_color(line,     COLOR_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0,          LV_PART_MAIN);
    lv_obj_set_style_radius(line,       0,          LV_PART_MAIN);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 54);

    circleBtn = lv_obj_create(scr);
    lv_obj_set_size(circleBtn, 140, 140);
    lv_obj_set_style_radius(circleBtn,       LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(circleBtn, 3,                LV_PART_MAIN);
    lv_obj_set_style_shadow_width(circleBtn, 0,                LV_PART_MAIN);
    lv_obj_align(circleBtn, LV_ALIGN_CENTER, -240, -30);
    lv_obj_add_flag(circleBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(circleBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(circleBtn, onCircleBtn, LV_EVENT_CLICKED, NULL);

    lv_obj_t* circleSymbol = lv_label_create(circleBtn);
    lv_label_set_text(circleSymbol, "O");
    lv_obj_set_style_text_font(circleSymbol, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_center(circleSymbol);

    lv_obj_t* circleName = lv_label_create(scr);
    lv_label_set_text(circleName, "Circle");
    lv_obj_set_style_text_color(circleName, COLOR_TEXT_DARK,        LV_PART_MAIN);
    lv_obj_set_style_text_font(circleName,  &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(circleName, LV_ALIGN_CENTER, -240, 60);

    triangleBtn = lv_obj_create(scr);
    lv_obj_set_size(triangleBtn, 140, 140);
    lv_obj_set_style_radius(triangleBtn,       20, LV_PART_MAIN);
    lv_obj_set_style_border_width(triangleBtn, 3,  LV_PART_MAIN);
    lv_obj_set_style_shadow_width(triangleBtn, 0,  LV_PART_MAIN);
    lv_obj_align(triangleBtn, LV_ALIGN_CENTER, -60, -30);
    lv_obj_add_flag(triangleBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(triangleBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(triangleBtn, onTriangleBtn, LV_EVENT_CLICKED, NULL);

    lv_obj_t* triangleSymbol = lv_label_create(triangleBtn);
    lv_label_set_text(triangleSymbol, "^");
    lv_obj_set_style_text_font(triangleSymbol, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_center(triangleSymbol);

    lv_obj_t* triangleName = lv_label_create(scr);
    lv_label_set_text(triangleName, "Triangle");
    lv_obj_set_style_text_color(triangleName, COLOR_TEXT_DARK,        LV_PART_MAIN);
    lv_obj_set_style_text_font(triangleName,  &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(triangleName, LV_ALIGN_CENTER, -60, 60);

    squareBtn = lv_obj_create(scr);
    lv_obj_set_size(squareBtn, 140, 140);
    lv_obj_set_style_radius(squareBtn,       20, LV_PART_MAIN);
    lv_obj_set_style_border_width(squareBtn, 3,  LV_PART_MAIN);
    lv_obj_set_style_shadow_width(squareBtn, 0,  LV_PART_MAIN);
    lv_obj_align(squareBtn, LV_ALIGN_CENTER, 120, -30);
    lv_obj_add_flag(squareBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(squareBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(squareBtn, onSquareBtn, LV_EVENT_CLICKED, NULL);

    lv_obj_t* squareSymbol = lv_label_create(squareBtn);
    lv_label_set_text(squareSymbol, "#");
    lv_obj_set_style_text_font(squareSymbol, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_center(squareSymbol);

    lv_obj_t* squareName = lv_label_create(scr);
    lv_label_set_text(squareName, "Square");
    lv_obj_set_style_text_color(squareName, COLOR_TEXT_DARK,        LV_PART_MAIN);
    lv_obj_set_style_text_font(squareName,  &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(squareName, LV_ALIGN_CENTER, 120, 60);

    lv_obj_t* logContainer = lv_obj_create(scr);
    lv_obj_set_size(logContainer, 350, 100);
    lv_obj_set_style_bg_color(logContainer,     COLOR_LOG_BG,     LV_PART_MAIN);
    lv_obj_set_style_border_color(logContainer, COLOR_LOG_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(logContainer, 1,                LV_PART_MAIN);
    lv_obj_set_style_radius(logContainer,       8,                LV_PART_MAIN);
    lv_obj_set_style_pad_all(logContainer,      8,                LV_PART_MAIN);
    lv_obj_clear_flag(logContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(logContainer, 440, 365);

    lv_obj_t* logTitle = lv_label_create(logContainer);
    lv_label_set_text(logTitle, "[ TX/RX LOG ]");
    lv_obj_set_style_text_color(logTitle, COLOR_LOG_BORDER,       LV_PART_MAIN);
    lv_obj_set_style_text_font(logTitle,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(logTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    logLabel = lv_label_create(logContainer);
    lv_label_set_text(logLabel, "");
    lv_obj_set_style_text_color(logLabel, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(logLabel,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(logLabel, 330);
    lv_obj_align(logLabel, LV_ALIGN_TOP_LEFT, 0, 20);

    updateCircleStyle();
    updateTriangleStyle();
    updateSquareStyle();
}

// ── [수정 8] WiFi 초기화 ─────────────────────────────────
void initWiFi() {
    esp_wifi_set_storage(WIFI_STORAGE_RAM); // NVS 플래시 접근 차단
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    addLog("AP started!");

    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);          // 모뎀 절전 비활성화
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    addLog("WiFi connecting...");

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
        delay(500);
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
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
    updateWifiUI();
}

// ── [수정 11] TCP 클라이언트 관리 + 수신 ─────────────────
void handleClients() {
    // WiFi 상태 체크
    if (millis() - lastWifiCheck > 50) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) updateWifiUI();
    }

    // 끊긴 클라이언트 정리
    for (int i = 0; i < 4; i++) {
        if (clients[i] && !clients[i].connected()) {
            clients[i].stop();
            clients[i] = WiFiClient();
            if (clientCount > 0) clientCount--;
            updateWifiUI();
        }
    }

    // 새 클라이언트 수락
    WiFiClient newClient = tcpServer.accept();
    if (newClient) {
        for (int i = 0; i < 4; i++) {
            if (!clients[i]) {
                clients[i] = newClient;
                clientCount++;
                char log[32];
                snprintf(log, sizeof(log), "Client %d connected!", i + 1);
                addLog(log);
                updateWifiUI();
                break;
            }
        }
    }

    // [수정 11] 수신: 헤더(0xAA) 탐색 후 읽기
    for (int i = 0; i < 4; i++) {
        if (!clients[i] || !clients[i].connected()) continue;

        while (clients[i].available() > 0) {
            if (clients[i].peek() != PKT_HEADER) {
                clients[i].read();
                continue;
            }
            if (clients[i].available() < PKT_SIZE) break;

            uint8_t pkt[PKT_SIZE];
            clients[i].read(pkt, PKT_SIZE);
            parsePacket(pkt);
        }
    }
}

// ── [수정 5] LVGL 태스크 (Core 1) ────────────────────────
void lvglTask(void* param) {
    while (true) {
        if (xSemaphoreTake(lvglMutex, portMAX_DELAY)) {
            lv_timer_handler();
            xSemaphoreGive(lvglMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ── [수정 5] 메인 태스크 (Core 0) ────────────────────────
void mainTask(void* param) {
    initWiFi();
    while (true) {
        // [수정 7] 버튼 큐 처리
        PktCmd cmd;
        while (xQueueReceive(pktQueue, &cmd, 0) == pdTRUE) {
            broadcastPacket(cmd.shape, cmd.state);
        }

        handleClients();
        updateLogUI();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\r\n=== SERVER BOOT ===\r\n");

    if (psramFound()) {
        Serial.printf("[PSRAM] OK: %d bytes\n", ESP.getPsramSize());
    } else {
        Serial.println("[PSRAM] NOT FOUND!");
    }

    smartdisplay_init();

    // [수정 1] lv_tick_set_cb 등록
    lv_tick_set_cb(lvgl_tick_cb);

    auto disp = lv_disp_get_default();
    lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_180);

    lv_indev_t* touch = lv_indev_get_next(NULL);
    Serial.println(touch ? "[TOUCH] OK" : "[TOUCH] 없음");

    // [수정 6] 뮤텍스·큐 buildUI() 이전에 생성
    lvglMutex = xSemaphoreCreateMutex();
    logMutex  = xSemaphoreCreateMutex();
    pktQueue  = xQueueCreate(8, sizeof(PktCmd));

    buildUI();

    Serial.println("[SERVER] 준비 완료!");

    // [수정 5] 태스크 생성
    xTaskCreatePinnedToCore(lvglTask, "LVGL", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(mainTask, "Main", 8192, NULL, 1, NULL, 0);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}