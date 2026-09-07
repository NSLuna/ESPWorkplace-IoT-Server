#include "ui.h"
#include "state.h"
#include "theme.h"
#include "config.h"
#include <esp32_smartdisplay.h>
#include <WiFi.h>

// WiFi UI 캐시 (이 모듈 전용)
static char lastDisplayedSTA[24] = "";

// ── 도형 스타일 (스위치) ──────────────────────────────────
void updateLight1Style() {
    if (!light1Btn) return;
    lv_obj_t* card = lv_obj_get_parent(light1Btn);
    if (light1On) {
        lv_obj_add_state(light1Btn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_CYAN_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_CYAN, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(light1Btn, 0);
        if (lbl) { lv_label_set_text(lbl, "ON"); lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0); lv_obj_set_style_text_color(lbl, lv_color_hex(0xC4B8FF), LV_PART_MAIN); }
    } else {
        lv_obj_clear_state(light1Btn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_CYAN, LV_PART_MAIN);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(light1Btn, 0);
        if (lbl) { lv_label_set_text(lbl, "OFF"); lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -8, 0); lv_obj_set_style_text_color(lbl, C_TEXT_DARK, LV_PART_MAIN); }
    }
}

void updateLight2Style() {
    if (!light2Btn) return;
    lv_obj_t* card = lv_obj_get_parent(light2Btn);
    if (light2On) {
        lv_obj_add_state(light2Btn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_CYAN_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_VIOLET, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(light2Btn, 0);
        if (lbl) { lv_label_set_text(lbl, "ON"); lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0); lv_obj_set_style_text_color(lbl, lv_color_hex(0xC4B8FF), LV_PART_MAIN); }
    } else {
        lv_obj_clear_state(light2Btn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(card, C_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_VIOLET, LV_PART_MAIN);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
        lv_obj_t* lbl = lv_obj_get_child(light2Btn, 0);
        if (lbl) { lv_label_set_text(lbl, "OFF"); lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -8, 0); lv_obj_set_style_text_color(lbl, C_TEXT_DARK, LV_PART_MAIN); }
    }
}

// ── 팬 UI (Vivid: 시안→블루→바이올렛) ────────────────────
void updateFanUI(uint8_t fanIdx, uint8_t speed) {
    lv_obj_t* arc    = (fanIdx == 0) ? fan1Arc      : fan2Arc;
    lv_obj_t* slider = (fanIdx == 0) ? fan1Slider   : fan2Slider;
    lv_obj_t* lbl    = (fanIdx == 0) ? fan1SpeedLbl : fan2SpeedLbl;
    if (!lbl) return;
    if (arc) lv_arc_set_value(arc, speed);
    if (slider) lv_slider_set_value(slider, speed, LV_ANIM_OFF);
    char buf[8];
    if (speed == 0) snprintf(buf, sizeof(buf), "OFF");
    else            snprintf(buf, sizeof(buf), "%d", speed);
    lv_label_set_text(lbl, buf);
    lv_color_t c = (speed == 0) ? C_BTN_OFF_BD :
                   (speed == 1) ? C_CYAN       :
                   (speed == 2) ? C_BLUE_MID   : C_VIOLET_DEEP;
    if (arc) lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    if (slider) {
        lv_obj_set_style_bg_color(slider, c, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, c, LV_PART_KNOB);
    }
    lv_obj_set_style_text_color(lbl, speed == 0 ? C_TEXT_DARK : c, LV_PART_MAIN);
}

