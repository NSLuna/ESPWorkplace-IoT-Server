#include "device.h"
#include "state.h"
#include "config.h"
#include "ui.h"
#include "serial2.h"
#include "net.h"

// ── 기기 이름 ─────────────────────────────────────────────
const char* devKey(uint8_t dev) {
    switch (dev) {
        case DEV_LIGHT1: return "light1";
        case DEV_LIGHT2: return "light2";
        case DEV_FAN1:   return "fan1";
        case DEV_FAN2:   return "fan2";
        default:         return "unknown";
    }
}

static bool isFan(uint8_t dev) {
    return dev == DEV_FAN1 || dev == DEV_FAN2;
}

// ── 값 검증 ───────────────────────────────────────────────
static bool isValid(uint8_t dev, uint8_t val) {
    switch (dev) {
        case DEV_LIGHT1:
        case DEV_LIGHT2: return val <= VAL_ON;
        case DEV_FAN1:
        case DEV_FAN2:   return val <= FAN_SPEED_MAX;
        default:         return false;
    }
}

// ── 상태 변경 진입점 ──────────────────────────────────────
bool applyDevice(uint8_t dev, uint8_t val) {
    if (!isValid(dev, val)) return false;

    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        switch (dev) {
            case DEV_LIGHT1: light1On = (val == VAL_ON); updateLight1Style(); break;
            case DEV_LIGHT2: light2On = (val == VAL_ON); updateLight2Style(); break;
            case DEV_FAN1:   fan1Speed = val; updateFanUI(0, fan1Speed);      break;
            case DEV_FAN2:   fan2Speed = val; updateFanUI(1, fan2Speed);      break;
        }
        updateAllBtnUI();
        xSemaphoreGive(lvglMutex);
    }

    sendToBridge(dev, val);
    notifySerial2(dev, val);
    return true;
}
