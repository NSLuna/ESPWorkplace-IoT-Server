#pragma once
#include <Arduino.h>

void initWiFi();
void handleNet();                                  // DNS 유지 + WiFi 상태 표시
void handleBridge();                               // 브리지 접속 수락 + 수신
void sendToBridge(uint8_t dev, uint8_t val);       // 상태 변경 통보