// ── All 버튼 UI ───────────────────────────────────────────
void updateAllBtnUI() {
    if (!btnAllOff || !btnAllOn) return;
    bool allOff = !light1On && !light2On && fan1Speed == 0 && fan2Speed == 0;
    bool allOn  =  light1On &&  light2On && fan1Speed == 3 && fan2Speed == 3;
    lv_obj_set_style_bg_color(btnAllOff, allOff ? C_CYAN : C_BTN_OFF_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOff, allOff ? C_CYAN : C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_t* offL = lv_obj_get_child(btnAllOff, 0);
    if (offL) lv_obj_set_style_text_color(offL, allOff ? lv_color_hex(0x062019) : C_TEXT_DARK, LV_PART_MAIN);
    lv_color_t onBg = allOn ? C_CYAN : allOff ? C_BTN_OFF_BG : C_GREEN;
    lv_obj_set_style_bg_color(btnAllOn, onBg, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOn, onBg, LV_PART_MAIN);
    lv_obj_t* onL = lv_obj_get_child(btnAllOn, 0);
    if (onL) lv_obj_set_style_text_color(onL, (allOff && !allOn) ? C_TEXT_DARK : C_TEXT, LV_PART_MAIN);
}

// ── WiFi / MQTT / Serial2 상태 UI ─────────────────────────
void updateWifiUI() {
    if (!staLabel || !wifiDot) return;
    bool connected = (WiFi.status() == WL_CONNECTED);
    char newSTA[24] = "";
    if (connected) snprintf(newSTA, sizeof(newSTA), "%s", WiFi.localIP().toString().c_str());
    if (strcmp(newSTA, lastDisplayedSTA) == 0) return;
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        if (connected) {
            lv_label_set_text(staLabel, newSTA);
            lv_obj_set_style_text_color(staLabel, C_GREEN, LV_PART_MAIN);
            lv_obj_set_style_bg_color(wifiDot, C_GREEN, LV_PART_MAIN);
        } else {
            lv_label_set_text(staLabel, "Connecting...");
            lv_obj_set_style_text_color(staLabel, C_RED, LV_PART_MAIN);
            lv_obj_set_style_bg_color(wifiDot, C_RED, LV_PART_MAIN);
        }
        strncpy(lastDisplayedSTA, newSTA, sizeof(lastDisplayedSTA) - 1);
        xSemaphoreGive(lvglMutex);
    }
}

// STM32 링크 상태 — 마지막으로 주고받은 프레임을 표시
void updateSerial2UI(const char* text) {
    if (!s2Label) return;
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        lv_label_set_text(s2Label, text);
        lv_obj_set_style_text_color(s2Label, C_CYAN, LV_PART_MAIN);
        xSemaphoreGive(lvglMutex);
    }
}

void updateMqttUI(bool connected) {
    if (!mqttDot || !mqttLabel) return;
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        lv_obj_set_style_bg_color(mqttDot, connected ? C_GREEN : C_RED, LV_PART_MAIN);
        lv_label_set_text(mqttLabel, connected ? "Bridge connected" : "Bridge waiting...");
        lv_obj_set_style_text_color(mqttLabel, connected ? C_GREEN : C_RED, LV_PART_MAIN);
        xSemaphoreGive(lvglMutex);
    }
}

