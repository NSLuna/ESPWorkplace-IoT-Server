#pragma once
#include <Arduino.h>

// STM32 제어보드와의 UART 중계
// P2 커넥터 · RX=IO12, TX=IO11 · 9600 8N1 · 6바이트 고정 프레임
void initSerial2();                             // setup()에서 1회
void handleSerial2();                           // mainTask 루프에서 폴링
void notifySerial2(uint8_t dev, uint8_t val);   // 상태 통보 (0x71)
