#pragma once
#include <Arduino.h>

// DJI R SDK (CANバス) プロトコル実装
// 対応機: Ronin RS 2 / RS 3 Pro / RS 4 Pro (RSSポート搭載機)
//
// フレーム構造 (V1プロトコル):
//   [0]     SOF 0xAA
//   [1-2]   フレーム全長 (LE, 下位10bit) | バージョン(上位6bit)=0
//   [3]     CmdType (0x03=要応答コマンド, 0x20bit=応答フレーム)
//   [4]     ENC 0x00
//   [5-7]   RES 0x00
//   [8-9]   SEQ (LE)
//   [10-11] CRC16 (先頭10バイト, init 0x3AA3)
//   [12..]  ペイロード: CmdSet, CmdID, データ
//   [末尾4] CRC32 (CRC32を除く全体, init 0x3AA3)
//
// CAN ID: 送信 0x223 / 受信 0x222, 1Mbps, 8バイトずつ分割送信
// 出典: DJI R SDK Protocol and User Interface (要ライセンス同意),
//       実装参考: rileyharmon/DJI-Ronin-RS2-Log-and-Replay

namespace dji {

// 初期化 (TWAIドライバ起動)。成功でtrue
bool begin(gpio_num_t txPin, gpio_num_t rxPin);

// 速度制御: 0.1deg/s単位 (CmdSet 0x0E / CmdID 0x01, ctrl_byte 0x80)
void speedControl(int16_t yaw, int16_t roll, int16_t pitch);

// 位置制御: 0.1deg単位, timeMs=移動時間 (CmdSet 0x0E / CmdID 0x00, ctrl_byte 0x01)
void positionControl(int16_t yaw, int16_t roll, int16_t pitch, uint16_t timeMs);

// 角度取得要求 (CmdSet 0x0E / CmdID 0x02)。応答はpollAngles()で受ける
void requestAngles();

// CAN受信処理。角度応答を受信したらtrueを返しout{yaw,roll,pitch}(0.1deg)を埋める
struct Angles { int16_t yaw, roll, pitch; };
bool pollAngles(Angles &out);

}  // namespace dji