// ── 버튼/팬 콜백 ─────────────────────────────────────────
static void onLight1Btn(lv_event_t* e) {
    light1On = !light1On; updateLight1Style(); updateAllBtnUI();
    DevCmd cmd = { DEV_LIGHT1, (uint8_t)(light1On ? VAL_ON : VAL_OFF) };
    xQueueSend(cmdQueue, &cmd, 0);
}
static void onLight2Btn(lv_event_t* e) {
    light2On = !light2On; updateLight2Style(); updateAllBtnUI();
    DevCmd cmd = { DEV_LIGHT2, (uint8_t)(light2On ? VAL_ON : VAL_OFF) };
    xQueueSend(cmdQueue, &cmd, 0);
}
static void onFan1Arc(lv_event_t* e) {
    fan1Speed = (uint8_t)lv_arc_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(0, fan1Speed); updateAllBtnUI();
    DevCmd cmd = { DEV_FAN1, fan1Speed }; xQueueSend(cmdQueue, &cmd, 0);
}
static void onFan2Arc(lv_event_t* e) {
    fan2Speed = (uint8_t)lv_arc_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(1, fan2Speed); updateAllBtnUI();
    DevCmd cmd = { DEV_FAN2, fan2Speed }; xQueueSend(cmdQueue, &cmd, 0);
}
static void onFan1Slider(lv_event_t* e) {
    fan1Speed = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(0, fan1Speed); updateAllBtnUI();
    DevCmd cmd = { DEV_FAN1, fan1Speed }; xQueueSend(cmdQueue, &cmd, 0);
}
static void onFan2Slider(lv_event_t* e) {
    fan2Speed = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    updateFanUI(1, fan2Speed); updateAllBtnUI();
    DevCmd cmd = { DEV_FAN2, fan2Speed }; xQueueSend(cmdQueue, &cmd, 0);
}
static void onAllOff(lv_event_t* e) {
    light1On = false; updateLight1Style();
    light2On = false; updateLight2Style();
    fan1Speed = 0; updateFanUI(0, 0);
    fan2Speed = 0; updateFanUI(1, 0);
    updateAllBtnUI();
    DevCmd cmds[4] = {{DEV_LIGHT1,VAL_OFF},{DEV_LIGHT2,VAL_OFF},{DEV_FAN1,0},{DEV_FAN2,0}};
    for (auto& c : cmds) xQueueSend(cmdQueue, &c, 0);
}
static void onAllOn(lv_event_t* e) {
    light1On = true; updateLight1Style();
    light2On = true; updateLight2Style();
    fan1Speed = 3; updateFanUI(0, 3);
    fan2Speed = 3; updateFanUI(1, 3);
    updateAllBtnUI();
    DevCmd cmds[4] = {{DEV_LIGHT1,VAL_ON},{DEV_LIGHT2,VAL_ON},{DEV_FAN1,3},{DEV_FAN2,3}};
    for (auto& c : cmds) xQueueSend(cmdQueue, &c, 0);
}

// ── 화면 전환 / 탭 ────────────────────────────────────────
static void onGoSettings(lv_event_t* e) { lv_scr_load(scrSettings); }
static void onGoMain(lv_event_t* e) { lv_scr_load(scrMain); }
static void onTabLog(lv_event_t* e) {
    lv_obj_set_style_bg_color(tabBtnLog, C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnMqtt, C_CARD, LV_PART_MAIN);
    lv_obj_clear_flag(tabLogPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tabMqttPanel, LV_OBJ_FLAG_HIDDEN);
}
static void onTabMqtt(lv_event_t* e) {
    lv_obj_set_style_bg_color(tabBtnMqtt, C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnLog, C_CARD, LV_PART_MAIN);
    lv_obj_clear_flag(tabMqttPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tabLogPanel, LV_OBJ_FLAG_HIDDEN);
}

