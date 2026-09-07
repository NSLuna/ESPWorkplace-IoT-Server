#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "config.h"

// ── UI 조작 → 중계 큐 항목 ─────────────────────────────────
struct DevCmd { uint8_t dev; uint8_t val; };

// ── 동기화 객체 (setup()에서 생성) ────────────────────────
extern SemaphoreHandle_t lvglMutex;
extern SemaphoreHandle_t logMutex;
extern QueueHandle_t     cmdQueue;

// ── 브리지 TCP ─────────────────────────────────────────────
extern WiFiServer tcpServer;
extern WiFiClient bridgeClient;

// ── 기기 상태 ──────────────────────────────────────────────
extern bool    light1On;
extern bool    light2On;
extern uint8_t fan1Speed;
extern uint8_t fan2Speed;

// ── 날씨 ───────────────────────────────────────────────────
extern float    lastTemp;
extern int      lastWcode;
extern bool     weatherLoaded;
extern uint32_t lastWeatherUpdate;

// ── LVGL 객체 (공유 핸들) ──────────────────────────────────
extern lv_obj_t* scrMain;
extern lv_obj_t* scrSettings;
extern lv_obj_t* light1Btn;
extern lv_obj_t* light2Btn;
extern lv_obj_t* fan1Arc;
extern lv_obj_t* fan1Slider;
extern lv_obj_t* fan1SpeedLbl;
extern lv_obj_t* fan2Arc;
extern lv_obj_t* fan2Slider;
extern lv_obj_t* fan2SpeedLbl;
extern lv_obj_t* clockLbl;
extern lv_obj_t* dateLbl;
extern lv_obj_t* weatherLbl;
extern lv_obj_t* weatherDesc;
extern lv_obj_t* weatherCard;
extern lv_obj_t* btnAllOff;
extern lv_obj_t* btnAllOn;
extern lv_obj_t* logLabel;
extern lv_obj_t* wifiDot;
extern lv_obj_t* staLabel;
extern lv_obj_t* s2Label;
extern lv_obj_t* mqttDot;    // 브리지 연결 표시로 전용
extern lv_obj_t* mqttLabel;  // 브리지 연결 표시로 전용
extern lv_obj_t* tabLogPanel;
extern lv_obj_t* tabMqttPanel;
extern lv_obj_t* tabBtnLog;
extern lv_obj_t* tabBtnMqtt;