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
   ├──(USBサブプロセス)──> fx3_wrapper/fx3cli ──(USB-LANアダプタ経由・有線LAN／本番)
   │                                          └─(USB-C直結／検証時のフォールバック)──> FX3
   └──(WebSocket)──> 操作パネル(ブラウザ/ゲームパッド)
```

FX3の接続方式は `config.json` の `cameras.{n}.ip` で切り替える
（指定時はUSB-LANアダプタ経由の有線LAN、省略時はUSB-C直結）。

| 機能 | 経路 |
|------|------|
| パン・チルト（ジョイスティック/画面） | パネル → ブリッジ → ESP32 → CAN速度制御 |
| プリセット保存・呼び出し | CAN角度取得 + 位置制御 |
| タリー表示・CCU横取り（アイリス/WB/シャッター/ゲイン） | PyATEMMax |
| ATEM Software Controlカメラページとの併用（操作をパネルUIへ同期） | CCU横取り → WebSocket配信 |
| FX3露出・WB・フォーカス・ズーム・REC | Sony Camera Remote SDK |

## 必要なハードウェア（1カメラ分）

- **Olimex ESP32-POE-ISO**（有線LANボード。約4,000円）
- **SN65HVD230 CANトランシーバモジュール**（3.3V用・数百円）
- **RoninのRSAポート接続用の自作コネクタ**: 3Dプリントハウジング
  （STL: `3d-print-ronin-can-connector.stl`、下記⚠️参照）+
  Preci-Dip 813/811-S1シリーズ ポゴピン（8コンタクト・数百〜千円程度）
- **USB電源アダプタ**（ESP32給電用。カメラポジションの電源タップから取る）
- **USB-LAN変換アダプタ**（FX3用。AX88179チップ系が定番、2〜3千円）
- ネットワークスイッチ（既存でOK。**PoEは不要**）
- ゲームパッド（PS4/PS5/Xbox系、ブラウザのGamepad API対応なら何でも可）

給電方針の詳細は下記②を参照（USBアダプタ給電がデフォルト、RSAポート
給電は配線削減オプション）。電源のないポジションが多い運用ならPoE
（ESP32-POEはPoE給電にも対応）へ後から移行できる。

### 配線（ESP32-POE 有線LAN・本番構成）

**全機器有線LAN固定。WiFiは使わない**（ファームウェアに`esp32dev`/WiFi版の
ビルド環境は予備として残っているが、本番では`esp32-poe`のみ使用）。

1カメラ分の配線は「Ronin RSAポート ⇔ 自作コネクタ ⇔ SN65HVD230 ⇔ ESP32-POE」
「ESP32-POE ⇔ LANケーブル ⇔ スイッチ」の2系統のみ。対応表で1対1に追える形に
した:

**① CAN配線（RSAポート ⇔ SN65HVD230 ⇔ ESP32-POE）**

| Ronin RSAポート（自作コネクタ側） | SN65HVD230モジュール | ESP32-POE |
|---|---|---|
| Pin 4 (CANH) | CANH | ― |
| Pin 2 (CANL) | CANL | ― |
| ― | CTX (TX) | GPIO14 |
| ― | CRX (RX) | GPIO13 |
| ― | VCC (3.3V) | 3.3V |
| ― | Rs (スロープ制御, 8番ピン) | GND（**直結または10kΩ以下でGNDへ**） |
| Pin 6 (GND) | GND | GND（3点とも共通GNDで接続） |
| Pin 5 (AD_COM) | ― | GND（10〜100kΩ経由でプルダウン。**これを繋がないとRSAポートがVCCを出力しない**） |
| Pin 1 (VCC 8V) | ― | 未接続（②の給電オプションを使う場合のみ配線） |
| Pin 3 (SBUS_RX) | ― | 未接続（未使用） |

**⚠️ Rsピンを必ずGNDへ落とすこと**: SN65HVD230のRsはHighまたは未接続だと
スタンバイモードになり、**受信回路だけ生きて送信ドライバが止まる**。この状態では
CANが一見動いているように見えて1フレームも送れない（症状: 起動時のCAN自己診断が
`state=BUS_OFF tx_err=128`、DC試験が`CTX=L→CRX=L 0/8`のCRX High固着）。
モジュールによってはRsが基板上でGNDに落ちているので、その場合は配線不要。

**⚠️ GPIO14/13を使う理由**: EthernetのRMIIがGPIO19/21/22/25/26/27を占有する
ため、ESP32-POEではこの2本しか空きがない（GPIO12はPHY電源で使用不可）。
ビルド環境`-e esp32-poe`を選べばファームウェアは自動でこのピン割り当てになる。

**② 給電（2案。まずはAで組んで、現場で電源タップが取りにくい場合にBへ）**

- **A: USBアダプタ給電（フェーズ1のデフォルト）** — ESP32-POEのUSB-CまたはMicro-USBに
  汎用USB電源アダプタを挿すだけ。RSAポートのVCCピンは未接続のままでよい。
- **B: RSAポート給電（配線削減オプション、後回しでよい）** — RSA Pin1(VCC 8V) →
  MP1584降圧モジュール(IN) → OUT 5V → ESP32-POEの5V/VINピンへ。GNDも共通接続。
  ①のAD_COMプルダウンが無いとPin1からVCCが出ないので①と②は連動する。

**③ LAN配線** — ESP32-POEのLANポートからPoEスイッチへ1本（データ・電源が
このケーブル1本で完結）。PoEスイッチ側はATEM・Mac(bridge)と同一LANに接続。

## セットアップ（簡単インストール）

### 1. ブリッジ（Mac）

```bash
cd bridge
./install.sh   # 初回のみ: venv作成・依存インストール・ATEM IP設定（対話式）
```

2回目以降は `./start.sh` だけでよい。起動後は `http://localhost:8090`
で操作パネルが開く。

