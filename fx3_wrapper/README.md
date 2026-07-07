# fx3cli — Sony Camera Remote SDK ラッパー

FX3をUSB経由で制御するCLI。ブリッジ本体からサブプロセスとして呼ばれる。

## SDKの入手（手動作業が必要）

1. https://support.d-imaging.sony.co.jp/app/sdk/ から
   **Camera Remote SDK** (macOS版) をダウンロード（ライセンス同意が必要）
2. zipを展開し、パスを控える（例: `~/sdk/CrSDK_v1.14.00_20250201a_Mac`）

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

- メニュー → セットアップ → USB → **USB接続モード: PCリモート**
- Camera Remote SDKの対応機種リストでFX3のファームウェアバージョン要件を確認

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
