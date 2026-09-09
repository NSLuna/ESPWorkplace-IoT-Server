#include "weather.h"
#include "state.h"
#include "theme.h"
#include "config.h"
#include "logger.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>

static const char* weatherCodeToStr(int code) {
    if (code == 0)                return "Clear";
    if (code <= 3)                return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code >= 85 && code <= 86) return "Snow";
    if (code >= 95)               return "Storm";
    return "--";
}

static void updateWeatherColor(int code) {
    if (!weatherDesc) return;
    lv_color_t c;
    if (code == 0)                     c = C_YELLOW;
    else if (code <= 3)                c = C_TEXT_DIM;
    else if (code >= 51 && code <= 67) c = C_CYAN;
    else if (code >= 71 && code <= 86) c = C_TEXT;
    else if (code >= 95)               c = C_MAGENTA;
    else                               c = C_TEXT_DIM;
    lv_obj_set_style_text_color(weatherDesc, c, LV_PART_MAIN);
}

void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClient client;
    HTTPClient http;
    char url[200];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
        "&current=temperature_2m,weather_code&timezone=Asia/Seoul",
        WEATHER_LAT, WEATHER_LON);
    http.setTimeout(5000);
    http.useHTTP10(true);
    if (!http.begin(client, url)) { addLog("Weather: begin fail"); return; }
    int code = http.GET();
    if (code != 200) {
        char log[32]; snprintf(log, sizeof(log), "Weather HTTP %d", code);
        addLog(log); http.end(); return;
    }
    char buf[512] = "";
    WiFiClient* stream = http.getStreamPtr();
    int idx = 0;
    unsigned long start = millis();
    while (idx < (int)sizeof(buf)-1 && millis() - start < 3000) {
        if (stream->available()) { buf[idx++] = stream->read(); start = millis(); }
    }
    buf[idx] = '\0';
    http.end();
    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) { addLog("Weather: JSON fail"); return; }
    float temp  = doc["current"]["temperature_2m"] | 0.0f;
    int   wcode = doc["current"]["weather_code"]    | -1;
    lastTemp  = temp;
    lastWcode = wcode;
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f C", temp);
    const char* descStr = weatherCodeToStr(wcode);
    if (xSemaphoreTake(lvglMutex, MUTEX_TIMEOUT_MS)) {
        if (weatherLbl)  lv_label_set_text(weatherLbl, tempBuf);
        if (weatherDesc) lv_label_set_text(weatherDesc, descStr);
        updateWeatherColor(wcode);
        xSemaphoreGive(lvglMutex);
    }
    char log[40]; snprintf(log, sizeof(log), "Weather: %s %s", tempBuf, descStr);
    addLog(log);
    weatherLoaded = true;
}