// ── 헬퍼: 카드 ───────────────────────────────────────────
static lv_obj_t* makeCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y); lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(c, C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

// ── 헬퍼: 제어 카드 (스위치) ──────────────────────────────
static lv_obj_t* makeCtrlCard(lv_obj_t* scr, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
    const char* title, const char* symbol, lv_event_cb_t cb, lv_obj_t** btnOut) {
    lv_obj_t* card = makeCard(scr, x, y, w, h);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(t, 14, 12);
    lv_obj_t* icon = lv_obj_create(card);
    lv_obj_set_pos(icon, w/2-28, 36); lv_obj_set_size(icon, 56, 56);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon, C_BTN_OFF_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(icon, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* il = lv_label_create(icon);
    lv_label_set_text(il, symbol);
    lv_obj_set_style_text_font(il, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(il, C_CYAN, LV_PART_MAIN);
    lv_obj_center(il);
    lv_obj_t* sw = lv_switch_create(card);
    lv_obj_set_pos(sw, w/2-50, 120); lv_obj_set_size(sw, 100, 38);
    lv_obj_set_style_radius(sw, 19, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, 19, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sw, 17, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, C_VIOLET, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, 3, LV_PART_MAIN);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* swLbl = lv_label_create(sw);
    lv_label_set_text(swLbl, "OFF");
    lv_obj_set_style_text_color(swLbl, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(swLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(swLbl);
    if (btnOut) *btnOut = sw;
    return card;
}

// ── 설정 화면 ─────────────────────────────────────────────
static void buildSettingsUI() {
    scrSettings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scrSettings, C_BG, LV_PART_MAIN);
    lv_obj_clear_flag(scrSettings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* topBar = lv_obj_create(scrSettings);
    lv_obj_set_pos(topBar, 0, 0); lv_obj_set_size(topBar, 800, 50);
    lv_obj_set_style_bg_color(topBar, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_width(topBar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(topBar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(topBar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* backBtn = lv_obj_create(topBar);
    lv_obj_set_pos(backBtn, 8, 8); lv_obj_set_size(backBtn, 80, 34);
    lv_obj_set_style_radius(backBtn, 17, LV_PART_MAIN);
    lv_obj_set_style_bg_color(backBtn, C_BTN_OFF_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(backBtn, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(backBtn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(backBtn, 0, LV_PART_MAIN);
    lv_obj_add_flag(backBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(backBtn, onGoMain, LV_EVENT_CLICKED, NULL);
    lv_obj_t* bl = lv_label_create(backBtn);
    lv_label_set_text(bl, "< Back");
    lv_obj_set_style_text_color(bl, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_t* tl = lv_label_create(topBar);
    lv_label_set_text(tl, "Settings");
    lv_obj_set_style_text_color(tl, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(tl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t* wCard = makeCard(scrSettings, 8, 58, 380, 110);
    lv_obj_t* wt = lv_label_create(wCard);
    lv_label_set_text(wt, "WiFi Status");
    lv_obj_set_style_text_color(wt, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(wt, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(wt, 10, 8);
    wifiDot = lv_obj_create(wCard);
    lv_obj_set_pos(wifiDot, 10, 34); lv_obj_set_size(wifiDot, 8, 8);
    lv_obj_set_style_radius(wifiDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(wifiDot, C_RED, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifiDot, 0, LV_PART_MAIN);
    staLabel = lv_label_create(wCard);
    lv_label_set_text(staLabel, "Connecting...");
    lv_obj_set_style_text_color(staLabel, C_RED, LV_PART_MAIN);
    lv_obj_set_style_text_font(staLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(staLabel, 24, 30);
    mqttDot = lv_obj_create(wCard);
    lv_obj_set_pos(mqttDot, 10, 60); lv_obj_set_size(mqttDot, 8, 8);
    lv_obj_set_style_radius(mqttDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(mqttDot, C_RED, LV_PART_MAIN);
    lv_obj_set_style_border_width(mqttDot, 0, LV_PART_MAIN);
    mqttLabel = lv_label_create(wCard);
    lv_label_set_text(mqttLabel, "Bridge waiting...");
    lv_obj_set_style_text_color(mqttLabel, C_RED, LV_PART_MAIN);
    lv_obj_set_style_text_font(mqttLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(mqttLabel, 24, 56);
    s2Label = lv_label_create(wCard);
    lv_label_set_text(s2Label, "UART: --");
    lv_obj_set_style_text_color(s2Label, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s2Label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(s2Label, 10, 84);
    lv_obj_t* tabBar = makeCard(scrSettings, 8, 176, 784, 34);
    lv_obj_set_style_radius(tabBar, 6, LV_PART_MAIN);
    tabBtnLog = lv_obj_create(tabBar);
    lv_obj_set_pos(tabBtnLog, 2, 2); lv_obj_set_size(tabBtnLog, 120, 28);
    lv_obj_set_style_radius(tabBtnLog, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnLog, C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabBtnLog, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabBtnLog, 0, LV_PART_MAIN);
    lv_obj_add_flag(tabBtnLog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tabBtnLog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tabBtnLog, onTabLog, LV_EVENT_CLICKED, NULL);
    lv_obj_t* tll = lv_label_create(tabBtnLog);
    lv_label_set_text(tll, "TX/RX Log");
    lv_obj_set_style_text_color(tll, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(tll, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(tll);
    tabBtnMqtt = lv_obj_create(tabBar);
    lv_obj_set_pos(tabBtnMqtt, 126, 2); lv_obj_set_size(tabBtnMqtt, 120, 28);
    lv_obj_set_style_radius(tabBtnMqtt, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabBtnMqtt, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabBtnMqtt, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabBtnMqtt, 0, LV_PART_MAIN);
    lv_obj_add_flag(tabBtnMqtt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tabBtnMqtt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tabBtnMqtt, onTabMqtt, LV_EVENT_CLICKED, NULL);
    lv_obj_t* tml = lv_label_create(tabBtnMqtt);
    lv_label_set_text(tml, "BRIDGE");
    lv_obj_set_style_text_color(tml, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(tml, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(tml);
    lv_obj_t* logArea = makeCard(scrSettings, 8, 216, 784, 256);
    lv_obj_set_style_bg_color(logArea, C_LOG_BG, LV_PART_MAIN);
    lv_obj_set_style_pad_all(logArea, 8, LV_PART_MAIN);
    tabLogPanel = lv_obj_create(logArea);
    lv_obj_set_size(tabLogPanel, 768, 240);
    lv_obj_set_style_bg_color(tabLogPanel, C_LOG_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabLogPanel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabLogPanel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tabLogPanel, LV_OBJ_FLAG_SCROLLABLE);
    logLabel = lv_label_create(tabLogPanel);
    lv_label_set_text(logLabel, "");
    lv_obj_set_style_text_color(logLabel, C_LOG_TX, LV_PART_MAIN);
    lv_obj_set_style_text_font(logLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(logLabel, 768);
    tabMqttPanel = lv_obj_create(logArea);
    lv_obj_set_size(tabMqttPanel, 768, 240);
    lv_obj_set_style_bg_color(tabMqttPanel, C_LOG_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabMqttPanel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabMqttPanel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tabMqttPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tabMqttPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* mll = lv_label_create(tabMqttPanel);
    lv_label_set_text(mll, "");
    lv_obj_set_style_text_color(mll, C_YELLOW, LV_PART_MAIN);
    lv_obj_set_style_text_font(mll, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(mll, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mll, 768);
}

// ── 메인 화면 ─────────────────────────────────────────────
static void buildMainUI() {
    scrMain = lv_obj_create(NULL);
    lv_obj_t* scr = scrMain;
    lv_obj_set_style_bg_color(scr, C_BG, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // 좌측 — 시계+날씨 통합 카드
    weatherCard = makeCard(scr, 8, 8, 212, 400);
    lv_obj_set_style_bg_opa(weatherCard, LV_OPA_80, LV_PART_MAIN);
    clockLbl = lv_label_create(weatherCard);
    lv_label_set_text(clockLbl, "--:--");
    lv_obj_set_style_text_color(clockLbl, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(clockLbl, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(clockLbl, LV_ALIGN_CENTER, 0, -40);
    dateLbl = lv_label_create(weatherCard);
    lv_label_set_text(dateLbl, "--.--.--");
    lv_obj_set_style_text_color(dateLbl, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(dateLbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(dateLbl, LV_ALIGN_CENTER, 0, 10);
    weatherLbl = lv_label_create(weatherCard);
    lv_label_set_text(weatherLbl, "-- C");
    lv_obj_set_style_text_color(weatherLbl, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(weatherLbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_pos(weatherLbl, 16, 350);
    weatherDesc = lv_label_create(weatherCard);
    lv_label_set_text(weatherDesc, "Loading...");
    lv_obj_set_style_text_color(weatherDesc, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(weatherDesc, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(weatherDesc, LV_ALIGN_BOTTOM_RIGHT, -14, -16);

    // All OFF / All ON
    btnAllOff = lv_obj_create(scr);
    lv_obj_set_pos(btnAllOff, 8, 416); lv_obj_set_size(btnAllOff, 100, 44);
    lv_obj_set_style_radius(btnAllOff, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnAllOff, C_BTN_OFF_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOff, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_border_width(btnAllOff, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnAllOff, 0, LV_PART_MAIN);
    lv_obj_add_flag(btnAllOff, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btnAllOff, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btnAllOff, onAllOff, LV_EVENT_CLICKED, NULL);
    lv_obj_t* offLbl = lv_label_create(btnAllOff);
    lv_label_set_text(offLbl, "All OFF");
    lv_obj_set_style_text_color(offLbl, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(offLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(offLbl);
    btnAllOn = lv_obj_create(scr);
    lv_obj_set_pos(btnAllOn, 116, 416); lv_obj_set_size(btnAllOn, 100, 44);
    lv_obj_set_style_radius(btnAllOn, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnAllOn, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnAllOn, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_border_width(btnAllOn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnAllOn, 0, LV_PART_MAIN);
    lv_obj_add_flag(btnAllOn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btnAllOn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btnAllOn, onAllOn, LV_EVENT_CLICKED, NULL);
    lv_obj_t* onLbl = lv_label_create(btnAllOn);
    lv_label_set_text(onLbl, "All ON");
    lv_obj_set_style_text_color(onLbl, lv_color_hex(0x062019), LV_PART_MAIN);
    lv_obj_set_style_text_font(onLbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(onLbl);

    // 설정 버튼
    lv_obj_t* gearBtn = lv_obj_create(scr);
    lv_obj_set_pos(gearBtn, 748, 436); lv_obj_set_size(gearBtn, 44, 44);
    lv_obj_set_style_radius(gearBtn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gearBtn, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(gearBtn, C_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(gearBtn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(gearBtn, 0, LV_PART_MAIN);
    lv_obj_add_flag(gearBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(gearBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gearBtn, onGoSettings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* gl = lv_label_create(gearBtn);
    lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gl, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(gl);

    // 우측 2x2 카드 — Light 1 / Light 2
    lv_obj_t* c1 = makeCtrlCard(scr, 228, 8, 272, 210, "Light 1", LV_SYMBOL_POWER, onLight1Btn, &light1Btn);
    lv_obj_set_style_border_side(c1, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c1, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(c1, C_CYAN, LV_PART_MAIN);
    lv_obj_t* c2 = makeCtrlCard(scr, 508, 8, 272, 210, "Light 2", LV_SYMBOL_POWER, onLight2Btn, &light2Btn);
    lv_obj_set_style_border_side(c2, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c2, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(c2, C_VIOLET, LV_PART_MAIN);

    // Fan 1
    lv_obj_t* c3 = makeCard(scr, 228, 226, 272, 234);
    lv_obj_set_style_border_side(c3, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c3, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(c3, C_BLUE_MID, LV_PART_MAIN);
    lv_obj_t* f1t = lv_label_create(c3);
    lv_label_set_text(f1t, "Fan 1");
    lv_obj_set_style_text_color(f1t, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(f1t, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(f1t, 14, 10);
    fan1Arc = lv_arc_create(c3);
    lv_obj_set_size(fan1Arc, 110, 110); lv_obj_set_pos(fan1Arc, 81, 20);
    lv_arc_set_range(fan1Arc, 0, 3); lv_arc_set_value(fan1Arc, 0);
    lv_arc_set_bg_angles(fan1Arc, 135, 405);
    lv_obj_set_style_arc_color(fan1Arc, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_arc_color(fan1Arc, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(fan1Arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(fan1Arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan1Arc, C_CARD, LV_PART_KNOB);
    lv_obj_set_style_pad_all(fan1Arc, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(fan1Arc, onFan1Arc, LV_EVENT_VALUE_CHANGED, NULL);
    fan1SpeedLbl = lv_label_create(fan1Arc);
    lv_label_set_text(fan1SpeedLbl, "OFF");
    lv_obj_set_style_text_color(fan1SpeedLbl, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(fan1SpeedLbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(fan1SpeedLbl);
    fan1Slider = lv_slider_create(c3);
    lv_obj_set_pos(fan1Slider, 16, 160); lv_obj_set_size(fan1Slider, 240, 20);
    lv_slider_set_range(fan1Slider, 0, 3); lv_slider_set_value(fan1Slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fan1Slider, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fan1Slider, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan1Slider, C_BTN_OFF_BD, LV_PART_KNOB);
    lv_obj_set_style_radius(fan1Slider, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(fan1Slider, 10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(fan1Slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(fan1Slider, onFan1Slider, LV_EVENT_VALUE_CHANGED, NULL);
    const char* spdL[] = {"0","1","2","3"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* sl = lv_label_create(c3);
        lv_label_set_text(sl, spdL[i]);
        lv_obj_set_style_text_color(sl, C_TEXT_DARK, LV_PART_MAIN);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(sl, 14 + i*78, 186);
    }
    lv_obj_t* f1n = lv_label_create(c3);
    lv_label_set_text(f1n, "");
    lv_obj_set_style_text_color(f1n, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(f1n, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(f1n, 88, 208);

    // Fan 2
    lv_obj_t* c4 = makeCard(scr, 508, 226, 272, 234);
    lv_obj_set_style_border_side(c4, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c4, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(c4, C_MAGENTA, LV_PART_MAIN);
    lv_obj_t* f2t = lv_label_create(c4);
    lv_label_set_text(f2t, "Fan 2");
    lv_obj_set_style_text_color(f2t, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(f2t, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(f2t, 14, 10);
    fan2Arc = lv_arc_create(c4);
    lv_obj_set_size(fan2Arc, 110, 110); lv_obj_set_pos(fan2Arc, 81, 20);
    lv_arc_set_range(fan2Arc, 0, 3); lv_arc_set_value(fan2Arc, 0);
    lv_arc_set_bg_angles(fan2Arc, 135, 405);
    lv_obj_set_style_arc_color(fan2Arc, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_arc_color(fan2Arc, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(fan2Arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(fan2Arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan2Arc, C_CARD, LV_PART_KNOB);
    lv_obj_set_style_pad_all(fan2Arc, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(fan2Arc, onFan2Arc, LV_EVENT_VALUE_CHANGED, NULL);
    fan2SpeedLbl = lv_label_create(fan2Arc);
    lv_label_set_text(fan2SpeedLbl, "OFF");
    lv_obj_set_style_text_color(fan2SpeedLbl, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(fan2SpeedLbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(fan2SpeedLbl);
    fan2Slider = lv_slider_create(c4);
    lv_obj_set_pos(fan2Slider, 16, 160); lv_obj_set_size(fan2Slider, 240, 20);
    lv_slider_set_range(fan2Slider, 0, 3); lv_slider_set_value(fan2Slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fan2Slider, C_BTN_OFF_BD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fan2Slider, C_BTN_OFF_BD, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan2Slider, C_BTN_OFF_BD, LV_PART_KNOB);
    lv_obj_set_style_radius(fan2Slider, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(fan2Slider, 10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(fan2Slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(fan2Slider, onFan2Slider, LV_EVENT_VALUE_CHANGED, NULL);
    for (int i = 0; i < 4; i++) {
        lv_obj_t* sl = lv_label_create(c4);
        lv_label_set_text(sl, spdL[i]);
        lv_obj_set_style_text_color(sl, C_TEXT_DARK, LV_PART_MAIN);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(sl, 14 + i*78, 186);
    }
    lv_obj_t* f2n = lv_label_create(c4);
    lv_label_set_text(f2n, "");
    lv_obj_set_style_text_color(f2n, C_TEXT_DARK, LV_PART_MAIN);
    lv_obj_set_style_text_font(f2n, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(f2n, 78, 208);

    updateLight1Style();
    updateLight2Style();
    updateFanUI(0, 0);
    updateFanUI(1, 0);
    updateAllBtnUI();
}

// ── 진입점 ────────────────────────────────────────────────
void buildUI() {
    buildSettingsUI();
    buildMainUI();
    lv_scr_load(scrMain);
}
