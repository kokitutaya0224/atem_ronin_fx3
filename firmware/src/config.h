#pragma once

// ---- ネットワーク（NET_ETH / NET_WIFI はplatformio.iniのビルド環境で決まる）----

#ifdef NET_WIFI
// WiFi版（esp32dev）のみ使用
#define WIFI_SSID     "your-ssid"
#define WIFI_PASS     "your-password"
#endif

// 固定IPを使う場合は1に（bridge/config.jsonのgimbals.ipと合わせる）
// 有線LAN版でもDHCPより固定IP推奨（現場でIPが変わると操作不能になるため）
#define USE_STATIC_IP 1
#define STATIC_IP     192, 168, 10, 51
#define GATEWAY_IP    192, 168, 10, 1
#define SUBNET_MASK   255, 255, 255, 0

// ---- UDP ----
#define UDP_PORT      5005

// ---- CAN (TWAI) ----
// SN65HVD230モジュール: CTX->CAN_TX_PIN, CRX->CAN_RX_PIN, 3.3V, GND
#ifdef NET_ETH
// Olimex ESP32-POE: EthernetのRMIIがGPIO19/21/22/25/26/27を占有するため
// CANはGPIO13/14を使う（EXT1/UEXTヘッダに出ている空きピン。GPIO12はPHY電源で使用不可）
#define CAN_TX_PIN    GPIO_NUM_14
#define CAN_RX_PIN    GPIO_NUM_13
#else
// 汎用ESP32ボード（WiFi版）
#define CAN_TX_PIN    GPIO_NUM_21
#define CAN_RX_PIN    GPIO_NUM_22
#endif

// ---- 安全装置 ----
// 速度指令が途絶えてから停止までの時間(ms)
#define SPEED_WATCHDOG_MS 400
