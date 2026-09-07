#pragma once
#include <Arduino.h>
#include "secrets.h"

// ── WiFi ───────────────────────────────────────────────────
// SSID/비밀번호는 secrets.h 에 정의한다 (git 미추적)

// ── 브리지 TCP ─────────────────────────────────────────────
// 이 보드는 화면 + WiFi 만으로 내부 SRAM이 빠듯해 TLS 핸드셰이크
// (약 50KB)를 열지 못한다. 따라서 MQTT는 직접 쓰지 않고,
// 브리지 보드가 TCP로 붙어 HiveMQ Cloud(TLS) 중계를 담당한다.
#define TCP_PORT            8080

#define PKT_HEADER          0xAA
#define PKT_TAIL            0xFF
#define PKT_SIZE            12
#define PKT_IDX_HEADER      0
#define PKT_IDX_DEV         1
#define PKT_IDX_VAL         2
#define PKT_IDX_SOURCE      3
#define PKT_IDX_CHKSUM      4
#define PKT_IDX_TAIL        11
#define PKT_SRC_SERVER      0x01   // 이 보드가 보낸 것
#define PKT_SRC_BRIDGE      0x02   // 브리지가 보낸 것

// ── 기기 정의 ──────────────────────────────────────────────
#define DEV_LIGHT1          0x01   // 조명 1  · VAL: 0=OFF, 1=ON
#define DEV_LIGHT2          0x02   // 조명 2  · VAL: 0=OFF, 1=ON
#define DEV_FAN1            0x03   // 팬 1    · VAL: 0=정지, 1~3=속도
#define DEV_FAN2            0x04   // 팬 2    · VAL: 0=정지, 1~3=속도

#define VAL_OFF             0x00
#define VAL_ON              0x01
#define FAN_SPEED_MAX       3

// ── Serial2 (STM32 제어보드) ───────────────────────────────
// P2 커넥터 · ESP32 RX=IO12, TX=IO11 · 공통 접지 필요
//   수신 0x65 : STM32 → ESP32 (명령)
//   송신 0x71 : ESP32 → STM32 (상태 통보)
//   프레임    : [헤더][DEV][VAL][CHK][예비 00][테일 FF]
//   체크섬    : 헤더 ^ DEV ^ VAL
#define S2_PIN_RX           12
#define S2_PIN_TX           11
#define S2_BAUD             9600
#define S2_CMD_HEADER       0x65
#define S2_RSP_HEADER       0x71
#define S2_TAIL             0xFF
#define S2_PKT_SIZE         6
#define S2_IDX_HEADER       0
#define S2_IDX_DEV          1
#define S2_IDX_VAL          2
#define S2_IDX_CHKSUM       3
#define S2_IDX_RESERVED     4
#define S2_IDX_TAIL         5

// ── 기타 ───────────────────────────────────────────────────
#define MUTEX_TIMEOUT_MS    pdMS_TO_TICKS(100)

// ── 날씨 ───────────────────────────────────────────────────
#define WEATHER_LAT  "37.24"
#define WEATHER_LON  "127.18"