カメラは初期設定で `"mode": "mock"`。**ATEM・ジンバル・パネルの動作を
実カメラなしで確認できる**。FX3実機に繋ぐときは3.のfx3_wrapperをビルドして
`config.json` の対象カメラを `"mode": "fx3"` に変更する
（`"ip"` を指定すればUSB-LANアダプタ経由の有線LAN接続、省略ならUSB直結）。

**FX3の接続は有線LAN推奨**（USBは5mの距離制約があり現場に不向き）。
FX3のUSB-C端子に市販のUSB-LAN変換アダプタ（AX88179チップ系が定番）を
装着し、カメラメニューで有線LANのPCリモート機能を有効化する。
**⚠️ LANアダプタがUSB-C端子を占有するためUSB給電が同時に使えない**。
長時間の本番はNP-FZ100型ダミーバッテリー（DCカプラー）でのAC給電を推奨。

### 2. ESP32ファームウェア

```bash
cd firmware
./flash.sh <カメラ番号>   # 例: 1台目のカメラ用なら ./flash.sh 1
```

固定IP設定（`192.168.10.(50+カメラ番号)`、bridgeのデフォルトgimbal IPと
一致）からビルド・書き込みまで自動で行う。PlatformIO CLI未インストール
なら導入するか聞かれる。ESP32-POEをUSBで接続した状態で実行すること。

（`esp32dev`/WiFiビルド環境はコードに残しているだけの予備。本番運用は
WiFiを使わないため`esp32-poe`のみ使えばよい。）

### 3. FX3ラッパー

SonyサイトからCamera Remote SDKの手動ダウンロードが必要
（`fx3_wrapper/README.md` 参照、ライセンス同意が要るため自動化不可）。

```bash
cd fx3_wrapper
./build.sh ~/sdk/CrSDK_vX.XX.XX_Mac   # SDK未指定ならスタブモードでビルド
```

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

   **物理配置（コネクタを正面から見た図。8穴あるが信号は6種類—VCCと
   GNDが2箇所ずつの冗長配置で穴の余りはない）**:

   ```
   上段（左→右）:  [Pin6:GND] [Pin5:AD_COM] [Pin3:SBUS_RX] [Pin1:VCC]
   下段（左→右）:  [Pin6:GND] [Pin2:CANL]   [Pin4:CANH]    [Pin1:VCC]
   ```

   番号順（1→2→3→4…）には並んでいないので注意。CANL/CANHは下段の
   内側2つに隣接。**左右どちらのRSAポートの図か文書に明記がなく、かつ
   左右のポートは回転対称（鏡像ではない）**ため、自作コネクタの配線を
   確定する前に実機でテスターによる導通確認（どの穴がGNDか等）を推奨。
   文書はRS 2時代のものなので
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
