#include "logger.h"
#include "state.h"
#include "config.h"
#include <lvgl.h>

static char     logBuffer[512]        = "";
static char     lastLogDisplayed[512] = "";
static uint32_t lastLogUpdate         = 0;

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
