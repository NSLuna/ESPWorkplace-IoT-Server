#include "serial2.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "device.h"
#include "ui.h"

// ── 초기화 ────────────────────────────────────────────────
void initSerial2() {
    Serial2.begin(S2_BAUD, SERIAL_8N1, S2_PIN_RX, S2_PIN_TX);
    addLog("Serial2 ready");
}

// ── 상태 통보 송신 (0x71) ─────────────────────────────────
void notifySerial2(uint8_t dev, uint8_t val) {
    uint8_t pkt[S2_PKT_SIZE];
    pkt[S2_IDX_HEADER]   = S2_RSP_HEADER;
    pkt[S2_IDX_DEV]      = dev;
    pkt[S2_IDX_VAL]      = val;
    pkt[S2_IDX_CHKSUM]   = S2_RSP_HEADER ^ dev ^ val;
    pkt[S2_IDX_RESERVED] = 0x00;
    pkt[S2_IDX_TAIL]     = S2_TAIL;
    Serial2.write(pkt, S2_PKT_SIZE);

    char log[40];
    snprintf(log, sizeof(log), "S2 TX: %s %d", devKey(dev), (int)val);
    addLog(log);
    updateSerial2UI(log);
}

// ── 수신 처리 (0x65) ──────────────────────────────────────
void handleSerial2() {
    while (Serial2.available() > 0) {
        // 헤더 동기화 — 0x65가 나올 때까지 버린다
        if (Serial2.peek() != S2_CMD_HEADER) { Serial2.read(); continue; }
        if (Serial2.available() < S2_PKT_SIZE) break;

        uint8_t pkt[S2_PKT_SIZE];
        Serial2.read(pkt, S2_PKT_SIZE);

        if (pkt[S2_IDX_TAIL] != S2_TAIL) { addLog("S2 RX: tail fail"); continue; }

        uint8_t expect = S2_CMD_HEADER ^ pkt[S2_IDX_DEV] ^ pkt[S2_IDX_VAL];
        if (pkt[S2_IDX_CHKSUM] != expect) { addLog("S2 RX: chksum fail"); continue; }

        uint8_t dev = pkt[S2_IDX_DEV];
        uint8_t val = pkt[S2_IDX_VAL];

        char log[40];
        snprintf(log, sizeof(log), "S2 RX: %s %d", devKey(dev), (int)val);
        addLog(log);
        updateSerial2UI(log);

        // 반영 + MQTT 발행 + 0x71 통보까지 applyDevice가 처리
        if (!applyDevice(dev, val)) addLog("S2 RX: bad dev/val");
    }
}
