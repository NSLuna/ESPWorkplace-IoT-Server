#include "state.h"

SemaphoreHandle_t lvglMutex = nullptr;
SemaphoreHandle_t logMutex  = nullptr;
QueueHandle_t     cmdQueue  = nullptr;

WiFiServer tcpServer(TCP_PORT);
WiFiClient bridgeClient;

bool    light1On  = false;
bool    light2On  = false;
uint8_t fan1Speed = 0;
uint8_t fan2Speed = 0;

float    lastTemp          = 0.0f;
int      lastWcode         = -1;
bool     weatherLoaded     = false;
uint32_t lastWeatherUpdate = 0;

lv_obj_t* scrMain      = nullptr;
lv_obj_t* scrSettings  = nullptr;
lv_obj_t* light1Btn    = nullptr;
lv_obj_t* light2Btn    = nullptr;
lv_obj_t* fan1Arc      = nullptr;
lv_obj_t* fan1Slider   = nullptr;
lv_obj_t* fan1SpeedLbl = nullptr;
lv_obj_t* fan2Arc      = nullptr;
lv_obj_t* fan2Slider   = nullptr;
lv_obj_t* fan2SpeedLbl = nullptr;
lv_obj_t* clockLbl     = nullptr;
lv_obj_t* dateLbl      = nullptr;
lv_obj_t* weatherLbl   = nullptr;
lv_obj_t* weatherDesc  = nullptr;
lv_obj_t* weatherCard  = nullptr;
lv_obj_t* btnAllOff    = nullptr;
lv_obj_t* btnAllOn     = nullptr;
lv_obj_t* logLabel     = nullptr;
lv_obj_t* wifiDot      = nullptr;
lv_obj_t* staLabel     = nullptr;
lv_obj_t* s2Label      = nullptr;
lv_obj_t* mqttDot      = nullptr;
lv_obj_t* mqttLabel    = nullptr;
lv_obj_t* tabLogPanel  = nullptr;
lv_obj_t* tabMqttPanel = nullptr;
lv_obj_t* tabBtnLog    = nullptr;
lv_obj_t* tabBtnMqtt   = nullptr;