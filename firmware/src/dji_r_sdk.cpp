#include "dji_r_sdk.h"
#include "driver/twai.h"

namespace dji {

static const uint32_t CAN_SEND_ID = 0x223;
static const uint32_t CAN_RECV_ID = 0x222;
static uint16_t g_seq = 0;

// ---- CRC (init値0x3AA3はDJI固有。テーブルは標準ARC/CRC32と同一) ----

static uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0x3AA3;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0x3AA3;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
    }
    return crc;
}

// ---- フレーム構築・送信 ----

static void sendFrame(uint8_t cmdType, const uint8_t *payload, size_t payloadLen) {
    static uint8_t frame[64];
    size_t total = 12 + payloadLen + 4;  // header(12) + payload + crc32(4)
    if (total > sizeof(frame)) return;

    frame[0] = 0xAA;
    frame[1] = total & 0xFF;
    frame[2] = (total >> 8) & 0x03;      // 上位6bitはversion=0
    frame[3] = cmdType;
    frame[4] = 0x00;                     // ENC
    frame[5] = frame[6] = frame[7] = 0x00;
    g_seq++;
    frame[8] = g_seq & 0xFF;
    frame[9] = (g_seq >> 8) & 0xFF;
    uint16_t hc = crc16(frame, 10);
    frame[10] = hc & 0xFF;
    frame[11] = (hc >> 8) & 0xFF;
    memcpy(frame + 12, payload, payloadLen);
    uint32_t fc = crc32(frame, 12 + payloadLen);
    memcpy(frame + 12 + payloadLen, &fc, 4);

    // 8バイトずつCANフレームに分割して送信
    for (size_t off = 0; off < total; off += 8) {
        twai_message_t msg = {};
        msg.identifier = CAN_SEND_ID;
        msg.data_length_code = min((size_t)8, total - off);
        memcpy(msg.data, frame + off, msg.data_length_code);
        twai_transmit(&msg, pdMS_TO_TICKS(20));
    }
}

bool begin(gpio_num_t txPin, gpio_num_t rxPin) {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_NORMAL);
    twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
    return twai_start() == ESP_OK;
}

void speedControl(int16_t yaw, int16_t roll, int16_t pitch) {
    uint8_t p[9];
    p[0] = 0x0E; p[1] = 0x01;            // CmdSet / CmdID
    memcpy(p + 2, &yaw, 2);
    memcpy(p + 4, &roll, 2);
    memcpy(p + 6, &pitch, 2);
    p[8] = 0x80;                         // ctrl_byte: 速度制御モード
    sendFrame(0x03, p, sizeof(p));
}

void positionControl(int16_t yaw, int16_t roll, int16_t pitch, uint16_t timeMs) {
    uint8_t p[10];
    p[0] = 0x0E; p[1] = 0x00;
    memcpy(p + 2, &yaw, 2);
    memcpy(p + 4, &roll, 2);
    memcpy(p + 6, &pitch, 2);
    p[8] = 0x01;                         // ctrl_byte: 絶対位置
    p[9] = constrain(timeMs / 100, 1, 255);  // 0.1秒単位
    sendFrame(0x03, p, sizeof(p));
}

void requestAngles() {
    uint8_t p[3] = {0x0E, 0x02, 0x01};
    sendFrame(0x03, p, sizeof(p));
}

// ---- 受信 (複数CANフレームからの再組立て) ----

static uint8_t rxBuf[128];
static size_t rxLen = 0;

static bool parseFrame(const uint8_t *f, size_t len, Angles &out) {
    // 角度応答: ペイロード末尾6バイトが yaw/roll/pitch (int16 LE, 0.1deg)
    if (len < 12 + 2 + 6 + 4) return false;
    if (f[12] != 0x0E && f[13] != 0x0E) return false;  // CmdSet確認(応答形式差異を許容)
    const uint8_t *data = f + len - 4 - 6;
    memcpy(&out.yaw,   data,     2);
    memcpy(&out.roll,  data + 2, 2);
    memcpy(&out.pitch, data + 4, 2);
    return true;
}

bool pollAngles(Angles &out) {
    twai_message_t msg;
    bool got = false;
    while (twai_receive(&msg, 0) == ESP_OK) {
        if (msg.identifier != CAN_RECV_ID) continue;
        if (rxLen + msg.data_length_code > sizeof(rxBuf)) rxLen = 0;
        memcpy(rxBuf + rxLen, msg.data, msg.data_length_code);
        rxLen += msg.data_length_code;

        // バッファ内のフレームを走査
        while (rxLen >= 12) {
            // SOF頭出し
            size_t s = 0;
            while (s < rxLen && rxBuf[s] != 0xAA) s++;
            if (s > 0) { memmove(rxBuf, rxBuf + s, rxLen - s); rxLen -= s; }
            if (rxLen < 3) break;
            size_t flen = rxBuf[1] | ((rxBuf[2] & 0x03) << 8);
            if (flen < 16 || flen > sizeof(rxBuf)) { rxLen = 0; break; }
            if (rxLen < flen) break;  // まだ全部届いていない
            if (crc16(rxBuf, 10) == (uint16_t)(rxBuf[10] | rxBuf[11] << 8)) {
                Angles a;
                if (parseFrame(rxBuf, flen, a)) { out = a; got = true; }
            }
            memmove(rxBuf, rxBuf + flen, rxLen - flen);
            rxLen -= flen;
        }
    }
    return got;
}

}  // namespace dji
