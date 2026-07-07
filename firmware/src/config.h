#pragma once

// ---- WiFi ----
#define WIFI_SSID     "your-ssid"
#define WIFI_PASS     "your-password"

// 固定IPを使う場合は1に（bridge/config.jsonのgimbals.ipと合わせる）
#define USE_STATIC_IP 1
#define STATIC_IP     192, 168, 10, 51
#define GATEWAY_IP    192, 168, 10, 1
#define SUBNET_MASK   255, 255, 255, 0

// ---- UDP ----
#define UDP_PORT      5005

// ---- CAN (TWAI) ----
// SN65HVD230モジュール: CTX->GPIO21, CRX->GPIO22, 3.3V, GND
#define CAN_TX_PIN    GPIO_NUM_21
#define CAN_RX_PIN    GPIO_NUM_22

// ---- 安全装置 ----
// 速度指令が途絶えてから停止までの時間(ms)
#define SPEED_WATCHDOG_MS 400
