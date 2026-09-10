#!/usr/bin/env bash
# ブリッジ起動。
#   - venvが無ければ自動で作成し依存をインストール（初回もこれ1コマンド）
#   - このMacのブラウザで操作パネルを自動で開く
#   - 同一LAN上の他PC/タブレット用のURLも表示（サーバーは 0.0.0.0 待受）
# 事前に ./setup-mac.sh でこのMacのIPとATEMのIPを設定しておくこと。
set -euo pipefail
cd "$(dirname "$0")"

ESP32_IP="192.168.10.51"

if ! command -v python3 >/dev/null; then
  echo "python3 が見つかりません。ターミナルで  xcode-select --install  を実行してください。"
  exit 1
fi

# ---- venv 自動セットアップ（初回のみ実体が走る） --------------------------
if [ ! -d .venv ]; then
  echo "→ 初回セットアップ: Python仮想環境を作成中..."
  python3 -m venv .venv
  # shellcheck disable=SC1091
  source .venv/bin/activate
  echo "→ 依存パッケージをインストール中..."
  pip install -q --upgrade pip
  pip install -q -r requirements.txt
else
  # shellcheck disable=SC1091
  source .venv/bin/activate
fi

PORT=$(python3 -c "import json;print(json.load(open('config.json')).get('web_port',8090))" 2>/dev/null || echo 8090)

# ---- 制御LANへの到達確認 ----------------------------------------------
if ! ping -c 1 -t 2 "$ESP32_IP" >/dev/null 2>&1; then
  echo "⚠ ジンバル(ESP32 ${ESP32_IP})に到達できません。"
  echo "  ./setup-mac.sh を先に実行してこのMacのIPを設定してください。"
  echo "  （ATEM/パネルだけ試すならこのまま続行しても構いません）"
  echo
fi

echo "操作パネル:"
echo "  このPC        → http://localhost:${PORT}"
lan_ips=$(ifconfig 2>/dev/null | awk '/inet /{print $2}' | grep -v '^127\.' || true)
for ip in $lan_ips; do
  echo "  他PC(同一LAN) → http://${ip}:${PORT}"
done
echo "  ※起動まで数十秒（ATEM接続待ちのあいだブラウザは開けません）"
echo

# サーバーが listen したらこのMacの既定ブラウザで開く
(
  for _ in $(seq 1 90); do
    if curl -s -o /dev/null "http://localhost:${PORT}/" 2>/dev/null; then
      command -v open >/dev/null && open "http://localhost:${PORT}"
      break
    fi
    sleep 1
  done
) &

python3 app.py
