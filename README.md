# ATEM PTZ Bridge（フェーズ1・社内ツール）

ATEMスイッチャーから Sony FX3 と DJI Ronin RS 4 Pro を統合制御し、
「シネマカメラ＋ジンバル」をPTZカメラ化するブリッジシステム。

```
ATEM ──(LAN)──> bridge/app.py ──(UDP/WiFi)──> ESP32+CANトランシーバ ──> Ronin RS4 Pro
                    │   └──(USBサブプロセス)──> fx3_wrapper/fx3cli ──> FX3
                    └──(WebSocket)──> 操作パネル(ブラウザ/ゲームパッド)
```

| 機能 | 経路 |
|------|------|
| パン・チルト（ジョイスティック/画面） | パネル → ブリッジ → ESP32 → CAN速度制御 |
| プリセット保存・呼び出し | CAN角度取得 + 位置制御 |
| タリー表示・CCU横取り（アイリス/WB/シャッター/ゲイン） | PyATEMMax |
| ATEM Software Controlカメラページとの併用（操作をパネルUIへ同期） | CCU横取り → WebSocket配信 |
| FX3露出・WB・フォーカス・ズーム・REC | Sony Camera Remote SDK |

## 必要なハードウェア

- **ESP32 DevKit**（技適取得済みモジュール搭載品）
- **SN65HVD230 CANトランシーバモジュール**（3.3V用・数百円）
- **RonoinのRSSポートへの接続ケーブル**（下記⚠️参照）
- USB-Cケーブル（Mac ↔ FX3）
- ゲームパッド（PS4/PS5/Xbox系、ブラウザのGamepad API対応なら何でも可）

### 配線

```
ESP32 GPIO21 ── CTX ┐
ESP32 GPIO22 ── CRX │ SN65HVD230 ── CANH ── Ronin RSS CANH
ESP32 3.3V  ── VCC  │            └─ CANL ── Ronin RSS CANL
ESP32 GND   ── GND  ┘            (GND共通も接続)
```

## セットアップ

### 1. ブリッジ（Mac）

```bash
cd bridge
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
# config.json の atem_ip / gimbals.ip を環境に合わせて編集
python app.py
# → http://localhost:8090
```

カメラは初期設定で `"mode": "mock"`。**ATEM・ジンバル・パネルの動作を
実カメラなしで確認できる**。FX3実機に繋ぐときは fx3_wrapper をビルドして
`"mode": "fx3"` に変更。

### 2. ESP32ファームウェア

```bash
cd firmware
# src/config.h の WiFi SSID/パスワード・固定IPを編集
pio run -t upload && pio device monitor
```

### 3. FX3ラッパー

`fx3_wrapper/README.md` 参照（SonyサイトからSDKの手動ダウンロードが必要）。

## ⚠️ 実機接続前に確認すべきこと（重要）

1. **RS 4「無印」はRSSポート非搭載の可能性が高い。CAN制御はRS 4 Pro
   （またはRS 3 Pro / RS 2）が前提。** 実機のポートを確認すること。
2. **RSSポートのピンアウトは公式資料（DJI R SDK文書）で要確認。**
   DJI開発者サイトでR SDKのライセンスに同意するとプロトコル文書と
   ピン配置が入手できる。コネクタは市販のRSSケーブル or DJI R SDK
   開発ボードの利用が確実。
3. **CANプロトコル実装の検証**: フレーム形式・CRC(init 0x3AA3)・
   コマンドID(0x0E/0x00,0x01,0x02)は実績あるOSS実装
   （rileyharmon/DJI-Ronin-RS2-Log-and-Replay）から採ったが、
   RS 4 Pro実機での動作確認が必須。まず `gp`（角度取得）だけ送って
   応答が返ることを確認 → 次に低速の速度制御 → 位置制御の順で試す。
4. **ATEMのCCU値スケール**（アイリス正規化値・シャッターus等）は
   実機ログを見てcamera_link.pyの変換を微調整する。
5. **PyATEMMaxのcameraControl対応**はバージョン依存。起動ログの
   「CCU監視フィールド」を確認。未対応ならタリーのみ動作する
   （その場合の代替策はフェーズ1.5で検討）。

## フェーズ1の完了条件

- [ ] パネル/ゲームパッドからRoninのパン・チルトが滑らかに動く
- [ ] プリセット6点の保存・呼び出しが2秒移動で決まる
- [ ] ATEM Software ControlのアイリスがFX3に反映される
- [ ] タリー（PGM/PVW）がパネルに出る
- [ ] REC一斉開始/停止が動く

## 既知の制限（フェーズ1スコープ外）

- ATEM本体の収録開始との自動REC連動（PyATEMMaxが録画ステータス
  コマンド未対応のため。必要ならフェーズ1.5で生プロトコル対応）
- 複数カメラ運用（構造上は対応済み、config.jsonに追記すれば動くはず
  だが未検証）
- ジンバルのフォーカスモーター制御（CAN 0x0E/0x12。FX3側AFで代替）
