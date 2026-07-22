#!/usr/bin/env bash
# ブリッジ起動（2回目以降はこれだけでOK）
set -e
cd "$(dirname "$0")"

if [ ! -d .venv ]; then
  echo "初回セットアップが未実施です。先に ./install.sh を実行してください。"
  exit 1
fi

source .venv/bin/activate
echo "→ http://localhost:8090 で操作パネルが開けます（起動まで数秒）"
python3 app.py
