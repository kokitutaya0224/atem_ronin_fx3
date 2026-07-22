# ATEM PTZ Bridge（フェーズ1・社内ツール）

ATEMスイッチャーから Sony FX3 と DJI Ronin RS 4 Pro を統合制御し、
「シネマカメラ＋ジンバル」をPTZカメラ化するブリッジシステム。

**全機器を有線LANで接続する**（現場のWiFiに依存しない。Middle Control同様の
「1台のMac＋1台のルーター」構成）。

```
ATEM ──(LAN)──┐
              ├─ PoEスイッチ ─(LAN/PoE 1本)─> ESP32-POE+CANトランシーバ ──> Ronin RS4 Pro
Mac(bridge) ──┘      │                          （カメラ台数分）
   │                 └──────────────────────── 操作用iPad等もこのLANに参加可
   ├──(USBサブプロセス)──> fx3_wrapper/fx3cli ──(USB-C)──> FX3
   └──(WebSocket)──> 操作パネル(ブラウザ/ゲームパッド)
```

| 機能 | 経路 |
|------|------|
| パン・チルト（ジョイスティック/画面） | パネル → ブリッジ → ESP32 → CAN速度制御 |
| プリセット保存・呼び出し | CAN角度取得 + 位置制御 |
| タリー表示・CCU横取り（アイリス/WB/シャッター/ゲイン） | PyATEMMax |
| ATEM Software Controlカメラページとの併用（操作をパネルUIへ同期） | CCU横取り → WebSocket配信 |
| FX3露出・WB・フォーカス・ズーム・REC | Sony Camera Remote SDK |

## 必要なハードウェア（1カメラ分）

- **Olimex ESP32-POE-ISO**（有線LANボード。約4,000円）
  ※WiFi版で検証する場合のみ汎用ESP32 DevKitでも可
- **SN65HVD230 CANトランシーバモジュール**（3.3V用・数百円）
- **RoninのRSAポート接続用の自作コネクタ**: 3Dプリントハウジング
  （STL: `3d-print-ronin-can-connector.stl`、下記⚠️参照）+
  Preci-Dip 813/811-S1シリーズ ポゴピン（8コンタクト・数百〜千円程度）
- **USB電源アダプタ**（ESP32給電用。カメラポジションの電源タップから取る）
- **USB-LAN変換アダプタ**（FX3用。AX88179チップ系が定番、2〜3千円）
- ネットワークスイッチ（既存でOK。**PoEは不要**）
- ゲームパッド（PS4/PS5/Xbox系、ブラウザのGamepad API対応なら何でも可）

**給電方針**: ESP32はUSBアダプタ給電が標準。CANの信号線自体は電力を運ばない
ため「CANケーブル給電」はRoninポートの電源ピン（CAN信号とは別ピン）を
引き出す必要があり、電圧・ピン位置をR SDK文書で確認できたら配線削減の
オプションとして検討する（DJIフォーカスモーターがポート給電で動く実績
から、ポート自体の給電能力は確認済み）。電源のないポジションが多い運用
ならPoE(ESP32-POEはPoE給電にも対応)へ後から移行できる。

### 配線（ESP32-POE 有線LAN版・本番用）

```
ESP32-POE GPIO14 ── CTX ┐
ESP32-POE GPIO13 ── CRX │ SN65HVD230 ── CANH ── Ronin RSS CANH
ESP32-POE 3.3V   ── VCC │            └─ CANL ── Ronin RSS CANL
ESP32-POE GND    ── GND ┘            (GND共通も接続)
LAN端子 ── PoEスイッチへ（データ+電源がLANケーブル1本）
```

**⚠️ ピンがWiFi版と違う**: EthernetのRMIIがGPIO19/21/22/25/26/27を占有する
ため、ESP32-POEではCANをGPIO14(CTX)/13(CRX)に配線する。GPIO12はPHY電源用で
使用不可。ビルド環境(`-e esp32-poe`)を選べばファームウェアは自動で正しい
ピンを使う。

### 配線（汎用ESP32 WiFi版・検証用）

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

