#include "tasks.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "weather.h"
#include "device.h"
#include "serial2.h"
#include "net.h"
#include <esp32_smartdisplay.h>
#include <WiFi.h>
#include <time.h>
#include <esp_heap_caps.h>

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

// ── 시계 갱신 ─────────────────────────────────────────────
static void updateClock() {
    struct tm t;
    if (!getLocalTime(&t)) return;
    char timeBuf[8], dateBuf[20];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M",    &t);
    strftime(dateBuf, sizeof(dateBuf), "%Y.%m.%d", &t);
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        if (clockLbl) lv_label_set_text(clockLbl, timeBuf);
        if (dateLbl)  lv_label_set_text(dateLbl,  dateBuf);
        xSemaphoreGive(lvglMutex);
    }
}

// ── 메인 태스크 (Core 0) ──────────────────────────────────
void mainTask(void* param) {
    initWiFi();
    initSerial2();

    static uint32_t lastClock   = 0;
    static uint32_t lastHeapLog = 0;

    while (true) {
        // 시계
        if (millis() - lastClock > 1000) { lastClock = millis(); updateClock(); }

        // 날씨 (최초 30초, 이후 10분 주기)
        if (WiFi.status() == WL_CONNECTED &&
            (lastWeatherUpdate == 0 ||
             millis() - lastWeatherUpdate > (weatherLoaded ? 600000 : 30000))) {
            lastWeatherUpdate = millis();
            fetchWeather();
        }

        // 힙 모니터 (5분 주기)
        if (millis() - lastHeapLog > 300000) {
            lastHeapLog = millis();
            Serial.printf("[MON] heap=%d largest=%d\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        }

        // UI 조작 반영
        DevCmd cmd;
        while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
            applyDevice(cmd.dev, cmd.val);
        }

        handleBridge();     // 브리지 접속/명령
        handleSerial2();    // STM32 명령
        handleNet();
        updateLogUI();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
