#pragma once
#include <Arduino.h>

// 기기 상태 변경의 유일한 진입점.
// UI 조작 · MQTT 수신 · Serial2 수신 모두 이 함수를 거친다.
//   1) 값 검증        2) 내부 상태 + 화면 갱신
//   3) 브리지로 통보    4) STM32(Serial2)로 통보
// 반환값: 값이 유효해 반영되었으면 true
bool applyDevice(uint8_t dev, uint8_t val);

// 기기 이름 문자열 ("light1" 등) — MQTT/로그 공용
const char* devKey(uint8_t dev);
