#!/usr/bin/env bash
# （旧セットアップスクリプト）
# venv作成と依存インストールは ./start.sh が自動で行うようになりました。
# 現場のMac設定は ./setup-mac.sh を使ってください。
set -e
cd "$(dirname "$0")"

cat <<'MSG'
セットアップ手順が変わりました:

  1. ./setup-mac.sh   … このMacのIPとATEMのIPを設定（現場ごと）
  2. ./start.sh       … 初回はvenv作成も自動。操作パネルがブラウザで開きます

このスクリプトはvenvの作成だけ行って終了します。
MSG

if [ ! -d .venv ]; then
  python3 -m venv .venv
  source .venv/bin/activate
  pip install -q --upgrade pip
  pip install -q -r requirements.txt
  echo "→ venv を作成しました。"
else
  echo "→ venv は既にあります。"
fi
