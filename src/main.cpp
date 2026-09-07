#include <Arduino.h>
#include "lv_conf.h"
#include <esp32_smartdisplay.h>
#include "state.h"
#include "config.h"
#include "ui.h"
#include "tasks.h"

static uint32_t lvgl_tick_cb() { return (uint32_t)millis(); }

void setup() {
    Serial.begin(115200);
    Serial.println("\r\n=== SERVER BOOT ===\r\n");

    if (psramFound()) Serial.printf("[PSRAM] OK: %d bytes\n", ESP.getPsramSize());
    else              Serial.println("[PSRAM] NOT FOUND!");

    smartdisplay_init();
    lv_tick_set_cb(lvgl_tick_cb);

    auto disp = lv_disp_get_default();
    lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_180);

    lv_indev_t* touch = lv_indev_get_next(NULL);
    Serial.println(touch ? "[TOUCH] OK" : "[TOUCH] none");

    lvglMutex = xSemaphoreCreateMutex();
    logMutex  = xSemaphoreCreateMutex();
    cmdQueue  = xQueueCreate(8, sizeof(DevCmd));

    buildUI();
    Serial.println("[SERVER] ready!");

    xTaskCreatePinnedToCore(lvglTask, "LVGL", 16384, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(mainTask, "Main", 16384, NULL, 1, NULL, 0);
}

void loop() { vTaskDelay(portMAX_DELAY); }
