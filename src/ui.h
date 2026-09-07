#pragma once
#include <Arduino.h>

// ── 위젯 상태 갱신 ─────────────────────────────────────────
void updateLight1Style();
void updateLight2Style();
void updateFanUI(uint8_t fanIdx, uint8_t speed);
void updateAllBtnUI();
void updateWifiUI();
void updateSerial2UI(const char* text);
void updateMqttUI(bool connected);

// ── 화면 구성 ──────────────────────────────────────────────
void buildUI();
