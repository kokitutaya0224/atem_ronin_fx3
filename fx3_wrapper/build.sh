#!/usr/bin/env bash
# fx3cli のビルド
#
#   ./build.sh                  … スタブモード（カメラなしでプロトコル確認のみ）
#   ./build.sh <SDK展開先パス>   … 実カメラ制御ビルド（Sony Camera Remote SDK必須）
set -e
cd "$(dirname "$0")"

SDK_DIR="$1"

if [ -n "$SDK_DIR" ]; then
  if [ ! -d "$SDK_DIR" ]; then
    echo "エラー: 指定したSDKパスが存在しません: $SDK_DIR"
    exit 1
  fi
  echo "→ Sony Camera Remote SDKを使ってビルド: $SDK_DIR"
  cmake -B build -DCRSDK_DIR="$SDK_DIR"
else
  echo "→ SDK未指定のためスタブモードでビルドします"
  echo "  （実カメラを使うには ./build.sh <SDK展開先パス> を指定。README.md参照）"
  cmake -B build
fi

cmake --build build

echo ""
echo "ビルド完了: build/fx3cli"
echo "bridge/config.json の wrapper に \"../fx3_wrapper/build/fx3cli\" を指定済みか確認してください。"
