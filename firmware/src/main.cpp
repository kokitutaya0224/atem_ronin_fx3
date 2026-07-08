// ATEM PTZ Bridge — ESP32ファームウェア
// LAN(有線Ethernet)またはWiFiのUDPで受けたコマンドをDJI R SDK (CAN)へ中継する
// ビルド環境で切替: esp32-poe=有線LAN(NET_ETH) / esp32dev=WiFi(NET_WIFI)
//
// UDPプロトコル (JSON, bridge/gimbal_link.py と対):
//   {"t":"spd","y":<0.1deg/s>,"p":...,"r":...}      速度指令
//   {"t":"pos","y":<0.1deg>,"p":...,"r":...,"ms":n} 位置指令
//   {"t":"gp"}                                       角度取得
//   応答 {"t":"ang","y":...,"p":...,"r":...}         (0.1deg)

#include <Arduino.h>
#ifdef NET_ETH
#include <ETH.h>   // Olimex ESP32-POE: LAN8720のピン定義はボードバリアントが持つ
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>  // UDPソケットはlwIP共通なので有線LANでもこのクラスを使う
#include <ArduinoJson.h>
#include "config.h"
#include "dji_r_sdk.h"

WiFiUDP udp;
IPAddress lastClient;
uint16_t lastClientPort = 0;

int16_t spdYaw = 0, spdPitch = 0, spdRoll = 0;
bool moving = false;
uint32_t lastSpdCmd = 0;
uint32_t lastSpdSent = 0;

void setup() {
    Serial.begin(115200);

#ifdef NET_ETH
    // 有線LAN（リンク確立を待ってから固定IPを設定）
    ETH.begin();
#if USE_STATIC_IP
    ETH.config(IPAddress(STATIC_IP), IPAddress(GATEWAY_IP), IPAddress(SUBNET_MASK));
#endif
    Serial.print("Ethernetリンク待ち");
    while (!ETH.linkUp()) { delay(300); Serial.print("."); }
#if !USE_STATIC_IP
    while (ETH.localIP() == IPAddress()) { delay(300); Serial.print("."); }  // DHCP待ち
#endif
    Serial.printf("\nIP: %s (Ethernet)\n", ETH.localIP().toString().c_str());
#else
    // WiFi（検証・予備用）
#if USE_STATIC_IP
    WiFi.config(IPAddress(STATIC_IP), IPAddress(GATEWAY_IP), IPAddress(SUBNET_MASK));
#endif
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi接続中");
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
    Serial.printf("\nIP: %s (WiFi)\n", WiFi.localIP().toString().c_str());
#endif

    udp.begin(UDP_PORT);

    if (!dji::begin(CAN_TX_PIN, CAN_RX_PIN)) {
        Serial.println("CAN(TWAI)初期化失敗 — 配線とピン設定を確認");
    } else {
        Serial.println("CAN 1Mbps 起動OK");
    }
}

void handlePacket(const char *buf) {
    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) return;
    const char *t = doc["t"];
    if (!t) return;

    if (strcmp(t, "spd") == 0) {
        spdYaw = doc["y"] | 0;
        spdPitch = doc["p"] | 0;
        spdRoll = doc["r"] | 0;
        moving = (spdYaw != 0 || spdPitch != 0 || spdRoll != 0);
        lastSpdCmd = millis();
        dji::speedControl(spdYaw, spdRoll, spdPitch);
        lastSpdSent = millis();
    } else if (strcmp(t, "pos") == 0) {
        moving = false;  // 位置移動中は速度再送を止める
        dji::positionControl(doc["y"] | 0, doc["r"] | 0, doc["p"] | 0,
                             doc["ms"] | 2000);
    } else if (strcmp(t, "gp") == 0) {
        dji::requestAngles();
    }
}

void loop() {
    // UDP受信
    int len = udp.parsePacket();
    if (len > 0) {
        static char buf[256];
        int n = udp.read(buf, sizeof(buf) - 1);
        buf[max(n, 0)] = '\0';
        lastClient = udp.remoteIP();
        lastClientPort = udp.remotePort();
        handlePacket(buf);
    }

    uint32_t now = millis();

    // ウォッチドッグ: 指令が途絶えたら停止（暴走防止の最重要処理）
    if (moving && now - lastSpdCmd > SPEED_WATCHDOG_MS) {
        spdYaw = spdPitch = spdRoll = 0;
        moving = false;
        dji::speedControl(0, 0, 0);
    }

    // 動作中は50msごとに速度指令を再送（ジンバル側のタイムアウト対策）
    if (moving && now - lastSpdSent >= 50) {
        dji::speedControl(spdYaw, spdRoll, spdPitch);
        lastSpdSent = now;
    }

    // CAN受信 → 角度応答をUDPで返す
    dji::Angles ang;
    if (dji::pollAngles(ang) && lastClientPort) {
        char out[96];
        snprintf(out, sizeof(out), "{\"t\":\"ang\",\"y\":%d,\"p\":%d,\"r\":%d}",
                 ang.yaw, ang.pitch, ang.roll);
        udp.beginPacket(lastClient, lastClientPort);
        udp.write((const uint8_t *)out, strlen(out));
        udp.endPacket();
    }

    delay(2);
}
