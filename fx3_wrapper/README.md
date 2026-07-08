# fx3cli — Sony Camera Remote SDK ラッパー

FX3を有線LAN（`--ip`指定、推奨）またはUSBで制御するCLI。
ブリッジ本体からサブプロセスとして呼ばれる。

## SDKの入手（手動作業が必要）

1. https://support.d-imaging.sony.co.jp/app/sdk/ja/index.html から
   **Camera Remote SDK** (macOS版) をダウンロード（無償・使用許諾同意が必要）。
   2026-07-09時点の最新は **v2.02.00**（2026-06-10リリース、macOS 14.1+対応）。
   **ILME-FX3はUSB/有線LAN/Wi-Fiすべて公式対応**（有線LANはVer.2.00以降）
2. 商用（業務）利用は利用申請も行う:
   https://www.sony.jp/camera-biz/camera-remote-toolkit/permission.html
3. zipを展開し、パスを控える（例: `~/sdk/CrSDK_v2.02.00_Mac`）

## ビルド

```bash
cd fx3_wrapper
cmake -B build -DCRSDK_DIR=~/sdk/CrSDK_vX.XX.XX_Mac
cmake --build build
# → build/fx3cli が生成される（bridge/config.json の wrapper に指定）
```

`CRSDK_DIR` を指定しなければ**スタブモード**でビルドされ、
カメラなしでブリッジ↔ラッパー間のプロトコル動作だけ確認できる。

## FX3側の設定

- **有線LAN（推奨）**: USB-C端子にUSB-LANアダプタ（AX88179系）を装着 →
  ネットワークメニューで有線LAN接続とPCリモート機能（有線LAN）を有効化 →
  固定IPを設定し bridge/config.json の `cameras.{n}.ip` に記載
- **USB（検証用）**: メニュー → セットアップ → USB → **USB接続モード: PCリモート**
- Camera Remote SDKの対応機種リストでFX3のファームウェアバージョン要件を確認
  （有線LANリモートはFX3本体ファームウェアの更新が必要な場合あり）

## 実装の合わせ込み（TODO）

CrSDKのプロパティ列挙値・パック形式はSDKバージョンで変わるため、
`main.cpp` 内の `TODO` コメント箇所をSDK同梱サンプル
`app/RemoteCli/CameraDevice.cpp` と突き合わせて調整すること。
特に:

- **絞り** `CrDeviceProperty_FNumber` — 可能値リスト取得→正規化値マップ
- **シャッター** — 分子/分母パック形式への変換
- **ズーム** `CrDeviceProperty_Zoom_Operation` — 速度レンジ
- **REC** — `CrCommandId_MovieRecord` はトグルなので、実RECステータスを
  `CrDeviceProperty_RecordingState` で読んで同期すること