**FX3の接続は有線LAN推奨**（USBは5mの距離制約があり現場に不向き）。
FX3のUSB-C端子に市販のUSB-LAN変換アダプタ（AX88179チップ系が定番）を
装着し、カメラメニューで有線LANのPCリモート機能を有効化、config.jsonで
IPを指定する。アダプタはバスパワーでPoE等は不要。
**⚠️ LANアダプタがUSB-C端子を占有するためUSB給電が同時に使えない**。
長時間の本番はNP-FZ100型ダミーバッテリー（DCカプラー）でのAC給電を推奨:

```json
"cameras": { "1": { "mode": "fx3", "wrapper": "../fx3_wrapper/build/fx3cli", "ip": "192.168.10.61" } }
```

`ip` を省略するとUSB直結（検証用）。

### 2. ESP32ファームウェア

```bash
cd firmware
# src/config.h の固定IPを編集（bridge/config.jsonのgimbals.ipと合わせる）
# 有線LAN版（本番・Olimex ESP32-POE）:
pio run -e esp32-poe -t upload && pio device monitor
# WiFi版（検証用・要SSID/パスワード編集）:
pio run -e esp32dev -t upload && pio device monitor
```

### 3. FX3ラッパー

`fx3_wrapper/README.md` 参照（SonyサイトからSDKの手動ダウンロードが必要）。

## ⚠️ 実機接続前に確認すべきこと（重要）

1. ~~RS 4「無印」はRSSポート非搭載の可能性~~ → **ユーザー実機はRS 4 Proと
   確認済み（2026-07-09）。R SDK公式対応機種なのでこのリスクは解消。**
2. **接続ポートはRSA/NATOポート（本体側面の6ピン拡張ポート）と判明**
   （DJI R SDK公式文書 v2.5 Figure 39、2026-07-09入手・照合済み）:

   | ピン | 信号 | 備考 |
   |-----|------|------|
   | 1 | VCC | **8V±0.4V出力・定格0.8A/ピーク1.2A**（アクセサリ給電用） |
   | 2 | CANL | SN65HVD230のCANLへ |
   | 3 | SBUS_RX | 未使用 |
   | 4 | CANH | SN65HVD230のCANHへ |
   | 5 | AD_COM | アクセサリ検出。**10〜100kΩでGNDへプルダウンしないとVCCが出力されない** |
   | 6 | GND | 共通GND |

   左右のポートは回転対称（鏡像ではない）。文書はRS 2時代のものなので
   RS 4 Proでの同一性は実機確認（APC-RがRS 4 Pro対応でRSAマウントの
   ため互換の公算大）。**VCC 8Vから降圧モジュール（MP1584等の8V→5V
   buck）を挟めばESP32をジンバル給電にでき、USBアダプタも不要になる**。

   **RSAポート自体は市販の変換ケーブルが存在しない**（アクセサリ側の
   マウント一体型ポゴピン接点のため）。解決策は先行プロジェクト
   [rileycoyote87/DJI-Ronin-RS2-Log-and-Replay](https://github.com/rileycoyote87/DJI-Ronin-RS2-Log-and-Replay)
   （CRC照合元と同一リポジトリ）で確立済み:
   - 同梱STL「`3d-print-ronin-can-connector.stl`」を3Dプリントしてハウジング作成
   - ポゴピンはPreci-Dip 813/811-S1シリーズ（8コンタクト・ダブルロー・
     2.54mmピッチ・低背はんだテール）を接着・はんだ付け
     （[RS Online](https://jp.rs-online.com/web/p/pcb-headers/7020389)国内取扱あり、
     [Mouser](https://www.mouser.com/ProductDetail/Preci-dip/811-S1-008-10-014101)。
     811系/813系の型番表記ゆれあり、発注前に現物ページで再確認）
   - 代替案: DJI R Focus Wheel純正品を1台購入しケーブルを流用
3. **CANプロトコル実装はDJI公式文書と照合済み（2026-07-09）**:
   CRC16（poly 0x8005 / XorIn 0xc55c 反転実装init 0x3AA3）は公式
   サンプルcustom_crc16.cと一致、CAN ID（PC側Tx 0x223/Rx 0x222）・
   SOF 0xAA・コマンドID(0x0E/0x00,0x01,0x02)もFigure 37-38と一致。
   残りは実機での動作確認のみ。まず `gp`（角度取得）だけ送って
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
